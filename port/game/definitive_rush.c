/* PRD 4 layer I, the Overlevel Rush's results on the field: what the panel lists and how long it stays up. Pure (no
 * game state: tests/port/definitive_rush.py drives it); definitive_rush_panel.c draws it, and the rewards themselves are
 * STFGTREP's own routines (def_rush_apply_quiet, src/stfgtrep/stfgtrep_80082E70.c), applied before the panel opens.
 *
 * The rows: each party member's experience (with LV UP! when it went up), under it NEW TECH! when the report would have
 * announced a technique or a form and TP FULL! when a level's TP did not fit under the ceiling; then the Bits, the item
 * and the Hunt Materials. Rows past the caller's maximum are given but not listed: the panel is a summary, the detail is
 * the status menu's. Text is glyph bytes (definitive_glyph.c): the names are the game's (the member's record, the item
 * names' file), composed at run time; the words are ours (tools/text_override/en.txt). */
#include <stddef.h>

#include "definitive.h"

extern const unsigned char def_str_RushPlus[], def_str_RushExp[], def_str_RushLvUp[], def_str_RushNewTech[],
    def_str_RushTpFull[], def_str_RushBits[], def_str_RushTimes[], def_str_Unknown[];

/* message_code_lengths (src/main/message.c): the bytes of control code `02 <code> ...`, by code. */
static const unsigned char code_lengths[10] = { 1, 2, 3, 2, 5, 3, 3, 2, 3, 2 };

int def_rush_rows(const DefRushSummary *s, DefRushRow *rows, int max) {
    int n = 0, i;

#define RUSH_ROW(k, ix)                                                                                                \
    do {                                                                                                               \
        if (n < max) {                                                                                                 \
            rows[n].kind = (signed char)(k);                                                                           \
            rows[n].index = (signed char)(ix);                                                                         \
            n++;                                                                                                       \
        }                                                                                                              \
    } while (0)
    for (i = 0; i < 3; i++) {
        if (s->members[i].digimon < 0) {
            continue;
        }
        RUSH_ROW(DEF_RUSH_ROW_MEMBER, i);
        if (s->members[i].learnt != 0) {
            RUSH_ROW(DEF_RUSH_ROW_TECH, i);
        }
        if (s->members[i].tp_full != 0) {
            RUSH_ROW(DEF_RUSH_ROW_TP, i);
        }
    }
    if (s->bits > 0) {
        RUSH_ROW(DEF_RUSH_ROW_BITS, 0);
    }
    if (s->item != 0) {
        RUSH_ROW(DEF_RUSH_ROW_ITEM, 0);
    }
    for (i = 0; i < s->materials && i < DEF_HUNT_LINES; i++) {
        RUSH_ROW(DEF_RUSH_ROW_MATERIAL, i);
    }
#undef RUSH_ROW
    return n;
}

void def_rush_material_text(unsigned char *buf, int *p, int size, const unsigned char *line) {
    const unsigned char *s = line;
    int i, len;

    /* the material's part: what follows the line's first break ("Hunt material:<1>Bit Shard x2<2,2>") */
    for (i = 0; line[i] != 0;) {
        if (line[i] == 2 && line[i + 1] == 1) {
            s = line + i + 2;
            break;
        }
        len = line[i] == 1 ? 2 : line[i] == 2 ? (line[i + 1] < 10 ? code_lengths[line[i + 1]] : 2) : 1;
        for (; len > 0 && line[i] != 0; len--) {
            i++;
        }
    }
    /* without its control codes: a window would wait for a press at its <2,2> */
    while (*s != 0 && *p < size - 1) {
        if (*s == 2) {
            len = s[1] < 10 && code_lengths[s[1]] >= 2 ? code_lengths[s[1]] : 2;
            for (; len > 0 && *s != 0; len--) {
                s++;
            }
            continue;
        }
        if (*s == 1) { /* an extra glyph is two bytes: both or neither */
            if (s[1] == 0 || *p >= size - 2) {
                break;
            }
            buf[(*p)++] = *s++;
        }
        buf[(*p)++] = *s++;
    }
    buf[*p] = 0;
}

void def_rush_row_text(const DefRushSummary *s, DefRushRow row, const unsigned char *item_name, unsigned char *left,
                       unsigned char *right, int size) {
    int l = 0, r = 0;

    left[0] = right[0] = 0;
    switch (row.kind) {
    case DEF_RUSH_ROW_MEMBER: {
        const DefRushMember *m = &s->members[row.index];

        def_glyph_put(left, &l, size, m->name);
        def_glyph_put(right, &r, size, def_str_RushPlus);
        def_glyph_number(right, &r, size, m->exp);
        def_glyph_put(right, &r, size, def_str_RushExp);
        if (m->levels > 0) {
            def_glyph_put(right, &r, size, def_str_RushLvUp);
        }
        break;
    }
    case DEF_RUSH_ROW_TECH:
        def_glyph_put(right, &r, size, def_str_RushNewTech);
        break;
    case DEF_RUSH_ROW_TP:
        def_glyph_put(right, &r, size, def_str_RushTpFull);
        break;
    case DEF_RUSH_ROW_BITS:
        def_glyph_put(left, &l, size, def_str_RushPlus);
        def_glyph_number(left, &l, size, s->bits);
        def_glyph_put(left, &l, size, def_str_RushBits);
        break;
    case DEF_RUSH_ROW_ITEM:
        /* the item names' file may still be on its way (the field does not keep it): "???" until it is in */
        def_glyph_put(left, &l, size, def_str_RushPlus);
        def_glyph_put(left, &l, size, item_name != NULL ? item_name : def_str_Unknown);
        if (s->item_count > 1) {
            def_glyph_put(left, &l, size, def_str_RushTimes);
            def_glyph_number(left, &l, size, s->item_count);
        }
        break;
    case DEF_RUSH_ROW_MATERIAL:
        def_glyph_put(left, &l, size, def_str_RushPlus);
        def_rush_material_text(left, &l, size, s->material[row.index]);
        break;
    default:
        break;
    }
}

int def_rush_clock_open(DefRushClock *c) {
    int replaced = c->left > 0;

    c->left = DEF_RUSH_FLOAT_FRAMES;
    c->opened++;
    return replaced;
}

int def_rush_clock_tick(DefRushClock *c, int ticks, int held) {
    if (c->left <= 0) {
        return 0;
    }
    c->left = held ? 0 : c->left > ticks ? c->left - ticks : 0;
    return c->left > 0;
}
