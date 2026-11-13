/* tests/port/definitive_economy.py's driver for port/game/definitive_economy.c (PRD 4 layers D on): the prices through
 * the generated table, with stand-in items and def_on. Prints "ok name" / "FAIL name" per case; exits 1 on a failure. */
#include <stdio.h>

#include "common.h"
#include "records.h"

#include "definitive.h"

static int on = 1;
int def_on(void) {
    return on;
}

/* Items 1..3: 1 a cheap one, 2 a 60000 sword the table changes, 3 one past the table's last ID. */
static RecordsItem items[3] = {
    { NULL, 400, 200, 0, 0, { 0, 0 } }, { NULL, 60000, 30000, 0, 0, { 0, 0 } }, { NULL, 100, 50, 0, 0, { 0, 0 } } };
RecordsItem *records_get_item(s32 i) {
    return i >= 1 && i <= 3 ? &items[i - 1] : NULL;
}
const DefItemPrice def_item_prices[] = { { 2, 90000, 9000 } };
const int def_item_price_count = 1;

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

int main(void) {
    check("an item the table has: its new prices, past the u16", def_item_buy(2) == 90000 && def_item_sell(2) == 9000);
    check("an item it does not have: the game's", def_item_buy(1) == 400 && def_item_sell(1) == 200 &&
                                                      def_item_buy(3) == 100 && def_item_sell(3) == 50);
    check("no item: 0", def_item_buy(0) == 0 && def_item_sell(99) == 0);
    on = 0;
    check("in a replay script the game's own prices", def_item_buy(2) == 60000 && def_item_sell(2) == 30000);
    return failures != 0;
}
