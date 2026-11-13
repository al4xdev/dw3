/* tests/port/definitive_dri.py's driver for port/game/definitive_dri.c (PRD 4 layer H, the DRI Gauntlet's rules), linked
 * with definitive_bestiary.c, whose second card record keeps the unlocked challenges and their best stages and whose
 * first and second records hold the materials the unlocks take. The challenge and agent tables, def_on and the names
 * are stand-ins. Prints "ok name" / "FAIL name" per case; exits 1 on a failure. */
#include <stdio.h>
#include <string.h>

#include "definitive.h"

int port_script_active;
void port_log(const char *fmt, ...) {
    (void)fmt;
}
int fightstg_enemy_find_record(int id) {
    (void)id;
    return -1;
}
static int on = 1;
int def_on(void) {
    return on;
}
const unsigned char *def_hunt_material_name(int m) {
    static const unsigned char name[2] = { 0x10, 0 };

    (void)m;
    return name;
}
void def_hunt_target_name(unsigned char *buf, int *p, int size, int rare) {
    static const unsigned char name[2] = { 0x11, 0 };

    (void)rare;
    def_glyph_put(buf, p, size, name);
}

/* Challenge 0: 15 of common 4 and one of rare 7; 1: 20 of common 5, one of rare 8 and two of rare 30; 2: rares only. */
const DefDriChallenge def_dri_challenges[] = {
    { { { 435, 24, 700, 9999, 14 }, { 40, 27, 765, 9999, 18 }, { 443, 30, 1736, 9999, 12 } }, 6, { 354, 355, 356 }, 4, 15,
      { 7, -1 }, { 1, 0 } },
    { { { 135, 32, 909, 9999, 13 }, { 450, 35, 1920, 9999, 27 }, { 444, 38, 2100, 9999, 14 } }, 10, { 351, 352, 353 }, 5, 20,
      { 8, 30 }, { 1, 2 } },
    { { { 7, 40, 1080, 9999, 65 }, { 452, 43, 2304, 9999, 24 }, { 447, 46, 2453, 9999, 16 } }, 15, { 348, 349, 350 }, -1, 0,
      { 2, -1 }, { 1, 0 } },
};
const int def_dri_challenge_count = 3;
/* Actor 60 is a DRI Agent on map 0x210 only, actor 61 on any map. */
const DefDriAgent def_dri_agents[] = { { 0x210, 60 }, { -1, 61 } };
const int def_dri_agent_count = 2;

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

static int all_zero(const unsigned char *p, int n) {
    while (n-- > 0) {
        if (*p++ != 0) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static unsigned char rec[DEF_BESTIARY_RECORD_SIZE], rec2[DEF_RECORD2_SIZE];
    short items[DEF_DRI_STAGES] = { 0, 0, 0 };
    int n, palette = -1, i;

    def_bestiary_reset();
    check("the tier: one per stage felled, 0..3", def_dri_tier(-1) == 0 && def_dri_tier(0) == 0 && def_dri_tier(1) == 1 &&
                                                      def_dri_tier(2) == 2 && def_dri_tier(3) == 3 && def_dri_tier(4) == 3);
    check("the table: in range, NULL outside", def_dri_challenge(0) == &def_dri_challenges[0] && def_dri_challenge(3) == NULL &&
                                                   def_dri_challenge(-1) == NULL);
    check("a DRI Agent on its map, one on any map, nobody else",
          def_dri_agent(0x210, 60) && !def_dri_agent(0x211, 60) && def_dri_agent(0x211, 61) && def_dri_agent(0x2A0, 61) &&
              !def_dri_agent(0x210, 62));
    {
        DefDriWait w;

        memset(&w, 0, sizeof(w));
        check("nothing armed: nothing opens", !def_dri_wait_step(&w, 0x210, 1, 0));
        def_dri_wait_arm(&w, 0x210);
        n = 0;
        for (i = 0; i < 40; i++) { /* the agent's own lines */
            n |= def_dri_wait_step(&w, 0x210, 0, 0);
        }
        for (i = 0; i < DEF_DRI_WAIT_IDLE - 1; i++) {
            n |= def_dri_wait_step(&w, 0x210, 1, 0);
        }
        check("after a talk: nothing while it runs, nor before the field has been idle long enough", !n && w.armed);
        check("then the list, once", def_dri_wait_step(&w, 0x210, 1, 0) && !w.armed && !def_dri_wait_step(&w, 0x210, 1, 0));
        def_dri_wait_arm(&w, 0x210);
        n = 0;
        for (i = 0; i < DEF_DRI_WAIT_IDLE - 1; i++) {
            n |= def_dri_wait_step(&w, 0x210, 1, 0);
        }
        n |= def_dri_wait_step(&w, 0x210, 0, 0); /* the talk's next part */
        for (i = 0; i < DEF_DRI_WAIT_IDLE - 1; i++) {
            n |= def_dri_wait_step(&w, 0x210, 1, 0);
        }
        check("an idle gap inside the talk is not its end", !n && w.armed && def_dri_wait_step(&w, 0x210, 1, 0));
        def_dri_wait_arm(&w, 0x210);
        check("a battle the talk starts drops it",
              !def_dri_wait_step(&w, 0x210, 0, 1) && !w.armed && !def_dri_wait_step(&w, 0x210, 1, 0));
        def_dri_wait_arm(&w, 0x210);
        n = 0;
        for (i = 0; i < 2 * DEF_DRI_WAIT_IDLE; i++) {
            n |= def_dri_wait_step(&w, 0x211, 1, 0);
        }
        check("another map drops it", !n && !w.armed);
        def_dri_wait_arm(&w, 0x210);
        n = 0;
        for (i = 0; i <= DEF_DRI_WAIT_MAX; i++) {
            n |= def_dri_wait_step(&w, 0x210, 0, 0);
        }
        check("a talk that never ends gives up", !n && !w.armed);
        on = 0;
        def_dri_wait_arm(&w, 0x210);
        check("in a replay script nothing is armed, and no Tamer", !w.armed && def_dri_rival(0, DEF_DRI_RIVAL_NAME) == NULL);
        on = 1;
    }
    check("each challenge's Tamer: title, before, after a clear, after less; none past the table",
          def_dri_rival(0, DEF_DRI_RIVAL_NAME) != NULL && def_dri_rival(2, DEF_DRI_RIVAL_LOST) != NULL &&
              def_dri_rival(0, DEF_DRI_RIVAL_HELLO) != def_dri_rival(0, DEF_DRI_RIVAL_WON) &&
              def_dri_rival(1, DEF_DRI_RIVAL_WON) != def_dri_rival(1, DEF_DRI_RIVAL_LOST) &&
              def_dri_rival(3, DEF_DRI_RIVAL_NAME) == NULL && def_dri_rival(0, 4) == NULL && def_dri_rival(0, -1) == NULL);

    /* Unlock: the materials, once. */
    check("nothing held: locked and cannot be unlocked", !def_record_dri_unlocked(0) && !def_dri_can_unlock(0) &&
                                                             !def_dri_unlock(0));
    def_record_material_add(4, 15);
    check("the common alone is not enough", !def_dri_can_unlock(0));
    def_record_rare_add(7, 1);
    check("both: it can be unlocked", def_dri_can_unlock(0));
    check("the unlock's line, white when the materials are there",
          def_dri_cost_label(0, &palette) != NULL && palette == 0 && def_dri_cost_label(9, &palette) == NULL);
    check("unlocking it takes the materials and opens it", def_dri_unlock(0) && def_record_dri_unlocked(0) &&
                                                               def_record_material(4) == 0 && def_record_rare(7) == 0);
    def_record_material_add(4, 15);
    def_record_rare_add(7, 1);
    check("an open challenge is never paid for again", !def_dri_can_unlock(0) && !def_dri_unlock(0) &&
                                                          def_record_material(4) == 15 && def_record_rare(7) == 1);
    def_record_material_add(5, 25);
    def_record_rare_add(8, 1);
    def_record_rare_add(30, 1);
    def_dri_cost_label(1, &palette);
    check("one of two of a rare short: locked, the line orange", !def_dri_can_unlock(1) && palette == 3);
    def_record_rare_add(30, 1);
    check("two rares and a common: paid exactly", def_dri_unlock(1) && def_record_material(5) == 5 &&
                                                       def_record_rare(8) == 0 && def_record_rare(30) == 0);
    def_record_rare_add(2, 1);
    check("a challenge without a common: rares only", def_dri_unlock(2) && def_record_rare(2) == 0 &&
                                                          def_record_material(4) == 15);

    /* Rewards: each tier once, the best only rises. */
    n = def_dri_award(0, 1, items);
    check("one stage felled: tier 1's accessory, best 1", n == 1 && items[0] == 354 && def_record_dri_best(0) == 1);
    n = def_dri_award(0, 1, items);
    check("one stage again: nothing new", n == 0 && def_record_dri_best(0) == 1);
    n = def_dri_award(0, 3, items);
    check("then all three: tiers 2 and 3, best 3", n == 2 && items[0] == 355 && items[1] == 356 &&
                                                       def_record_dri_best(0) == 3);
    n = def_dri_award(0, 2, items);
    check("a worse run after: nothing, the best stays", n == 0 && def_record_dri_best(0) == 3);
    n = def_dri_award(1, 3, items);
    check("a first full clear: all three tiers", n == 3 && items[0] == 351 && items[1] == 352 && items[2] == 353 &&
                                                     def_record_dri_best(1) == 3);
    n = def_dri_award(2, 0, items);
    check("no stage felled: nothing", n == 0 && def_record_dri_best(2) == 0);
    def_record_dri_raise_best(2, 9);
    check("the best is clamped to the stages", def_record_dri_best(2) == 3);
    def_record_dri_raise_best(2, 1);
    check("and never lowered", def_record_dri_best(2) == 3);

    /* The second card record: bytes 152.. (after the rares' bits). */
    def_bestiary_reset();
    memset(rec2, 0xAA, sizeof(rec2));
    def_record2_write(rec2);
    check("a new game writes an all-zero second record", all_zero(rec2, DEF_RECORD2_SIZE));
    def_record_dri_unlock(0);
    def_record_dri_unlock(9);
    def_record_dri_raise_best(0, 3);
    def_record_dri_raise_best(5, 2);
    def_record2_write(rec2);
    check("DRI state alone writes the record: HUNT, version, the bytes at 152..157",
          memcmp(rec2, "HUNT", 4) == 0 && rec2[4] == DEF_RECORD2_VERSION && rec2[152] == 0x01 && rec2[153] == 0x02 &&
              rec2[154] == 0x03 && rec2[155] == 0x08 && rec2[156] == 0 && rec2[157] == 0 && all_zero(rec2 + 158, 98) &&
              all_zero(rec2 + 8, 144));
    def_bestiary_reset();
    check("a new game forgets them", !def_record_dri_unlocked(0) && def_record_dri_best(0) == 0);
    def_record2_read(rec2);
    check("read back", def_record_dri_unlocked(0) && def_record_dri_unlocked(9) && !def_record_dri_unlocked(1) &&
                           def_record_dri_best(0) == 3 && def_record_dri_best(5) == 2 && def_record_dri_best(1) == 0);
    rec2[153] ^= 4; /* the checksum covers them */
    def_record2_read(rec2);
    check("a damaged record reads as nothing unlocked", !def_record_dri_unlocked(0) && def_record_dri_best(0) == 0);

    def_bestiary_reset();
    def_record_rare_add(3, 2);
    def_record2_write(rec2); /* a record of the layer before: rares only, zeros at 152.. */
    check("a rares-only record has zeros where the DRI state goes", all_zero(rec2 + 152, 6));
    def_record_dri_unlock(4);
    def_record2_read(rec2);
    check("and reads as none unlocked, the rares kept", !def_record_dri_unlocked(4) && def_record_rare(3) == 2);
    def_record_dri_unlock(4);
    def_record_dri_raise_best(4, 1);
    def_record2_write(rec2);
    def_bestiary_reset();
    def_record2_read(rec2);
    check("rares and DRI state together", def_record_rare(3) == 2 && def_record_dri_unlocked(4) &&
                                              def_record_dri_best(4) == 1);
    def_bestiary_record_write(rec);
    def_bestiary_record_read(rec); /* the first record's read resets the slot's state: the second is read after it */
    check("the first record's read resets them (the card reads the second after)", !def_record_dri_unlocked(4));
    def_record2_read(rec2);
    check("then the second brings them back", def_record_dri_unlocked(4) && def_record_dri_best(4) == 1);
    def_record_dri_unlock(-1);
    def_record_dri_unlock(DEF_DRI_CHALLENGES);
    def_record_dri_raise_best(DEF_DRI_CHALLENGES, 3);
    check("out-of-range challenges change nothing", !def_record_dri_unlocked(-1) &&
                                                       !def_record_dri_unlocked(DEF_DRI_CHALLENGES) &&
                                                       def_record_dri_best(DEF_DRI_CHALLENGES) == 0);

    /* A replay script. */
    def_bestiary_reset();
    for (i = 0; i < DEF_HUNT_MATERIALS; i++) {
        def_record_material_add(i, 99);
    }
    def_record_rare_add(7, 5);
    on = 0;
    n = def_dri_award(0, 3, items);
    check("in a replay script: no challenge, no agent, no unlock, no reward",
          def_dri_challenge(0) == NULL && !def_dri_agent(0x211, 61) && !def_dri_can_unlock(0) && !def_dri_unlock(0) &&
              n == 0 && !def_record_dri_unlocked(0) && def_record_dri_best(0) == 0 && def_record_rare(7) == 5);
    on = 1;

    return failures != 0;
}
