/* The Guide's screen and the field's notices (the rules are definitive_quest.c).
 *
 * The screen is a field overlay drawn as the Guardromon network's (definitive_travel.c): the bestiary's panels in
 * polygons, the game's font in message windows that only this file runs, fieldstg_stage.event_running held while it is
 * up. The field menu's GUIDE entry asks for it and closes (def_guide_request); FIELDSTG's idle step runs it
 * (def_guide_frame). Two tabs, L1/R1 (or left/right on the first) to switch:
 *  - Quest: the main quest's step and the missions still open, in a list; the chosen one's title, what to do and
 *    where to go (the game's own map title, in the game's language) above it;
 *  - Auctions: the Cargo Tower's sixteen, coloured by state; the chosen one's price, state and when it opens. On one
 *    open now the confirm button, pressed twice, travels to El Dorado (def_guide_can_travel).
 *
 * The notices: when the main quest's step changes, or an auction opens, a small panel slides in at the top of the
 * field for DEF_NOTICE_TICKS (def_notice_frame, run from the camera's update as the Rush's panel is). It never holds
 * the player, and waits while an event, the menu or a battle's start holds the field. Its windows live on the field's
 * heap: a map's load forgets them (def_notice_reset) and the next frame makes them again. */
#include <stdlib.h>

#include "common.h"

#include "cdload.h"
#include "fieldstg.h"
#include "gamestate.h"
#include "gfx.h"
#include "heap.h"
#include "message.h"
#include "pad.h"
#include "psyq/libgpu.h"
#include "records.h"
#include "sound.h"

#include "psxstack/savestate.h"

#include "definitive.h"
#include "port_runtime.h"

extern void *font_get_entry(void *data, s32 i);
extern const unsigned char def_str_GuideTitle[], def_str_GuideTabQuest[], def_str_GuideTabAuctions[],
    def_str_GuideMain[], def_str_GuideGoTo[], def_str_GuideSep[], def_str_GuideKeysQuest[],
    def_str_GuideKeysAuctions[], def_str_GuideKeysTravel[], def_str_GuideKeysTravel2[], def_str_GuideBit[],
    def_str_AucOpen[], def_str_AucDone[], def_str_AucMissed[], def_str_AucSoon[], def_str_AucOpenHint[],
    def_str_AucMissedHint[], def_str_AucDoneHint[], def_str_NoticeQuest[], def_str_NoticeAuction[],
    def_str_Neutral[], def_str_Unknown[];

/* FIELDSTG's map titles (fieldstg_80085590.c, ended by map == 0): the two texts a map's arrival shows. */
typedef struct GuideMapTitle {
    u8 text_0; /* its sector, in text file 0xA9 */
    u8 text_1; /* its name, in text file 0xB7 */
    s16 map;
} GuideMapTitle;
extern GuideMapTitle fieldstg_map_titles[];

#define GD_SECTORS 0xA9 /* the text files (records_language + it): the map titles' two, */
#define GD_MAPS 0xB7
#define GD_ITEMS 0x6A   /* and the item names */

/* The layout (320x240), the Guardromon screen's shapes and colours. */
#define GD_LAYER 0x1002 /* the field's window layer (the field menu's) */
#define GD_LINE 14      /* a row's pitch: the 14-pixel font's height */
#define GD_TITLE_H 23
#define GD_TAB_X 196    /* the two tabs' names, in the title bar */
#define GD_TAB_DX 52
#define GD_TEXT_X 10
#define GD_W 316        /* the panels' width, at x 2 */
#define GD_HINT_Y 208
#define GD_COL_DX 158   /* a list's second column */
#define GD_ROW_X 18     /* a row's text, clear of the cursor */
/* the Quest tab: the chosen quest (its title's band, six lines, where to go), then the list */
#define GD_Q_Y 25
#define GD_Q_H 124
#define GD_Q_LINES 6
#define GD_QL_Y 151
#define GD_QL_H 55
#define GD_QL_ROWS 3
#define GD_Q_ROWS (1 + DEF_QUEST_MISSIONS)
/* the Auctions tab: the list in two columns, then the chosen auction */
#define GD_A_Y 25
#define GD_A_H 122
#define GD_A_ROWS 8
#define GD_AD_Y 149
#define GD_AD_H 57
#define GD_PRICE_X 308 /* the price's right end */

#define GD_PAL_TEXT 0
#define GD_PAL_HOT 3  /* the row under the cursor, an auction open now (the bestiary's orange) */
#define GD_PAL_GOOD 4 /* where to go, an auction attended (its green) */
#define GD_PAL_DIM 7

#define GD_SND_MOVE 0x8004513E
#define GD_SND_OK 0x8004503C
#define GD_SND_BACK 0x800450BD

enum { GD_QUEST, GD_AUCTIONS };
enum { GD_IDLE, GD_CLOSE, GD_TRAVEL };

static struct {
    s32 request; /* the menu's entry was chosen: the field's next idle frame opens the screen */
    s32 open;
    s32 fresh;   /* the opening frame: the pad is not read (the press that chose the entry is long gone, but so the
                  * Black Market's screen does) */
    s32 tab;
    s32 sel;     /* the Quest list's row */
    s32 asel;    /* the auction under the cursor */
    s32 armed;   /* the auction the confirm button chose once, or -1 */
    s32 action;
    s32 waiting; /* a text file was still loading when the tab was filled: fill again */
    const DefQuest *rows[GD_Q_ROWS];
    s32 row_count;
    MessageWindow *title, *tabs[2], *hint;
    MessageWindow *q_title, *q_text, *q_goto, *q_rows[GD_Q_ROWS];
    MessageWindow *a_rows[DEF_QUEST_AUCTIONS], *a_name, *a_price, *a_state, *a_hint;
    MessageCursor *cursor;
} gd;

static const u8 gd_none[1] = { 0 };

/* A text file once it is in, else NULL. cdload_is_loading queues a file it does not have, and get_file would read one
 * synchronously (a stall in the middle of the field's frame): so is_loading first, get_file once it says 0. */
static void *gd_file(s32 base) {
    if (cdload_module.is_loading(records_language + base)) {
        return NULL;
    }
    return cdload_module.files.get_file(records_language + base);
}

static s32 gd_append(u8 *out, s32 n, s32 size, const u8 *text) {
    s32 i;

    for (i = 0; text != NULL && text[i] != 0 && n < size - 1; i++) {
        out[n++] = text[i];
    }
    out[n] = 0;
    return n;
}

/* "Go to: East Sector - Seiryu City" for a map, from the game's own map titles; 0 while their files load, and an empty
 * text for a quest that sends nowhere. The window keeps the pointer: `out` is static at the caller. */
static s32 gd_place(s32 map, u8 *out, s32 size) {
    void *sectors, *maps;
    s32 i, n;

    out[0] = 0;
    if (map == 0) {
        return 1;
    }
    sectors = gd_file(GD_SECTORS);
    maps = gd_file(GD_MAPS);
    if (sectors == NULL || maps == NULL) {
        return 0;
    }
    for (i = 0; fieldstg_map_titles[i].map != 0; i++) {
        if (fieldstg_map_titles[i].map == map) {
            n = gd_append(out, 0, size, def_str_GuideGoTo);
            n = gd_append(out, n, size, font_get_entry(sectors, fieldstg_map_titles[i].text_0));
            n = gd_append(out, n, size, def_str_GuideSep);
            gd_append(out, n, size, font_get_entry(maps, fieldstg_map_titles[i].text_1));
            break;
        }
    }
    return 1;
}

static MessageWindow *gd_window(s32 font, s32 x, s32 y) {
    MessageWindow *w = message_create_window(GD_LAYER, font, x, y);

    w->set_ot_depth(w, 1);
    return w;
}

/* An opaque rectangle; the layer draws what is added last first, so frames go before fills. */
static void gd_rect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(GD_LAYER);
    POLY_F4 *poly = gfx_module.funcs.get_packet();

    setPolyF4(poly);
    setRGB0(poly, r, g, b);
    poly->x0 = poly->x2 = x;
    poly->x1 = poly->x3 = x + w;
    poly->y0 = poly->y1 = y;
    poly->y2 = poly->y3 = y + h;
    addPrim(layer->get_ot_entry(layer, 2), poly);
    gfx_module.funcs.set_packet(poly + 1);
}

/* One of the bestiary's panels: a frame, a fill, and the band of its heading. */
static void gd_panel(s32 x, s32 y, s32 w, s32 h, s32 band) {
    if (band) {
        gd_rect(x + 2, y + 2, w - 4, GD_LINE + 4, 0, 0, 74);
    }
    gd_rect(x + 1, y + 1, w - 2, h - 2, 0, 0, 33);
    gd_rect(x, y, w, h, 107, 107, 214);
}

/* The Quest tab's rows: the main quest, then the missions still open. */
static void gd_collect(void) {
    gd.rows[0] = def_quest_main();
    gd.row_count = 1 + def_quest_missions(&gd.rows[1], DEF_QUEST_MISSIONS);
    if (gd.sel >= gd.row_count) {
        gd.sel = 0;
    }
}

static void gd_fill_quest(void) {
    static u8 place[96]; /* the window keeps the pointer */
    const DefQuest *q = gd.rows[gd.sel];
    s32 i;

    for (i = 0; i < GD_Q_ROWS; i++) {
        if (i >= gd.row_count) {
            gd.q_rows[i]->set_text(gd.q_rows[i], (u8 *)gd_none, -1);
            continue;
        }
        gd.q_rows[i]->set_text(gd.q_rows[i], (u8 *)(i == 0 ? def_str_GuideMain : gd.rows[i]->title), -1);
        gd.q_rows[i]->set_palette(gd.q_rows[i], i == gd.sel ? GD_PAL_HOT : GD_PAL_TEXT);
    }
    if (q == NULL) { /* a story step the table has no row for */
        gd.q_title->set_text(gd.q_title, (u8 *)def_str_Neutral, -1);
        gd.q_text->set_text(gd.q_text, (u8 *)gd_none, -1);
        gd.q_goto->set_text(gd.q_goto, (u8 *)gd_none, -1);
    } else {
        gd.q_title->set_text(gd.q_title, (u8 *)q->title, -1);
        gd.q_text->set_text(gd.q_text, (u8 *)q->text, -1);
        gd.waiting = !gd_place(q->map, place, sizeof(place));
        gd.q_goto->set_text(gd.q_goto, place, -1);
        gd.q_goto->set_palette(gd.q_goto, GD_PAL_GOOD);
    }
    gd.q_title->set_palette(gd.q_title, GD_PAL_HOT);
    gd.hint->set_text(gd.hint, (u8 *)def_str_GuideKeysQuest, -1);
    gd.cursor->set_pos(gd.cursor, GD_ROW_X - 12 + gd.sel / GD_QL_ROWS * GD_COL_DX,
                       GD_QL_Y + 6 + gd.sel % GD_QL_ROWS * GD_LINE);
}

static s32 gd_state_palette(s32 state) {
    return state == DEF_AUCTION_OPEN ? GD_PAL_HOT : state == DEF_AUCTION_DONE ? GD_PAL_GOOD : GD_PAL_DIM;
}

static void gd_fill_auctions(void) {
    static u8 price[24]; /* the window keeps the pointer */
    void *items = gd_file(GD_ITEMS);
    s32 state = def_auction_state(gd.asel);
    s32 value = def_auction_price(gd.asel);
    s32 div, n = 0, i;

    gd.waiting = items == NULL;
    for (i = 0; i < DEF_QUEST_AUCTIONS; i++) {
        gd.a_rows[i]->set_text(gd.a_rows[i], items != NULL ? font_get_entry(items, def_auctions[i].item) : (u8 *)def_str_Unknown,
                               -1);
        gd.a_rows[i]->set_palette(gd.a_rows[i], gd_state_palette(def_auction_state(i)));
    }
    gd.a_name->set_text(gd.a_name, items != NULL ? font_get_entry(items, def_auctions[gd.asel].item) : (u8 *)def_str_Unknown,
                        -1);
    for (div = 10000000; div > 1 && value / div == 0; div /= 10) {
    }
    for (; div > 0; div /= 10) {
        price[n++] = 4 + value / div % 10; /* the font's digits (glyph 4 is 0) */
    }
    gd_append(price, n, sizeof(price), def_str_GuideBit);
    gd.a_price->set_text(gd.a_price, price, -1);
    gd.a_price->set_pos(gd.a_price, GD_PRICE_X, GD_AD_Y + 4);
    gd.a_price->measure(gd.a_price, 1); /* placed by its right end */
    gd.a_state->set_text(gd.a_state,
                         (u8 *)(state == DEF_AUCTION_OPEN ? def_str_AucOpen : state == DEF_AUCTION_DONE ? def_str_AucDone
                                : state == DEF_AUCTION_MISSED ? def_str_AucMissed : def_str_AucSoon), -1);
    gd.a_state->set_palette(gd.a_state, gd_state_palette(state));
    gd.a_hint->set_text(gd.a_hint,
                        (u8 *)(state == DEF_AUCTION_OPEN ? def_str_AucOpenHint : state == DEF_AUCTION_DONE ? def_str_AucDoneHint
                               : state == DEF_AUCTION_MISSED ? def_str_AucMissedHint : def_auctions[gd.asel].when), -1);
    gd.hint->set_text(gd.hint,
                      (u8 *)(gd.armed >= 0 ? def_str_GuideKeysTravel2
                             : def_guide_can_travel(gd.asel, gamestate_data.funcs.get_map()) ? def_str_GuideKeysTravel
                             : def_str_GuideKeysAuctions), -1);
    gd.cursor->set_pos(gd.cursor, GD_ROW_X - 12 + gd.asel / GD_A_ROWS * GD_COL_DX, GD_A_Y + 5 + gd.asel % GD_A_ROWS * GD_LINE);
}

static void gd_fill(void) {
    gd.waiting = 0;
    gd.tabs[GD_QUEST]->set_palette(gd.tabs[GD_QUEST], gd.tab == GD_QUEST ? GD_PAL_HOT : GD_PAL_DIM);
    gd.tabs[GD_AUCTIONS]->set_palette(gd.tabs[GD_AUCTIONS], gd.tab == GD_AUCTIONS ? GD_PAL_HOT : GD_PAL_DIM);
    if (gd.tab == GD_QUEST) {
        gd_fill_quest();
    } else {
        gd_fill_auctions();
    }
}

static void gd_show(s32 tab) {
    s32 quest = tab == GD_QUEST;
    s32 i;

    gd.tab = tab;
    gd.armed = -1;
    gd.q_title->set_visible(gd.q_title, quest);
    gd.q_text->set_visible(gd.q_text, quest);
    gd.q_goto->set_visible(gd.q_goto, quest);
    for (i = 0; i < GD_Q_ROWS; i++) {
        gd.q_rows[i]->set_visible(gd.q_rows[i], quest);
    }
    for (i = 0; i < DEF_QUEST_AUCTIONS; i++) {
        gd.a_rows[i]->set_visible(gd.a_rows[i], !quest);
    }
    gd.a_name->set_visible(gd.a_name, !quest);
    gd.a_price->set_visible(gd.a_price, !quest);
    gd.a_state->set_visible(gd.a_state, !quest);
    gd.a_hint->set_visible(gd.a_hint, !quest);
    gd_fill();
}

/* DW3_GUIDE_CHECK: every quest's and auction's text measured in the screen's own windows, a line logged for one wider
 * than its panel (the texts are broken by hand in tools/text_override/en.txt). */
static void gd_check(void) {
    s32 room = GD_W - 2 * (GD_TEXT_X - 2);
    s32 wide = 0, i;

    for (i = 0; i < def_quest_count + DEF_QUEST_MISSIONS + DEF_QUEST_AUCTIONS; i++) {
        const DefQuest *q = i < def_quest_count ? &def_quests[i]
                            : i < def_quest_count + DEF_QUEST_MISSIONS ? &def_missions[i - def_quest_count] : NULL;
        const u8 *text = q != NULL ? q->text : def_auctions[i - def_quest_count - DEF_QUEST_MISSIONS].when;

        gd.q_text->set_text(gd.q_text, (u8 *)text, -1);
        gd.q_text->measure(gd.q_text, 0);
        if (gd.q_text->measured_width > room) {
            wide++;
            port_log("guide: text %d is %d pixels wide, the panel holds %d", (int)i, (int)gd.q_text->measured_width, (int)room);
        }
    }
    port_log("guide: %d texts measured, %d too wide", (int)i, (int)wide);
}

static void gd_open(void) {
    s32 i;

    gd.title = gd_window(1, 8, 5);
    gd.title->set_text(gd.title, (u8 *)def_str_GuideTitle, -1);
    gd.tabs[GD_QUEST] = gd_window(1, GD_TAB_X, 5);
    gd.tabs[GD_QUEST]->set_text(gd.tabs[GD_QUEST], (u8 *)def_str_GuideTabQuest, -1);
    gd.tabs[GD_AUCTIONS] = gd_window(1, GD_TAB_X + GD_TAB_DX, 5);
    gd.tabs[GD_AUCTIONS]->set_text(gd.tabs[GD_AUCTIONS], (u8 *)def_str_GuideTabAuctions, -1);
    gd.q_title = gd_window(1, GD_TEXT_X, GD_Q_Y + 4);
    gd.q_text = gd_window(1, GD_TEXT_X, GD_Q_Y + 22);
    gd.q_text->set_page_lines(gd.q_text, GD_Q_LINES);
    gd.q_goto = gd_window(1, GD_TEXT_X, GD_Q_Y + 22 + GD_Q_LINES * GD_LINE + 2);
    for (i = 0; i < GD_Q_ROWS; i++) {
        gd.q_rows[i] = gd_window(1, GD_ROW_X + i / GD_QL_ROWS * GD_COL_DX, GD_QL_Y + 6 + i % GD_QL_ROWS * GD_LINE);
    }
    for (i = 0; i < DEF_QUEST_AUCTIONS; i++) {
        gd.a_rows[i] = gd_window(2, GD_ROW_X + i / GD_A_ROWS * GD_COL_DX, GD_A_Y + 5 + i % GD_A_ROWS * GD_LINE);
    }
    gd.a_name = gd_window(1, GD_TEXT_X, GD_AD_Y + 4);
    gd.a_price = gd_window(1, GD_PRICE_X, GD_AD_Y + 4);
    gd.a_state = gd_window(1, GD_TEXT_X, GD_AD_Y + 4 + GD_LINE);
    gd.a_hint = gd_window(1, GD_TEXT_X, GD_AD_Y + 4 + 2 * GD_LINE);
    gd.a_hint->set_page_lines(gd.a_hint, 2);
    gd.hint = gd_window(1, 8, GD_HINT_Y + 2);
    gd.hint->set_page_lines(gd.hint, 2);
    gd.cursor = message_create_cursor(GD_LAYER, 1, 0, 0);
    gd.cursor->show(gd.cursor, 1);
    gd.open = 1;
    gd.fresh = 1;
    gd.request = 0;
    gd.sel = 0;
    gd.asel = def_auction_open() >= 0 ? def_auction_open() : 0; /* the list opens on the auction open now */
    gd_collect();
    if (getenv("DW3_GUIDE_CHECK") != NULL) {
        gd_check();
    }
    gd_show(GD_QUEST);
    port_log("guide: frame %ld: open on map 0x%X: progress %d, quest row %d, %d mission(s), auction open %d",
             port_frames, gamestate_data.funcs.get_map(), (int)gamestate_data.progress, def_quest_main_index(),
             (int)gd.row_count - 1, def_auction_open());
}

/* Every window of the screen, for the loops that run and stop them. */
static s32 gd_windows(MessageWindow ***all) {
    s32 n = 0, i;

    all[n++] = &gd.title;
    all[n++] = &gd.tabs[0];
    all[n++] = &gd.tabs[1];
    all[n++] = &gd.hint;
    all[n++] = &gd.q_title;
    all[n++] = &gd.q_text;
    all[n++] = &gd.q_goto;
    for (i = 0; i < GD_Q_ROWS; i++) {
        all[n++] = &gd.q_rows[i];
    }
    for (i = 0; i < DEF_QUEST_AUCTIONS; i++) {
        all[n++] = &gd.a_rows[i];
    }
    all[n++] = &gd.a_name;
    all[n++] = &gd.a_price;
    all[n++] = &gd.a_state;
    all[n++] = &gd.a_hint;
    return n;
}
#define GD_WINDOWS (11 + GD_Q_ROWS + DEF_QUEST_AUCTIONS)

static void gd_close(void) {
    MessageWindow **all[GD_WINDOWS];
    s32 n = gd_windows(all), i;

    for (i = 0; i < n; i++) {
        heap_objects.stop((Object *)*all[i]);
        *all[i] = NULL;
    }
    heap_objects.stop((Object *)gd.cursor);
    gd.cursor = NULL;
    gd.open = 0;
    gd.request = 0;
}

/* The windows belong to no object's data block, so nothing runs them but this (as on the Guardromon screen). */
static void gd_run_windows(void) {
    MessageWindow **all[GD_WINDOWS];
    s32 n = gd_windows(all), i;

    for (i = 0; i < n; i++) {
        heap_objects.try_run((Object *)*all[i]);
    }
    heap_objects.try_run((Object *)gd.cursor);
}

static void gd_draw(void) {
    gd_panel(0, 0, 320, GD_TITLE_H, 0);
    if (gd.tab == GD_QUEST) {
        gd_panel(2, GD_Q_Y, GD_W, GD_Q_H, 1);
        gd_panel(2, GD_QL_Y, GD_W, GD_QL_H, 0);
    } else {
        /* the auction the confirm button chose once: a bar behind its name (added before the panel's fill, which covers
         * all but it) */
        if (gd.armed >= 0) {
            gd_rect(5 + gd.armed / GD_A_ROWS * GD_COL_DX, GD_A_Y + 3 + gd.armed % GD_A_ROWS * GD_LINE, GD_COL_DX - 8,
                    GD_LINE, 74, 41, 0);
        }
        gd_panel(2, GD_A_Y, GD_W, GD_A_H, 0);
        gd_panel(2, GD_AD_Y, GD_W, GD_AD_H, 1);
    }
    gd_panel(0, GD_HINT_Y, 320, 240 - GD_HINT_Y, 0);
}

static void gd_quest_input(void) {
    s32 sel = gd.sel;

    if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
        sel = (sel + gd.row_count - 1) % gd.row_count;
    } else if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
        sel = (sel + 1) % gd.row_count;
    } else if (PAD_PRESSED(10) || PAD_PRESSED(11) || PAD_PRESSED(5) || PAD_PRESSED(7)) {
        sound_module.play(GD_SND_MOVE);
        gd_show(GD_AUCTIONS);
        return;
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(GD_SND_BACK);
        gd.action = GD_CLOSE;
        return;
    }
    if (sel != gd.sel) {
        gd.sel = sel;
        sound_module.play(GD_SND_MOVE);
        gd_fill();
    }
}

static void gd_auction_input(void) {
    s32 sel = gd.asel;

    if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
        sel = sel / GD_A_ROWS * GD_A_ROWS + (sel % GD_A_ROWS + GD_A_ROWS - 1) % GD_A_ROWS;
    } else if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
        sel = sel / GD_A_ROWS * GD_A_ROWS + (sel % GD_A_ROWS + 1) % GD_A_ROWS;
    } else if (PAD_PRESSED(5) || PAD_PRESSED(7)) {
        sel = (sel + GD_A_ROWS) % DEF_QUEST_AUCTIONS;
    } else if (PAD_PRESSED(10) || PAD_PRESSED(11)) {
        sound_module.play(GD_SND_MOVE);
        gd_show(GD_QUEST);
        return;
    } else if (PAD_PRESSED(13)) {
        if (!def_guide_can_travel(gd.asel, gamestate_data.funcs.get_map())) {
            sound_module.play(GD_SND_BACK);
        } else if (gd.armed == gd.asel) {
            sound_module.play(GD_SND_OK);
            gd.action = GD_TRAVEL;
        } else {
            gd.armed = gd.asel;
            sound_module.play(GD_SND_OK);
            gd_fill();
        }
        return;
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(GD_SND_BACK);
        if (gd.armed >= 0) {
            gd.armed = -1;
            gd_fill();
        } else {
            gd.action = GD_CLOSE;
        }
        return;
    }
    if (sel != gd.asel) {
        gd.asel = sel;
        gd.armed = -1;
        sound_module.play(GD_SND_MOVE);
        gd_fill();
    }
}

void def_guide_request(void) {
    if (!def_guide_active() || gd.open) {
        return;
    }
    /* the names the screen reads, queued while the menu closes */
    cdload_module.queue_file(records_language + GD_SECTORS);
    cdload_module.queue_file(records_language + GD_MAPS);
    cdload_module.queue_file(records_language + GD_ITEMS);
    gd.request = 1;
}

int def_guide_frame(void) {
    s32 action;

    if (!gd.open) {
        if (!gd.request) {
            return 0;
        }
        gd_open();
    }
    fieldstg_stage.event_running = 1;
    gd.action = GD_IDLE;
    if (gd.fresh) {
        gd.fresh = 0;
    } else if (gd.tab == GD_QUEST) {
        gd_quest_input();
    } else {
        gd_auction_input();
    }
    if (gd.action == GD_IDLE) {
        if (gd.waiting) {
            gd_fill(); /* a text file came in */
        }
        gd_run_windows();
        gd_draw();
        return 1;
    }
    action = gd.action;
    gd.action = GD_IDLE;
    gd_close();
    fieldstg_stage.event_running = 0;
    if (action == GD_TRAVEL) {
        port_log("guide: frame %ld: travel to map 0x%X for auction %d", port_frames, DEF_GUIDE_TRAVEL_MAP, (int)gd.asel);
        def_hunt_chain_reset(); /* a teleport ends the hunt's chain, as the Guardromon's does */
        fieldstg_goto_map(DEF_GUIDE_TRAVEL_MAP, 0, 0, 0, 0); /* entry 0: the stage's own start position */
    }
    return 1;
}

/* ---- the notices ---- */

#define NC_LAYER 0x1004 /* the Rush's layer, over the field's sprites */
#define NC_PAD 8
#define NC_MIN_W 120
#define NC_H 34
#define NC_Y 6

static struct {
    s32 kind, arg;
    s32 left; /* 1/60 s still to show; 0: no notice */
    s32 w;
    MessageWindow *label, *title;
    s32 label_w, title_w;
} nc;

/* A text's window placed by its right end (message_measure), as the Rush's panel places its own. */
static MessageWindow *nc_window(s32 font, const u8 *text, s32 palette, s32 *width) {
    MessageWindow *w = message_create_window(NC_LAYER, font, 0, 0);

    w->set_ot_depth(w, 0);
    w->set_text(w, (u8 *)text, -1);
    w->set_palette(w, (u8)palette);
    w->measure(w, 1);
    *width = w->measured_width;
    return w;
}

/* The notice's two windows; 0 while the auction's item name is still loading. */
static s32 nc_make(void) {
    const u8 *title;

    if (nc.kind == DEF_NOTICE_AUCTION) {
        void *items = gd_file(GD_ITEMS);

        if (items == NULL) {
            return 0;
        }
        title = font_get_entry(items, def_auctions[nc.arg].item);
    } else {
        title = def_quests[nc.arg].title;
    }
    nc.label = nc_window(2, nc.kind == DEF_NOTICE_AUCTION ? def_str_NoticeAuction : def_str_NoticeQuest, GD_PAL_HOT,
                         &nc.label_w);
    nc.title = nc_window(1, title, GD_PAL_TEXT, &nc.title_w);
    nc.w = (nc.label_w > nc.title_w ? nc.label_w : nc.title_w) + 2 * NC_PAD;
    if (nc.w < NC_MIN_W) {
        nc.w = NC_MIN_W;
    }
    return 1;
}

static void nc_quad(GfxLayer *layer, s32 x, s32 y, s32 w, s32 h, s32 abr, u8 r, u8 g, u8 b) {
    POLY_F4 *poly = gfx_module.funcs.get_packet();
    DR_TPAGE *tpage = (DR_TPAGE *)(poly + 1);
    u32 *ot = layer->get_ot_entry(layer, 0);

    setPolyF4(poly);
    setSemiTrans(poly, 1);
    setRGB0(poly, r, g, b);
    setXY4(poly, x, y, x + w, y, x, y + h, x + w, y + h);
    addPrim(ot, poly);
    SetDrawTPage(tpage, 0, 1, getTPage(1, abr, 0, 0));
    addPrim(ot, tpage);
    gfx_module.funcs.set_packet(tpage + 1);
}

static void nc_close(void) {
    heap_objects.stop((Object *)nc.label);
    heap_objects.stop((Object *)nc.title);
    nc.label = nc.title = NULL;
    nc.left = 0;
}

void def_notice_frame(void) {
    Object *mgr;
    GfxLayer *layer;
    s32 x, y, shown;

    if (!def_guide_active()) {
        return;
    }
    mgr = heap_objects.find(7, -1, -1);
    layer = gfx_module.funcs.get_layer(NC_LAYER);
    if (fieldstg_stage.menu_open != 0 || fieldstg_stage.title_shown != 0 || fieldstg_stage.event_running != 0 ||
        fieldstg_stage.battle_starting != 0 || mgr == NULL || mgr->state != OBJECT_STATE_RUN || layer == NULL) {
        return; /* the field is held: the notice waits, and so does its clock */
    }
    if (nc.left <= 0) {
        nc.kind = def_quest_notice(&nc.arg);
        if (nc.kind == DEF_NOTICE_NONE) {
            return;
        }
        nc.left = DEF_NOTICE_TICKS;
        port_log("guide: frame %ld: notice %d (%d) at progress %d", port_frames, (int)nc.kind, (int)nc.arg,
                 (int)gamestate_data.progress);
    }
    if (nc.label == NULL && !nc_make()) {
        return;
    }
    /* slides in from above, holds, slides back out */
    shown = DEF_NOTICE_TICKS - nc.left < nc.left ? DEF_NOTICE_TICKS - nc.left : nc.left;
    y = shown >= DEF_NOTICE_FADE ? NC_Y : NC_Y - (NC_H + NC_Y) * (DEF_NOTICE_FADE - shown) / DEF_NOTICE_FADE;
    x = (320 - nc.w) / 2;
    nc.label->set_pos(nc.label, 160 + nc.label_w / 2, y + 4);
    nc.title->set_pos(nc.title, 160 + nc.title_w / 2, y + 16);
    nc.label = (MessageWindow *)heap_objects.try_run((Object *)nc.label);
    nc.title = (MessageWindow *)heap_objects.try_run((Object *)nc.title);
    nc_quad(layer, x, y, nc.w, 1, 1, 60, 130, 230);
    nc_quad(layer, x, y + NC_H - 1, nc.w, 1, 1, 60, 130, 230);
    nc_quad(layer, x, y, nc.w, NC_H, 2, 225, 215, 160);
    nc.left -= gfx_module.funcs.get_frame_ticks();
    if (nc.left <= 0) {
        nc_close();
    }
}

/* A map's load took the field's heap, and the windows with it: the notice still due makes them again. */
void def_notice_reset(void) {
    nc.label = nc.title = NULL;
}

void def_guide_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, gd);
    PORT_STATE_VAR(s, nc);
    def_quest_savestate(s);
}
