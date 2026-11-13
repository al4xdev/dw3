/* The Guardromon network: the inns' Guardromon are the game's save terminals, and here they are the teleport's too.
 * Talking to one offers the game's own save screen (SAVE) or a list of every Guardromon met so far (TELEPORT), in the
 * bestiary's two-column layout; a place is chosen with one press of the confirm button and taken with a second. A
 * Guardromon registers when it is talked to, and the mask rides the slot's record (definitive_bestiary.c, REC_EXTRA
 * "X1", four bytes), so the list survives a load.
 *
 * The screen is a field overlay, run from FIELDSTG's idle step (fieldstg_manager_update): the inn's event script asks to
 * leave for its 0xC.. save screen and dw3_travel_leave takes that transition over; dw3_travel_frame reads the pad and
 * draws every frame until the player picks, then replays the transition (SAVE), goes to the chosen inn (TELEPORT) or
 * hands the field back. While it is up fieldstg_stage.event_running stays set: that is what freezes the player, the map's
 * own events and the encounters.
 *
 * The places' names are the save screens' (?SHPNAM, the same entry STGMCARD shows), and the table is gs_inns' order
 * (game_mods.c): the node's bit in def_network_mask. */
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

#include "definitive.h"
#include "port_runtime.h"

extern int port_script_active;
extern const unsigned char def_str_Guardromon[], def_str_Save[], def_str_Teleport[], def_str_TravelKeys[],
    def_str_TravelKeys2[], def_str_Unknown[];

/* The inns' Guardromon and the spot the game itself walks the player to before it (the event's FIELDSTG_EVENT_WALK):
 * where a travel lands. Keep in step with gs_inns (port/game/game_mods.c), whose order this is. */
typedef struct NetNode {
    s16 map;   /* the inn's field map */
    s16 x, y;  /* pixels */
    s16 dir;
} NetNode;

#define NT_NODES 25
static const NetNode nodes[NT_NODES] = {
    { 0x20A, 207, 208, 3 },  { 0x223, 153, 181, 3 }, { 0x230, 376, 428, 5 },  { 0x238, 160, 328, 3 },
    { 0x23F, 720, 416, 5 },  { 0x247, 400, 224, 3 }, { 0x249, 279, 307, 3 },  { 0x25B, 215, 297, 3 },
    { 0x25D, 1086, 516, 5 }, { 0x263, 315, 267, 3 }, { 0x269, 429, 206, 5 },  { 0x26D, 256, 456, 3 },
    { 0x26F, 1200, 897, 5 }, { 0x279, 207, 208, 3 }, { 0x292, 153, 181, 3 },  { 0x29E, 376, 428, 5 },
    { 0x2A5, 160, 328, 3 },  { 0x2AC, 720, 416, 5 }, { 0x2B1, 400, 224, 3 },  { 0x2B3, 279, 307, 3 },
    { 0x2C4, 215, 297, 3 },  { 0x2C6, 1086, 516, 5 }, { 0x2CB, 315, 267, 3 }, { 0x2D1, 429, 206, 5 },
    { 0x2D6, 1200, 897, 5 },
};

/* The screen's layout (320x240), in the bestiary's colours and panel shapes: a title bar, two columns of places and the
 * keys, or the two answers under the Guardromon's name. */
#define NT_LAYER 0x1002  /* the field's window layer (the field menu's) */
#define NT_LINE 14       /* a row's pitch: the 14-pixel font's height */
#define NT_TITLE_H 23
#define NT_TOP 30        /* the list's first row */
#define NT_COL_ROWS 13   /* places a column holds: two columns of 25 */
#define NT_COL_X 2
#define NT_COL_W 156
#define NT_COL_DX 160
#define NT_COL_H 184     /* NT_TOP - 5 .. NT_TOP - 5 + NT_COL_H */
#define NT_TEXT_X 18     /* a row's name, clear of the cursor */
#define NT_HINT_Y 208 /* its two lines of the 14-pixel font */
#define NT_CHOICE_X 8
#define NT_CHOICE_Y 176
#define NT_CHOICE_W 304
#define NT_CHOICE_H 56
#define NT_CHOICE_TEXT_X (NT_CHOICE_X + 22)
#define NT_CHOICE_ROW_Y (NT_CHOICE_Y + 24)
#define NT_PAL_TEXT 0
#define NT_PAL_HOT 3  /* the chosen place (the bestiary's orange) */
#define NT_PAL_HERE 4 /* the Guardromon in front of the player (its green) */
#define NT_PAL_DIM 7  /* a place not met yet */

#define NT_SND_MOVE 0x8004513E
#define NT_SND_OK 0x8004503C
#define NT_SND_BACK 0x800450BD

enum { NT_CHOICE, NT_LIST };
enum { NT_IDLE, NT_CLOSE, NT_SAVE, NT_TRAVEL };

static struct {
    s32 request; /* a Guardromon asked for its save screen: the field's next idle frame opens the screen */
    s32 open;
    s32 replay;  /* the SAVE answer replays the script's transition: that one is not the Guardromon's anymore */
    s32 screen;  /* NT_CHOICE or NT_LIST */
    s32 row;     /* the answer under the cursor */
    s32 sel;     /* the place under the cursor */
    s32 armed;   /* the place the confirm button chose once, or -1 */
    s32 next;    /* the save screen map the script asked for */
    s32 action;
    s32 layer;
    MessageWindow *name, *title, *hint, *answers[2], *places[NT_NODES];
    MessageCursor *cursor;
} nt;

static int nt_node_of(s32 map) {
    int i;

    for (i = 0; i < NT_NODES; i++) {
        if (nodes[i].map == map) {
            return i;
        }
    }
    return -1;
}

/* The network is off under a replay script (the scripts are shared with the emulator, and one of them may walk to a
 * Guardromon): DW3_DEFINITIVE or DW3_FEAT_TRAVEL turns it back on there. */
static int nt_active(void) {
    return !port_script_active || getenv("DW3_DEFINITIVE") != NULL || getenv("DW3_FEAT_TRAVEL") != NULL;
}

static int nt_unlocked(int node) {
    return node >= 0 && node < NT_NODES && ((def_network_mask() >> node) & 1) != 0;
}

/* The place's name in ?SHPNAM: STGMCARD's own entry for the inn's 0xC.. save screen (stgmcard_map_names). */
static s32 nt_name(int node) {
    s32 inn = port_global_save_inn(nodes[node].map);

    return inn != 0 ? 0x2B + (inn & 0xFF) - 1 : 0;
}

static MessageWindow *nt_window(s32 font, s32 x, s32 y) {
    MessageWindow *w = message_create_window(nt.layer, font, x, y);

    w->set_ot_depth(w, 1);
    return w;
}

/* An opaque rectangle; the layer draws what is added last first, so frames go before fills. */
static void nt_rect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(nt.layer);
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
static void nt_panel(s32 x, s32 y, s32 w, s32 h, s32 band) {
    if (band) {
        nt_rect(x + 2, y + 2, w - 4, NT_LINE + 2, 0, 0, 74);
    }
    nt_rect(x + 1, y + 1, w - 2, h - 2, 0, 0, 33);
    nt_rect(x, y, w, h, 107, 107, 214);
}

/* Fills the windows of the screen that is up. */
static void nt_fill(void) {
    s32 here = nt_node_of(gamestate_data.funcs.get_map());
    s32 i;

    if (nt.screen == NT_CHOICE) {
        for (i = 0; i < 2; i++) {
            nt.answers[i]->set_palette(nt.answers[i], i == nt.row ? NT_PAL_HOT : NT_PAL_TEXT);
        }
        nt.cursor->set_pos(nt.cursor, NT_CHOICE_X + 10, NT_CHOICE_ROW_Y + nt.row * NT_LINE);
        return;
    }
    for (i = 0; i < NT_NODES; i++) {
        if (!nt_unlocked(i)) {
            nt.places[i]->set_text(nt.places[i], (u8 *)def_str_Unknown, -1);
            nt.places[i]->set_palette(nt.places[i], NT_PAL_DIM);
            continue;
        }
        nt.places[i]->set_text(nt.places[i], cdload_module.files.get_file(records_language + 0x94), nt_name(i));
        if (i == nt.armed) {
            nt.places[i]->set_palette(nt.places[i], NT_PAL_HOT);
        } else if (i == here) {
            nt.places[i]->set_palette(nt.places[i], NT_PAL_HERE);
        } else {
            nt.places[i]->set_palette(nt.places[i], NT_PAL_TEXT);
        }
    }
    nt.hint->set_text(nt.hint, (u8 *)(nt.armed >= 0 ? def_str_TravelKeys2 : def_str_TravelKeys), -1);
    nt.cursor->set_pos(nt.cursor, NT_COL_X + 4 + nt.sel / NT_COL_ROWS * NT_COL_DX,
                       NT_TOP + nt.sel % NT_COL_ROWS * NT_LINE);
}

static void nt_show(s32 screen) {
    s32 i;

    nt.screen = screen;
    nt.name->set_visible(nt.name, screen == NT_LIST);
    nt.title->set_visible(nt.title, screen == NT_CHOICE);
    for (i = 0; i < 2; i++) {
        nt.answers[i]->set_visible(nt.answers[i], screen == NT_CHOICE);
    }
    nt.hint->set_visible(nt.hint, screen == NT_LIST);
    for (i = 0; i < NT_NODES; i++) {
        nt.places[i]->set_visible(nt.places[i], screen == NT_LIST);
    }
    nt.cursor->show(nt.cursor, 1);
    nt_fill();
}

static void nt_open(void) {
    s32 i;

    nt.name = nt_window(1, 8, 5);
    nt.name->set_text(nt.name, (u8 *)def_str_Guardromon, -1);
    nt.title = nt_window(1, NT_CHOICE_X + 10, NT_CHOICE_Y + 4);
    nt.title->set_text(nt.title, (u8 *)def_str_Guardromon, -1);
    for (i = 0; i < 2; i++) {
        nt.answers[i] = nt_window(1, NT_CHOICE_TEXT_X, NT_CHOICE_ROW_Y + i * NT_LINE);
    }
    nt.answers[0]->set_text(nt.answers[0], (u8 *)def_str_Save, -1);
    nt.answers[1]->set_text(nt.answers[1], (u8 *)def_str_Teleport, -1);
    for (i = 0; i < NT_NODES; i++) {
        nt.places[i] = nt_window(2, NT_TEXT_X + i / NT_COL_ROWS * NT_COL_DX, NT_TOP + i % NT_COL_ROWS * NT_LINE);
    }
    nt.hint = nt_window(1, 8, NT_HINT_Y + 2);
    nt.hint->set_page_lines(nt.hint, 2);
    nt.cursor = message_create_cursor(nt.layer, 1, NT_CHOICE_X + 10, NT_CHOICE_ROW_Y);
    nt.open = 1;
    nt.request = 0;
    nt.armed = -1;
    nt_show(NT_CHOICE);
    port_log("network: frame %ld: the Guardromon screen of map 0x%X (mask 0x%X)", port_frames,
             gamestate_data.funcs.get_map(), def_network_mask());
}

static void nt_close(void) {
    MessageWindow **all[] = { &nt.name, &nt.title, &nt.hint, &nt.answers[0], &nt.answers[1] };
    s32 i;

    for (i = 0; i < (s32)(sizeof(all) / sizeof(all[0])); i++) {
        heap_objects.stop((Object *)*all[i]);
        *all[i] = NULL;
    }
    for (i = 0; i < NT_NODES; i++) {
        heap_objects.stop((Object *)nt.places[i]);
        nt.places[i] = NULL;
    }
    heap_objects.stop((Object *)nt.cursor);
    nt.cursor = NULL;
    nt.open = 0;
    nt.request = 0;
}

/* The windows belong to no object's data block, so nothing runs them but this (as the battle's toast does). */
static void nt_run_windows(void) {
    s32 i;

    heap_objects.try_run((Object *)nt.name);
    heap_objects.try_run((Object *)nt.title);
    heap_objects.try_run((Object *)nt.hint);
    for (i = 0; i < 2; i++) {
        heap_objects.try_run((Object *)nt.answers[i]);
    }
    for (i = 0; i < NT_NODES; i++) {
        heap_objects.try_run((Object *)nt.places[i]);
    }
    heap_objects.try_run((Object *)nt.cursor);
}

static void nt_draw(void) {
    s32 c;

    if (nt.screen == NT_CHOICE) {
        nt_panel(NT_CHOICE_X, NT_CHOICE_Y, NT_CHOICE_W, NT_CHOICE_H, 1);
        return;
    }
    /* the place the confirm button chose once: a bar behind its name (the layer draws what is added last first, so the
     * panels' fill goes after it and covers all but it) */
    if (nt.armed >= 0) {
        nt_rect(NT_COL_X + 3 + nt.armed / NT_COL_ROWS * NT_COL_DX, NT_TOP + nt.armed % NT_COL_ROWS * NT_LINE - 2,
                NT_COL_W - 6, NT_LINE, 74, 41, 0);
    }
    nt_panel(0, 0, 320, NT_TITLE_H, 1);
    for (c = 0; c < 2; c++) {
        nt_panel(NT_COL_X + c * NT_COL_DX, NT_TOP - 5, NT_COL_W, NT_COL_H, 0);
    }
    nt_panel(0, NT_HINT_Y, 320, 240 - NT_HINT_Y, 0);
}

static void nt_choice_input(void) {
    s32 row = nt.row;

    if (PAD_PRESSED(4) || PAD_PRESSED(6)) {
        nt.row ^= 1;
    }
    if (row != nt.row) {
        sound_module.play(NT_SND_MOVE);
        nt_fill();
    }
    if (PAD_PRESSED(13)) {
        sound_module.play(NT_SND_OK);
        if (nt.row == 0) {
            nt.action = NT_SAVE;
        } else {
            nt_show(NT_LIST);
        }
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(NT_SND_BACK);
        nt.action = NT_CLOSE;
    }
}

/* Moves the cursor to (column, row), wrapping inside a column and keeping the shorter second one in range. */
static void nt_move(s32 col, s32 row) {
    s32 sel;

    if (col != 0) {
        col = 1;
        if (row >= NT_NODES - NT_COL_ROWS) {
            row = NT_NODES - NT_COL_ROWS - 1;
        }
    }
    sel = col * NT_COL_ROWS + row;
    if (sel == nt.sel) {
        return;
    }
    nt.sel = sel;
    nt.armed = -1;
    sound_module.play(NT_SND_MOVE);
    nt_fill();
}

static void nt_list_input(void) {
    s32 col = nt.sel / NT_COL_ROWS;
    s32 row = nt.sel % NT_COL_ROWS;
    s32 rows = col == 0 ? NT_COL_ROWS : NT_NODES - NT_COL_ROWS;
    s32 here = nt_node_of(gamestate_data.funcs.get_map());

    if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
        nt_move(col, (row + rows - 1) % rows);
        return;
    }
    if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
        nt_move(col, (row + 1) % rows);
        return;
    }
    if (PAD_PRESSED(5) || PAD_REPEAT(5)) {
        nt_move(1, row);
        return;
    }
    if (PAD_PRESSED(7) || PAD_REPEAT(7)) {
        nt_move(0, row);
        return;
    }
    if (PAD_PRESSED(13)) {
        if (nt.sel == here || !nt_unlocked(nt.sel)) { /* this very Guardromon, or one not met yet */
            sound_module.play(NT_SND_BACK);
        } else if (nt.armed == nt.sel) {
            sound_module.play(NT_SND_OK);
            nt.action = NT_TRAVEL;
        } else {
            nt.armed = nt.sel;
            sound_module.play(NT_SND_OK);
            nt_fill();
        }
    } else if (PAD_PRESSED(12) || PAD_PRESSED(14)) {
        sound_module.play(NT_SND_BACK);
        if (nt.armed >= 0) {
            nt.armed = -1;
            nt_fill();
        } else {
            nt.row = 1; /* back to the answers, on TELEPORT */
            nt_show(NT_CHOICE);
        }
    }
}

/* One frame of the screen: its keys, its windows and its drawing. */
static void nt_update(void) {
    nt.action = NT_IDLE;
    if (nt.screen == NT_CHOICE) {
        nt_choice_input();
    } else {
        nt_list_input();
    }
    if (nt.action != NT_IDLE) {
        return;
    }
    nt_run_windows();
    nt_draw();
}

/* Closes the screen and carries out what the player chose: the script's own transition (SAVE), the chosen inn
 * (TELEPORT), or nothing. The event that asked is left in FIELDSTG's DONE state (its GOTO_MAP command ends it there, the
 * map change being what would have finished it), so its END never runs: the player's control is restored here, as that
 * END would have (fieldstg_actor_reset_control), and event_running goes back down. */
static void nt_finish(void) {
    s32 action = nt.action;
    s32 sel = nt.sel;
    FieldstgActor *player;

    nt.action = NT_IDLE;
    nt_close();
    fieldstg_stage.event_running = 0;
    player = (FieldstgActor *)heap_objects.find(5, -1, 0);
    if (player != NULL) {
        player->reset_control(player);
    }
    if (action == NT_SAVE) {
        port_log("network: the save screen of map 0x%X", nt.next);
        nt.replay = 1;
        fieldstg_goto_map(nt.next, -1, 0, 0, 0); /* the script's own FIELDSTG_EVENT_GOTO_MAP(0xC.., 0, 0, 0) */
        nt.replay = 0;
    } else if (action == NT_TRAVEL) {
        port_log("network: frame %ld: travel to map 0x%X", port_frames, nodes[sel].map);
        def_hunt_chain_reset(); /* PRD 4 layer A: a teleport ends the hunt's chain, even within one region */
        fieldstg_goto_map(nodes[sel].map, -1, nodes[sel].x << 8, nodes[sel].y << 8, nodes[sel].dir);
    }
}

/* FIELDSTG leaves `map` for `next`: an inn's Guardromon being talked to (its save screen is a 0xC.. map). 1 when the
 * screen takes the transition over: the game stays on the map until the player picks. */
int dw3_travel_leave(s32 map, s32 next) {
    int node = nt_node_of(map);

    if (!dw3_feat_travel || nt.replay || nt.open || nt.request || !nt_active() || node < 0 ||
        (next & 0xFF00) != 0xC00) {
        return 0;
    }
    if (!nt_unlocked(node)) {
        def_network_unlock(node);
        port_log("network: the Guardromon of map 0x%X registered (mask 0x%X)", map, def_network_mask());
    }
    cdload_module.queue_file(records_language + 0x94); /* the places' names, read by the list */
    nt.layer = NT_LAYER;
    nt.next = next;
    nt.sel = node; /* the list opens on the Guardromon in front of the player, or the next met one */
    for (; nt.sel < NT_NODES; nt.sel++) {
        if (nt.sel != node && nt_unlocked(nt.sel)) {
            break;
        }
    }
    if (nt.sel >= NT_NODES) {
        nt.sel = node;
    }
    nt.request = 1;
    return 1;
}

/* One frame of FIELDSTG's idle step: 1 while the screen owns the field. */
int dw3_travel_frame(void) {
    if (!nt.open) {
        if (!nt.request) {
            return 0;
        }
        nt_open();
    }
    fieldstg_stage.event_running = 1;
    nt_update();
    if (nt.action != NT_IDLE) {
        nt_finish();
    }
    return 1;
}
