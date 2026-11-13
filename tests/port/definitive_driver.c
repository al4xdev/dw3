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

static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

/* One player turn on target 0: starts it (normal or extra, as the queue would), the action resolves, then the queue is
 * asked for the next event: returns 1 when an extra action was granted. */
static int turn(DefChain *c, int hit, int elem, int weak) {
    def_chain_party_turn(c);
    def_chain_player_hit(c, 0, hit, elem, weak);
    return def_chain_take_extra(c);
}

static void chain_cases(void) {
    DefChain c;
    int i;

    def_chain_reset(&c);
    check("chain: a weakness hit gives an extra action and opens the target", turn(&c, 1, 0, 1) == 1 && c.staggered[0]);
    check("chain: the extra turn is not a new window", (def_chain_party_turn(&c), c.cur_extra == 1 && c.window_used == 1));
    def_chain_player_hit(&c, 0, 1, 0, 1);
    check("chain: the same weakness again in the window: no extra", def_chain_take_extra(&c) == 0);
    check("chain: a hit on an open target by the same weakness leaves it open", c.staggered[0] == 1);
    def_chain_party_turn(&c);
    def_chain_player_hit(&c, 0, 1, 3, 1);
    check("chain: the second weakness gives another extra", def_chain_take_extra(&c) == 1);
    def_chain_party_turn(&c);
    def_chain_player_hit(&c, 0, 1, 0, 0);
    check("chain: a hit that is no weakness closes the open target", c.staggered[0] == 0 && def_chain_take_extra(&c) == 0);
    def_chain_party_turn(&c); /* the next normal action: a new window */
    check("chain: a normal action starts the window again", c.cur_extra == 0 && c.window_used == 0);
    check("chain: so the first weakness gives its extra again", turn(&c, 1, 0, 1) == 1);

    def_chain_reset(&c);
    check("chain: a miss does nothing", turn(&c, 0, 0, 1) == 0 && !c.staggered[0]);
    check("chain: a physical blow without element never opens", turn(&c, 1, -1, 1) == 0 && !c.staggered[0]);
    check("chain: a hit that is no weakness does not open", turn(&c, 1, 2, 0) == 0 && !c.staggered[0]);

    def_chain_reset(&c);
    turn(&c, 1, 0, 1);
    def_chain_enemy_acts(&c, 0);
    check("chain: the enemy that acts is closed", c.staggered[0] == 0);
    def_chain_enemy_acts(&c, 1);
    check("chain: another enemy's action closes nothing else", def_chain_swap_free(&c, 0) == 0);

    def_chain_reset(&c);
    check("swap: not free with no open target", def_chain_swap_free(&c, 0) == 0);
    turn(&c, 1, 0, 1); /* opens target 0; the extra is taken */
    def_chain_party_turn(&c); /* the extra action's turn */
    check("swap: free with an open target", def_chain_swap_free(&c, 0) == 1);
    def_chain_swap_chosen(&c, 0);
    check("swap: the free swap gives an extra action and is spent", c.extra_pending == 1 && def_chain_swap_free(&c, 0) == 0);
    def_chain_take_extra(&c);
    def_chain_party_turn(&c);
    def_chain_swap_chosen(&c, 0);
    check("swap: once per window", c.extra_pending == 0);
    def_chain_party_turn(&c); /* a normal turn: the window starts again */
    c.staggered[0] = 1;
    check("swap: free again in the next window", def_chain_swap_free(&c, 0) == 1);

    def_chain_reset(&c);
    def_chain_relay_set(&c);
    check("relay: the next swap is free with no open target", def_chain_swap_free(&c, 0) == 1);
    def_chain_swap_chosen(&c, 0);
    check("relay: it gives an extra action and is spent", c.extra_pending == 1 && c.relay == 0 && def_chain_swap_free(&c, 0) == 0);
    def_chain_reset(&c);
    turn(&c, 1, 0, 1);
    def_chain_party_turn(&c);
    def_chain_relay_set(&c);
    def_chain_swap_chosen(&c, 0);
    check("relay: it leaves the window's own free swap for later", c.swap_used == 0 && def_chain_swap_free(&c, 0) == 1);

    def_chain_reset(&c);
    for (i = 0; i < 3; i++) { /* Fire, Water, Wind: three families in one window give three */
        static const int elems[3] = { 0, 1, 3 };

        def_chain_party_turn(&c);
        def_chain_player_hit(&c, 0, 1, elems[i], 1);
        c.staggered[0] = 1;
        if (def_chain_take_extra(&c) != 1) {
            break;
        }
    }
    check("chain: three weakness families in one window, three extras (no cap)", i == 3 && c.window_used == 0x0F);

    def_chain_reset(&c);
    turn(&c, 1, 1, 1); /* Water: its extra */
    def_chain_party_turn(&c);
    def_chain_player_hit(&c, 0, 1, 2, 1);
    check("chain: Ice after Water in the window gives no second extra (one affinity), the target stays open",
          def_chain_take_extra(&c) == 0 && c.staggered[0] == 1);

}

static void mult_case(const char *name, int got, int want) {
    check(name, got == want);
    if (got != want) {
        printf("  got %d want %d\n", got, want);
    }
}

static void multiplier_cases(void) {
    const short none[7] = { 0, 0, 0, 0, 0, 0, 0 };
    const short full[7] = { 999, 999, 999, 999, 999, 999, 999 };
    const short water_only[7] = { 0, 999, 0, 0, 0, 0, 0 };
    const short ice_only[7] = { 0, 0, 999, 0, 0, 0, 0 };
    const short over[7] = { 5000, 0, 0, 0, 0, 0, 0 };

    mult_case("mult: neutral is 1.0", def_multiplier(0, 0, 0, def_affinity(0, full)), 1000);
    mult_case("mult: weak is 1.5", def_multiplier(1, 0, 0, 0), 1500);
    mult_case("mult: weak and open, full Fire affinity: 1+.5+.2+.3+.3", def_multiplier(1, 1, 0, def_affinity(0, full)), 2300);
    mult_case("mult: a pair 999+0 is 0.3 of affinity (water)", def_affinity(1, water_only), 300);
    mult_case("mult: ... and the same for ice", def_affinity(2, ice_only), 300);
    mult_case("mult: the pair at 999+999 is 0.6", def_affinity(1, full), 600);
    mult_case("mult: Thunder with Machine, 999+999", def_affinity(4, full), 600);
    mult_case("mult: Machine alone 0, Thunder 999: 0.3", def_affinity(5, (const short[7]){ 0, 0, 0, 0, 999, 0, 0 }), 300);
    mult_case("mult: Wind full 0.3", def_affinity(3, full), 300);
    mult_case("mult: Dark full 0.6", def_affinity(6, full), 600);
    mult_case("mult: no resists, no affinity", def_affinity(0, none), 0);
    mult_case("mult: a stat past 999 counts as 999", def_affinity(0, over), 300);
    mult_case("mult: resisted is the resisted multiplier, no bonus", def_multiplier(1, 1, 1, 300), 500);
    mult_case("mult: open alone with affinity: 1+.2+.3", def_multiplier(0, 1, 0, 300), 1500);
    mult_case("mult: weak alone, half Fire affinity (499): 1+.5+.149", def_multiplier(1, 0, 0, def_affinity(0, (const short[7]){ 499, 0, 0, 0, 0, 0, 0 })), 1649);
}

int main(void) {
    const short equal[7] = { 100, 100, 100, 100, 100, 100, 100 };
    const short one[7] = { 60, 100, 100, 100, 100, 100, 100 };
    const short two[7] = { 60, 60, 60, 100, 100, 100, 100 };      /* Fire and Water+Ice: two families */
    const short water[7] = { 100, 60, 100, 100, 100, 100, 100 };  /* Water low, Ice not (a modifier): the pair is one */
    const short machine[7] = { 100, 100, 100, 100, 100, 60, 100 };
    const short three[7] = { 60, 60, 60, 60, 100, 100, 100 };     /* three families: no cap any more */
    const short all_low[7] = { 60, 60, 60, 60, 60, 60, 100 };     /* every family: no median to hide them */
    const short edge_in[7] = { 80, 100, 100, 100, 100, 100, 100 }; /* DEF_WEAK_MAX itself */
    const short edge_out[7] = { 81, 100, 100, 100, 100, 100, 100 };
    const short broken[7] = { 60, 60, 60, 100, 20, 20, 100 };     /* a plain enemy's two and a Mega Break on a third */
    const short darkvalue[7] = { 100, 100, 100, 100, 100, 100, 10 };
    const short strong[7] = { 300, 100, 100, 100, 100, 100, 100 };
    const short strong_down[7] = { 220, 100, 100, 100, 100, 100, 100 }; /* a resisted 300 after Resist Down: neutral */

    mask_case("all equal: none", equal, 0, 0);
    mask_case("one low value: that element", one, 0, 1 << 0);
    mask_case("Fire and Water+Ice: both families, three bits", two, 0, 0x07);
    mask_case("Water alone low: Water and Ice", water, 0, 0x06);
    mask_case("Machine alone low: Thunder and Machine", machine, 0, 0x30);
    mask_case("three weak families: all three", three, 0, 0x0F);
    mask_case("every family low: all six", all_low, 0, 0x3F);
    mask_case("at DEF_WEAK_MAX: weak", edge_in, 0, 1 << 0);
    mask_case("one above DEF_WEAK_MAX: not weak", edge_out, 0, 0);
    mask_case("a Break on a third family adds it, hides none", broken, 0, 0x37);
    mask_case("Dark's own value is ignored", darkvalue, 0, 0);
    mask_case("a strong resist is no weakness", strong, 0, 0);
    mask_case("a lowered strong resist stays above the line", strong_down, 0, 0);
    mask_case("Dark marked, all equal", equal, 1, 1 << 6);
    mask_case("Dark marked, one weakness: on top", one, 1, (1 << 0) | (1 << 6));
    mask_case("Dark marked, two families", two, 1, 0x47);
    check("family bits: a pair both ways, a single alone, Dark, outside 0..6 none",
          def_family_bits(1) == 0x06 && def_family_bits(2) == 0x06 && def_family_bits(4) == 0x30 &&
              def_family_bits(5) == 0x30 && def_family_bits(0) == 0x01 && def_family_bits(3) == 0x08 &&
              def_family_bits(6) == 0x40 && def_family_bits(7) == 0 && def_family_bits(-1) == 0);
    if (def_resisted(249) != 0 || def_resisted(250) != 1 || def_resisted(999) != 1 || def_resisted(-5) != 0) {
        failures++;
        printf("FAIL def_resisted\n");
    } else {
        printf("ok def_resisted\n");
    }
    chain_cases();
    multiplier_cases();
    return failures ? 1 : 0;
}
