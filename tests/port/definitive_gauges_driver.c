/* tests/port/definitive_gauges.py's driver for port/game/definitive_progression.c (PRD 2, layer F). */
#include <stdio.h>
#include <string.h>

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
    short src[DEF_GAUGE_SLOTS] = { 0, 1, 250, 500, 999, 1000, -10, 1500 };
    short dst[DEF_GAUGE_SLOTS];
    unsigned char buf[DEF_GAUGE_SLOTS * 2];
    unsigned char old_save_zeros[DEF_GAUGE_SLOTS * 2];
    unsigned char corrupt[DEF_GAUGE_SLOTS * 2];
    int i;

    check("clamp 0", def_gauge_clamp(0), 0);
    check("clamp 1000", def_gauge_clamp(1000), 1000);
    check("clamp -1", def_gauge_clamp(-1), 0);
    check("clamp 1001", def_gauge_clamp(1001), 0);

    def_gauges_encode(src, buf);
    def_gauges_decode(buf, dst);
    check("roundtrip slot 0", dst[0], 0);
    check("roundtrip slot 1", dst[1], 1);
    check("roundtrip slot 2", dst[2], 250);
    check("roundtrip slot 3", dst[3], 500);
    check("roundtrip slot 4", dst[4], 999);
    check("roundtrip slot 5", dst[5], 1000);
    check("out of range <0 on encode -> 0", dst[6], 0);
    check("out of range >1000 on encode -> 0", dst[7], 0);

    memset(old_save_zeros, 0, sizeof(old_save_zeros));
    def_gauges_decode(old_save_zeros, dst);
    for (i = 0; i < DEF_GAUGE_SLOTS; i++) {
        check("old zero save loads 0", dst[i], 0);
    }

    /* Raw corrupt/PS1-garbage values < 0 or > 1000 on card become 0; valid 333 survives. */
    memset(corrupt, 0, sizeof(corrupt));
    corrupt[0] = 0xE9; corrupt[1] = 0x03; /* 1001 -> 0 */
    corrupt[2] = 0xFF; corrupt[3] = 0xFF; /* -1 -> 0 */
    corrupt[4] = 0x4D; corrupt[5] = 0x01; /* 333 -> 333 */
    def_gauges_decode(corrupt, dst);
    check("corrupt 1001 -> 0", dst[0], 0);
    check("corrupt -1 -> 0", dst[1], 0);
    check("valid 333 -> 333", dst[2], 333);

    return failures ? 1 : 0;
}
