/* Definitive combat, layer 3: which elements an enemy is weak to and which it resists, from its resists as they stand
 * right now. Pure (no game headers): tests/port/definitive.py compiles it alone and checks it case by case. */
#include "definitive.h"

/* The six non-Dark elements (resists[0..5]) sorted by value, ties by the fixed element order; `order` gets the indexes. */
static void sort_six(const short resists[7], int order[6]) {
    int i, j;

    for (i = 0; i < 6; i++) {
        order[i] = i;
    }
    for (i = 1; i < 6; i++) { /* insertion sort, stable: equal values keep the element order */
        int cur = order[i];

        for (j = i - 1; j >= 0 && resists[order[j]] > resists[cur]; j--) {
            order[j + 1] = order[j];
        }
        order[j + 1] = cur;
    }
}

int def_weakness_mask(const short resists[7], int dark_weak) {
    int order[6];
    int mask = 0;
    int taken = 0;
    int i;
    int median2; /* the median doubled: the mean of the 3rd and 4th values, kept in halves */

    sort_six(resists, order);
    median2 = resists[order[2]] + resists[order[3]];
    for (i = 0; i < 6 && taken < DEF_WEAK_COUNT; i++) {
        /* value <= median - gap, in halves: 2 * value <= median2 - 2 * gap */
        if (2 * resists[order[i]] <= median2 - 2 * DEF_WEAK_GAP) {
            mask |= 1 << order[i];
            taken++;
        }
    }
    if (dark_weak) {
        mask |= 1 << 6; /* an extra weakness, outside the count */
    }
    return mask;
}

int def_resisted(int resist) {
    return resist >= DEF_RESIST_MIN;
}
