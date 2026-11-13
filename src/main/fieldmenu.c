#include "common.h"

#include "fieldmenu.h"
#include "records.h"
#include "object.h"
#include "gamestate.h"
#include "sound.h"
#include "gfx.h"
#include "message.h"
#include "cdload.h"
#include "pad.h"
#include "window_anim.h"
#ifdef PC_PORT
#include "fightstg.h"
#include "psyq/libgpu.h"

/* The bestiary screen (definitive combat, layer 9; port/game/definitive_bestiary.c): a last entry of the field's menu that
 * lists the species of SDIGIEDT with what the player learned of their elements. It reuses the menu's own windows: the
 * title bar carries its name and the page, the option windows a page of names; in the member panels' place a panel of
 * its own (fieldmenu_bestiary_panel) carries the chosen species: its name, then a line per element (picture, name,
 * Weak / Resist / - / ???). */
extern const unsigned char def_str_Unknown[], def_str_Bestiary[], def_str_BestiaryMenu[], def_str_BestiaryHint[],
    def_str_Slash[], def_str_Neutral[], def_str_HuntDrops[], def_str_HuntHeld[], def_str_HuntRareLabel[];
int def_bestiary_active(void);
/* The Guide (port/game/definitive_quest_screen.c): an entry before the bestiary's. It has no screen in this menu: the
 * menu closes as on a cancel and the field opens the Guide's own. */
extern const unsigned char def_str_GuideMenu[];
int def_guide_active(void);
void def_guide_request(void);
#define BS_SPECIES 193 /* species in the table (DEF_BESTIARY_SPECIES) */
#define BS_ROWS 6      /* names per page (DEF_BESTIARY_ROWS): the options' panel holds at least six rows */
#define BS_STEP 100    /* fieldmenu_update's step of the open screen (the PS1 menu has 0..7) */
#define BS_PANEL_X 4   /* the species' panel: left of the options' (x 0xA8), from their top down */
#define BS_PANEL_Y 0x28
#define BS_PANEL_W 154
#define BS_PANEL_H 142
#define BS_TEXT_X 12   /* the species' name and the elements' pictures */
#define BS_NAME_Y 0x2C
#define BS_ROW_Y 0x40  /* the first element's line, 14 pixels each */
#define BS_FAMILIES 5  /* the lines: the seven elements by family (DEF_FAMILIES), Water+Ice and Thunder+Machine as one */
#define BS_PAIR_DX 11  /* a pair's second picture, behind the first's right side */
#define BS_ELEM_X 42   /* the family's name, clear of a pair's two pictures */
#define BS_STATE_X 100 /* its state */
#define BS_HINT_Y 0xA5
#define BS_PAGE_X 0x112 /* the page, at the title bar's right end */
#define BS_DROPS_Y (BS_ROW_Y + BS_FAMILIES * 14) /* PRD 4 layer C: the Hunt Materials' two lines, under the families' */
#define BS_HELD_X 136   /* a material's count, clear of the longest name */
#define BS_RARE_X 122   /* the rare items' "13/13", clear of the frame */
#define BS_MATERIALS 16 /* DEF_HUNT_MATERIALS */
#endif

/* A window's position and message: a party member's name (fieldmenu_name_pos), the labels (fieldmenu_label_pos) and
 * the values (fieldmenu_value_pos), each y + 46 per party member. fieldmenu_create_windows matches only with the labels
 * and values as arrays of their own, fieldmenu_show_member only reading the values as fieldmenu_name_pos[6 + i]
 * (past the end of fieldmenu_name_pos), so that is how both read them. */
typedef struct FieldmenuPos {
    /* 0x0 */ s32 message; /* message number (?SSTATUS) */
    /* 0x4 */ s32 x;
    /* 0x8 */ s32 y;
} FieldmenuPos; /* size 0xC */

FieldmenuPos fieldmenu_name_pos[1] = {
    { 0x0C, 0x37, 0x13 },
};
FieldmenuPos fieldmenu_label_pos[5] = {
    { 0x01, 0x10, 0x1C },
    { 0x02, 0x10, 0x25 },
    { 0x04, 0x3D, 0x25 },
    { 0x03, 0x10, 0x2E },
    { 0x04, 0x3D, 0x2E },
};
FieldmenuPos fieldmenu_value_pos[5] = {
    { 0x0D, 0x2C, 0x1C },
    { 0x0E, 0x3B, 0x25 },
    { 0x0E, 0x5E, 0x25 },
    { 0x0E, 0x3B, 0x2E },
    { 0x0E, 0x5E, 0x2E },
};
s32 fieldmenu_value_stats[5] = { 0, 2, 3, 4, 5 };                                /* the GamestateStats.values shown by the values */
s32 fieldmenu_option_messages[2][6] = { { 6, 7, 8, 9, 10, 0 }, { 6, 7, 8, 9, 10, 11 } }; /* the options' message numbers, without / with the sixth option */
s32 fieldmenu_anim_pictures[6] = { 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22 };          /* pictures of the state-2 animation */

/* A party member's windows. */
typedef struct FieldmenuMemberWindows {
    /* 0x00 */ MessageWindow *name;    /* name */
    /* 0x04 */ MessageWindow *labels[5]; /* labels */
    /* 0x18 */ MessageWindow *values[5]; /* values */
} FieldmenuMemberWindows; /* size 0x2C */

/* fieldmenu_update's data block (object_new's third argument). */
typedef struct FieldmenuWindows {
    /* 0x00 */ MessageWindow *help;       /* "(triangle) Button: Close Status" */
    /* 0x04 */ MessageWindow *money_unit; /* "BIT" */
    /* 0x08 */ MessageWindow *money;    /* the money */
#ifdef PC_PORT
    /* 0x0C */ MessageWindow *options[9]; /* the options; from [5 + extended]: the SAVE entry (global_save), the Guide and
                                           * the bestiary (the PS1 has 6) */
#else
    /* 0x0C */ MessageWindow *options[6]; /* the options */
#endif
    /* 0x24 */ MessageCursor *cursor;    /* the cursor */
    /* 0x28 */ FieldmenuMemberWindows members[3];
#ifdef PC_PORT
    MessageWindow *bs_info[8]; /* the bestiary screen: the chosen species' name, then its seven elements' names, */
    MessageWindow *bs_icons[7];  /* their pictures */
    MessageWindow *bs_states[7]; /* and states */
    MessageWindow *bs_page;      /* "3/33" */
    MessageWindow *bs_hint;      /* the page buttons */
    MessageWindow *bs_drops;     /* "Drops" (PRD 4 layer C): the materials' names and counts take bs_info[6..7], bs_states[5..6] */
#endif
} FieldmenuWindows; /* size 0xAC */

typedef struct Fieldmenu {
    /* 0x00 */ Object base;
    /* 0x50 */ s32 layer_id; /* gfx layer (fieldmenu_create) */
    /* 0x54 */ s32 ot_depth; /* its ordering table entry (1) */
    /* 0x58 */ s32 cursor;   /* the option under the cursor */
    /* 0x5C */ s32 option_count; /* 5 + extended */
    /* 0x60 */ s32 extended; /* the sixth option is there */
    /* 0x64 */ s32 map_enabled; /* the third option (MAP) can be chosen (fieldmenu_get_map_region() >= 0) */
    /* 0x68 */ s32 anim_palette; /* state 2's animation: the palette of the picture being faded in */
    /* 0x6C */ s32 anim_time; /* gfx_module.funcs.get_time() at the last step */
    /* 0x70 */ WindowAnim panel_anims[3]; /* per party member's panel; [0] also the title, [1] the options, [2] the money */
#ifdef PC_PORT
    s32 save_entry; /* the global_save mod's SAVE entry is there (option extended + 5) */
    s32 bestiary;   /* the bestiary entry is there (the last option) */
    s32 guide;      /* the Guide's entry is there, */
    s32 guide_at;   /* at this option */
    s32 bs_sel;     /* the bestiary screen's chosen species, or -1 while it is closed */
#endif
} Fieldmenu; /* size 0xA0 */

void fieldmenu_create_windows(Fieldmenu *obj, FieldmenuWindows *data);
void fieldmenu_show_member(Fieldmenu *obj, FieldmenuWindows *data, s32 slot, s32 show);
s32 fieldmenu_get_map_region(void);
void fieldmenu_update(Fieldmenu *obj, FieldmenuWindows *data);

/* Creates the windows: the title, the options and their cursor, the money, and each party member's. */
void fieldmenu_create_windows(Fieldmenu *obj, FieldmenuWindows *data) {
    FieldmenuPos *pos;
    s32 i;
    s32 j;

    data->help = message_create_window(obj->layer_id, 1, 0x98, 0x13);
    for (j = 0; j < obj->option_count; j++) {
        data->options[j] = message_create_window(obj->layer_id, 1, 0xBD, j * 14 + 0x31);
    }
#ifdef PC_PORT
    if (obj->bestiary) {
        data->bs_info[0] = message_create_window(obj->layer_id, 1, BS_TEXT_X, BS_NAME_Y);
        for (j = 0; j < 7; j++) {
            data->bs_icons[j] = message_create_window(obj->layer_id, 1, BS_TEXT_X, j * 14 + BS_ROW_Y);
            data->bs_info[j + 1] = message_create_window(obj->layer_id, 1, BS_ELEM_X, j * 14 + BS_ROW_Y);
            data->bs_states[j] = message_create_window(obj->layer_id, 1, BS_STATE_X, j * 14 + BS_ROW_Y);
        }
        data->bs_page = message_create_window(obj->layer_id, 1, BS_PAGE_X, 0x13);
        data->bs_hint = message_create_window(obj->layer_id, 1, BS_TEXT_X, BS_HINT_Y);
        data->bs_drops = message_create_window(obj->layer_id, 1, BS_TEXT_X, BS_DROPS_Y);
    }
#endif
    data->cursor = message_create_cursor(obj->layer_id, 0, 0xB0, obj->cursor * 14 + 0x31);
    data->cursor->show(data->cursor, 0);
    data->money_unit = message_create_window(obj->layer_id, 3, 0x46, 0xA6);
    data->money = message_create_window(obj->layer_id, 3, 0x42, 0xA6);
    for (i = 0; i < 3; i++) {
        pos = fieldmenu_name_pos;
        data->members[i].name = message_create_window(obj->layer_id, 1, pos->x, pos->y + i * 46);
        for (j = 0; j < 5; j++) {
            pos = &fieldmenu_label_pos[j];
            data->members[i].labels[j] = message_create_window(obj->layer_id, 3, pos->x, pos->y + i * 46);
        }
        for (j = 0; j < 5; j++) {
            pos = &fieldmenu_value_pos[j];
            data->members[i].values[j] = message_create_window(obj->layer_id, 3, pos->x, pos->y + i * 46);
        }
    }
}

/* Shows party member `slot`'s name, level, HP and MP (or the empty-slot texts), or closes them. */
void fieldmenu_show_member(Fieldmenu *obj, FieldmenuWindows *data, s32 slot, s32 show) {
    GamestateStats stats;
    s32 id;
    u8 *name;
    s32 i;

    if (show) {
        id = gamestate_data.funcs.get_party_member(slot);
        if (id >= 0) {
            name = gamestate_data.funcs.get_record(id)->name;
            gamestate_data.funcs.get_stats(id, &stats);
            data->members[slot].name->set_text(data->members[slot].name, name, -1);
            for (i = 0; i < 5; i++) {
                data->members[slot].labels[i]->set_text(data->members[slot].labels[i],
                                                      cdload_module.files.get_file(records_language + 0xB0), fieldmenu_label_pos[i].message);
                data->members[slot].values[i]->set_line_number(data->members[slot].values[i], 0, stats.values[fieldmenu_value_stats[i]]);
                data->members[slot].values[i]->measure(data->members[slot].values[i], 1);
            }
        } else {
            data->members[slot].name->set_text(data->members[slot].name, cdload_module.files.get_file(records_language + 0xB0), 0xC);
            for (i = 0; i < 5; i++) {
                data->members[slot].values[i]->set_text(data->members[slot].values[i],
                                                      cdload_module.files.get_file(records_language + 0xB0), fieldmenu_name_pos[i + 6].message);
                data->members[slot].values[i]->measure(data->members[slot].values[i], 1);
            }
        }
    } else {
        data->members[slot].name->set_visible(data->members[slot].name, 0);
        for (i = 0; i < 5; i++) {
            data->members[slot].labels[i]->set_visible(data->members[slot].labels[i], 0);
            data->members[slot].values[i]->set_visible(data->members[slot].values[i], 0);
        }
    }
}

/* The MAP option's region for the current map (the field map when on map 0x1000): -1 from map 0x2D7 on (no MAP:
 * the option is greyed), 1 from 0x270, else 0. */
s32 fieldmenu_get_map_region(void) {
    s32 x = gamestate_data.funcs.get_map();

    if (x == 0x1000) {
        x = gamestate_data.field_map;
    }
    if (x >= 0x2D7) {
        return -1;
    }
    return x >= 0x270;
}


#ifdef PC_PORT
/* A species' name in `win`: its SDIGIEDT record's name (line of text 0x4E), "???" in grey while nothing of it is known;
 * the chosen one in the highlight's orange. */
static void fieldmenu_bestiary_name(MessageWindow *win, s32 index, s32 chosen) {
    if (def_bestiary_known(index) == 0) {
        win->set_text(win, (u8 *)def_str_Unknown, -1);
        win->set_palette(win, chosen ? 3 : 7);
    } else {
        FightstgEnemyRecord *recs = (FightstgEnemyRecord *)cdload_module.files.get_file(0x1CF);
        win->set_text(win, cdload_module.files.get_file(records_language + 0x4E), recs[index].name);
        win->set_palette(win, chosen ? 3 : 0);
    }
}

/* PRD 4 layers B and C (the owner's materials model of 2026-11-13): under the families, the species' Hunt line. The
 * first line its common material and how many the player holds (grey at none; "-" for a species no random encounter
 * has); the second, for a species that leads Rare Targets, its rare items as a collection: how many of its regions'
 * ones were ever held, of how many ("4/13", orange when all). "???" while nothing of the species is known. */
static void fieldmenu_bestiary_drops(Fieldmenu *obj, FieldmenuWindows *data, s32 species) {
    static u8 held[8];   /* "x12": the windows keep the pointers */
    static u8 rares[12]; /* "4/13" */
    static const u8 none[1] = { 0 };
    s32 known = def_bestiary_known(obj->bs_sel) != 0;
    s32 m = known ? def_hunt_common_of(obj->bs_sel) : -1;
    s32 count;
    int seen;
    int total;
    s32 n;
    s32 i;

    data->bs_drops->set_text(data->bs_drops, (u8 *)def_str_HuntDrops, -1);
    if (m >= 0) {
        count = def_record_material(m);
        n = 0;
        for (i = 0; def_str_HuntHeld[i] != 0 && n < (s32)sizeof(held) - 3; i++) {
            held[n++] = def_str_HuntHeld[i];
        }
        if (count >= 10) {
            held[n++] = 4 + count / 10; /* the font's digits (glyph 4 is 0) */
        }
        held[n++] = 4 + count % 10;
        held[n] = 0;
        data->bs_info[6]->set_text(data->bs_info[6], (u8 *)def_hunt_material_name(m), -1);
        data->bs_info[6]->set_palette(data->bs_info[6], 0);
        data->bs_states[5]->set_text(data->bs_states[5], held, -1);
        data->bs_states[5]->set_palette(data->bs_states[5], count != 0 ? 0 : 7);
        data->bs_states[5]->set_pos(data->bs_states[5], BS_HELD_X, BS_DROPS_Y);
    } else {
        data->bs_info[6]->set_text(data->bs_info[6], (u8 *)(known ? def_str_Neutral : def_str_Unknown), -1);
        data->bs_info[6]->set_palette(data->bs_info[6], 7);
        data->bs_states[5]->set_text(data->bs_states[5], (u8 *)none, -1);
    }
    def_hunt_species_rares(species, &seen, &total);
    if (known && total > 0) {
        n = 0;
        if (seen >= 10) {
            rares[n++] = 4 + seen / 10;
        }
        rares[n++] = 4 + seen % 10;
        for (i = 0; def_str_Slash[i] != 0 && n < (s32)sizeof(rares) - 3; i++) {
            rares[n++] = def_str_Slash[i];
        }
        if (total >= 10) {
            rares[n++] = 4 + total / 10;
        }
        rares[n++] = 4 + total % 10;
        rares[n] = 0;
        data->bs_info[7]->set_text(data->bs_info[7], (u8 *)def_str_HuntRareLabel, -1);
        data->bs_info[7]->set_palette(data->bs_info[7], 0);
        data->bs_states[6]->set_text(data->bs_states[6], rares, -1);
        data->bs_states[6]->set_palette(data->bs_states[6], seen == total ? 3 : seen != 0 ? 0 : 7);
        data->bs_states[6]->set_pos(data->bs_states[6], BS_RARE_X, BS_DROPS_Y + 14);
    } else {
        data->bs_info[7]->set_text(data->bs_info[7], (u8 *)none, -1);
        data->bs_states[6]->set_text(data->bs_states[6], (u8 *)none, -1);
    }
}

/* Fills the screen for the chosen species: its page of names in the options' windows, its elements in the info ones. */
static void fieldmenu_bestiary_draw(Fieldmenu *obj, FieldmenuWindows *data) {
    static u8 page[10]; /* the window keeps the pointer */
    FightstgEnemyRecord *recs = (FightstgEnemyRecord *)cdload_module.files.get_file(0x1CF);
    s32 first = obj->bs_sel / BS_ROWS * BS_ROWS;
    s32 pages = (BS_SPECIES + BS_ROWS - 1) / BS_ROWS;
    s32 palette;
    s32 n;
    s32 i;

    for (i = 0; i < obj->option_count; i++) {
        if (i < BS_ROWS && first + i < BS_SPECIES) {
            fieldmenu_bestiary_name(data->options[i], first + i, first + i == obj->bs_sel);
        } else {
            data->options[i]->set_visible(data->options[i], 0);
        }
    }
    fieldmenu_bestiary_name(data->bs_info[0], obj->bs_sel, 0);
    /* A line per family. Of the seven lines' windows the two left over carry the pairs' second pictures (on those lines)
     * and the Hunt Materials' names and counts (fieldmenu_bestiary_drops). */
    for (i = 0; i < 7; i++) {
        if (i < BS_FAMILIES) {
            data->bs_icons[i]->set_text(data->bs_icons[i], (u8 *)def_family_icon(i, 0), -1);
            data->bs_info[i + 1]->set_text(data->bs_info[i + 1], (u8 *)def_family_name(i), -1);
            data->bs_states[i]->set_text(data->bs_states[i], (u8 *)def_bestiary_family_state(&recs[obj->bs_sel], obj->bs_sel, i, &palette), -1);
            data->bs_states[i]->set_palette(data->bs_states[i], palette);
        } else {
            s32 f = i == BS_FAMILIES ? 1 : 3; /* Water+Ice, Thunder+Machine */

            data->bs_icons[i]->set_text(data->bs_icons[i], (u8 *)def_family_icon(f, 1), -1);
            data->bs_icons[i]->set_pos(data->bs_icons[i], BS_TEXT_X + BS_PAIR_DX, f * 14 + BS_ROW_Y);
        }
    }
    fieldmenu_bestiary_drops(obj, data, recs[obj->bs_sel].id);
    /* "page/pages" in the font's digits (glyph 4 is 0) */
    i = first / BS_ROWS + 1;
    n = 0;
    if (i >= 10) {
        page[n++] = 4 + i / 10;
    }
    page[n++] = 4 + i % 10;
    for (i = 0; def_str_Slash[i] != 0; i++) {
        page[n++] = def_str_Slash[i];
    }
    page[n++] = 4 + pages / 10;
    page[n++] = 4 + pages % 10;
    page[n] = 0;
    data->bs_page->set_text(data->bs_page, page, -1);
    data->cursor->set_pos(data->cursor, 0xB0, (obj->bs_sel - first) * 14 + 0x31);
}

/* An opaque rectangle on the menu's layer. */
static void fieldmenu_bestiary_rect(Fieldmenu *obj, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(obj->layer_id);
    POLY_F4 *poly = gfx_module.funcs.get_packet();

    setPolyF4(poly);
    setRGB0(poly, r, g, b);
    poly->x0 = poly->x2 = x;
    poly->x1 = poly->x3 = x + w;
    poly->y0 = poly->y1 = y;
    poly->y2 = poly->y3 = y + h;
    addPrim(layer->get_ot_entry(layer, obj->ot_depth), poly);
    gfx_module.funcs.set_packet(poly + 1);
}

/* The chosen species' panel. The bank has none of this size (the member panels are three boxes with a name tab each),
 * so it is drawn flat in the bank's panels' colours: the frame, the name's band, a rule between the lines. The layer
 * draws what is added last first, so the fill goes last. */
static void fieldmenu_bestiary_panel(Fieldmenu *obj) {
    s32 i;

    for (i = 1; i <= BS_FAMILIES; i++) { /* between the families' lines, and above the Hunt Materials' (PRD 4 layer C) */
        fieldmenu_bestiary_rect(obj, BS_PANEL_X + 6, BS_ROW_Y + i * 14 - 1, BS_PANEL_W - 12, 1, 41, 41, 115);
    }
    fieldmenu_bestiary_rect(obj, BS_PANEL_X + 2, BS_PANEL_Y + 2, BS_PANEL_W - 4, BS_ROW_Y - 4 - BS_PANEL_Y, 0, 0, 74);
    fieldmenu_bestiary_rect(obj, BS_PANEL_X + 1, BS_PANEL_Y + 1, BS_PANEL_W - 2, BS_PANEL_H - 2, 0, 0, 33);
    fieldmenu_bestiary_rect(obj, BS_PANEL_X, BS_PANEL_Y, BS_PANEL_W, BS_PANEL_H, 107, 107, 214);
}

/* Opens the screen on the first species: the member panels' texts and the money give way to it. */
static void fieldmenu_bestiary_open(Fieldmenu *obj, FieldmenuWindows *data) {
    s32 i;

    for (i = 0; i < 3; i++) {
        fieldmenu_show_member(obj, data, i, 0);
    }
    data->money_unit->set_visible(data->money_unit, 0);
    data->money->set_visible(data->money, 0);
    data->help->set_text(data->help, (u8 *)def_str_Bestiary, -1);
    data->bs_hint->set_text(data->bs_hint, (u8 *)def_str_BestiaryHint, -1);
    data->bs_hint->set_palette(data->bs_hint, 7);
    obj->bs_sel = 0;
    fieldmenu_bestiary_draw(obj, data);
}

/* One frame of the open screen: up/down move, L1/R1 turn the page, triangle/circle/cross close it (returns 1). */
static s32 fieldmenu_bestiary_update(Fieldmenu *obj, FieldmenuWindows *data) {
    s32 sel = obj->bs_sel;
    s32 i;

    if (PAD_PRESSED(12) || PAD_PRESSED(13) || PAD_PRESSED(14)) {
        sound_module.play(0x800450BD);
        for (i = 0; i < 8; i++) {
            data->bs_info[i]->set_visible(data->bs_info[i], 0);
        }
        for (i = 0; i < 7; i++) {
            data->bs_icons[i]->set_visible(data->bs_icons[i], 0);
            data->bs_states[i]->set_visible(data->bs_states[i], 0);
        }
        data->bs_page->set_visible(data->bs_page, 0);
        data->bs_hint->set_visible(data->bs_hint, 0);
        data->bs_drops->set_visible(data->bs_drops, 0);
        for (i = 0; i < 3; i++) {
            fieldmenu_show_member(obj, data, i, 1);
        }
        data->money_unit->set_text(data->money_unit, cdload_module.files.get_file(records_language + 0xB0), 5);
        data->money->set_line_number(data->money, 0, gamestate_data.money);
        data->money->measure(data->money, 1);
        data->help->set_text(data->help, cdload_module.files.get_file(records_language + 0xB0), 0x13);
        for (i = 0; i < obj->option_count; i++) {
            if (obj->save_entry && i == obj->extended + 5) {
                data->options[i]->set_text(data->options[i], cdload_module.files.get_file(records_language + 0x78), 1);
            } else if (i == obj->option_count - 1) {
                data->options[i]->set_text(data->options[i], (u8 *)def_str_BestiaryMenu, -1);
            } else if (obj->guide && i == obj->guide_at) {
                data->options[i]->set_text(data->options[i], (u8 *)def_str_GuideMenu, -1);
            } else {
                data->options[i]->set_text(data->options[i], cdload_module.files.get_file(records_language + 0xB0),
                                           fieldmenu_option_messages[obj->extended][i]);
            }
            data->options[i]->set_palette(data->options[i], 0);
        }
        if (obj->map_enabled == 0) {
            data->options[2]->set_palette(data->options[2], 7);
        }
        data->cursor->set_pos(data->cursor, 0xB0, obj->cursor * 14 + 0x31);
        obj->bs_sel = -1;
        return 1;
    }
    if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
        sel = (sel + BS_SPECIES - 1) % BS_SPECIES;
    } else if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
        sel = (sel + 1) % BS_SPECIES;
    } else if (PAD_PRESSED(10) || PAD_REPEAT(10)) {
        sel = sel >= BS_ROWS ? sel - BS_ROWS : 0;
    } else if (PAD_PRESSED(11) || PAD_REPEAT(11)) {
        sel = sel + BS_ROWS < BS_SPECIES ? sel + BS_ROWS : BS_SPECIES - 1;
    }
    if (sel != obj->bs_sel) {
        obj->bs_sel = sel;
        sound_module.play(0x8004513E);
        fieldmenu_bestiary_draw(obj, data);
    }
    return 0;
}
#endif

/* The object's function. State 0 sets up; state 1 runs the menu (obj->base.step: open the windows,
 * choose, close them) and draws its frames; state 2 plays the picture animation, then changes the map. */
void fieldmenu_update(Fieldmenu *obj, FieldmenuWindows *data) {
    Sprite gfx;
    Sprite gfx2;
    s32 cursor;
    s32 done;
    s32 level;
    s32 i;
    s32 j;

    switch (obj->base.state) {
    case OBJECT_STATE_INIT:
    default:
        obj->base.next_state(obj);
        obj->panel_anims[0].duration = obj->panel_anims[1].duration = obj->panel_anims[2].duration = 10;
        window_anim_start(&obj->panel_anims[0], 1);
        window_anim_start(&obj->panel_anims[1], 1);
        window_anim_start(&obj->panel_anims[2], 1);
        if (gamestate_data.items[0x192] != 0) {
            records_field_menu_choice.extended = 1;
            obj->extended = 1;
        } else {
            records_field_menu_choice.extended = 0;
        }
        obj->option_count = obj->extended + 5;
#ifdef PC_PORT
        /* The destination survives this: the map page closes, STSTATUS rebuilds this menu, and the choice at case 6
         * is what consumes it. A stale one from a cancelled menu is cleared by the next START (fieldstg). */
        if (port_mod_global_save != 0 && gamestate_data.funcs.get_map() != 0x1000) {
            obj->option_count++; /* global_save: a SAVE entry last, in the field's menu only (docs/LAUNCHER.md) */
            obj->save_entry = 1;
        } else {
            obj->save_entry = 0;
        }
        /* The bestiary, after SAVE, in the field's menu only; not in a replay script (shared with the emulator) */
        obj->bestiary = def_bestiary_active() && gamestate_data.funcs.get_map() != 0x1000;
        obj->bs_sel = -1;
        /* The Guide, between SAVE and the bestiary (which stays the last entry), in the field's menu only */
        obj->guide = def_guide_active() && gamestate_data.funcs.get_map() != 0x1000;
        if (obj->guide) {
            obj->guide_at = obj->option_count++;
        }
        if (obj->bestiary) {
            obj->option_count++;
        }
#endif
        if (fieldmenu_get_map_region() >= 0) {
            obj->map_enabled = 1;
        } else {
            obj->map_enabled = 0;
        }
        fieldmenu_create_windows(obj, data);
        break;
    case OBJECT_STATE_RUN:
        switch (obj->base.step) {
        case 0:
        default:
            if (window_anim_update(&obj->panel_anims[0])) {
                fieldmenu_show_member(obj, data, 0, 1);
                data->help->set_text(data->help, cdload_module.files.get_file(records_language + 0xB0), 0x13);
                sound_module.play(0x40019);
                obj->base.step++;
            }
            break;
        case 1:
            if (window_anim_update(&obj->panel_anims[1])) {
                fieldmenu_show_member(obj, data, 1, 1);
                for (obj->base.timer = 0; obj->base.timer < obj->option_count; obj->base.timer++) {
#ifdef PC_PORT
                    if (obj->save_entry && obj->base.timer == obj->extended + 5) {
                        /* global_save's SAVE entry: the save screen's title (?SMEMCRD entry 1, in every language) */
                        data->options[obj->base.timer]->set_text(data->options[obj->base.timer],
                                                                cdload_module.files.get_file(records_language + 0x78), 1);
                        continue;
                    }
                    if (obj->bestiary && obj->base.timer == obj->option_count - 1) {
                        data->options[obj->base.timer]->set_text(data->options[obj->base.timer], (u8 *)def_str_BestiaryMenu, -1);
                        continue;
                    }
                    if (obj->guide && obj->base.timer == obj->guide_at) {
                        data->options[obj->base.timer]->set_text(data->options[obj->base.timer], (u8 *)def_str_GuideMenu, -1);
                        continue;
                    }
#endif
                    data->options[obj->base.timer]->set_text(data->options[obj->base.timer],
                                                            cdload_module.files.get_file(records_language + 0xB0),
                                                            fieldmenu_option_messages[obj->extended][obj->base.timer]);
                }
                sound_module.play(0x40019);
                if (obj->map_enabled == 0) {
                    data->options[2]->set_palette(data->options[2], 7);
                }
                obj->base.step++;
            }
            break;
        case 2:
            if (window_anim_update(&obj->panel_anims[2])) {
                fieldmenu_show_member(obj, data, 2, 1);
                data->money_unit->set_text(data->money_unit, cdload_module.files.get_file(records_language + 0xB0), 5);
                data->money->set_line_number(data->money, 0, gamestate_data.money);
                data->money->measure(data->money, 1);
                data->cursor->show(data->cursor, 1);
                obj->base.step++;
            }
            break;
        case 3:
            cursor = obj->cursor;
            if (PAD_PRESSED(4) || PAD_REPEAT(4)) {
                if (--obj->cursor < 0) {
                    obj->cursor = 0;
                }
            } else if (PAD_PRESSED(6) || PAD_REPEAT(6)) {
                if (++obj->cursor > obj->option_count - 1) {
                    obj->cursor = obj->option_count - 1;
                }
            }
            if (cursor != obj->cursor) {
                sound_module.play(0x8004513E);
                data->cursor->set_pos(data->cursor, 0xB0, obj->cursor * 14 + 0x31);
                break;
            }
            done = 0;
            if (PAD_PRESSED(13)) {
                sound_module.play(0x8004503C);
                if (obj->map_enabled == 0 && obj->cursor == 2) {
                    break;
                }
#ifdef PC_PORT
                if (obj->bestiary && obj->cursor == obj->option_count - 1) {
                    fieldmenu_bestiary_open(obj, data);
                    obj->base.step = BS_STEP;
                    break;
                }
                if (obj->guide && obj->cursor == obj->guide_at) {
                    /* the menu closes as on a cancel (the field's menu only: substep 1 ends it without a map change),
                     * and the field's next idle frame opens the Guide */
                    def_guide_request();
                    done = 1;
                    obj->base.substep = 1;
                } else {
#endif
                done = 1;
                if (gamestate_data.funcs.get_map() == 0x1000) {
                    obj->base.substep = 1;
                } else {
                    obj->base.substep = 0;
                }
                records_field_menu_choice.option = obj->cursor;
#ifdef PC_PORT
                }
#endif
            } else if (PAD_PRESSED(14)) {
                sound_module.play(0x800450BD);
                done = 1;
                if (gamestate_data.funcs.get_map() == 0x1000) {
                    obj->base.substep = 0;
                } else {
                    obj->base.substep = 1;
                }
            }
            if (done) {
                window_anim_start(&obj->panel_anims[0], 0);
                window_anim_start(&obj->panel_anims[1], 0);
                window_anim_start(&obj->panel_anims[2], 0);
                fieldmenu_show_member(obj, data, 2, 0);
                data->money_unit->set_visible(data->money_unit, 0);
                data->money->set_visible(data->money, 0);
                data->cursor->show(data->cursor, 0);
                obj->base.step++;
            }
            break;
#ifdef PC_PORT
        case BS_STEP:
            if (fieldmenu_bestiary_update(obj, data)) {
                obj->base.step = 3;
            }
            break;
#endif
        case 4:
            if (window_anim_update(&obj->panel_anims[2])) {
                fieldmenu_show_member(obj, data, 1, 0);
                for (obj->base.timer = 0; obj->base.timer < obj->option_count; obj->base.timer++) {
                    data->options[obj->base.timer]->set_visible(data->options[obj->base.timer], 0);
                }
                sound_module.play(0x4001A);
                obj->base.step++;
            }
            break;
        case 5:
            if (window_anim_update(&obj->panel_anims[1])) {
                fieldmenu_show_member(obj, data, 0, 0);
                data->help->set_visible(data->help, 0);
                sound_module.play(0x4001A);
                obj->base.step++;
            }
            break;
        case 6:
            if (window_anim_update(&obj->panel_anims[0])) {
                if (obj->base.substep == 0) {
                    obj->base.set_state(obj, OBJECT_STATE_DONE);
                    obj->anim_time = gfx_module.funcs.get_time();
                } else {
                    obj->base.set_state(obj, OBJECT_STATE_END);
                }
            }
            break;
        }
        sprite_init(&gfx);
        gfx.set_vram_pos(0x140, 0);
        gfx.set_layer_id(obj->layer_id, obj->ot_depth);
        gfx.set_follow_scroll(0);
        for (i = 0; i < 3; i++) {
            level = obj->panel_anims[i].level;
#ifdef PC_PORT
            if (obj->bs_sel >= 0) {
                break; /* the bestiary screen has no member panels: its element list takes their space */
            }
#endif
            if (level != 0) {
                if (level != 0x1000) {
                    gfx.set_scale(level, 0x1000, 0x1000);
                    gfx.set_pivot(0, i * 46 + 0x25);
                } else {
                    gfx.set_scale(0x1000, 0x1000, 0x1000);
                }
                gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x15, 0, i * 46 + 0x11);
                gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x17, 0, i * 46 + 0x11);
            }
        }
        if (obj->panel_anims[0].level != 0) {
            if (obj->panel_anims[0].level != 0x1000) {
                gfx.set_scale(obj->panel_anims[0].level, 0x1000, 0x1000);
                gfx.set_pivot(0x140, 0x19);
            } else {
                gfx.set_scale(0x1000, 0x1000, 0x1000);
            }
            gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x18, 0x22, 0xD);
        }
        if (obj->panel_anims[1].level != 0) {
            if (obj->panel_anims[1].level != 0x1000) {
                gfx.set_scale(obj->panel_anims[1].level, 0x1000, 0x1000);
                gfx.set_pivot(0x140, 0x52);
            } else {
                gfx.set_scale(0x1000, 0x1000, 0x1000);
            }
#ifdef PC_PORT
            if (obj->bs_sel >= 0) {
                /* the bestiary's page is six names whatever the menu has: the 6-entry panel as it is */
                gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x1B, 0xA8, 0x28);
            } else if (obj->option_count >= 7) {
                /* global_save's entry with the card case's: the bank has panels for 5 and 6 entries only (a 22 px top,
                 * 14 px rows, a 20 px bottom), so the 6-entry one (98 px) is stretched by 14 px per entry more
                 * (7: 112 px, 8: 126 px) from its top */
                gfx.set_scale(obj->panel_anims[1].level, 0x1000 + (obj->option_count - 6) * 0x249, 0x1000);
                gfx.set_pivot(0x140, 0x28);
                gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x1B, 0xA8, 0x28);
            } else {
                gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x1C - (obj->option_count - 5), 0xA8, 0x28);
            }
#else
            gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x1C - obj->extended, 0xA8, 0x28);
#endif
        }
#ifdef PC_PORT
        if (obj->bs_sel >= 0) {
            fieldmenu_bestiary_panel(obj); /* in the member panels' and the money's place */
            break;
        }
#endif
        if (obj->panel_anims[2].level != 0) {
            if (obj->panel_anims[2].level != 0x1000) {
                gfx.set_scale(obj->panel_anims[2].level, 0x1000, 0x1000);
                gfx.set_pivot(0, 0xA8);
            } else {
                gfx.set_scale(0x1000, 0x1000, 0x1000);
            }
            gfx.draw(cdload_module.get_subfile_by_id(0x2860000), 0x1A, 0, 0x9E);
        }
        break;
    case OBJECT_STATE_DONE:
        switch (obj->base.step) {
        default:
            obj->base.set_state(obj, OBJECT_STATE_DONE);
            /* fallthrough */
        case 0:
        case 1:
        case 2:
            if (gfx_module.funcs.get_time() - obj->anim_time >= 2) {
                obj->anim_time = gfx_module.funcs.get_time();
                if (++obj->anim_palette >= 8) {
                    if (++obj->base.step == 3) {
                        obj->anim_palette = 8;
                    } else {
                        obj->anim_palette = 0;
                    }
                }
            }
            break;
        case 3:
        case 4:
        case 5:
            if (gfx_module.funcs.get_time() - obj->anim_time >= 2) {
                obj->anim_time = gfx_module.funcs.get_time();
                if (++obj->anim_palette >= 16) {
                    if (++obj->base.step == 6) {
                        obj->anim_palette = 15;
                    } else {
                        obj->anim_palette = 8;
                    }
                }
            }
            break;
        case 6:
#ifdef PC_PORT
            if (obj->save_entry && obj->cursor == obj->extended + 5) {
                port_global_save_open(); /* global_save's SAVE: STGMCARD in save mode instead of STSTATUS */
                obj->base.step++;
                break;
            }
#endif
            if (gamestate_data.funcs.get_map() == 0x1000) {
                gamestate_data.funcs.set_next_map(gamestate_data.field_map, 0);
            } else {
                gamestate_data.funcs.set_next_map(0x1000, 0);
                records_field_menu_choice.option = obj->cursor;
            }
            obj->base.step++;
            break;
        case 7:
            break;
        }
        sprite_init(&gfx2);
        gfx2.set_vram_pos(0x140, 0);
        gfx2.set_layer_id(obj->layer_id, obj->ot_depth);
        gfx2.set_follow_scroll(0);
        for (j = 0; j <= obj->base.step; j++) {
            if (j == 6) {
                break;
            }
            if (j == obj->base.step) {
                gfx2.set_palette(obj->anim_palette);
            } else if (j < 3) {
                gfx2.set_palette(7);
            } else {
                gfx2.set_palette(15);
            }
            gfx2.draw(cdload_module.get_subfile_by_id(0x2860000), fieldmenu_anim_pictures[j], 0, 0);
        }
        break;
    case OBJECT_STATE_END:
        break;
    }
}

Fieldmenu *fieldmenu_create(s32 layer, s32 option) {
    Fieldmenu *obj = object_new(fieldmenu_update, sizeof(Fieldmenu), sizeof(FieldmenuWindows));

    obj->layer_id = layer;
    obj->ot_depth = 1;
    obj->cursor = option;
    return obj;
}
