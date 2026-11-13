/* tests/port/definitive_rush.py's driver for port/game/definitive_rush.c (PRD 4 layer I): the floating Rush panel's
 * rows from a summary (order, absent members, nothing listed for no Bits / no item, the cap), each row's texts (the
 * numbers' digits, LV UP!, the item's count and its "???" before the names are in, a results screen's material line made
 * one panel line), and the clock (open, a second Rush replacing the panel still up, time running out, the field holding
 * the player). The words are stand-ins with bytes of their own, so the texts are checked byte for byte. Prints
 * "ok name" / "FAIL name" per case; exits 1 on a failure. */
#include <stdio.h>
#include <string.h>

#include "definitive.h"

/* The words' stand-ins (tools/text_override/en.txt has the real ones). Digits are glyph 4 + d; 01 01 a space. */
const unsigned char def_str_RushPlus[] = { 0x50, 0 };
const unsigned char def_str_RushExp[] = { 0x01, 0x01, 0x60, 0x61, 0x62, 0 };
const unsigned char def_str_RushLvUp[] = { 0x01, 0x01, 0x01, 0x01, 0x70, 0x71, 0 };
const unsigned char def_str_RushNewTech[] = { 0x72, 0x73, 0 };
const unsigned char def_str_RushTpFull[] = { 0x74, 0x75, 0 };
const unsigned char def_str_RushBits[] = { 0x01, 0x01, 0x76, 0 };
const unsigned char def_str_RushTimes[] = { 0x01, 0x01, 0x77, 0 };
const unsigned char def_str_Unknown[] = { 0x01, 0x09, 0x01, 0x09, 0x01, 0x09, 0 };
const unsigned char def_str_HuntRomanI[] = { 0x30, 0 }, def_str_HuntRomanV[] = { 0x31, 0 }, def_str_HuntRomanX[] = { 0x32, 0 };

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

static int same(const unsigned char *a, const unsigned char *b) {
    return strcmp((const char *)a, (const char *)b) == 0;
}

static const unsigned char agumon[] = { 0x20, 0x21, 0x22, 0 }, gabumon[] = { 0x23, 0x24, 0 }, patamon[] = { 0x25, 0 };

/* Three members: the first went up a level, learnt a form and lost TP to the ceiling; 240 Bits, item 91 x15, two
 * material lines. */
static void full(DefRushSummary *s) {
    static const unsigned char line0[] = { 0x40, 0x41, 0x02, 0x01, 0x42, 0x43, 0x01, 0x01, 0x44, 0x06, 0x02, 0x02, 0x02, 0 };
    static const unsigned char line1[] = { 0x40, 0x02, 0x01, 0x45, 0x02, 0x02, 0x02, 0 };
    int i;

    memset(s, 0, sizeof(*s));
    s->members[0].digimon = 0;
    s->members[0].exp = 120;
    s->members[0].levels = 1;
    s->members[0].learnt = 1;
    s->members[0].tp_full = 1;
    s->members[0].name = agumon;
    s->members[1].digimon = 3;
    s->members[1].exp = 60;
    s->members[1].name = gabumon;
    s->members[2].digimon = 5;
    s->members[2].exp = 0;
    s->members[2].name = patamon;
    s->bits = 240;
    s->item = 91;
    s->item_count = 15;
    s->materials = 2;
    for (i = 0; line0[i] != 0; i++) {
        s->material[0][i] = line0[i];
    }
    for (i = 0; line1[i] != 0; i++) {
        s->material[1][i] = line1[i];
    }
}

static int kinds(const DefRushRow *rows, int n, const int *want, int m) {
    int i;

    if (n != m) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (rows[i].kind != want[2 * i] || rows[i].index != want[2 * i + 1]) {
            return 0;
        }
    }
    return 1;
}

static void rows(void) {
    DefRushSummary s;
    DefRushRow r[16];
    static const int all[] = { DEF_RUSH_ROW_MEMBER, 0, DEF_RUSH_ROW_TECH, 0, DEF_RUSH_ROW_TP, 0, DEF_RUSH_ROW_MEMBER, 1,
                               DEF_RUSH_ROW_MEMBER, 2, DEF_RUSH_ROW_BITS, 0, DEF_RUSH_ROW_ITEM, 0,
                               DEF_RUSH_ROW_MATERIAL, 0, DEF_RUSH_ROW_MATERIAL, 1 };
    static const int two[] = { DEF_RUSH_ROW_MEMBER, 1, DEF_RUSH_ROW_BITS, 0 };
    int n;

    full(&s);
    n = def_rush_rows(&s, r, DEF_RUSH_ROWS);
    check("rows: each member (its NEW TECH! and TP FULL! under it), the Bits, the item, the materials",
          kinds(r, n, all, 9));
    n = def_rush_rows(&s, r, 4);
    check("rows: at most the cap, the first ones kept", kinds(r, n, all, 4));
    s.members[0].digimon = -1;
    s.members[2].digimon = -1;
    s.item = 0;
    s.materials = 0;
    n = def_rush_rows(&s, r, DEF_RUSH_ROWS);
    check("rows: an empty party slot and no item list nothing", kinds(r, n, two, 2));
    s.bits = 0;
    n = def_rush_rows(&s, r, DEF_RUSH_ROWS);
    check("rows: no Bits row for 0 Bits", n == 1 && r[0].kind == DEF_RUSH_ROW_MEMBER);
    full(&s);
    s.materials = 9;
    n = def_rush_rows(&s, r, 16);
    check("rows: no more material lines than a screen has (DEF_HUNT_LINES)", n == 7 + DEF_HUNT_LINES);
}

static void texts(void) {
    DefRushSummary s;
    DefRushRow row;
    unsigned char l[96], r[96];
    static const unsigned char exp_up[] = { 0x50, 0x05, 0x06, 0x04, 0x01, 0x01, 0x60, 0x61, 0x62,
                                            0x01, 0x01, 0x01, 0x01, 0x70, 0x71, 0 };
    static const unsigned char exp60[] = { 0x50, 0x0A, 0x04, 0x01, 0x01, 0x60, 0x61, 0x62, 0 };
    static const unsigned char exp0[] = { 0x50, 0x04, 0x01, 0x01, 0x60, 0x61, 0x62, 0 };
    static const unsigned char bits[] = { 0x50, 0x06, 0x08, 0x04, 0x01, 0x01, 0x76, 0 };
    static const unsigned char item_name[] = { 0x55, 0x56, 0 };
    static const unsigned char item15[] = { 0x50, 0x55, 0x56, 0x01, 0x01, 0x77, 0x05, 0x09, 0 };
    static const unsigned char item1[] = { 0x50, 0x55, 0x56, 0 };
    static const unsigned char unknown[] = { 0x50, 0x01, 0x09, 0x01, 0x09, 0x01, 0x09, 0x01, 0x01, 0x77, 0x05, 0x09, 0 };
    static const unsigned char mat0[] = { 0x50, 0x42, 0x43, 0x01, 0x01, 0x44, 0x06, 0 };
    static const unsigned char mat1[] = { 0x50, 0x45, 0 };

    full(&s);
    row.kind = DEF_RUSH_ROW_MEMBER;
    row.index = 0;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("member: its name on the left, +120 EXP  LV UP! on the right", same(l, agumon) && same(r, exp_up));
    row.index = 1;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("member: +60 EXP without a level", same(l, gabumon) && same(r, exp60));
    row.index = 2;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("member: +0 EXP (knocked out in the last battle: no share)", same(l, patamon) && same(r, exp0));
    row.kind = DEF_RUSH_ROW_TECH;
    row.index = 0;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("NEW TECH!: on the right only", l[0] == 0 && same(r, def_str_RushNewTech));
    row.kind = DEF_RUSH_ROW_TP;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("TP FULL!: on the right only", l[0] == 0 && same(r, def_str_RushTpFull));
    row.kind = DEF_RUSH_ROW_BITS;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("Bits: +240 Bits", same(l, bits) && r[0] == 0);
    row.kind = DEF_RUSH_ROW_ITEM;
    def_rush_row_text(&s, row, item_name, l, r, sizeof(l));
    check("item: +<name> x15 (a Booster Pack drop)", same(l, item15) && r[0] == 0);
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("item: ??? while the item names are not in", same(l, unknown));
    s.item_count = 1;
    def_rush_row_text(&s, row, item_name, l, r, sizeof(l));
    check("item: one, no count", same(l, item1));
    row.kind = DEF_RUSH_ROW_MATERIAL;
    row.index = 0;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("material: the line after its break, no wait code (Bit Shard x2)", same(l, mat0) && r[0] == 0);
    row.index = 1;
    def_rush_row_text(&s, row, NULL, l, r, sizeof(l));
    check("material: a second line", same(l, mat1));
    row.kind = DEF_RUSH_ROW_MEMBER;
    row.index = 0;
    memset(l, 0xEE, sizeof(l));
    memset(r, 0xEE, sizeof(r));
    def_rush_row_text(&s, row, NULL, l, r, 5);
    {
        static const unsigned char cut[] = { 0x50, 0x05, 0x06, 0x04, 0 };

        check("a short buffer: cut, 0-ended, nothing past it", l[3] == 0 && same(r, cut) && l[5] == 0xEE && r[5] == 0xEE);
    }
}

static void material_lines(void) {
    unsigned char out[16];
    int p;
    static const unsigned char no_break[] = { 0x40, 0x02, 0x05, 0x01, 0x41, 0x01, 0x07, 0x02, 0x02, 0x02, 0 };
    static const unsigned char want[] = { 0x40, 0x41, 0x01, 0x07, 0 };
    static const unsigned char ext_last[] = { 0x02, 0x01, 0x41, 0x42, 0x01, 0x07, 0 };

    p = 0;
    def_rush_material_text(out, &p, sizeof(out), no_break);
    check("material text: no break: the whole line, its codes dropped (an extra glyph kept)", same(out, want) && p == 4);
    p = 0;
    def_rush_material_text(out, &p, 4, ext_last);
    check("material text: an extra glyph that does not fit is left out whole", p == 2 && out[0] == 0x41 && out[1] == 0x42
                                                                                && out[2] == 0);
}

static void numbers(void) {
    unsigned char out[12];
    int p;
    static const unsigned char big[] = { 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0 };

    p = 0;
    def_glyph_number(out, &p, sizeof(out), 0);
    check("number: 0", p == 1 && out[0] == 4 && out[1] == 0);
    p = 0;
    def_glyph_number(out, &p, sizeof(out), 9999999);
    check("number: the wallet's ceiling, every digit", same(out, big) && p == 7);
    p = 0;
    def_glyph_number(out, &p, sizeof(out), -5);
    check("number: below 0 is 0", p == 1 && out[0] == 4);
    p = 9;
    out[9] = 0x33;
    def_glyph_number(out, &p, sizeof(out), 1234);
    check("number: without the room for all its digits, none", p == 9 && out[9] == 0);
}

static void clock(void) {
    DefRushClock c = { 0, 0 };
    int r, up;

    r = def_rush_clock_open(&c);
    check("clock: opens for DEF_RUSH_FLOAT_FRAMES", r == 0 && c.left == DEF_RUSH_FLOAT_FRAMES && c.opened == 1);
    up = def_rush_clock_tick(&c, 20, 0);
    check("clock: counts the field's ticks", up && c.left == DEF_RUSH_FLOAT_FRAMES - 20);
    r = def_rush_clock_open(&c);
    check("clock: a second Rush with the panel up replaces it (full time again, not stacked)",
          r == 1 && c.left == DEF_RUSH_FLOAT_FRAMES && c.opened == 2);
    up = def_rush_clock_tick(&c, DEF_RUSH_FLOAT_FRAMES - 1, 0);
    check("clock: up to its last tick", up && c.left == 1);
    up = def_rush_clock_tick(&c, 2, 0);
    check("clock: then closed", !up && c.left == 0);
    up = def_rush_clock_tick(&c, 1, 0);
    check("clock: stays closed", !up && c.left == 0);
    r = def_rush_clock_open(&c);
    check("clock: a Rush after it closed replaces nothing", r == 0 && c.opened == 3);
    up = def_rush_clock_tick(&c, 1, 1);
    check("clock: the field holding the player (menu, event, battle, map's end) closes it at once", !up && c.left == 0);
}

int main(void) {
    rows();
    texts();
    material_lines();
    numbers();
    clock();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
