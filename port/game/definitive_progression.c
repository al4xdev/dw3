/* Definitive edition, PRD 2 (.plan/tasks/definitive-progression-prd.md): progression and comfort. Pure where it can be
 * (tests/port/definitive.py compiles the rules), the game's hooks call in under #ifdef PC_PORT. */
#include "definitive.h"

/* ---- Layer D: TP by the band of the level reached (the original gives 5 a level, to 99). After 50 only the Train Chip
 * gives TP. The ceiling stays DEF_TP_CAP on purpose: TP cannot be hoarded for the last gym. */
int def_tp_gain(int level) {
    if (level <= 1) {
        return 0;
    }
    if (level <= 20) {
        return DEF_TP_GAIN_LV_2_20;
    }
    if (level <= 40) {
        return DEF_TP_GAIN_LV_21_40;
    }
    if (level <= 50) {
        return DEF_TP_GAIN_LV_41_50;
    }
    return DEF_TP_GAIN_LV_51_UP;
}

/* The TP a Digimon has earned by levels, from 1 to `level` (the table's total; tools/convert_save_mp.py has the same). */
int def_tp_total(int level) {
    int lv, total = 0;

    for (lv = 2; lv <= level; lv++) {
        total += def_tp_gain(lv);
    }
    return total;
}

/* Adds a level's TP to `tp` (capped); returns the TP lost to the ceiling in *lost. */
int def_tp_add(int tp, int level, int *lost) {
    int gain = def_tp_gain(level);

    *lost = 0;
    if (tp + gain > DEF_TP_CAP) {
        *lost = tp + gain - DEF_TP_CAP;
        return DEF_TP_CAP;
    }
    return tp + gain;
}

/* ---- Layer E: training pity (RAM-only, shared across Digimon). ---- */
static int train_pity = 0;

int def_train_pity_count(void) {
    if (DEF_TRAIN_PITY_AFTER <= 0) {
        return 0;
    }
    return train_pity > DEF_TRAIN_PITY_AFTER ? DEF_TRAIN_PITY_AFTER : train_pity;
}

int def_train_pity_guaranteed(void) {
    return DEF_TRAIN_PITY_AFTER > 0 && train_pity >= DEF_TRAIN_PITY_AFTER;
}

void def_train_pity_record(int perfect) {
    if (DEF_TRAIN_PITY_AFTER <= 0 || perfect || train_pity >= DEF_TRAIN_PITY_AFTER) {
        train_pity = 0;
        return;
    }
    train_pity++;
}

void def_train_pity_reset(void) {
    train_pity = 0;
}

/* ---- Layer F: Blast Gauge slot serialization (0..DEF_GAUGE_MAX). ---- */
int def_gauge_clamp(int val) {
    return (val < 0 || val > DEF_GAUGE_MAX) ? 0 : val;
}

void def_gauges_encode(const short gauges[DEF_GAUGE_SLOTS], unsigned char out[DEF_GAUGE_SLOTS * 2]) {
    int i;

    for (i = 0; i < DEF_GAUGE_SLOTS; i++) {
        int v = def_gauge_clamp(gauges[i]);
        out[i * 2] = (unsigned char)(v & 0xFF);
        out[i * 2 + 1] = (unsigned char)((v >> 8) & 0xFF);
    }
}

void def_gauges_decode(const unsigned char in[DEF_GAUGE_SLOTS * 2], short out[DEF_GAUGE_SLOTS]) {
    int i;

    for (i = 0; i < DEF_GAUGE_SLOTS; i++) {
        short raw = (short)((unsigned short)in[i * 2] | ((unsigned short)in[i * 2 + 1] << 8));
        out[i] = (short)def_gauge_clamp(raw);
    }
}
