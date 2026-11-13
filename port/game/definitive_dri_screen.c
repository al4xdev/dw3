/* PRD 4 layer H: the DRI Agents' Gauntlet, the game's side (the rules are definitive_dri.c).
 *
 * Talking to a DRI Agent (def_dri_agents; FIELDSTG's talk hook asks def_dri_talk) opens the list of simulations in the
 * Guardromon network's layout: each line's three stages by name, its best stage (or locked), and for the one under the
 * cursor its rewards, its levels and what unlocking it takes. The confirm button chooses once and acts on a second press:
 * a locked challenge is unlocked (its materials handed over), an open one starts. The screen is a field overlay run from
 * FIELDSTG's idle step (def_dri_frame, next to dw3_travel_frame); while it is up fieldstg_stage.event_running stays set,
 * which freezes the player, the map's events and the encounters.
 *
 * Starting a simulation sets up an existing boss battle (fieldstg_start_battle: its kind, immunities and, on a win, its
 * XP and Bits) and then gives records_state the line's stages as its three enemies, on the boss battles' scenery and
 * music. It never goes through fieldstg_encounter_start, so no Overlevel Rush, no Rare Target and no hunt chain; escape
 * stays blocked, as in any boss battle. In the battle, the native next-member entrance brings each stage in; a
 * "DIGIVOLVE!" callout marks it (def_dri_stage_enters). At the battle's end (def_log_end) the stages felled give their
 * tiers' accessories; a lost simulation does not end the game: wfightmn sends it back to the field (def_dri_defeat), with
 * the party's HP and MP as they were before it (a simulation hurts nobody; the items it spent stay spent). Back on the
 * field, a panel tells what the simulation gave.
 *
 * Off in a replay script (def_on): no agent answers and nothing starts. */
#include <string.h>

#include "common.h"

#include "cdload.h"
#include "fieldstg.h"
#include "fightstg.h"
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

extern int def_on(void);
extern void fieldstg_start_battle(s32 battle);
extern void *font_get_entry(void *data, s32 i);
extern const unsigned char def_str_DriTitle[], def_str_DriSep[], def_str_DriDash[], def_str_DriReward[],
    def_str_DriBest[], def_str_DriClear[], def_str_DriKeysUnlock[], def_str_DriKeysStart[], def_str_DriKeysConfirm[],
    def_str_DriStages[], def_str_DriNewReward[], def_str_DriNoReward[], def_str_DriOk[], def_str_EvoLv[],
    def_str_HuntSpace[], def_str_DriOf[], def_str_DriSep[], def_str_Unknown[];

/* The layout (320x240), the Guardromon network's colours and panels: a title bar, the list, the chosen challenge's
 * panel and the keys; the debrief is one panel over the field. */
#define DRI_LAYER 0x1002 /* the field's window layer */
#define DRI_LINE 14      /* a row's pitch */
#define DRI_SMALL 13     /* a line of the 11-pixel font, with its gap */
#define DRI_TITLE_H 23
#define DRI_ROWS 8       /* challenges listed at once; the list scrolls past them */
#define DRI_TOP 30       /* the list's first row */
#define DRI_LIST_H (DRI_ROWS * DRI_LINE + 6)
#define DRI_STATE_X 18   /* a row's best stage, "2/3" ("--" while locked) */
#define DRI_NAME_X 46    /* its stages' names */
#define DRI_INFO_Y (DRI_TOP - 5 + DRI_LIST_H + 3)
#define DRI_INFO_H 58
#define DRI_HINT_Y 208
#define DRI_DEB_Y 146    /* the debrief's panel */
#define DRI_DEB_H 88
#define DRI_TEXT 96      /* bytes of a composed line */
#define DRI_WINDOWS 32
#define DRI_PAL_TEXT 0
#define DRI_PAL_HOT 3    /* chosen once (the bestiary's orange) */
#define DRI_PAL_DONE 4   /* cleared (its green) */
#define DRI_PAL_DIM 7    /* locked */
#define DRI_SND_MOVE 0x8004513E
#define DRI_SND_OK 0x8004503C
#define DRI_SND_BACK 0x800450BD
/* gamestate_data.progress at which fieldstg_start_battle sends every battle to 0xE0A instead of the battle stage: no
 * simulation then. */
#define DRI_PROGRESS_SPECIAL 0x2B

enum { DRI_LIST, DRI_DEBRIEF };
enum { DRI_IDLE, DRI_CLOSE, DRI_START };

int def_dri_debug = -1; /* tests: 0.. starts that challenge from the idle field, DEF_DRI_DEBUG_MENU opens the list */

static struct {
    s32 request; /* the screen opens on the field's next idle frame with its texts loaded */
    s32 open;
    s32 mode;    /* DRI_LIST or DRI_DEBRIEF */
    s32 sel;     /* the challenge under the cursor */
    s32 top;     /* the list's first row */
    s32 armed;   /* the challenge the confirm button chose once, or -1 */
    s32 fresh;   /* the frame it opened: the pad is not read (the press that talked may still be down) */
    s32 action;
    s32 count;
    MessageWindow *wins[DRI_WINDOWS];
    MessageWindow *title, *hint, *states[DRI_ROWS], *names[DRI_ROWS], *reward, *levels, *cost;
    MessageCursor *cursor;
} dri;

/* A simulation, from its start to the field's debrief. No pointers: a save state carries it (a battle loaded from one
 * is still a simulation). */
static struct {
    signed char pending;  /* its battle was set up: the next battle to begin is it */
    signed char fighting; /* that battle runs */
    signed char lost;     /* it was lost: wfightmn sends it back to the field (def_dri_defeat) */
    signed char debrief;  /* its result waits for the field */
    signed char challenge;
    signed char fallen;   /* stages felled */
    signed char result;   /* fightstg_events.result: 1 won, 2 lost */
    signed char count;    /* accessories it gave */
    short items[DEF_DRI_STAGES];
    short map;            /* the field it left from */
    short party[3];       /* the party's Digimon then, and their HP and MP (restored after a defeat) */
    short hp[3], mp[3];
} dri_sim;

static unsigned char row_name[DRI_ROWS][DRI_TEXT], row_state[DRI_ROWS][8], info_reward[DRI_TEXT], info_levels[DRI_TEXT],
    info_best[DRI_TEXT], deb_name[DRI_TEXT], deb_stages[DRI_TEXT], deb_reward[2 * DRI_TEXT];

static int idle(void) {
    return !fieldstg_stage.event_running && !fieldstg_stage.battle_starting && !fieldstg_stage.menu_open &&
           !fieldstg_stage.actor_busy && !fieldstg_stage.title_shown;
}

/* ---- Texts. */

static const unsigned char *item_name(int item) {
    void *names = cdload_module.files.get_file(records_language + 0x6A);
    const unsigned char *s = names != NULL ? (const unsigned char *)font_get_entry(names, item) : NULL;

    return s != NULL ? s : def_str_Unknown;
}

/* "Tyrannomon / MetalGreymon / BK WarGreymon": the stages, by their species' names (text 0x4E). */
static void put_line(unsigned char *buf, int size, const DefDriChallenge *ch) {
    int p = 0, i;

    buf[0] = 0;
    for (i = 0; i < DEF_DRI_STAGES; i++) {
        const unsigned char *name = def_species_name(ch->stages[i].digimon);

        if (i > 0) {
            def_glyph_put(buf, &p, size, def_str_DriSep);
        }
        def_glyph_put(buf, &p, size, name != NULL ? name : def_str_Unknown);
    }
}

static void put_items(unsigned char *buf, int *p, int size, const short *items, int n) {
    int i;

    for (i = 0; i < n; i++) {
        if (i > 0) {
            def_glyph_put(buf, p, size, def_str_DriSep);
        }
        def_glyph_put(buf, p, size, item_name(items[i]));
    }
}

/* "2 of 3" (the 11-pixel font has no slash). */
static void put_stage(unsigned char *buf, int *p, int size, int n) {
    def_glyph_count(buf, p, size, n);
    def_glyph_put(buf, p, size, def_str_DriOf);
    def_glyph_count(buf, p, size, DEF_DRI_STAGES);
}

/* The three texts the screen reads: the species' records (0x1CF), their names (0x4E) and the items' (0x6A).
 * cdload's is_loading queues a file that is not there, so asking is loading. */
static int texts_loaded(void) {
    s32 a = cdload_module.is_loading(0x1CF);
    s32 b = cdload_module.is_loading(records_language + 0x4E);
    s32 c = cdload_module.is_loading(records_language + 0x6A);

    return a == 0 && b == 0 && c == 0;
}

/* ---- The windows and panels (definitive_travel.c's). */

static MessageWindow *dri_window(s32 font, s32 x, s32 y) {
    MessageWindow *w = message_create_window(DRI_LAYER, font, x, y);

    w->set_ot_depth(w, 1);
    if (dri.count < DRI_WINDOWS) {
        dri.wins[dri.count++] = w;
    }
    return w;
}

/* An opaque rectangle; the layer draws what is added last first, so frames go before fills. */
static void dri_rect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(DRI_LAYER);
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

static void dri_panel(s32 x, s32 y, s32 w, s32 h, s32 band) {
    if (band) {
        dri_rect(x + 2, y + 2, w - 4, DRI_LINE + 2, 0, 0, 74);
    }
    dri_rect(x + 1, y + 1, w - 2, h - 2, 0, 0, 33);
    dri_rect(x, y, w, h, 107, 107, 214);
}

/* ---- The list. */

static void list_fill(void) {
    const DefDriChallenge *ch;
    int r, c, p, best, pal;

    for (r = 0; r < DRI_ROWS; r++) {
        c = dri.top + r;
        ch = def_dri_challenge(c);
        dri.names[r]->set_visible(dri.names[r], ch != NULL);
        dri.states[r]->set_visible(dri.states[r], ch != NULL);
        if (ch == NULL) {
            continue;
        }
        put_line(row_name[r], DRI_TEXT, ch);
        p = 0;
        row_state[r][0] = 0;
        best = def_record_dri_best(c);
        if (def_record_dri_unlocked(c)) {
            put_stage(row_state[r], &p, sizeof(row_state[r]), best);
        } else {
            def_glyph_put(row_state[r], &p, sizeof(row_state[r]), def_str_DriDash);
        }
        dri.names[r]->set_text(dri.names[r], row_name[r], -1);
        dri.states[r]->set_text(dri.states[r], row_state[r], -1);
        pal = c == dri.armed ? DRI_PAL_HOT : !def_record_dri_unlocked(c) ? DRI_PAL_DIM : DRI_PAL_TEXT;
        dri.names[r]->set_palette(dri.names[r], pal);
        dri.states[r]->set_palette(dri.states[r], !def_record_dri_unlocked(c) ? DRI_PAL_DIM
                                                   : best >= DEF_DRI_STAGES   ? DRI_PAL_DONE
                                                                              : DRI_PAL_TEXT);
    }
    ch = def_dri_challenge(dri.sel);
    if (ch == NULL) {
        return;
    }
    p = 0;
    info_reward[0] = 0;
    def_glyph_put(info_reward, &p, DRI_TEXT, def_str_DriReward);
    put_items(info_reward, &p, DRI_TEXT, ch->rewards, DEF_DRI_STAGES);
    dri.reward->set_text(dri.reward, info_reward, -1);
    p = 0;
    info_levels[0] = 0;
    def_glyph_put(info_levels, &p, DRI_TEXT, def_str_EvoLv);
    for (r = 0; r < DEF_DRI_STAGES; r++) {
        def_glyph_put(info_levels, &p, DRI_TEXT, r == 0 ? def_str_HuntSpace : def_str_DriSep);
        def_glyph_count(info_levels, &p, DRI_TEXT, ch->stages[r].level);
    }
    dri.levels->set_text(dri.levels, info_levels, -1);
    if (!def_record_dri_unlocked(dri.sel)) {
        const unsigned char *cost = def_dri_cost_label(dri.sel, &pal);

        dri.cost->set_text(dri.cost, (u8 *)cost, -1);
        dri.cost->set_palette(dri.cost, pal);
    } else {
        best = def_record_dri_best(dri.sel);
        p = 0;
        info_best[0] = 0;
        if (best >= DEF_DRI_STAGES) {
            def_glyph_put(info_best, &p, DRI_TEXT, def_str_DriClear);
        } else {
            def_glyph_put(info_best, &p, DRI_TEXT, def_str_DriBest);
            put_stage(info_best, &p, DRI_TEXT, best);
        }
        dri.cost->set_text(dri.cost, info_best, -1);
        dri.cost->set_palette(dri.cost, best >= DEF_DRI_STAGES ? DRI_PAL_DONE : DRI_PAL_TEXT);
    }
    dri.hint->set_text(dri.hint, (u8 *)(dri.armed >= 0                     ? def_str_DriKeysConfirm
                                        : def_record_dri_unlocked(dri.sel) ? def_str_DriKeysStart
                                                                           : def_str_DriKeysUnlock),
                       -1);
    dri.cursor->set_pos(dri.cursor, 6, DRI_TOP + (dri.sel - dri.top) * DRI_LINE);
}

static void list_open(void) {
    s32 r;

    dri.title = dri_window(1, 8, 5);
    dri.title->set_text(dri.title, (u8 *)def_str_DriTitle, -1);
    for (r = 0; r < DRI_ROWS; r++) {
        dri.states[r] = dri_window(2, DRI_STATE_X, DRI_TOP + r * DRI_LINE);
        dri.names[r] = dri_window(2, DRI_NAME_X, DRI_TOP + r * DRI_LINE);
    }
    dri.reward = dri_window(2, 10, DRI_INFO_Y + 5);
    dri.levels = dri_window(2, 10, DRI_INFO_Y + 5 + DRI_SMALL);
    dri.cost = dri_window(2, 10, DRI_INFO_Y + 5 + 2 * DRI_SMALL);
    dri.cost->set_page_lines(dri.cost, 2);
    dri.hint = dri_window(1, 8, DRI_HINT_Y + 2);
    dri.hint->set_page_lines(dri.hint, 2);
    dri.cursor = message_create_cursor(DRI_LAYER, 1, 6, DRI_TOP);
    dri.cursor->show(dri.cursor, 1);
    list_fill();
}

static void list_draw(void) {
    if (dri.armed >= dri.top && dri.armed < dri.top + DRI_ROWS) {
        /* a bar behind the chosen row: drawn before the panels' fill, which then covers all but it */
        dri_rect(5, DRI_TOP + (dri.armed - dri.top) * DRI_LINE - 2, 310, DRI_LINE, 74, 41, 0);
    }
    dri_panel(0, 0, 320, DRI_TITLE_H, 1);
    dri_panel(2, DRI_TOP - 5, 316, DRI_LIST_H, 0);
    dri_panel(2, DRI_INFO_Y, 316, DRI_INFO_H, 0);
    dri_panel(0, DRI_HINT_Y, 320, 240 - DRI_HINT_Y, 0);
}

static void list_move(int sel) {
    if (sel == dri.sel) {
        return;
    }
    dri.sel = sel;
    if (dri.sel < dri.top) {
        dri.top = dri.sel;
    } else if (dri.sel >= dri.top + DRI_ROWS) {
        dri.top = dri.sel - DRI_ROWS + 1;
    }
    dri.armed = -1;
    sound_module.play(DRI_SND_MOVE);
    list_fill();
}

static void list_input(void) {
    int n = def_dri_challenge_count < DEF_DRI_CHALLENGES ? def_dri_challenge_count : DEF_DRI_CHALLENGES;
    int c = dri.sel;

    if (n <= 0) { /* an empty table: only Back */
        if (PAD_PRESSED(12) || PAD_PRESSED(13) || PAD_PRESSED(14)) {
            dri.action = DRI_CLOSE;
        }
        return;
    }
    if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
        list_move((c + n - 1) % n);
        return;
    }
    if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
        list_move((c + 1) % n);
        return;
    }
    if (PAD_PRESSED(13)) {
        if (!def_record_dri_unlocked(c) && !def_dri_can_unlock(c)) { /* short of its materials */
            sound_module.play(DRI_SND_BACK);
        } else if (dri.armed != c) {
            dri.armed = c;
            sound_module.play(DRI_SND_OK);
            list_fill();
        } else if (!def_record_dri_unlocked(c)) {
            def_dri_unlock(c);
            dri.armed = -1;
            sound_module.play(DRI_SND_OK);
            list_fill();
        } else {
            sound_module.play(DRI_SND_OK);
            dri.action = DRI_START;
        }
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(DRI_SND_BACK);
        if (dri.armed >= 0) {
            dri.armed = -1;
            list_fill();
        } else {
            dri.action = DRI_CLOSE;
        }
    }
}

/* ---- The debrief: what the simulation just fought gave. */

static void debrief_open(void) {
    const DefDriChallenge *ch = def_dri_challenge(dri_sim.challenge);
    MessageWindow *w;
    int p = 0;

    dri_sim.debrief = 0; /* shown once */
    if (ch == NULL) {
        return;
    }
    put_line(deb_name, DRI_TEXT, ch);
    w = dri_window(2, 18, DRI_DEB_Y + 5);
    w->set_text(w, deb_name, -1);
    deb_stages[0] = 0;
    def_glyph_put(deb_stages, &p, DRI_TEXT, def_str_DriStages);
    put_stage(deb_stages, &p, DRI_TEXT, dri_sim.fallen);
    w = dri_window(1, 18, DRI_DEB_Y + 24);
    w->set_text(w, deb_stages, -1);
    w->set_palette(w, dri_sim.fallen >= DEF_DRI_STAGES ? DRI_PAL_DONE : DRI_PAL_TEXT);
    p = 0;
    deb_reward[0] = 0;
    if (dri_sim.count > 0) {
        static const unsigned char line_break[] = { 2, 1, 0 };

        def_glyph_put(deb_reward, &p, sizeof(deb_reward), def_str_DriNewReward);
        def_glyph_put(deb_reward, &p, sizeof(deb_reward), line_break);
        put_items(deb_reward, &p, sizeof(deb_reward), dri_sim.items, dri_sim.count);
    } else {
        def_glyph_put(deb_reward, &p, sizeof(deb_reward), def_str_DriNoReward);
    }
    w = dri_window(2, 18, DRI_DEB_Y + 42);
    w->set_page_lines(w, 2);
    w->set_text(w, deb_reward, -1);
    w->set_palette(w, dri_sim.count > 0 ? DRI_PAL_HOT : DRI_PAL_TEXT);
    w = dri_window(1, 18, DRI_DEB_Y + 68);
    w->set_text(w, (u8 *)def_str_DriOk, -1);
}

/* ---- The screen. */

static void dri_open(void) {
    dri.count = 0;
    dri.cursor = NULL;
    dri.armed = -1;
    dri.action = DRI_IDLE;
    if (dri.mode == DRI_LIST) {
        list_open();
    } else {
        debrief_open();
    }
    dri.open = 1;
    dri.request = 0;
    dri.fresh = 1;
    port_log("dri: frame %ld: the %s of map 0x%X", port_frames, dri.mode == DRI_LIST ? "list" : "debrief",
             gamestate_data.funcs.get_map());
}

static void dri_close(void) {
    s32 i;

    for (i = 0; i < dri.count; i++) {
        heap_objects.stop((Object *)dri.wins[i]);
        dri.wins[i] = NULL;
    }
    dri.count = 0;
    if (dri.cursor != NULL) {
        heap_objects.stop((Object *)dri.cursor);
        dri.cursor = NULL;
    }
    dri.open = 0;
    dri.request = 0;
}

/* The windows belong to no object's data block, so nothing runs them but this (as the network's screen does). */
static void dri_run(void) {
    s32 i;

    for (i = 0; i < dri.count; i++) {
        heap_objects.try_run((Object *)dri.wins[i]);
    }
    if (dri.cursor != NULL) {
        heap_objects.try_run((Object *)dri.cursor);
    }
}

/* The simulation's battle: the template (an existing boss battle) as fieldstg_start_battle sets it up, then the line's
 * stages as its three enemies. 1 when the field is leaving for it. */
static int sim_start(int c) {
    const DefDriChallenge *ch = def_dri_challenge(c);
    int i, id;

    if (ch == NULL || fieldstg_stage.battle_starting || gamestate_data.progress == DRI_PROGRESS_SPECIAL) {
        return 0;
    }
    fieldstg_start_battle(ch->battle);
    if (!fieldstg_stage.battle_starting) { /* no field manager: nothing was set up */
        port_log("dri: challenge %d not started (no field manager)", c);
        return 0;
    }
    records_state.stage = DEF_DRI_STAGE;
    records_state.music = DEF_DRI_MUSIC;
    for (i = 0; i < DEF_DRI_STAGES; i++) {
        RecordsEnemy *e = &records_state.enemies[i];

        e->digimon = ch->stages[i].digimon;
        e->level = ch->stages[i].level;
        e->hp = ch->stages[i].hp;
        e->mp = ch->stages[i].mp;
        e->stat_scale = ch->stages[i].stat_scale;
    }
    records_state.blocked[11] = 1; /* no escape, as a boss's: a defeat is the way out, and it costs nothing */
    records_state.has_prize = 0;
    memset(&dri_sim, 0, sizeof(dri_sim));
    dri_sim.pending = 1;
    dri_sim.challenge = (signed char)c;
    dri_sim.map = (short)gamestate_data.funcs.get_map();
    for (i = 0; i < 3; i++) {
        id = gamestate_data.funcs.get_party_member(i);
        dri_sim.party[i] = (short)id;
        if (id >= 0) {
            dri_sim.hp[i] = gamestate_data.digimon[id].record.stats.values[2];
            dri_sim.mp[i] = gamestate_data.digimon[id].record.stats.values[4];
        }
    }
    port_log("dri: challenge %d started from map 0x%X (battle %d: %d, %d, %d at %d, %d, %d)", c, dri_sim.map, ch->battle,
             ch->stages[0].digimon, ch->stages[1].digimon, ch->stages[2].digimon, ch->stages[0].level,
             ch->stages[1].level, ch->stages[2].level);
    return 1;
}

/* Closes the screen and carries out the choice: the simulation's battle, or the field handed back. */
static void dri_finish(void) {
    s32 action = dri.action;
    FieldstgActor *player;

    dri.action = DRI_IDLE;
    dri_close();
    if (action == DRI_START && sim_start(dri.sel)) {
        return; /* fieldstg_start_battle keeps the field frozen until the battle */
    }
    fieldstg_stage.event_running = 0;
    player = (FieldstgActor *)heap_objects.find(5, -1, 0);
    if (player != NULL) {
        player->reset_control(player);
    }
}

static void list_request(void) {
    dri.request = 1;
    dri.mode = DRI_LIST;
    if (dri.sel < 0 || dri.sel >= def_dri_challenge_count) {
        dri.sel = 0;
    }
    dri.top = dri.sel >= DRI_ROWS ? dri.sel - DRI_ROWS + 1 : 0;
    fieldstg_stage.event_running = 1; /* the player stands still from the talk on */
}

int def_dri_talk(s32 map, s32 actor) {
    if (!def_dri_agent(map, actor) || dri.open || dri.request || dri_sim.pending ||
        gamestate_data.progress == DRI_PROGRESS_SPECIAL) {
        return 0;
    }
    port_log("dri: the DRI Agent %d of map 0x%X", actor, map);
    list_request();
    return 1;
}

int def_dri_frame(void) {
    if (!def_on()) {
        return 0;
    }
    if (!dri.open && !dri.request) {
        if (def_dri_debug >= 0 && idle()) {
            int d = def_dri_debug;

            def_dri_debug = -1;
            if (d == DEF_DRI_DEBUG_MENU) {
                list_request();
            } else if (sim_start(d)) {
                return 1;
            }
        } else if (dri_sim.debrief && idle()) {
            if (dri_sim.map != gamestate_data.funcs.get_map()) { /* not the field it left from: a load, a reset */
                dri_sim.debrief = 0;
                return 0;
            }
            dri.request = 1;
            dri.mode = DRI_DEBRIEF;
            dri_sim.lost = 0; /* back on the field: the defeat's redirect is done */
        }
        if (!dri.request) {
            return 0;
        }
    }
    fieldstg_stage.event_running = 1;
    if (!dri.open) {
        if (!texts_loaded()) {
            return 1;
        }
        dri_open();
    }
    dri.action = DRI_IDLE;
    if (!dri.fresh) {
        if (dri.mode == DRI_LIST) {
            list_input();
        } else if (PAD_PRESSED(13) || PAD_PRESSED(12) || PAD_PRESSED(14)) {
            sound_module.play(DRI_SND_OK);
            dri.action = DRI_CLOSE;
        }
    }
    dri.fresh = 0;
    if (dri.action != DRI_IDLE) {
        dri_finish();
        return 1;
    }
    dri_run();
    if (dri.mode == DRI_LIST) {
        list_draw();
    } else {
        dri_panel(8, DRI_DEB_Y, 304, DRI_DEB_H, 1);
    }
    return 1;
}

/* ---- The battle's hooks. */

void def_dri_battle_begin(void) {
    const DefDriChallenge *ch;

    dri_sim.fighting = 0;
    dri_sim.lost = 0;
    if (!dri_sim.pending) {
        return;
    }
    dri_sim.pending = 0;
    ch = def_dri_challenge(dri_sim.challenge);
    /* only the battle sim_start set up: anything else that began meanwhile is not a simulation */
    if (ch != NULL && records_state.battle == ch->battle && records_state.enemies[0].digimon == ch->stages[0].digimon) {
        dri_sim.fighting = 1;
        port_log("dri: challenge %d: the battle begins", dri_sim.challenge);
    }
}

void def_dri_stage_enters(int member) {
    if (!dri_sim.fighting) {
        return;
    }
    def_callout_show(DEF_CALLOUT_EVOLVE, 1);
    port_log("dri: challenge %d: stage %d enters (species %d)", dri_sim.challenge, member,
             fightstg_battle.state.members[1][member % 3].digimon);
}

void def_dri_battle_end(int result) {
    short items[DEF_DRI_STAGES];
    int i, n, fallen = 0;

    if (!dri_sim.fighting) {
        return;
    }
    dri_sim.fighting = 0;
    for (i = 0; i < 3; i++) {
        FightstgMember *m = &fightstg_battle.state.members[1][i];

        if (m->digimon != 0 && m->hp <= 0) {
            fallen++;
        }
    }
    n = def_dri_award(dri_sim.challenge, fallen, items);
    for (i = 0; i < n; i++) { /* counted at once: the results screen and the field come after */
        if (items[i] > 0 && items[i] < 0x193 && gamestate_data.items[items[i]] < 99) {
            gamestate_data.items[items[i]]++;
        }
        dri_sim.items[i] = items[i];
    }
    dri_sim.count = (signed char)n;
    dri_sim.fallen = (signed char)fallen;
    dri_sim.result = (signed char)result;
    dri_sim.lost = result == 2;
    dri_sim.debrief = 1;
    port_log("dri: challenge %d ended: result %d, %d stages felled, best %d, %d new rewards (%d %d %d)", dri_sim.challenge,
             result, fallen, def_record_dri_best(dri_sim.challenge), n, n > 0 ? items[0] : 0, n > 1 ? items[1] : 0,
             n > 2 ? items[2] : 0);
}

int def_dri_defeat(void) {
    int i, id;

    if (dri_sim.fighting) { /* WFIGHTMN picks the next map before the battle's log ends it (def_log_end): end it here */
        def_dri_battle_end(2);
    }
    /* WFIGHTMN asks every frame until the map changes, so this stays 1 (and the party restored) until the next battle or
     * the field's debrief clears it: a 0 on a later frame would let the game over's 0xE00 overwrite the field */
    if (!dri_sim.lost) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        id = dri_sim.party[i];
        if (id >= 0 && id == gamestate_data.funcs.get_party_member(i)) {
            gamestate_data.digimon[id].record.stats.values[2] = dri_sim.hp[i];
            gamestate_data.digimon[id].record.stats.values[4] = dri_sim.mp[i];
        }
    }
    port_log("dri: challenge %d lost: back to map 0x%X, the party as it went in", dri_sim.challenge,
             gamestate_data.field_map);
    return 1;
}

void def_dri_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, dri_sim);
}
