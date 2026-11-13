/* tests/port/definitive_hunt.py's driver for port/game/definitive_hunt.c (PRD 4 layer A), linked with
 * definitive_bestiary.c, whose card record keeps the chain. The game's state the module reads (records_state,
 * pad_random, ststatus_map_areas, def_on) and the Rare Target table are stand-ins. Prints "ok name" / "FAIL name" per
 * case; exits 1 on a failure. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pad.h"
#include "records.h"
#include "fieldstg.h"

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

RecordsState records_state;
unsigned char ststatus_map_areas[0xF0];

static int draws;
static s32 next_value;
static s32 next(void) {
    draws++;
    return next_value;
}
PadRandom pad_random = { 0, NULL, next };

/* Region 0x05 has a target (battle 40 at level 30, material 4); region 0x06 has none; 0x07 and 0x08 are there for the
 * rewards' levels (layer B). */
const DefHuntTarget def_hunt_targets[] = { { 0x05, 40, 30, 4 }, { 0x07, 41, 70, 9 }, { 0x08, 42, 15, 0 } };
const int def_hunt_target_count = 3;
/* Their battles' enemies (layer C): species 100 in battles 40 and 41, 101 in 41 and 42; an empty slot is species 0. */
static RecordsEnemy none, e100 = { 100, 0, 0, 0, 0 }, e101 = { 101, 0, 0, 0, 0 };
FieldstgBattle fieldstg_battles[43] = {
    [40] = { { &e100, &none, &none }, 0, 0, { 0 } },
    [41] = { { &e101, &e100, &none }, 0, 0, { 0 } },
    [42] = { { &none, &e101, &none }, 0, 0, { 0 } },
};

#define MAP_A 0x210 /* region 0x05 */
#define MAP_B 0x211 /* region 0x05 too */
#define MAP_C 0x212 /* region 0x06 */

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

static int chain_now(void) {
    int region, chain;

    def_record_hunt(&region, &chain);
    return chain;
}

/* A Rare Target won in `region` (its chain at the cap, a draw that always hits): layer B's reward is then pending. */
static void win_rare(int region) {
    def_record_set_hunt(region, DEF_HUNT_CHAIN_CAP);
    next_value = 0;
    def_hunt_encounter(7);
    def_hunt_battle_end(1);
}

/* A random encounter won (or lost): what fieldstg_encounter_start and wfightmn do. Returns the battle fought. */
static int encounter(int battle, int won) {
    int fought = def_hunt_encounter(battle);

    def_hunt_battle_end(won);
    return fought;
}

int main(void) {
    unsigned char rec[DEF_BESTIARY_RECORD_SIZE];
    int i, region, chain, fought;

    ststatus_map_areas[MAP_A & 0xFF] = 0x05;
    ststatus_map_areas[MAP_B & 0xFF] = 0x05;
    ststatus_map_areas[MAP_C & 0xFF] = 0x06;
    def_bestiary_reset();

    check("chance: 0 under the minimum, base at it, grows per win, stops at the ceiling",
          def_hunt_chance(DEF_HUNT_CHAIN_MIN - 1) == 0 && def_hunt_chance(DEF_HUNT_CHAIN_MIN) == DEF_HUNT_CHANCE_BASE &&
              def_hunt_chance(DEF_HUNT_CHAIN_MIN + 1) == DEF_HUNT_CHANCE_BASE + DEF_HUNT_CHANCE_STEP &&
              def_hunt_chance(200) == DEF_HUNT_CHANCE_MAX);

    def_hunt_map(MAP_A);
    next_value = 0; /* a draw that would always hit */
    draws = 0;
    for (i = 0; i < DEF_HUNT_CHAIN_MIN; i++) {
        encounter(7, 1);
    }
    check("won encounters count, and no draw happens under the minimum", chain_now() == DEF_HUNT_CHAIN_MIN && draws == 0);

    /* From here the chain is at the minimum, so each encounter draws: a missing draw keeps the cases below about the
     * chain alone (a hit would make them Rare Targets, which reset it). */
    next_value = 99;
    encounter(7, 0);
    check("a lost or fled encounter neither counts nor resets", chain_now() == DEF_HUNT_CHAIN_MIN);

    def_hunt_encounter(7);
    def_hunt_skipped(); /* the Rush took it */
    def_hunt_battle_end(1); /* a later scripted battle's end */
    check("an encounter the Rush skipped does not count, nor does the next scripted battle", chain_now() == DEF_HUNT_CHAIN_MIN);

    def_hunt_battle_end(1); /* a battle that did not come from an encounter */
    check("a scripted battle does not count", chain_now() == DEF_HUNT_CHAIN_MIN);

    def_hunt_map(MAP_B);
    check("another map of the same region keeps the chain", chain_now() == DEF_HUNT_CHAIN_MIN);

    next_value = 99; /* a draw that misses (99 >= any chance up to 40) */
    draws = 0;
    fought = def_hunt_encounter(7);
    check("past the minimum a draw happens; a miss keeps the encounter", fought == 7 && draws == 1 && !def_hunt_active());
    def_hunt_battle_end(1);

    next_value = 0;
    fought = def_hunt_encounter(7);
    check("a hit swaps in the region's Rare Target", fought == 40 && def_hunt_active());
    memset(&records_state, 0, sizeof(records_state));
    records_state.enemies[0].digimon = 100;
    records_state.enemies[0].level = 20;
    records_state.enemies[0].hp = 1000;
    records_state.enemies[1].digimon = 101;
    records_state.enemies[1].level = 35; /* already above the target's level: left alone */
    records_state.enemies[1].hp = 2000;
    def_hunt_scale();
    check("the Rare Target's enemies rise to its level, HP with them; a stronger one is left as it is",
          records_state.enemies[0].level == 30 && records_state.enemies[0].hp == 1500 &&
              records_state.enemies[1].level == 35 && records_state.enemies[1].hp == 2000);
    def_hunt_battle_end(1);
    check("fighting the Rare Target resets the chain and ends the escape rule", chain_now() == 0 && !def_hunt_active());

    /* Layer B: the reward of that win (the stand-in target is level 30, material 4; R-Booster 02 from 25). */
    check("the win gives the target's own material, in the record at once", def_record_material(4) == 1);
    check("its results screen pays the replaced encounter's XP and drops the band's rare Booster",
          def_hunt_reward_battle() == 7 && def_hunt_reward_pack() == DEF_HUNT_PACK_FIRST + 1);
    check("the material's line is pending, is a text, and is shown once",
          def_hunt_material_pending() && def_hunt_material_text()[0] != 0 && !def_hunt_material_pending());
    def_hunt_reward_done();
    check("the screen's end clears the reward", def_hunt_reward_battle() == -1 && def_hunt_reward_pack() == 0);
    win_rare(0x07);
    check("a level-70 target drops R-Booster 05 and its own material",
          def_hunt_reward_pack() == DEF_HUNT_PACK_FIRST + 4 && def_record_material(9) == 1);
    check("the screen gives that Booster as one pack (DEF_HUNT_PACKS), and only that item counts as it",
          def_hunt_pack_count(DEF_HUNT_PACK_FIRST + 4) == 0 && def_hunt_take_pack() == DEF_HUNT_PACK_FIRST + 4 &&
              def_hunt_pack_count(DEF_HUNT_PACK_FIRST + 4) == DEF_HUNT_PACKS && def_hunt_pack_count(DEF_HUNT_PACK_FIRST) == 0);
    def_hunt_reward_done();
    win_rare(0x08);
    check("a level-15 target drops R-Booster 01 and its own material",
          def_hunt_reward_pack() == DEF_HUNT_PACK_FIRST && def_record_material(0) == 1 &&
              def_hunt_pack_count(DEF_HUNT_PACK_FIRST + 4) == 0);
    def_hunt_reward_done();

    /* Layer C: what the bestiary lists under a species. */
    check("a species drops the materials of the target battles it fights in; others and the empty slot none",
          def_hunt_species_materials(100) == (1u << 4 | 1u << 9) && def_hunt_species_materials(101) == (1u << 9 | 1u << 0) &&
              def_hunt_species_materials(102) == 0 && def_hunt_species_materials(0) == 0);

    for (i = 0; i < 3; i++) {
        encounter(7, 1);
    }
    def_hunt_map(MAP_C);
    def_record_hunt(&region, &chain);
    check("entering another region resets the chain and records it", region == 0x06 && chain == 0);

    for (i = 0; i < DEF_HUNT_CHAIN_MIN + 4; i++) {
        encounter(7, 1);
    }
    next_value = 0;
    draws = 0;
    fought = def_hunt_encounter(7);
    check("a region without a target never swaps and never draws", fought == 7 && draws == 0 && !def_hunt_active());
    def_hunt_battle_end(1);

    def_hunt_chain_reset();
    check("the teleport resets the chain", chain_now() == 0);

    for (i = 0; i < 260; i++) {
        encounter(7, 1);
    }
    check("the chain stops at its cap", chain_now() == DEF_HUNT_CHAIN_CAP);

    def_bestiary_reset();
    memset(rec, 0xAA, sizeof(rec));
    def_bestiary_record_write(rec);
    for (i = 0; i < DEF_BESTIARY_RECORD_SIZE && rec[i] == 0; i++) {
    }
    check("a new game writes an all-zero record (nothing to keep)", i == DEF_BESTIARY_RECORD_SIZE);

    def_record_set_hunt(0x93, 12);
    def_bestiary_record_write(rec);
    def_bestiary_reset();
    def_bestiary_record_read(rec);
    def_record_hunt(&region, &chain);
    check("a record with only the hunt's state is written and read back", region == 0x93 && chain == 12);

    def_bestiary_reset();
    def_record_material_add(5, 3);
    def_record_material_add(15, 250);
    def_bestiary_record_write(rec);
    def_bestiary_reset();
    def_bestiary_record_read(rec);
    check("materials alone write the record and read back, clamped at the cap",
          def_record_material(5) == 3 && def_record_material(15) == DEF_HUNT_MATERIAL_CAP && def_record_material(0) == 0);
    rec[0xD0 + 10 + 7] = 200; /* no checksum covers it: a damaged count */
    def_bestiary_record_read(rec);
    check("a count past the cap reads as the cap", def_record_material(7) == DEF_HUNT_MATERIAL_CAP);

    memset(rec, 0, sizeof(rec));
    memcpy(rec, "BEST", 4);
    rec[4] = DEF_BESTIARY_VERSION;
    rec[0xD0] = 'X';
    rec[0xD1] = '1';
    def_bestiary_record_read(rec); /* an older save: "X1" without the hunt's bytes */
    def_record_hunt(&region, &chain);
    check("an older record reads the hunt's state as zero", region == 0 && chain == 0);

    on = 0;
    def_bestiary_reset();
    def_hunt_map(MAP_A);
    for (i = 0; i < DEF_HUNT_CHAIN_MIN + 5; i++) {
        encounter(7, 1);
    }
    draws = 0;
    fought = def_hunt_encounter(7);
    def_record_hunt(&region, &chain);
    check("in a replay script nothing changes and nothing is drawn", fought == 7 && draws == 0 && region == 0 && chain == 0 &&
                                                                          !def_hunt_active());

    return failures != 0;
}
