#include <stdio.h>
#include "definitive.h"

static int fail_count;

static void check_eq(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", what, got, want);
        fail_count++;
    } else {
        printf("ok %s (%d)\n", what, got);
    }
}

int main(void) {
    unsigned int bits[DEF_EVO_SEEN_WORDS];
    int hold = 0;
    int i;

    def_evo_seen_reset(bits);
    check_eq("initial form 0 unseen", def_evo_seen_get(bits, 0), 0);
    check_eq("initial form 470 unseen", def_evo_seen_get(bits, 470), 0);
    check_eq("out of bounds negative", def_evo_seen_get(bits, -1), 0);
    check_eq("out of bounds max", def_evo_seen_get(bits, DEF_EVO_FORM_MAX), 0);

    def_evo_seen_set(bits, 35);
    def_evo_seen_set(bits, 470);
    def_evo_seen_set(bits, -1);
    def_evo_seen_set(bits, DEF_EVO_FORM_MAX);

    check_eq("form 35 seen", def_evo_seen_get(bits, 35), 1);
    check_eq("form 34 still unseen", def_evo_seen_get(bits, 34), 0);
    check_eq("form 36 still unseen", def_evo_seen_get(bits, 36), 0);
    check_eq("form 470 seen", def_evo_seen_get(bits, 470), 1);
    check_eq("form 469 still unseen", def_evo_seen_get(bits, 469), 0);

    def_evo_seen_reset(bits);
    check_eq("after reset form 470 unseen", def_evo_seen_get(bits, 470), 0);

    for (i = 1; i < DEF_EVO_SKIP_HOLD_FRAMES; i++) {
        check_eq("hold before threshold", def_evo_skip_update(&hold, 1), 0);
    }
    check_eq("hold at threshold triggers skip", def_evo_skip_update(&hold, 1), 1);
    check_eq("release clears skip hold", def_evo_skip_update(&hold, 0), 0);
    check_eq("re-press frame 1 does not skip immediately", def_evo_skip_update(&hold, 1), 0);

    return fail_count != 0;
}
