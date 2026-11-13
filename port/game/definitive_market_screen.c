/* PRD 4 layer G: the Black Market's trader and its screen (the rules are definitive_market.c).
 *
 * The trader is one placed actor of the field (traders below: a map and the actor's FieldstgPlacedActor.id, which is
 * its object's key1), a figure of a secondary area chosen with tools/bm_npc_candidates.py. Talking to it does not
 * start its own talk: FIELDSTG's talk asks def_bm_talk first, and when that takes it the screen opens on the field's
 * next idle frame (def_bm_frame, from fieldstg_manager_update's idle step, after the Guardromon's dw3_travel_frame).
 *
 * The screen is a field overlay drawn as the Guardromon network's (definitive_travel.c): the bestiary's panels in
 * polygons, the game's font in message windows that only this file runs, fieldstg_stage.event_running held while it is
 * up (that freezes the player, the map's events and the encounters). Two tabs, L1/R1 (or left/right) to switch:
 *  - Exchange: the rare items held, one row each ("Datamon IV x2"); the confirm button on one picks the common to
 *    receive (up/down or left/right over the DEF_HUNT_MATERIALS), and each further press trades one rare item;
 *  - Replicas: def_replicas with their Bits; the cost line names the rare items the purchase would take, and the
 *    confirm button pressed twice buys.
 * A result or a refusal shows in one of the detail lines until the cursor moves.
 *
 * Off in a replay script (def_on): the trader talks as the game has it. */
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

#include "definitive.h"
#include "port_runtime.h"

extern const unsigned char def_str_BMTitle[], def_str_BMTabTrade[], def_str_BMTabReplica[], def_str_BMGive[],
    def_str_BMGet[], def_str_BMHeld[], def_str_BMCost[], def_str_BMPrice[], def_str_BMBits[], def_str_BMNone[],
    def_str_BMTraded[], def_str_BMBought[], def_str_BMArmed[], def_str_BMNoRoom[], def_str_BMNeedA[], def_str_BMNeedB[],
    def_str_BMNoBits[], def_str_BMBagFull[], def_str_BMKeysTrade[], def_str_BMKeysPick[], def_str_BMKeysReplica[],
    def_str_BMKeysArmed[], def_str_HuntSpace[], def_str_HuntHeld[], def_str_RecipeSep[], def_str_Slash[];

/* The trader: a figure whose own talk sets no flag and that no event script moves, so taking its talk over loses
 * nothing (tools/bm_npc_candidates.py lists the towns' figures with their lines). The pick is to confirm on the lab with
 * the lines decoded [owner]; a map's copy in the second half of the game keeps the trader there too. */
typedef struct BmTrader {
    s16 map;   /* the field map */
    s16 actor; /* FieldstgPlacedActor.id: its actor's key1 */
} BmTrader;
static const BmTrader traders[] = {
    { 0x20C, 42 }, /* WSTAG240, in the first town (the inn 0x20A's talk file): one line (entry 34), always there */
    { 0x27B, 42 }, /* WSTAG930, the same house in the second half of the game (entry 128) */
};
#define BM_TRADERS ((s32)(sizeof(traders) / sizeof(traders[0])))

/* The layout (320x240), the Guardromon screen's: a title bar, the list, two detail lines and the keys. */
#define BM_LAYER 0x1002 /* the field's window layer (the field menu's) */
#define BM_LINE 14      /* a row's pitch */
#define BM_TITLE_H 23
#define BM_ROWS 9       /* rows the list shows; it scrolls */
#define BM_TOP 30       /* the list's first row */
#define BM_LIST_X 2
#define BM_LIST_W 316
#define BM_NAME_X 18    /* a row's name, clear of the cursor */
#define BM_COUNT_X 214  /* its count (Exchange) or price (Replicas) */
#define BM_DETAIL_Y 162 /* the detail panel; its two lines below */
#define BM_DETAIL_H 42
#define BM_HINT_Y 208   /* the keys, two lines of the 14-pixel font */
#define BM_TAB_X 150
#define BM_TAB_DX 86
#define BM_PAL_TEXT 0
#define BM_PAL_HOT 3  /* the row being traded or bought, a line that is short (the bestiary's orange) */
#define BM_PAL_HELD 4 /* a replica the player holds (its green) */
#define BM_PAL_DIM 7  /* the other tab, a price above the Bits held */

#define BM_SND_MOVE 0x8004513E
#define BM_SND_OK 0x8004503C
#define BM_SND_BACK 0x800450BD

#define BM_FILE_ITEMS (records_language + 0x6A) /* the items' names (?SITMNAM): an entry per item ID */

enum { BM_TRADE, BM_REPLICA };       /* the tabs */
enum { BM_LIST, BM_PICK, BM_ARMED }; /* the list; the Exchange's common being chosen; a replica chosen once */
enum { BM_NOTE_NONE = -1 };

static struct {
    s32 request; /* the trader was talked to: the field's next idle frame opens the screen (the in-game test pokes it) */
    s32 open;
    s32 fresh; /* the frame it opened: the press that talked is not read again */
    s32 close;
    s32 tab;
    s32 mode;
    s32 sel; /* the row under the cursor */
    s32 top; /* the first row shown */
    s32 rows;
    s32 common; /* the common the Exchange gives (kept between visits) */
    s32 note;   /* a DEF_BM_* result shown in a detail line, or BM_NOTE_NONE */
    s32 layer;
    s16 rares[DEF_HUNT_RARES]; /* the Exchange's rows: the rare items held, by index of def_hunt_targets */
    MessageWindow *title, *tabs[2], *names[BM_ROWS], *counts[BM_ROWS], *lines[2], *hint;
    MessageCursor *cursor;
} bm_ui;

/* The composed texts (a window copies its text when it is set, message_copy_line_text, so these are only scratch). */
static unsigned char bm_row_text[BM_ROWS][DEF_HUNT_LINE_SIZE];
static unsigned char bm_count_text[BM_ROWS][16];
static unsigned char bm_line_text[2][DEF_HUNT_LINE_SIZE * 2];
static const unsigned char bm_empty[1] = { 0 };

/* A number in the game's digits (glyph 4 + d), any size (def_glyph_count stops at 99). */
static void bm_put_number(unsigned char *buf, int *p, int size, s32 n) {
    unsigned char digits[12];
    int k = 0;

    if (n < 0) {
        n = 0;
    }
    do {
        digits[k++] = (unsigned char)(4 + n % 10);
        n /= 10;
    } while (n > 0 && k < (int)sizeof(digits));
    while (k > 0 && *p < size - 1) {
        buf[(*p)++] = digits[--k];
    }
    buf[*p] = 0;
}

static MessageWindow *bm_window(s32 font, s32 x, s32 y) {
    MessageWindow *w = message_create_window(bm_ui.layer, font, x, y);

    w->set_ot_depth(w, 1);
    return w;
}

/* An opaque rectangle; the layer draws what is added last first, so frames go before fills. */
static void bm_rect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(bm_ui.layer);
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

static void bm_panel(s32 x, s32 y, s32 w, s32 h, s32 band) {
    if (band) {
        bm_rect(x + 2, y + 2, w - 4, BM_LINE + 2, 0, 0, 74);
    }
    bm_rect(x + 1, y + 1, w - 2, h - 2, 0, 0, 33);
    bm_rect(x, y, w, h, 107, 107, 214);
}

/* The tab's rows: the replicas, or the rare items held (`keep`: the cursor stays on the rare it was on, while that is
 * still there). */
static void bm_collect(s32 keep) {
    s32 was = keep && bm_ui.tab == BM_TRADE && bm_ui.sel < bm_ui.rows ? bm_ui.rares[bm_ui.sel] : -1;
    s32 t;

    if (bm_ui.tab == BM_REPLICA) {
        bm_ui.rows = def_replica_count < DEF_REPLICAS ? def_replica_count : DEF_REPLICAS;
    } else {
        bm_ui.rows = 0;
        for (t = 0; t < def_hunt_target_count && t < DEF_HUNT_RARES; t++) {
            if (def_record_rare(t) > 0) {
                if (t == was) {
                    bm_ui.sel = bm_ui.rows;
                }
                bm_ui.rares[bm_ui.rows++] = (s16)t;
            }
        }
    }
    if (bm_ui.sel >= bm_ui.rows) {
        bm_ui.sel = bm_ui.rows > 0 ? bm_ui.rows - 1 : 0;
    }
    if (bm_ui.sel < bm_ui.top) {
        bm_ui.top = bm_ui.sel;
    } else if (bm_ui.sel >= bm_ui.top + BM_ROWS) {
        bm_ui.top = bm_ui.sel - BM_ROWS + 1;
    }
}

/* A result's text for the detail lines; NULL for what the tab's lines show by themselves (a replica's missing bosses
 * are already its first line, in orange). */
static const unsigned char *bm_note_text(void) {
    switch (bm_ui.note) {
    case DEF_BM_OK:
        return bm_ui.tab == BM_TRADE ? def_str_BMTraded : def_str_BMBought;
    case DEF_BM_NO_RARE:
        return def_str_BMNone;
    case DEF_BM_NO_ROOM:
        return def_str_BMNoRoom;
    case DEF_BM_NO_BITS:
        return def_str_BMNoBits;
    case DEF_BM_BAG_FULL:
        return def_str_BMBagFull;
    }
    return NULL;
}

/* The detail of the Exchange: "Give: Datamon IV x1" and "Get: Data Chip x12   Held: 30/99" (orange while it is the
 * line being chosen, or while the lot would not fit). */
static void bm_fill_trade(void) {
    unsigned char *a = bm_line_text[0], *b = bm_line_text[1];
    int pa = 0, pb = 0, size = sizeof(bm_line_text[0]);
    s32 rare = bm_ui.sel < bm_ui.rows ? bm_ui.rares[bm_ui.sel] : -1;

    a[0] = b[0] = 0;
    if (rare < 0) {
        bm_ui.lines[0]->set_text(bm_ui.lines[0], (u8 *)def_str_BMNone, -1);
        bm_ui.lines[1]->set_text(bm_ui.lines[1], (u8 *)bm_empty, -1);
        return;
    }
    def_glyph_put(a, &pa, size, def_str_BMGive);
    def_hunt_target_name(a, &pa, size, rare);
    def_glyph_put(a, &pa, size, def_str_HuntSpace);
    def_glyph_put(a, &pa, size, def_str_HuntHeld);
    bm_put_number(a, &pa, size, 1);
    def_glyph_put(b, &pb, size, def_str_BMGet);
    def_glyph_put(b, &pb, size, def_hunt_material_name(bm_ui.common));
    def_glyph_put(b, &pb, size, def_str_HuntSpace);
    def_glyph_put(b, &pb, size, def_str_HuntHeld);
    bm_put_number(b, &pb, size, DEF_BM_COMMONS);
    def_glyph_put(b, &pb, size, def_str_BMHeld);
    bm_put_number(b, &pb, size, def_record_material(bm_ui.common));
    def_glyph_put(b, &pb, size, def_str_Slash);
    bm_put_number(b, &pb, size, DEF_HUNT_MATERIAL_CAP);
    bm_ui.lines[0]->set_text(bm_ui.lines[0], (u8 *)a, -1);
    bm_ui.lines[1]->set_text(bm_ui.lines[1], (u8 *)b, -1);
    bm_ui.lines[1]->set_palette(bm_ui.lines[1],
                                bm_ui.mode == BM_PICK || def_bm_exchange_check(rare, bm_ui.common) == DEF_BM_NO_ROOM
                                    ? BM_PAL_HOT
                                    : BM_PAL_TEXT);
}

/* The detail of a replica: "Cost: Datamon IV, Bulbmon III" (the rare items it would take) and "Price: 40000 Bits
 * Held: 123456", orange while short; armed, the warning that a replica resells for nothing. */
static void bm_fill_replica(void) {
    unsigned char *a = bm_line_text[0], *b = bm_line_text[1];
    int pa = 0, pb = 0, size = sizeof(bm_line_text[0]), picks[DEF_BM_REPLICA_RARES], k;
    s32 r = bm_ui.sel;
    s32 ok = def_bm_replica_pick(picks);

    a[0] = b[0] = 0;
    if (ok) {
        def_glyph_put(a, &pa, size, def_str_BMCost);
        for (k = 0; k < DEF_BM_REPLICA_RARES; k++) {
            if (k > 0) {
                def_glyph_put(a, &pa, size, def_str_RecipeSep);
            }
            def_hunt_target_name(a, &pa, size, picks[k]);
        }
    } else {
        def_glyph_put(a, &pa, size, def_str_BMNeedA);
        bm_put_number(a, &pa, size, DEF_BM_REPLICA_RARES);
        def_glyph_put(a, &pa, size, def_str_BMNeedB);
    }
    bm_ui.lines[0]->set_text(bm_ui.lines[0], (u8 *)a, -1);
    bm_ui.lines[0]->set_palette(bm_ui.lines[0], ok ? BM_PAL_TEXT : BM_PAL_HOT);
    if (bm_ui.mode == BM_ARMED) {
        bm_ui.lines[1]->set_text(bm_ui.lines[1], (u8 *)def_str_BMArmed, -1);
        bm_ui.lines[1]->set_palette(bm_ui.lines[1], BM_PAL_HOT);
        return;
    }
    def_glyph_put(b, &pb, size, def_str_BMPrice);
    bm_put_number(b, &pb, size, def_replicas[r].bits);
    def_glyph_put(b, &pb, size, def_str_BMBits);
    def_glyph_put(b, &pb, size, def_str_BMHeld);
    bm_put_number(b, &pb, size, gamestate_data.money);
    bm_ui.lines[1]->set_text(bm_ui.lines[1], (u8 *)b, -1);
    bm_ui.lines[1]->set_palette(bm_ui.lines[1], gamestate_data.money < def_replicas[r].bits ? BM_PAL_HOT : BM_PAL_TEXT);
}

/* Fills the windows from the state. */
static void bm_fill(void) {
    const unsigned char *note;
    s32 i, k, palette;
    int p;

    bm_ui.tabs[0]->set_palette(bm_ui.tabs[0], bm_ui.tab == BM_TRADE ? BM_PAL_HOT : BM_PAL_DIM);
    bm_ui.tabs[1]->set_palette(bm_ui.tabs[1], bm_ui.tab == BM_REPLICA ? BM_PAL_HOT : BM_PAL_DIM);
    for (i = 0; i < BM_ROWS; i++) {
        k = bm_ui.top + i;
        p = 0;
        bm_row_text[i][0] = bm_count_text[i][0] = 0;
        palette = k == bm_ui.sel && bm_ui.mode != BM_LIST ? BM_PAL_HOT : BM_PAL_TEXT;
        if (k >= bm_ui.rows) {
            bm_ui.names[i]->set_text(bm_ui.names[i], (u8 *)bm_empty, -1);
            bm_ui.counts[i]->set_text(bm_ui.counts[i], (u8 *)bm_empty, -1);
            continue;
        }
        if (bm_ui.tab == BM_TRADE) {
            def_hunt_target_name(bm_row_text[i], &p, sizeof(bm_row_text[i]), bm_ui.rares[k]);
            bm_ui.names[i]->set_text(bm_ui.names[i], (u8 *)bm_row_text[i], -1);
            p = 0;
            def_glyph_put(bm_count_text[i], &p, sizeof(bm_count_text[i]), def_str_HuntHeld);
            bm_put_number(bm_count_text[i], &p, sizeof(bm_count_text[i]), def_record_rare(bm_ui.rares[k]));
            bm_ui.counts[i]->set_palette(bm_ui.counts[i], palette);
        } else {
            bm_ui.names[i]->set_text(bm_ui.names[i], cdload_module.files.get_file(BM_FILE_ITEMS), def_replicas[k].item);
            if (palette == BM_PAL_TEXT && def_record_replica(k)) {
                palette = BM_PAL_HELD;
            }
            bm_put_number(bm_count_text[i], &p, sizeof(bm_count_text[i]), def_replicas[k].bits);
            bm_ui.counts[i]->set_palette(bm_ui.counts[i],
                                         gamestate_data.money < def_replicas[k].bits ? BM_PAL_DIM : BM_PAL_TEXT);
        }
        bm_ui.names[i]->set_palette(bm_ui.names[i], palette);
        bm_ui.counts[i]->set_text(bm_ui.counts[i], (u8 *)bm_count_text[i], -1);
    }
    bm_ui.lines[0]->set_palette(bm_ui.lines[0], BM_PAL_TEXT);
    if (bm_ui.tab == BM_TRADE) {
        bm_fill_trade();
    } else {
        bm_fill_replica();
    }
    /* a result takes the line that changes least: the Exchange's "Give:" (its "Get:" line shows the count going up as
     * the player trades on), a replica's price */
    note = bm_note_text();
    if (note != NULL) {
        MessageWindow *at = bm_ui.lines[bm_ui.tab == BM_TRADE ? 0 : 1];

        at->set_text(at, (u8 *)note, -1);
        at->set_palette(at, bm_ui.note == DEF_BM_OK ? BM_PAL_HELD : BM_PAL_HOT);
    }
    if (bm_ui.tab == BM_TRADE) {
        bm_ui.hint->set_text(bm_ui.hint, (u8 *)(bm_ui.mode == BM_PICK ? def_str_BMKeysPick : def_str_BMKeysTrade), -1);
    } else {
        bm_ui.hint->set_text(bm_ui.hint, (u8 *)(bm_ui.mode == BM_ARMED ? def_str_BMKeysArmed : def_str_BMKeysReplica),
                             -1);
    }
    if (bm_ui.mode == BM_PICK) {
        bm_ui.cursor->set_pos(bm_ui.cursor, BM_LIST_X + 4, BM_DETAIL_Y + 4 + BM_LINE);
    } else {
        bm_ui.cursor->set_pos(bm_ui.cursor, BM_LIST_X + 4, BM_TOP + (bm_ui.sel - bm_ui.top) * BM_LINE);
    }
    bm_ui.cursor->show(bm_ui.cursor, bm_ui.rows > 0);
}

static void bm_open(void) {
    FieldstgActor *player = (FieldstgActor *)heap_objects.find(5, -1, 0);
    s32 i;

    /* the talk that was skipped would have stopped the player: a press while walking leaves its walking step on, so it
     * is set to stand as the field menu does when it opens (fieldstg_manager_open_menu) */
    if (player != NULL) {
        player->base.set_step(player, 1);
    }
    bm_ui.layer = BM_LAYER;
    bm_ui.title = bm_window(1, 8, 5);
    bm_ui.title->set_text(bm_ui.title, (u8 *)def_str_BMTitle, -1);
    bm_ui.tabs[0] = bm_window(1, BM_TAB_X, 5);
    bm_ui.tabs[0]->set_text(bm_ui.tabs[0], (u8 *)def_str_BMTabTrade, -1);
    bm_ui.tabs[1] = bm_window(1, BM_TAB_X + BM_TAB_DX, 5);
    bm_ui.tabs[1]->set_text(bm_ui.tabs[1], (u8 *)def_str_BMTabReplica, -1);
    for (i = 0; i < BM_ROWS; i++) {
        bm_ui.names[i] = bm_window(2, BM_NAME_X, BM_TOP + i * BM_LINE);
        bm_ui.counts[i] = bm_window(2, BM_COUNT_X, BM_TOP + i * BM_LINE);
    }
    for (i = 0; i < 2; i++) {
        bm_ui.lines[i] = bm_window(2, BM_NAME_X, BM_DETAIL_Y + 4 + i * BM_LINE);
    }
    bm_ui.hint = bm_window(1, 8, BM_HINT_Y + 2);
    bm_ui.hint->set_page_lines(bm_ui.hint, 2);
    bm_ui.cursor = message_create_cursor(bm_ui.layer, 1, BM_LIST_X + 4, BM_TOP);
    bm_ui.open = 1;
    bm_ui.fresh = 1;
    bm_ui.request = 0;
    bm_ui.close = 0;
    bm_ui.tab = BM_TRADE;
    bm_ui.mode = BM_LIST;
    bm_ui.sel = bm_ui.top = 0;
    bm_ui.note = BM_NOTE_NONE;
    if (bm_ui.common < 0 || bm_ui.common >= DEF_HUNT_MATERIALS) {
        bm_ui.common = 0;
    }
    bm_collect(0);
    bm_fill();
    port_log("market: frame %ld: the Black Market on map 0x%X (%d rare items held)", port_frames,
             gamestate_data.funcs.get_map(), bm_ui.rows);
}

static void bm_close(void) {
    MessageWindow **all[] = { &bm_ui.title, &bm_ui.tabs[0], &bm_ui.tabs[1], &bm_ui.lines[0], &bm_ui.lines[1],
                              &bm_ui.hint };
    s32 i;

    for (i = 0; i < (s32)(sizeof(all) / sizeof(all[0])); i++) {
        heap_objects.stop((Object *)*all[i]);
        *all[i] = NULL;
    }
    for (i = 0; i < BM_ROWS; i++) {
        heap_objects.stop((Object *)bm_ui.names[i]);
        heap_objects.stop((Object *)bm_ui.counts[i]);
        bm_ui.names[i] = bm_ui.counts[i] = NULL;
    }
    heap_objects.stop((Object *)bm_ui.cursor);
    bm_ui.cursor = NULL;
    bm_ui.open = 0;
    bm_ui.request = 0;
}

static void bm_run_windows(void) {
    s32 i;

    heap_objects.try_run((Object *)bm_ui.title);
    heap_objects.try_run((Object *)bm_ui.tabs[0]);
    heap_objects.try_run((Object *)bm_ui.tabs[1]);
    for (i = 0; i < BM_ROWS; i++) {
        heap_objects.try_run((Object *)bm_ui.names[i]);
        heap_objects.try_run((Object *)bm_ui.counts[i]);
    }
    heap_objects.try_run((Object *)bm_ui.lines[0]);
    heap_objects.try_run((Object *)bm_ui.lines[1]);
    heap_objects.try_run((Object *)bm_ui.hint);
    heap_objects.try_run((Object *)bm_ui.cursor);
}

static void bm_draw(void) {
    /* the row being traded or bought: a bar behind it (drawn before the panels' fill, which then covers all but it) */
    if (bm_ui.mode != BM_LIST && bm_ui.sel >= bm_ui.top && bm_ui.sel < bm_ui.top + BM_ROWS) {
        bm_rect(BM_LIST_X + 3, BM_TOP + (bm_ui.sel - bm_ui.top) * BM_LINE - 2, BM_LIST_W - 6, BM_LINE, 74, 41, 0);
    }
    bm_panel(0, 0, 320, BM_TITLE_H, 1);
    bm_panel(BM_LIST_X, BM_TOP - 5, BM_LIST_W, BM_ROWS * BM_LINE + 8, 0);
    bm_panel(BM_LIST_X, BM_DETAIL_Y, BM_LIST_W, BM_DETAIL_H, 0);
    bm_panel(0, BM_HINT_Y, 320, 240 - BM_HINT_Y, 0);
}

static s32 bm_pressed(s32 button) {
    return PAD_PRESSED(button) || PAD_REPEAT(button);
}

/* Moves the cursor by `d` rows, wrapping; scrolls to keep it shown. */
static void bm_move(s32 d) {
    if (bm_ui.rows <= 0) {
        return;
    }
    bm_ui.sel = (bm_ui.sel + d + bm_ui.rows) % bm_ui.rows;
    if (bm_ui.sel < bm_ui.top) {
        bm_ui.top = bm_ui.sel;
    } else if (bm_ui.sel >= bm_ui.top + BM_ROWS) {
        bm_ui.top = bm_ui.sel - BM_ROWS + 1;
    }
    bm_ui.note = BM_NOTE_NONE;
    sound_module.play(BM_SND_MOVE);
    bm_fill();
}

static void bm_pick_input(void) {
    s32 d = (bm_pressed(4) || bm_pressed(7)) ? -1 : (bm_pressed(6) || bm_pressed(5)) ? 1 : 0;

    if (d != 0) {
        bm_ui.common = (bm_ui.common + d + DEF_HUNT_MATERIALS) % DEF_HUNT_MATERIALS;
        bm_ui.note = BM_NOTE_NONE;
        sound_module.play(BM_SND_MOVE);
        bm_fill();
    } else if (PAD_PRESSED(13)) {
        bm_ui.note = def_bm_exchange(bm_ui.rares[bm_ui.sel], bm_ui.common);
        sound_module.play(bm_ui.note == DEF_BM_OK ? BM_SND_OK : BM_SND_BACK);
        if (bm_ui.note == DEF_BM_OK && def_record_rare(bm_ui.rares[bm_ui.sel]) <= 0) {
            bm_ui.mode = BM_LIST; /* that was the last of it: its row goes */
        }
        bm_collect(1);
        bm_fill();
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(BM_SND_BACK);
        bm_ui.mode = BM_LIST;
        bm_ui.note = BM_NOTE_NONE;
        bm_fill();
    }
}

static void bm_list_input(void) {
    int picks[DEF_BM_REPLICA_RARES];

    if (PAD_PRESSED(10) || PAD_PRESSED(11) || PAD_PRESSED(5) || PAD_PRESSED(7)) {
        bm_ui.tab ^= 1;
        bm_ui.mode = BM_LIST;
        bm_ui.sel = bm_ui.top = 0;
        bm_ui.note = BM_NOTE_NONE;
        sound_module.play(BM_SND_MOVE);
        bm_collect(0);
        bm_fill();
    } else if (bm_pressed(4)) {
        bm_ui.mode = BM_LIST;
        bm_move(-1);
    } else if (bm_pressed(6)) {
        bm_ui.mode = BM_LIST;
        bm_move(1);
    } else if (PAD_PRESSED(13)) {
        if (bm_ui.rows <= 0) {
            sound_module.play(BM_SND_BACK); /* nothing to trade: the first line says so */
        } else if (bm_ui.tab == BM_TRADE) {
            sound_module.play(BM_SND_OK);
            bm_ui.mode = BM_PICK;
            bm_ui.note = BM_NOTE_NONE;
        } else if (bm_ui.mode == BM_ARMED) {
            bm_ui.note = def_bm_replica_buy(bm_ui.sel);
            sound_module.play(bm_ui.note == DEF_BM_OK ? BM_SND_OK : BM_SND_BACK);
            bm_ui.mode = BM_LIST;
        } else {
            bm_ui.note = def_bm_replica_check(bm_ui.sel, picks);
            if (bm_ui.note == DEF_BM_OK) {
                bm_ui.mode = BM_ARMED;
                bm_ui.note = BM_NOTE_NONE;
                sound_module.play(BM_SND_OK);
            } else {
                sound_module.play(BM_SND_BACK);
            }
        }
        bm_fill();
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(BM_SND_BACK);
        if (bm_ui.mode == BM_ARMED) {
            bm_ui.mode = BM_LIST;
            bm_fill();
        } else {
            bm_ui.close = 1;
        }
    }
}

/* Closes the screen and hands the field back. The talk never started, so the player's control was never taken: it is
 * put back only if something else took it (as the Guardromon screen does after its event). */
static void bm_finish(void) {
    FieldstgActor *player;

    bm_close();
    bm_ui.close = 0;
    fieldstg_stage.event_running = 0;
    player = (FieldstgActor *)heap_objects.find(5, -1, 0);
    if (player != NULL && player->control == NULL) {
        player->reset_control(player);
    }
    port_log("market: frame %ld: closed", port_frames);
}

/* FIELDSTG's talk: the player talks to the actor `actor_key1` on `map`. 1 when it is the trader: the game's talk is
 * skipped and the screen opens on the next idle frame. */
int def_bm_talk(s32 map, s32 actor_key1) {
    s32 i;

    if (!def_on() || bm_ui.open || bm_ui.request) {
        return 0;
    }
    for (i = 0; i < BM_TRADERS; i++) {
        if (traders[i].map == map && traders[i].actor == actor_key1) {
            break;
        }
    }
    if (i >= BM_TRADERS) {
        return 0;
    }
    cdload_module.queue_file(BM_FILE_ITEMS); /* the replicas' names, read by the list */
    bm_ui.request = 1;
    fieldstg_stage.event_running = 1; /* frozen from this frame on, as the screen will hold it */
    port_log("market: the trader of map 0x%X (actor %d) talked to", map, actor_key1);
    return 1;
}

/* One frame of FIELDSTG's idle step: 1 while the screen owns the field. Every field frame also lets a replica the
 * player no longer holds give its ID's resale back (def_bm_sweep): where the counts change (the shop's sale, an
 * event's take, a discard) has no hook of its own, and the shop pays 0 until then anyway. */
int def_bm_frame(void) {
    if (!def_on()) {
        return 0;
    }
    def_bm_sweep();
    if (!bm_ui.open) {
        if (!bm_ui.request) {
            return 0;
        }
        bm_open();
    }
    fieldstg_stage.event_running = 1;
    if (bm_ui.fresh) {
        bm_ui.fresh = 0;
    } else if (bm_ui.mode == BM_PICK) {
        bm_pick_input();
    } else {
        bm_list_input();
    }
    if (bm_ui.close) {
        bm_finish();
        return 1;
    }
    bm_run_windows();
    bm_draw();
    return 1;
}
