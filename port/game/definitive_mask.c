/* Definitive combat, layer 3: which elements an enemy is weak to and which it resists, from its resists as they stand
 * right now. Pure (no game headers): tests/port/definitive.py compiles it alone and checks it case by case. */
#include "definitive.h"

int def_family_bits(int e) {
    /* the families of definitive.c's family_elems: Fire, Water+Ice, Wind, Thunder+Machine, Dark */
    static const unsigned char bits[7] = { 0x01, 0x06, 0x06, 0x08, 0x30, 0x30, 0x40 };

    return e >= 0 && e < 7 ? bits[e] : 0;
}

int def_weakness_mask(const short resists[7], int dark_weak) {
    int mask = 0;
    int e;

    /* No median and no cap any more: the enemy table gives a family its weak value, and a Resist Down that brings a
     * neutral family under DEF_WEAK_MAX adds a weakness without hiding one the target already has. */
    for (e = 0; e < 6; e++) {
        if (resists[e] <= DEF_WEAK_MAX) {
            mask |= def_family_bits(e); /* either of a pair: both */
        }
    }
    if (dark_weak) {
        mask |= 1 << 6;
    }
    return mask;
}

int def_resisted(int resist) {
    return resist >= DEF_RESIST_MIN;
}

/* ---- Layer 4: stagger, the chain of extra actions and the free swap, as a state machine on its own (the game feeds it
 * events: definitive.c). A weakness hit with an elemental technique opens the target; the next blow it takes, or its own
 * action, closes it. Each weakness family hit gives one extra action per window (Ice after Water gives none); the window starts again with every
 * normal action of the player (an extra one does not start it). While a target is open one swap is free per window. */
void def_chain_reset(DefChain *c) {
    int i;

    for (i = 0; i < 3; i++) {
        c->staggered[i] = 0;
    }
    c->window_used = c->swap_used = c->extra_pending = c->next_extra = c->cur_extra = c->relay = 0;
}

void def_chain_player_hit(DefChain *c, int target, int hit, int elem, int weak) {
    if (!hit || target < 0 || target > 2) {
        return;
    }
    c->staggered[target] = 0; /* it takes this blow (the bonus is the caller's); the weakness below opens it again */
    if (weak && elem >= 0 && elem < 7) {
        c->staggered[target] = 1;
        if (!(c->window_used & def_family_bits(elem))) {
            c->window_used |= def_family_bits(elem);
            c->extra_pending = 1;
        }
    }
}

void def_chain_enemy_acts(DefChain *c, int slot) {
    if (slot >= 0 && slot <= 2) {
        c->staggered[slot] = 0;
    }
}

void def_chain_party_turn(DefChain *c) {
    if (c->next_extra) {
        c->cur_extra = 1;
        c->next_extra = 0;
    } else {
        c->cur_extra = 0;
        c->window_used = 0;
        c->swap_used = 0;
    }
}

int def_chain_swap_free(const DefChain *c, int target) {
    return c->relay || (target >= 0 && target <= 2 && c->staggered[target] && !c->swap_used);
}

void def_chain_relay_set(DefChain *c) {
    c->relay = 1; /* Relay (layer 6): the next swap is free, stagger or not */
}

void def_chain_swap_chosen(DefChain *c, int target) {
    if (c->relay) { /* the relay's swap does not use the window's own */
        c->relay = 0;
        c->extra_pending = 1;
        return;
    }
    if (def_chain_swap_free(c, target)) {
        c->swap_used = 1;
        c->extra_pending = 1;
    }
}

int def_chain_take_extra(DefChain *c) {
    if (!c->extra_pending) {
        return 0;
    }
    c->extra_pending = 0;
    c->next_extra = 1;
    return 1;
}

/* ---- Layer 5: the multiplier. Fire and Wind count on their own resist, Water with Ice and Thunder with Machine on the
 * pair's sum, Dark on its own; each is its ceiling times the share of the maximum (999 a stat). */
static int stat_cap(int v) {
    return v < 0 ? 0 : v > 999 ? 999 : v;
}

int def_affinity(int element, const short resists[7]) {
    switch (element) {
    case 0:
    case 3:
        return DEF_AFF_SINGLE_MAX * stat_cap(resists[element]) / 999;
    case 1:
    case 2:
        return DEF_AFF_PAIR_MAX * (stat_cap(resists[1]) + stat_cap(resists[2])) / 1998;
    case 4:
    case 5:
        return DEF_AFF_PAIR_MAX * (stat_cap(resists[4]) + stat_cap(resists[5])) / 1998;
    case 6:
        return DEF_AFF_DARK_MAX * stat_cap(resists[6]) / 999;
    }
    return 0;
}

int def_multiplier(int weak, int open, int resisted, int affinity) {
    int m = DEF_MULT_ONE;

    if (resisted) {
        return DEF_MULT_RESISTED;
    }
    if (weak) {
        m += DEF_MULT_WEAK + affinity;
    }
    if (open) {
        m += DEF_MULT_STAGGER + affinity;
    }
    return m;
}
