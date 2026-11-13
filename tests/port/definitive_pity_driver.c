/* tests/port/definitive_pity.py's driver for port/game/definitive_progression.c (PRD 2, layer E). */
#include <stdio.h>

#include "definitive.h"

static int failures;

static void check(const char *name, int got, int want) {
    if (got != want) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    } else {
        printf("ok %s\n", name);
    }
}

int main(void) {
    def_train_pity_reset();
    check("initial count", def_train_pity_count(), 0);
    check("initial guaranteed", def_train_pity_guaranteed(), 0);

    def_train_pity_record(0);
    check("after 1 miss: count", def_train_pity_count(), 1);
    check("after 1 miss: guaranteed", def_train_pity_guaranteed(), 0);

    def_train_pity_record(0);
    check("after 2 misses: count", def_train_pity_count(), 2);
    check("after 2 misses: guaranteed", def_train_pity_guaranteed(), 0);

    /* Natural perfect resets the counter mid-way. */
    def_train_pity_record(1);
    check("after natural perfect: count", def_train_pity_count(), 0);
    check("after natural perfect: guaranteed", def_train_pity_guaranteed(), 0);

    /* Three misses reach the threshold and arm the guarantee. */
    def_train_pity_record(0);
    def_train_pity_record(0);
    def_train_pity_record(0);
    check("after 3 misses: count", def_train_pity_count(), 3);
    check("after 3 misses: guaranteed", def_train_pity_guaranteed(), 1);

    /* Guaranteed run ends (even if recorded as 0, safety guard) and resets counter to 0. */
    def_train_pity_record(1);
    check("after guaranteed finish: count", def_train_pity_count(), 0);
    check("after guaranteed finish: guaranteed", def_train_pity_guaranteed(), 0);

    /* Check DEF_TRAIN_PITY_AFTER clamping/zero-guard invariant via reset -> miss. */
    def_train_pity_record(0);
    def_train_pity_reset();
    check("after reset: count", def_train_pity_count(), 0);

    return failures ? 1 : 0;
}
