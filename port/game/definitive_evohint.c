/* Definitive edition, PRD 2 layer J: the Digi-Lab's "Next Forms" panel. Read only: it shows, for a party member, the
 * forms one step ahead (DEF_EVO_HINT_DEPTH) and what each still needs, from the game's own learn lists
 * (stfgtrep_learn_lists, checked by stfgtrep_learn_technique at each level-up). It changes no rule and no save.
 *
 * The Digi-Lab's Digimon screen (stgdglab_80087070.c) opens it with Square from its entries and calls
 * def_evohint_frame every frame until it returns 1. Layout (320x240), in the bestiary's colours: the lab's own partner
 * panel stays at the top left (it names the partner, and L1/R1 switches it), "Next Forms" beside it; below, on the
 * left the forms in reach, each with its requirements met / total or "Ready" in green, and on the right the chosen
 * form, one block per requirement (green when met) with its current value, then a status line; the keys at the bottom.
 * The 9-pixel font has no space nor symbols (it draws them as 0): only numbers go in it. */
#include "common.h"
#include "cdload.h"
#include "gamestate.h"
#include "gfx.h"
#include "heap.h"
#include "message.h"
#include "pad.h"
#include "records.h"
#include "sound.h"

#include "definitive.h"

/* STFGTREP's learn list, as stfgtrep_80082E70.c declares it (that overlay's data; the lists are constant). */
typedef struct EvoRequirement {
    s16 technique; /* a form it needs (1-based records_digimon row), 0: none */
    s16 level;     /* at this form level at least */
} EvoRequirement;
typedef struct EvoLearn {
    s32 technique; /* the form, 1-based records_digimon row */
    EvoRequirement requires[2];
    s16 stat; /* 1-6 the six stats, 7 the level, 8-14 the resists; 0: none */
    s16 min;
} EvoLearn;
extern EvoLearn *stfgtrep_learn_lists[];

extern const unsigned char def_str_EvoTitle[], def_str_EvoKeys[], def_str_EvoLv[], def_str_EvoLevel[],
    def_str_EvoReady[], def_str_EvoReadyLine[], def_str_EvoMissing[], def_str_EvoNone[], def_str_EvoNoneHint[],
    def_str_EvoStr[], def_str_EvoDef[], def_str_EvoSpi[], def_str_EvoWis[], def_str_EvoSpd[], def_str_EvoCha[],
    def_str_Slash[], def_str_EvoOpen[];

#define EH_ROWS 9         /* forms a page of the list shows */
#define EH_MAX 44         /* forms a list has */
#define EH_LIST_X 4
#define EH_LIST_W 150
#define EH_INFO_X 158
#define EH_INFO_W 158
#define EH_TOP 76         /* the panels' top: under the lab's partner panel */
#define EH_BOTTOM 214     /* and bottom */
#define EH_ROW_Y 82       /* the first row's text */
#define EH_TITLE_X 236    /* "Next Forms", right of the partner panel */
#define EH_TITLE_Y 26
#define EH_LINE 14
#define EH_PAL_TEXT 0
#define EH_PAL_HOT 3      /* the chosen form (the bestiary's orange) */
#define EH_PAL_MET 4      /* a requirement met, "Ready" */
#define EH_PAL_DIM 7

typedef struct EvoReq {
    s16 kind;    /* 0 a form, 1 a stat, 2 the level */
    s16 what;    /* the form's row, or the stat (1-6, 8-14) */
    s16 need;
    s16 have;
} EvoReq;

typedef struct EvoForm {
    s16 row; /* records_digimon row (1-based) */
    s16 count;
    s16 met;
    EvoReq reqs[3];
} EvoForm;

static struct {
    s32 open;
    s32 layer;
    s32 slot;     /* party slot shown */
    s32 sel;      /* the form chosen */
    s32 n;
    EvoForm forms[EH_MAX];
    MessageWindow *title, *keys, *label, *list[EH_ROWS], *count[EH_ROWS], *head, *req[3], *val[3], *status, *page;
    u8 title_buf[48], count_buf[EH_ROWS][8], req_buf[3][48], val_buf[3][16], status_buf[48], page_buf[8];
} eh;

static s32 eh_member(s32 slot) {
    return gamestate_data.funcs.get_party_member(slot);
}

/* The stat a requirement names, as the screen calls it (the six stats, the level, the seven elements). */
static const unsigned char *eh_stat_name(s32 stat) {
    static const unsigned char *const six[6] = { def_str_EvoStr, def_str_EvoDef, def_str_EvoSpi,
                                                 def_str_EvoWis, def_str_EvoSpd, def_str_EvoCha };

    if (stat >= 1 && stat <= 6) {
        return six[stat - 1];
    }
    if (stat >= 8 && stat <= 14) {
        return def_inspect_element(stat - 8);
    }
    return def_str_EvoLevel;
}

/* Gathers the forms in reach of Digimon `d`: not owned, and (depth 1) every form they need already owned. */
static void eh_collect(s32 d) {
    GamestateRecord *r = gamestate_data.funcs.get_record(d);
    EvoLearn *list = stfgtrep_learn_lists[d];
    GamestateForm f;
    s32 i, k, id, owned_all;

    eh.n = 0;
    for (i = 0; i < EH_MAX; i++) {
        EvoForm *e = &eh.forms[eh.n];

        id = records_digimon[list[i].technique - 1].id;
        if (id <= 0 || gamestate_data.funcs.get_form(d, id, &f) >= 0) {
            continue;
        }
        e->row = (s16)list[i].technique;
        e->count = e->met = 0;
        owned_all = 1;
        for (k = 0; k < 2; k++) {
            if (list[i].requires[k].technique != 0) {
                EvoReq *q = &e->reqs[e->count++];

                q->kind = 0;
                q->what = list[i].requires[k].technique;
                q->need = list[i].requires[k].level;
                if (gamestate_data.funcs.get_form(d, records_digimon[q->what - 1].id, &f) >= 0) {
                    q->have = f.level;
                } else {
                    q->have = 0;
                    owned_all = 0;
                }
                e->met += q->have >= q->need;
            }
        }
        if (list[i].stat != 0) {
            EvoReq *q = &e->reqs[e->count++];

            q->kind = list[i].stat == 7 ? 2 : 1;
            q->what = list[i].stat;
            q->need = list[i].min;
            if (list[i].stat < 7) {
                q->have = (&r->stats.values[5])[list[i].stat];
            } else if (list[i].stat == 7) {
                q->have = r->stats.values[0];
            } else {
                q->have = (&r->stats.values[4])[list[i].stat];
            }
            e->met += q->have >= q->need;
        }
        /* one step ahead: a form whose required forms are not all owned yet is two steps away (DEF_EVO_HINT_DEPTH 1);
         * 99 shows the whole list */
        if (!owned_all && DEF_EVO_HINT_DEPTH < 2) {
            continue;
        }
        eh.n++;
    }
}

/* Glyph-byte string building: the game's text is bytes (digits are glyphs 4..13), the windows keep the pointer. */
static u8 *eh_cat(u8 *p, const unsigned char *s) {
    while (*s != 0) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

static u8 *eh_num(u8 *p, s32 v) {
    u8 tmp[8];
    s32 n = 0;

    if (v < 0) {
        v = 0;
    }
    do {
        tmp[n++] = (u8)(4 + v % 10);
        v /= 10;
    } while (v > 0 && n < 7);
    while (n > 0) {
        *p++ = tmp[--n];
    }
    *p = 0;
    return p;
}

static u8 *eh_space(u8 *p) {
    *p++ = 1;
    *p++ = 1;
    *p = 0;
    return p;
}

/* The form's name (text 0x4E), through the font's entry lookup so the port's text overrides apply. */
static u8 *eh_name(u8 *p, s32 row) {
    Font funcs;
    u8 *s;

    font_init(&funcs);
    s = funcs.get_entry(cdload_module.files.get_file(records_language + 0x4E), records_digimon[row - 1].name_id);
    return s != NULL ? eh_cat(p, s) : p;
}

static MessageWindow *eh_window(s32 font, s32 x, s32 y) {
    MessageWindow *w = message_create_window(eh.layer, font, x, y);

    w->set_ot_depth(w, 1);
    return w;
}

static void eh_hide(MessageWindow *w) {
    w->set_text(w, (u8 *)"", -1);
}

/* Fills every window for the slot and the chosen form. */
static void eh_fill(void) {
    GamestateRecord *r = gamestate_data.funcs.get_record(eh_member(eh.slot));
    s32 first = eh.sel / EH_ROWS * EH_ROWS;
    s32 i;
    u8 *p;

    (void)r;
    eh.title->set_text(eh.title, (u8 *)def_str_EvoTitle, -1);

    for (i = 0; i < EH_ROWS; i++) {
        EvoForm *e = &eh.forms[first + i];

        if (first + i >= eh.n) {
            eh_hide(eh.list[i]);
            eh_hide(eh.count[i]);
            continue;
        }
        eh.list[i]->set_text(eh.list[i], cdload_module.files.get_file(records_language + 0x4E),
                             records_digimon[e->row - 1].name_id);
        eh.list[i]->set_palette(eh.list[i], first + i == eh.sel ? EH_PAL_HOT : EH_PAL_TEXT);
        if (e->met == e->count) {
            eh_cat(eh.count_buf[i], def_str_EvoReady);
            eh.count[i]->set_palette(eh.count[i], EH_PAL_MET);
        } else {
            p = eh_num(eh.count_buf[i], e->met);
            p = eh_cat(p, def_str_Slash);
            eh_num(p, e->count);
            eh.count[i]->set_palette(eh.count[i], EH_PAL_DIM);
        }
        eh.count[i]->set_text(eh.count[i], eh.count_buf[i], -1);
    }

    if (eh.n == 0) {
        eh.head->set_text(eh.head, (u8 *)def_str_EvoNone, -1);
        eh.head->set_palette(eh.head, EH_PAL_TEXT);
        for (i = 0; i < 3; i++) {
            eh_hide(eh.req[i]);
            eh_hide(eh.val[i]);
        }
        eh.status->set_text(eh.status, (u8 *)def_str_EvoNoneHint, -1);
        eh.status->set_palette(eh.status, EH_PAL_DIM);
        eh_hide(eh.page);
        return;
    }
    {
        EvoForm *e = &eh.forms[eh.sel];

        eh.head->set_text(eh.head, cdload_module.files.get_file(records_language + 0x4E),
                          records_digimon[e->row - 1].name_id);
        eh.head->set_palette(eh.head, EH_PAL_HOT);
        for (i = 0; i < 3; i++) {
            EvoReq *q = &e->reqs[i];

            if (i >= e->count) {
                eh_hide(eh.req[i]);
                eh_hide(eh.val[i]);
                continue;
            }
            /* "Greymon Lv 40", "Strength 180", "Level 15" / "12/40" */
            if (q->kind == 0) {
                p = eh_name(eh.req_buf[i], q->what);
                p = eh_space(p);
                p = eh_cat(p, def_str_EvoLv);
            } else {
                p = eh_cat(eh.req_buf[i], eh_stat_name(q->what));
            }
            p = eh_space(p);
            eh_num(p, q->need);
            eh.req[i]->set_text(eh.req[i], eh.req_buf[i], -1);
            eh.req[i]->set_palette(eh.req[i], q->have >= q->need ? EH_PAL_MET : EH_PAL_TEXT);
            p = eh_num(eh.val_buf[i], q->have);
            p = eh_cat(p, def_str_Slash);
            eh_num(p, q->need);
            eh.val[i]->set_text(eh.val[i], eh.val_buf[i], -1);
            eh.val[i]->set_palette(eh.val[i], q->have >= q->need ? EH_PAL_MET : EH_PAL_DIM);
        }
        if (e->met == e->count) {
            eh.status->set_text(eh.status, (u8 *)def_str_EvoReadyLine, -1);
            eh.status->set_palette(eh.status, EH_PAL_MET);
        } else {
            p = eh_num(eh.status_buf, e->count - e->met);
            p = eh_space(p);
            eh_cat(p, def_str_EvoMissing);
            eh.status->set_text(eh.status, eh.status_buf, -1);
            eh.status->set_palette(eh.status, EH_PAL_DIM);
        }
    }
    /* "page/pages" when the list has more than one */
    if (eh.n > EH_ROWS) {
        p = eh_num(eh.page_buf, first / EH_ROWS + 1);
        p = eh_cat(p, def_str_Slash);
        eh_num(p, (eh.n + EH_ROWS - 1) / EH_ROWS);
        eh.page->set_text(eh.page, eh.page_buf, -1);
    } else {
        eh_hide(eh.page);
    }
}

/* An opaque rectangle; the layer draws what is added last first, so frames go before fills. */
static void eh_rect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b) {
    GfxLayer *layer = gfx_module.funcs.get_layer(eh.layer);
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
static void eh_panel(s32 x, s32 y, s32 w, s32 h, s32 band) {
    if (band) {
        eh_rect(x + 2, y + 2, w - 4, EH_LINE + 2, 0, 0, 74);
    }
    eh_rect(x + 1, y + 1, w - 2, h - 2, 0, 0, 33);
    eh_rect(x, y, w, h, 107, 107, 214);
}

/* The windows belong to no object's data block, so nothing runs them but this (as the battle's toast does). */
static void eh_run_windows(void) {
    MessageWindow *all[] = { eh.title, eh.keys, eh.head, eh.status, eh.page };
    s32 i;

    for (i = 0; i < (s32)(sizeof(all) / sizeof(all[0])); i++) {
        heap_objects.try_run((Object *)all[i]);
    }
    for (i = 0; i < EH_ROWS; i++) {
        heap_objects.try_run((Object *)eh.list[i]);
        heap_objects.try_run((Object *)eh.count[i]);
    }
    for (i = 0; i < 3; i++) {
        heap_objects.try_run((Object *)eh.req[i]);
        heap_objects.try_run((Object *)eh.val[i]);
    }
}

static void eh_draw(void) {
    s32 i;

    /* the chosen requirement blocks' rules, then the panels */
    for (i = 1; i < 3; i++) {
        eh_rect(EH_INFO_X + 6, EH_ROW_Y + EH_LINE + 2 + i * 30 - 4, EH_INFO_W - 12, 1, 41, 41, 115);
    }
    eh_panel(EH_LIST_X, EH_TOP, EH_LIST_W, EH_BOTTOM - EH_TOP, 0);
    eh_panel(EH_INFO_X, EH_TOP, EH_INFO_W, EH_BOTTOM - EH_TOP, 1);
    eh_panel(EH_TITLE_X - 6, EH_TITLE_Y - 6, 320 - EH_TITLE_X + 2, 44, 0); /* "Next Forms" and the page */
    eh_panel(0, 216, 320, 22, 0);                                            /* the keys */
}

static void eh_open_windows(void) {
    s32 i;

    eh.title = eh_window(1, EH_TITLE_X, EH_TITLE_Y);
    eh.keys = eh_window(1, 10, 219);
    eh.keys->set_text(eh.keys, (u8 *)def_str_EvoKeys, -1);
    eh.page = eh_window(3, EH_TITLE_X, EH_TITLE_Y + 20);
    for (i = 0; i < EH_ROWS; i++) {
        eh.list[i] = eh_window(1, EH_LIST_X + 6, EH_ROW_Y + i * EH_LINE);
        eh.count[i] = eh_window(3, EH_LIST_X + EH_LIST_W - 28, EH_ROW_Y + i * EH_LINE + 3);
    }
    eh.head = eh_window(1, EH_INFO_X + 6, EH_TOP + 3);
    for (i = 0; i < 3; i++) {
        eh.req[i] = eh_window(1, EH_INFO_X + 6, EH_ROW_Y + EH_LINE + 2 + i * 30);
        eh.val[i] = eh_window(3, EH_INFO_X + EH_INFO_W - 44, EH_ROW_Y + EH_LINE + 2 + i * 30 + 15);
    }
    eh.status = eh_window(1, EH_INFO_X + 6, EH_BOTTOM - 18);
}

static void eh_close_windows(void) {
    MessageWindow **all[] = { &eh.title, &eh.keys, &eh.head, &eh.status, &eh.page };
    s32 i;

    for (i = 0; i < (s32)(sizeof(all) / sizeof(all[0])); i++) {
        heap_objects.stop((Object *)*all[i]);
        *all[i] = NULL;
    }
    for (i = 0; i < EH_ROWS; i++) {
        heap_objects.stop((Object *)eh.list[i]);
        heap_objects.stop((Object *)eh.count[i]);
    }
    for (i = 0; i < 3; i++) {
        heap_objects.stop((Object *)eh.req[i]);
        heap_objects.stop((Object *)eh.val[i]);
    }
}

/* The Digi-Lab asks whether Square may open the panel (the definitive edition, with hints on). */
int def_evohint_enabled(void) {
    return def_on() && DEF_EVO_HINT_DEPTH > 0;
}

/* "□ Next Forms" under the Digimon screen's entries, while they are shown (the screen's data block has no room for it). */
void def_evohint_label(s32 layer, s32 show) {
    if (!def_evohint_enabled()) {
        return;
    }
    if (show && eh.label == NULL) {
        eh.label = message_create_window(layer, 1, 0xA6, 0x9A);
        eh.label->set_ot_depth(eh.label, 5);
        eh.label->set_text(eh.label, (u8 *)def_str_EvoOpen, -1);
        eh.label->set_palette(eh.label, EH_PAL_DIM);
    } else if (!show && eh.label != NULL) {
        heap_objects.stop((Object *)eh.label);
        eh.label = NULL;
    }
}

void def_evohint_open(s32 layer, s32 slot) {
    eh.layer = layer;
    eh.slot = slot;
    eh.sel = 0;
    eh.open = 1;
    eh_collect(eh_member(slot));
    eh_open_windows();
    eh_fill();
}

/* One frame of the panel: its keys and its drawing. 1 when it has closed (Triangle). */
int def_evohint_frame(void) {
    s32 old = eh.sel;
    s32 slot;
    s32 k;

    if (!eh.open) {
        return 1;
    }
    if (PAD_PRESSED(0xE) || PAD_PRESSED(0xC)) { /* cancel, or Triangle: the lab's "Back" */
        sound_module.play(0x800450BD);
        eh_close_windows();
        eh.open = 0;
        return 1;
    }
    if (PAD_PRESSED(0xA) || PAD_PRESSED(0xB)) { /* L1 / R1: the previous / next party member with a Digimon */
        slot = eh.slot;
        for (k = 0; k < 3; k++) {
            slot = (slot + (PAD_PRESSED(0xB) ? 1 : 2)) % 3;
            if (eh_member(slot) >= 0) {
                break;
            }
        }
        if (slot != eh.slot) {
            sound_module.play(0x8004513E);
            eh.slot = slot;
            eh.sel = 0;
            eh_collect(eh_member(slot));
            eh_fill();
        }
    } else if (eh.n > 0 && (PAD_PRESSED(4) || PAD_REPEAT(4))) {
        eh.sel = eh.sel > 0 ? eh.sel - 1 : eh.n - 1;
    } else if (eh.n > 0 && (PAD_PRESSED(6) || PAD_REPEAT(6))) {
        eh.sel = eh.sel + 1 < eh.n ? eh.sel + 1 : 0;
    }
    if (old != eh.sel) {
        sound_module.play(0x8004513E);
        eh_fill();
    }
    eh_run_windows();
    eh_draw();
    return 0;
}

/* The party slot the panel shows (L1/R1 switch it): the lab's partner panel follows it. */
s32 def_evohint_slot(void) {
    return eh.slot;
}

/* Every frame of the Digimon screen: runs the "□ Next Forms" label while it is up. */
void def_evohint_tick(void) {
    if (eh.label != NULL && !eh.open) {
        heap_objects.try_run((Object *)eh.label);
    }
}
