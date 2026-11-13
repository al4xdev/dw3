/* tests/port/definitive_variants.py's driver for port/game/definitive_variants.c with the generated table
 * (definitive_equipment.c): stand-in items, loaded from stdin as src/main/records.c has them (the Python side parses
 * them), one per line:
 *   id category type W charisma slot group members value0 value1 power stat0 stat1 accuracy chance power2 strong
 *   id category type A charisma slot group members value0 value1 stat0 stat1 guard evasion
 * Prints every item in the same shape after each pass ("pass <name>" first): a replay script (def_on 0), the run's
 * first hook and a second one; and "text <file> <id> <hidden>" for every item and both item text files (and one that
 * is not an item text), in a replay script and not. With argument "mismatch" every item is loaded as the other
 * category (a table and a records.c drifted apart) and the one pass printed is a hook with def_on: nothing may change. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "records.h"

#include "definitive.h"

#define MAX_ITEMS 64

static int on;
int def_on(void) {
    return on;
}
void port_log(const char *fmt, ...) {
    (void)fmt;
}

static RecordsItem items[MAX_ITEMS];
static RecordsWeapon weapons[MAX_ITEMS];
static RecordsArmor armours[MAX_ITEMS];
static int ids[MAX_ITEMS], count;

RecordsItem *records_get_item(s32 i) {
    int k;

    for (k = 0; k < count; k++) {
        if (ids[k] == i) {
            return &items[k];
        }
    }
    return NULL;
}

static void dump(const char *pass) {
    int k;

    printf("pass %s\n", pass);
    for (k = 0; k < count; k++) {
        RecordsItem *it = &items[k];

        if (it->data == (void *)&weapons[k]) {
            RecordsWeapon *w = &weapons[k];
            printf("%d %d %d W %d %d %d %d %d %d %d %d %d %d %d %d %d\n", ids[k], it->category, it->type, w->charisma,
                   w->slot, w->group, w->members, w->bonus_values[0], w->bonus_values[1], w->power, w->bonus_stats[0],
                   w->bonus_stats[1], w->accuracy, w->status_chance, w->status_power, w->strong_type);
        } else {
            RecordsArmor *a = &armours[k];
            printf("%d %d %d A %d %d %d %d %d %d %d %d %d %d\n", ids[k], it->category, it->type, a->charisma, a->slot,
                   a->group, a->members, a->bonus_values[0], a->bonus_values[1], a->bonus_stats[0], a->bonus_stats[1],
                   a->guard, a->evasion);
        }
    }
}

static void texts(const char *pass) {
    static const int files[3] = { DEF_TEXT_ITMNAM, DEF_TEXT_ITMINF, 0xA2 }; /* the last: SKLNAM, not an item text */
    int k, f;

    printf("pass %s\n", pass);
    for (k = 0; k < count; k++) {
        for (f = 0; f < 3; f++) {
            printf("text %d %d %d\n", files[f], ids[k], def_variant_text_hidden(files[f], ids[k]));
        }
    }
}

int main(int argc, char **argv) {
    char kind[2];
    int v[13];
    int mismatch = argc > 1 && strcmp(argv[1], "mismatch") == 0;

    while (count < MAX_ITEMS && scanf("%d %d %d %1s", &ids[count], &v[0], &v[1], kind) == 4) {
        RecordsItem *it = &items[count];

        it->category = v[0];
        it->type = v[1];
        if (kind[0] == 'W') {
            RecordsWeapon *w = &weapons[count];
            if (scanf("%d %d %d %d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                      &v[8], &v[9], &v[10], &v[11], &v[12]) != 13) {
                return 2;
            }
            w->charisma = v[0];
            w->slot = v[1];
            w->group = v[2];
            w->members = v[3];
            w->bonus_values[0] = v[4];
            w->bonus_values[1] = v[5];
            w->power = v[6];
            w->bonus_stats[0] = v[7];
            w->bonus_stats[1] = v[8];
            w->accuracy = v[9];
            w->status_chance = v[10];
            w->status_power = v[11];
            w->strong_type = v[12];
            it->data = w;
        } else {
            RecordsArmor *a = &armours[count];
            if (scanf("%d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                      &v[9]) != 10) {
                return 2;
            }
            a->charisma = v[0];
            a->slot = v[1];
            a->group = v[2];
            a->members = v[3];
            a->bonus_values[0] = v[4];
            a->bonus_values[1] = v[5];
            a->bonus_stats[0] = v[6];
            a->bonus_stats[1] = v[7];
            a->guard = v[8];
            a->evasion = v[9];
            it->data = a;
        }
        /* A table and a records.c drifted apart: every item the other category the row says (its data untouched) */
        if (mismatch) {
            it->category = it->category == RECORDS_ITEM_WEAPON ? RECORDS_ITEM_ARMOR : RECORDS_ITEM_WEAPON;
        }
        count++;
    }
    if (count == 0) {
        return 2;
    }
    if (mismatch) {
        on = 1;
        def_variants_hook();
        dump("mismatch");
        return 0;
    }
    on = 0;
    def_variants_hook();
    dump("replay");
    texts("text-replay");
    on = 1;
    def_variants_hook();
    dump("first");
    def_variants_hook();
    dump("second");
    texts("text-on");
    return 0;
}
