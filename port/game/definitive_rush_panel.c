/* PRD 4 layer I: the Overlevel Rush's results on the field. With DEF_RUSH_FLOAT the Rush's leave
 * (fieldstg_rush_leave) keeps the field, gives the report's rewards at once (def_rush_apply_quiet: STFGTREP's own
 * routines in its order, so the game's state is the report's) and opens this panel; fieldstg_rush_frame runs it once a
 * frame from the camera's update, whatever the player does, as it runs the Rush's question. It never sets
 * event_running: that is what would hold the player, who walks on under it. definitive_rush.c gives its rows and clock.
 *
 * Drawing: a window of the game's font per text (windows of no object's data block: only this runs them, as on the
 * Guardromon screen), on the Rush's own layer 0x1004, over the field's sprites, with the Rush question's dark panel. In
 * one ordering-table entry what is added last is drawn first, so the texts go in before the frame and the fill. Placed
 * over the player (its actor's pixel position less the field layer's scroll, which the camera sets) and kept inside the
 * screen.
 *
 * The windows live on the field's heap: the panel closes and frees them as soon as the field holds the player or the map
 * ends, a map's load forgets them (def_rush_panel_reset: that heap went with the last stage), and a save state keeps
 * the panel with the heap it points into (def_rush_savestate). */
#include <stdlib.h>

#include "common.h"
#include "cdload.h"
#include "fieldstg.h"
#include "gamestate.h"
#include "gfx.h"
#include "heap.h"
#include "message.h"
#include "psyq/libgpu.h"
#include "records.h"

#include "psxstack/savestate.h"

#include "definitive.h"

extern void *font_get_entry(void *data, s32 i);
extern void port_log(const char *fmt, ...);

#define RP_LAYER 0x1004 /* the Rush's layer (its question and RUSH!), over the field's sprites; offset (0, 0) on the field */
#define RP_FIELD 0x1002 /* the field's window layer: its scroll is the camera's (fieldstg_camera_apply) */
#define RP_FONT 2       /* the 11-pixel font: names and numbers */
#define RP_LINE 12      /* a row's pitch in it */
#define RP_PAD 6        /* the texts' inset from the panel's sides */
#define RP_TOP 3        /* the first row's from its top */
#define RP_GAP 10       /* the least room between a row's two texts */
#define RP_MIN_W 112
#define RP_MAX_W 312
#define RP_RISE 52      /* from the player's feet to the panel's bottom: clear of its sprite */
#define RP_EDGE 4       /* the panel stays this far inside the screen */
#define RP_TEXT 96      /* bytes of a row's text */
#define RP_PAL_UP 3     /* LV UP! and TP FULL! (the bestiary's orange) */
#define RP_PAL_NEW 4    /* NEW TECH! (its green) */
#define RP_ITEM_NAMES 0x6A /* the item names' text file (records_language + it), STFGTREP's own */

/* The tests read rush_panel.clock (left, opened) first. */
static struct {
    DefRushClock clock;
    DefRushSummary summary;
    DefRushRow rows[DEF_RUSH_ROWS];
    s32 count;
    s32 w, h;
    s32 item_row; /* the item's row, still waiting for the item names' file; -1: none */
    MessageWindow *left[DEF_RUSH_ROWS], *right[DEF_RUSH_ROWS];
    s32 left_w[DEF_RUSH_ROWS], right_w[DEF_RUSH_ROWS];
} rush_panel;

int def_rush_float_mode = -1; /* -1: DW3_RUSH_FLOAT not read yet (a debug session may set it directly) */

int def_rush_float(void) {
    if (def_rush_float_mode < 0) {
        const char *v = getenv("DW3_RUSH_FLOAT");

        def_rush_float_mode = v != NULL ? atoi(v) != 0 : DEF_RUSH_FLOAT;
        if (v != NULL) {
            port_log("rush: DW3_RUSH_FLOAT=%s: the %s", v, def_rush_float_mode ? "panel" : "results screen");
        }
    }
    return def_rush_float_mode != 0 && def_on();
}

/* The item names' file once it is in, else NULL. cdload_is_loading queues a file it does not have, and get_file would
 * read one synchronously (a stall in the middle of the field's frame): so is_loading first, get_file once it says 0. */
static void *rp_item_names(void) {
    if (cdload_module.is_loading(records_language + RP_ITEM_NAMES)) {
        return NULL;
    }
    return cdload_module.files.get_file(records_language + RP_ITEM_NAMES);
}

static void rp_stop_row(s32 i) {
    heap_objects.stop((Object *)rush_panel.left[i]);
    heap_objects.stop((Object *)rush_panel.right[i]);
    rush_panel.left[i] = rush_panel.right[i] = NULL;
    rush_panel.left_w[i] = rush_panel.right_w[i] = 0;
}

static void rp_close(void) {
    s32 i;

    for (i = 0; i < DEF_RUSH_ROWS; i++) {
        rp_stop_row(i);
    }
    rush_panel.count = 0;
    rush_panel.item_row = -1;
    rush_panel.clock.left = 0;
}

/* A text's window, placed by its right end (message_measure: the lines start at x less their width), so that a left
 * text goes at its row's start plus its width and a right one at the panel's right edge. */
static MessageWindow *rp_window(const unsigned char *text, s32 palette, s32 *width) {
    MessageWindow *w;

    *width = 0;
    if (text[0] == 0) {
        return NULL;
    }
    w = message_create_window(RP_LAYER, RP_FONT, 0, 0);
    w->set_ot_depth(w, 0);
    w->set_text(w, (u8 *)text, -1);
    w->set_palette(w, (u8)palette);
    w->measure(w, 1);
    *width = w->measured_width;
    return w;
}

static void rp_fill_row(s32 i) {
    unsigned char left[RP_TEXT], right[RP_TEXT];
    const unsigned char *item_name = NULL;
    DefRushRow row = rush_panel.rows[i];
    s32 pal = 0;
    void *names;

    if (row.kind == DEF_RUSH_ROW_ITEM && (names = rp_item_names()) != NULL) {
        item_name = (const unsigned char *)font_get_entry(names, rush_panel.summary.item);
    }
    def_rush_row_text(&rush_panel.summary, row, item_name, left, right, RP_TEXT);
    if (row.kind == DEF_RUSH_ROW_TECH) {
        pal = RP_PAL_NEW;
    } else if (row.kind == DEF_RUSH_ROW_TP || (row.kind == DEF_RUSH_ROW_MEMBER && rush_panel.summary.members[row.index].levels)) {
        pal = RP_PAL_UP;
    }
    rp_stop_row(i);
    rush_panel.left[i] = rp_window(left, 0, &rush_panel.left_w[i]);
    rush_panel.right[i] = rp_window(right, pal, &rush_panel.right_w[i]);
}

static void rp_size(void) {
    s32 i, w;

    rush_panel.w = RP_MIN_W;
    for (i = 0; i < rush_panel.count; i++) {
        w = RP_PAD * 2 + rush_panel.left_w[i] + (rush_panel.right_w[i] > 0 ? RP_GAP + rush_panel.right_w[i] : 0);
        if (w > rush_panel.w) {
            rush_panel.w = w;
        }
    }
    if (rush_panel.w > RP_MAX_W) {
        rush_panel.w = RP_MAX_W;
    }
    rush_panel.h = RP_TOP * 2 + rush_panel.count * RP_LINE;
}

/* The Rush question's panel pieces (fieldstg_rush_quad): a flat quad with its blend mode `abr` (1 adds, 2 subtracts). */
static void rp_quad(GfxLayer *layer, s32 x, s32 y, s32 w, s32 h, s32 abr, u8 r, u8 g, u8 b) {
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

void def_rush_panel_open(const DefRushSummary *s) {
    s32 replaced = def_rush_clock_open(&rush_panel.clock);
    s32 i;

    for (i = 0; i < DEF_RUSH_ROWS; i++) { /* a second Rush's panel takes the place of the first */
        rp_stop_row(i);
    }
    rush_panel.summary = *s;
    rush_panel.count = def_rush_rows(&rush_panel.summary, rush_panel.rows, DEF_RUSH_ROWS);
    rush_panel.item_row = -1;
    for (i = 0; i < rush_panel.count; i++) {
        if (rush_panel.rows[i].kind == DEF_RUSH_ROW_ITEM && rp_item_names() == NULL) {
            rush_panel.item_row = i; /* the field does not keep the item names: "???" until they are in */
        }
        rp_fill_row(i);
    }
    rp_size();
    port_log("rush: floating results (random %d): battle %d, exp %d/%d/%d, levels +%d/+%d/+%d, learnt %d/%d/%d, "
             "bits %d, item %d x%d, %d material line(s); the panel, %d rows%s",
             s->random, (int)records_battle_results.battle, s->members[0].exp, s->members[1].exp, s->members[2].exp,
             s->members[0].levels, s->members[1].levels, s->members[2].levels, s->members[0].learnt,
             s->members[1].learnt, s->members[2].learnt, s->bits, s->item, s->item_count, s->materials,
             (int)rush_panel.count, replaced ? " (in place of the last one)" : "");
}

void def_rush_panel_frame(void) {
    FieldstgActor *player;
    Object *mgr;
    GfxLayer *layer, *field;
    s32 held, x, y, i, ty;

    if (rush_panel.clock.left <= 0) {
        return;
    }
    mgr = heap_objects.find(7, -1, -1);
    player = (FieldstgActor *)heap_objects.find(5, -1, 0);
    layer = gfx_module.funcs.get_layer(RP_LAYER);
    field = gfx_module.funcs.get_layer(RP_FIELD);
    held = fieldstg_stage.menu_open != 0 || fieldstg_stage.event_running != 0 || fieldstg_stage.battle_starting != 0 ||
           mgr == NULL || mgr->state != OBJECT_STATE_RUN || player == NULL || layer == NULL || field == NULL;
    if (!def_rush_clock_tick(&rush_panel.clock, gfx_module.funcs.get_frame_ticks(), held)) {
        rp_close();
        return;
    }
    if (rush_panel.item_row >= 0 && rp_item_names() != NULL) {
        rp_fill_row(rush_panel.item_row);
        rush_panel.item_row = -1;
        rp_size();
    }
    /* the player's place on the screen, at the height the camera centres (its jump counted), and the panel over it */
    x = player->pixel_pos.x - (field->scroll_x >> 8) - rush_panel.w / 2;
    y = player->pixel_pos.y - (player->height >> 8) - (field->scroll_y >> 8) - RP_RISE - rush_panel.h;
    if (x > 320 - RP_EDGE - rush_panel.w) {
        x = 320 - RP_EDGE - rush_panel.w;
    }
    if (x < RP_EDGE) {
        x = RP_EDGE;
    }
    if (y > 240 - RP_EDGE - rush_panel.h) {
        y = 240 - RP_EDGE - rush_panel.h;
    }
    if (y < RP_EDGE) {
        y = RP_EDGE;
    }
    for (i = 0; i < rush_panel.count; i++) {
        ty = y + RP_TOP + i * RP_LINE;
        if (rush_panel.left[i] != NULL) {
            rush_panel.left[i]->set_pos(rush_panel.left[i], x + RP_PAD + rush_panel.left_w[i], ty);
            rush_panel.left[i] = (MessageWindow *)heap_objects.try_run((Object *)rush_panel.left[i]);
        }
        if (rush_panel.right[i] != NULL) {
            rush_panel.right[i]->set_pos(rush_panel.right[i], x + rush_panel.w - RP_PAD, ty);
            rush_panel.right[i] = (MessageWindow *)heap_objects.try_run((Object *)rush_panel.right[i]);
        }
    }
    rp_quad(layer, x, y, rush_panel.w, 1, 1, 60, 130, 230);
    rp_quad(layer, x, y + rush_panel.h - 1, rush_panel.w, 1, 1, 60, 130, 230);
    rp_quad(layer, x, y, rush_panel.w, rush_panel.h, 2, 225, 215, 160);
}

void def_rush_panel_reset(void) {
    s32 i;

    for (i = 0; i < DEF_RUSH_ROWS; i++) {
        rush_panel.left[i] = rush_panel.right[i] = NULL;
    }
    rush_panel.count = 0;
    rush_panel.item_row = -1;
    rush_panel.clock.left = 0;
}

void def_rush_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, rush_panel);
}
