/* tests/port/definitive_tp.py's driver for port/game/definitive_progression.c (PRD 2, layer D). */
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
    int lost = -1;

    check("tp_gain(1)", def_tp_gain(1), 0);
    check("tp_gain(2)", def_tp_gain(2), 7);
    check("tp_gain(20)", def_tp_gain(20), 7);
    check("tp_gain(21)", def_tp_gain(21), 9);
    check("tp_gain(40)", def_tp_gain(40), 9);
    check("tp_gain(41)", def_tp_gain(41), 30);
    check("tp_gain(50)", def_tp_gain(50), 30);
    check("tp_gain(51)", def_tp_gain(51), 0);
    check("tp_gain(99)", def_tp_gain(99), 0);

    check("tp_total(1)", def_tp_total(1), 0);
    check("tp_total(20)", def_tp_total(20), 133);
    check("tp_total(40)", def_tp_total(40), 313);
    check("tp_total(50)", def_tp_total(50), 613);
    check("tp_total(99)", def_tp_total(99), 613);

    check("tp_add(90, 20)", def_tp_add(90, 20, &lost), 97);
    check("tp_add(90, 20) lost", lost, 0);
    check("tp_add(95, 20)", def_tp_add(95, 20, &lost), 99);
    check("tp_add(95, 20) lost", lost, 3);
    check("tp_add(80, 41)", def_tp_add(80, 41, &lost), 99);
    check("tp_add(80, 41) lost", lost, 11);
    check("tp_add(99, 51)", def_tp_add(99, 51, &lost), 99);
    check("tp_add(99, 51) lost", lost, 0);

    return failures ? 1 : 0;
}
