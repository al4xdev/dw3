/* tests/port/definitive.py's driver for port/game/definitive_mask.c: closed cases of the weakness rule (PRD layer 3).
 * Prints "ok <name>" or "FAIL <name> got <mask> want <mask>" per case; exits 1 on a failure. */
#include <stdio.h>

#include "definitive.h"

static int failures;

static void mask_case(const char *name, const short r[7], int dark, int want) {
    int got = def_weakness_mask(r, dark);

    if (got != want) {
        failures++;
        printf("FAIL %s got 0x%02X want 0x%02X\n", name, got, want);
    } else {
        printf("ok %s\n", name);
    }
}

int main(void) {
    const short equal[7] = { 100, 100, 100, 100, 100, 100, 100 };
    const short one[7] = { 60, 100, 100, 100, 100, 100, 100 };
    const short two[7] = { 60, 60, 100, 100, 100, 100, 100 };
    const short three[7] = { 60, 70, 50, 100, 100, 100, 100 };
    const short ties[7] = { 60, 60, 60, 100, 100, 100, 100 };
    const short edge_in[7] = { 80, 100, 100, 100, 100, 100, 100 };
    const short edge_out[7] = { 81, 100, 100, 100, 100, 100, 100 };
    const short buffed[7] = { 120, 60, 100, 100, 100, 100, 100 }; /* `one` with Fire raised and Water lowered */
    const short crowd[7] = { 60, 60, 60, 60, 200, 200, 100 };      /* half the six low: the median is low, no gap */
    const short darkvalue[7] = { 100, 100, 100, 100, 100, 100, 10 };
    const short half_in[7] = { 80, 100, 100, 101, 101, 101, 100 };   /* median 100.5: 80 <= 80.5 */
    const short half_out[7] = { 81, 100, 100, 101, 101, 101, 100 };  /* 81 > 80.5 */
    const short strong[7] = { 300, 100, 100, 100, 100, 100, 100 };   /* a high resist is not a weakness, nor moves it */

    mask_case("all equal: none", equal, 0, 0);
    mask_case("one low value: that element", one, 0, 1 << 0);
    mask_case("two low values: both", two, 0, (1 << 0) | (1 << 1));
    mask_case("three candidates: the two lowest values", three, 0, (1 << 2) | (1 << 0));
    mask_case("a tie for the last place: the element order", ties, 0, (1 << 0) | (1 << 1));
    mask_case("exactly the gap below the median: weak", edge_in, 0, 1 << 0);
    mask_case("one point short of the gap: not weak", edge_out, 0, 0);
    mask_case("a buff and a debuff move the weakness", buffed, 0, 1 << 1);
    mask_case("half the six low: no median gap, none", crowd, 0, 0);
    mask_case("Dark's own value is ignored by the median", darkvalue, 0, 0);
    mask_case("the median keeps its half (weak)", half_in, 0, 1 << 0);
    mask_case("the median keeps its half (not weak)", half_out, 0, 0);
    mask_case("a strong resist changes nothing else", strong, 0, 0);
    mask_case("Dark marked, all equal", equal, 1, 1 << 6);
    mask_case("Dark marked, one weakness: on top", one, 1, (1 << 0) | (1 << 6));
    mask_case("Dark marked, two weaknesses: three in all", two, 1, (1 << 0) | (1 << 1) | (1 << 6));
    mask_case("Dark not marked", one, 0, 1 << 0);
    if (def_resisted(249) != 0 || def_resisted(250) != 1 || def_resisted(999) != 1 || def_resisted(-5) != 0) {
        failures++;
        printf("FAIL def_resisted\n");
    } else {
        printf("ok def_resisted\n");
    }
    return failures ? 1 : 0;
}
