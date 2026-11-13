/* tests/port/definitive_market.py's driver for port/game/definitive_market.c (PRD 4 layer G, the Black Market's rules),
 * linked with definitive_bestiary.c, whose card records keep the materials, the rare items and the replicas' bits. The
 * Rare Target table, its lead species, the replica table, gamestate_data and def_on are stand-ins. Prints "ok name" /
 * "FAIL name" per case; exits 1 on a failure. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "gamestate.h"

#include "definitive.h"

int port_script_active;
void port_log(const char *fmt, ...) {
    (void)fmt;
}
int fightstg_enemy_find_record(int id) {
    return id >= 100 && id <= 102 ? id - 100 : -1;
}
static int on = 1;
int def_on(void) {
    return on;
}
GamestateData gamestate_data;

/* Rares 0 and 1 are one boss (species 100 leads both, at levels 15 and 30), 2 another (101, level 40), 3 a third (102,
 * level 20). */
const DefHuntTarget def_hunt_targets[] = { { 0x05, 40, 15, 1 }, { 0x06, 41, 30, 2 }, { 0x07, 42, 40, 1 }, { 0x08, 43, 20, 1 } };
const int def_hunt_target_count = 4;
int def_hunt_target_lead(int t) {
    static const int leads[4] = { 100, 100, 101, 102 };

    return t >= 0 && t < 4 ? leads[t] : 0;
}
/* Replica 0 is item 341 at 40000 Bits, replica 1 item 121 at 30000. */
const DefReplica def_replicas[DEF_REPLICAS] = { { 341, 40000 }, { 121, 30000 } };
const int def_replica_count = 2;

static int failures;
static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

static void hold_rares(int r0, int r1, int r2, int r3) {
    int want[4] = { r0, r1, r2, r3 }, t;

    for (t = 0; t < 4; t++) {
        def_record_rare_add(t, -DEF_HUNT_RARE_CAP);
        def_record_rare_add(t, want[t]);
    }
}

static void hold_common(int m, int n) {
    def_record_material_add(m, -DEF_HUNT_MATERIAL_CAP);
    def_record_material_add(m, n);
}

/* The whole stock in commons: an exchange turns one rare into exactly DEF_BM_COMMONS, so this never moves. */
static int stock(void) {
    int t, m, n = 0;

    for (t = 0; t < 4; t++) {
        n += def_record_rare(t) * DEF_BM_COMMONS;
    }
    for (m = 0; m < DEF_HUNT_MATERIALS; m++) {
        n += def_record_material(m);
    }
    return n;
}

int main(void) {
    unsigned char rec[DEF_BESTIARY_RECORD_SIZE];
    int picks[DEF_BM_REPLICA_RARES], i, ok, before, seed;

    def_bestiary_reset();

    /* ---- The Exchange */
    hold_rares(2, 0, 0, 0);
    hold_common(3, 10);
    ok = def_bm_exchange(0, 3) == DEF_BM_OK && def_record_rare(0) == 1 && def_record_material(3) == 10 + DEF_BM_COMMONS;
    ok &= def_bm_exchange(0, 3) == DEF_BM_OK && def_record_rare(0) == 0 && def_record_material(3) == 10 + 2 * DEF_BM_COMMONS;
    check("an exchange: one rare item for DEF_BM_COMMONS of the common picked", ok);
    check("without the rare item: refused, nothing moves",
          def_bm_exchange(0, 3) == DEF_BM_NO_RARE && def_record_rare(0) == 0 && def_record_material(3) == 10 + 2 * DEF_BM_COMMONS);
    hold_rares(1, 0, 0, 0);
    hold_common(5, DEF_HUNT_MATERIAL_CAP - DEF_BM_COMMONS);
    check("a lot that just fits: taken, the common at the cap",
          def_bm_exchange(0, 5) == DEF_BM_OK && def_record_material(5) == DEF_HUNT_MATERIAL_CAP && def_record_rare(0) == 0);
    hold_rares(1, 0, 0, 0);
    hold_common(5, DEF_HUNT_MATERIAL_CAP - DEF_BM_COMMONS + 1);
    check("a lot that would pass the cap: refused, nothing lost",
          def_bm_exchange(0, 5) == DEF_BM_NO_ROOM && def_record_rare(0) == 1 &&
              def_record_material(5) == DEF_HUNT_MATERIAL_CAP - DEF_BM_COMMONS + 1);
    check("a common or a rare out of range: refused", def_bm_exchange(0, -1) == DEF_BM_NO_ROOM &&
                                                          def_bm_exchange(0, DEF_HUNT_MATERIALS) == DEF_BM_NO_ROOM &&
                                                          def_bm_exchange(4, 0) == DEF_BM_NO_RARE &&
                                                          def_bm_exchange(-1, 0) == DEF_BM_NO_RARE && def_record_rare(0) == 1);
    /* Many exchanges, any rare to any common, some refused: the stock in commons never moves, nothing passes a cap. */
    def_bestiary_reset();
    hold_rares(30, 20, 9, 4);
    for (i = 0; i < DEF_HUNT_MATERIALS; i++) {
        hold_common(i, i * 6);
    }
    before = stock();
    seed = 12345;
    ok = 1;
    for (i = 0; i < 400; i++) {
        seed = seed * 1103515245 + 12345;
        def_bm_exchange((seed >> 8) & 3, (seed >> 12) & 15);
        ok &= stock() == before;
    }
    for (i = 0; i < DEF_HUNT_MATERIALS; i++) {
        ok &= def_record_material(i) <= DEF_HUNT_MATERIAL_CAP;
    }
    check("400 exchanges: no value made or lost outside the rate, no common past the cap", ok);

    /* ---- The replicas' rare items: distinct bosses */
    def_bestiary_reset();
    hold_rares(5, 3, 0, 0);
    check("rare items of one boss only (two regions of one lead species): no replica", !def_bm_replica_pick(picks) &&
                                                                                          def_bm_replica_check(0, picks) == DEF_BM_NO_BOSSES);
    hold_rares(5, 3, 1, 0);
    check("a second boss: the most plentiful of each", def_bm_replica_pick(picks) && picks[0] == 0 && picks[1] == 2);
    hold_rares(2, 0, 2, 2);
    check("ties: the weaker target first", def_bm_replica_pick(picks) && picks[0] == 0 && picks[1] == 3);
    hold_rares(0, 2, 0, 0);
    check("two of one rare item are not two bosses", !def_bm_replica_pick(picks));

    /* ---- Buying */
    hold_rares(3, 1, 2, 0);
    gamestate_data.money = 50000;
    gamestate_data.items[341] = 0;
    ok = def_bm_replica_buy(0) == DEF_BM_OK && gamestate_data.money == 10000 && gamestate_data.items[341] == 1 &&
         def_record_replica(0) && !def_record_replica(1);
    ok &= def_record_rare(0) == 2 && def_record_rare(1) == 1 && def_record_rare(2) == 1;
    check("a purchase: its Bits, one rare item of each of two bosses, the item, the replica's bit", ok);
    check("short of Bits: refused, nothing moves", def_bm_replica_buy(1) == DEF_BM_NO_BITS && gamestate_data.money == 10000 &&
                                                       gamestate_data.items[121] == 0 && def_record_rare(0) == 2 &&
                                                       !def_record_replica(1));
    gamestate_data.money = 1000000;
    gamestate_data.items[341] = DEF_BM_ITEM_CAP;
    check("a full bag: refused", def_bm_replica_buy(0) == DEF_BM_BAG_FULL && gamestate_data.money == 1000000 &&
                                     def_record_rare(0) == 2);
    hold_rares(4, 4, 0, 0);
    check("rare items of one boss: refused", def_bm_replica_buy(1) == DEF_BM_NO_BOSSES && def_record_rare(0) == 4 &&
                                                 gamestate_data.money == 1000000);
    check("no such replica: refused", def_bm_replica_buy(2) == DEF_BM_NO_BOSSES && def_bm_replica_buy(-1) == DEF_BM_NO_BOSSES);

    /* ---- Resale 0 and the bit's end */
    gamestate_data.items[341] = 1;
    check("a replica held: its ID resells for 0; an item that is no replica, or a replica not held, does not",
          def_bm_resale_zero(341) && !def_bm_resale_zero(121) && !def_bm_resale_zero(98) && def_bm_replica_of(121) == 1 &&
              def_bm_replica_of(98) == -1);
    gamestate_data.items[341] = 0;
    gamestate_data.items_equipped[341] = 1;
    def_bm_sweep();
    check("equipped still counts as held", def_record_replica(0) && def_bm_resale_zero(341) && def_bm_held(341) == 1);
    gamestate_data.items_equipped[341] = 0;
    def_bm_sweep();
    check("none held any more: the bit clears and the ID resells again", !def_record_replica(0) && !def_bm_resale_zero(341));

    /* ---- The card record */
    def_bestiary_reset();
    def_record_replica_set(0, 1);
    def_record_replica_set(DEF_REPLICAS - 1, 1);
    def_bestiary_record_write(rec);
    ok = memcmp(rec, "BEST", 4) == 0 && rec[0xD0] == 'X' && rec[0xD1] == '1' && rec[0xD0 + 26] == 0x01 &&
         rec[0xD0 + 27] == 0x80;
    def_bestiary_reset();
    check("reset clears the bits", !def_record_replica(0) && !def_record_replica(DEF_REPLICAS - 1));
    def_bestiary_record_read(rec);
    ok &= def_record_replica(0) && def_record_replica(DEF_REPLICAS - 1) && !def_record_replica(1);
    check("a record with only replicas writes its \"X1\" part, and reads them back", ok);
    def_bestiary_reset();
    hold_common(2, 7);
    def_bestiary_record_write(rec);
    def_record_replica_set(3, 1);
    def_bestiary_record_read(rec);
    check("an older record (\"X1\", bytes 26..27 never written): no replica", !def_record_replica(3) &&
                                                                                 def_record_material(2) == 7 &&
                                                                                 rec[0xD0 + 26] == 0 && rec[0xD0 + 27] == 0);
    memset(rec, 0, sizeof(rec));
    def_record_replica_set(3, 1);
    def_bestiary_record_read(rec);
    check("no record at all: no replica", !def_record_replica(3));
    def_bestiary_reset();
    def_bestiary_record_write(rec);
    for (i = 0, ok = 1; i < DEF_BESTIARY_RECORD_SIZE; i++) {
        ok &= rec[i] == 0;
    }
    check("a new game still writes the record all zero", ok);

    /* ---- In a replay script */
    def_bestiary_reset();
    hold_rares(5, 0, 5, 0);
    hold_common(1, 0);
    gamestate_data.money = 1000000;
    gamestate_data.items[121] = 0;
    def_record_replica_set(1, 1);
    on = 0;
    ok = def_bm_exchange(0, 1) == DEF_BM_OFF && def_bm_replica_buy(1) == DEF_BM_OFF && def_record_rare(0) == 5 &&
         def_record_material(1) == 0 && gamestate_data.money == 1000000 && !def_bm_resale_zero(121);
    def_bm_sweep();
    ok &= def_record_replica(1);
    on = 1;
    check("in a replay script nothing changes and every resale is the game's", ok);

    return failures != 0;
}
