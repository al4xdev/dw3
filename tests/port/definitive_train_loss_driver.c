#include <stdio.h>
#include "definitive.h"

static int failures = 0;

static void check(const char *label, int got, int want) {
    if (got != want) {
        printf("FAIL %s: got %d, want %d\n", label, got, want);
        failures++;
    } else {
        printf("ok   %s: %d\n", label, got);
    }
}

int main(void) {
    check("DEF_TRAIN_NO_STAT_LOSS enabled by default", def_train_no_stat_loss_on(), EXPECTED_ON);
    return failures ? 1 : 0;
}
