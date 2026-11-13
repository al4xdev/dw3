/* tests/port/definitive_economy.py's driver for port/game/definitive_economy.c (PRD 4 layers D and E): the prices and
 * the forge's recipes through stand-in tables, items, held materials and def_on. Prints "ok name" / "FAIL name" per
 * case; exits 1 on a failure. */
#include <stdio.h>

#include "common.h"
#include "records.h"

#include "definitive.h"

static int on = 1;
int def_on(void) {
    return on;
}
void port_log(const char *fmt, ...) {
    (void)fmt;
}

/* Items 1..3: 1 a cheap one, 2 a 60000 sword the price table changes, 3 one past the table's last ID. */
static RecordsItem items[3] = {
    { NULL, 400, 200, 0, 0, { 0, 0 } }, { NULL, 60000, 30000, 0, 0, { 0, 0 } }, { NULL, 100, 50, 0, 0, { 0, 0 } } };
RecordsItem *records_get_item(s32 i) {
    return i >= 1 && i <= 3 ? &items[i - 1] : NULL;
}
const DefItemPrice def_item_prices[] = { { 2, 90000, 9000 } };
const int def_item_price_count = 1;
/* Item 10: 30 of common 3; item 20: 40 of common 12 and 2 of rare 5. */
const DefRecipe def_recipes[] = { { 10, 3, 30, -1, 0 }, { 20, 12, 40, 5, 2 } };
const int def_recipe_count = 2;

static int mats[DEF_HUNT_MATERIALS], rares[DEF_HUNT_RARES];
int def_record_material(int m) {
    return mats[m];
}
void def_record_material_add(int m, int n) {
    mats[m] += n;
}
int def_record_rare(int r) {
    return rares[r];
}
void def_record_rare_add(int r, int n) {
    rares[r] += n;
}
const unsigned char *def_hunt_material_name(int m) {
    static const unsigned char name[2] = { 0x10, 0 };

    (void)m;
    return name;
}
void def_hunt_target_name(unsigned char *buf, int *p, int size, int rare) {
    static const unsigned char name[2] = { 0x11, 0 };

    (void)rare;
    def_glyph_put(buf, p, size, name);
}

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

int main(void) {
    int palette = -1;
    const unsigned char *label;

    /* Layer D */
    check("an item the price table has: its new prices, past the u16", def_item_buy(2) == 90000 && def_item_sell(2) == 9000);
    check("an item it does not have: the game's", def_item_buy(1) == 400 && def_item_sell(1) == 200 &&
                                                      def_item_buy(3) == 100 && def_item_sell(3) == 50);
    check("no item: 0", def_item_buy(0) == 0 && def_item_sell(99) == 0);

    /* Layer E */
    check("an item without a recipe: no limit, no label", def_recipe_max(1) == 99 && def_recipe_can_buy(1) &&
                                                             def_recipe_label(1, &palette) == NULL);
    mats[3] = 29;
    check("short of its common: none can be bought", def_recipe_max(10) == 0 && !def_recipe_can_buy(10));
    label = def_recipe_label(10, &palette);
    check("its label is there, orange while short", label != NULL && label[0] == 0x10 && palette == 3);
    mats[3] = 65;
    check("65 of 30 each: two can be bought, the label back to white", def_recipe_max(10) == 2 &&
                                                                          def_recipe_label(10, &palette) && palette == 0);
    def_recipe_pay(10, 2);
    check("buying two takes 60", mats[3] == 5);
    mats[12] = 90;
    rares[5] = 3;
    check("a top item: the scarcer of its two materials decides (90/40 = 2, 3/2 = 1)", def_recipe_max(20) == 1);
    def_recipe_pay(20, 1);
    check("buying it takes both", mats[12] == 50 && rares[5] == 1 && def_recipe_max(20) == 0);
    def_recipe_pay(1, 5);
    check("paying for an item without a recipe takes nothing", mats[12] == 50 && rares[5] == 1);

    on = 0;
    check("in a replay script the game's own prices and no recipe",
          def_item_buy(2) == 60000 && def_item_sell(2) == 30000 && def_recipe_max(20) == 99 &&
              def_recipe_label(20, &palette) == NULL);
    return failures != 0;
}
