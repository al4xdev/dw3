/* tests/port/definitive_bestiary.py's driver for port/game/definitive_bestiary.c (PRD layer 9): the table and the card
 * record's serialization, on a stand-in 0x800-byte tail of the save file (three 0x100 records). Prints "ok name" or
 * "FAIL name" per case; exits 1 on a failure. The game's two lookups the module reaches are stubbed. */
#include <stdio.h>
#include <string.h>

#include "definitive.h"

int port_script_active;
void port_log(const char *fmt, ...) {
    (void)fmt;
}
int fightstg_enemy_find_record(int id) {
    return id >= 1000 && id < 1000 + 200 ? id - 1000 : -1; /* the stand-in file: ids 1000.. are records 0.. */
}

static int failures;
static unsigned char tail[DEF_BESTIARY_SLOTS * DEF_BESTIARY_RECORD_SIZE]; /* the card file's bytes from 0x7800 */

static void check(const char *name, int ok) {
    if (!ok) {
        failures++;
    }
    printf("%s %s\n", ok ? "ok" : "FAIL", name);
}

static unsigned char *rec(int slot) {
    return tail + slot * DEF_BESTIARY_RECORD_SIZE;
}

/* What the stgmcard save step does for `slot`: pack the table into the slot's record. */
static void save_slot(int slot) {
    def_bestiary_record_write(rec(slot));
}

/* What the load step does: the slot's record into the table. */
static void load_slot(int slot) {
    def_bestiary_record_read(rec(slot));
}

static int all_zero(const unsigned char *p, int n) {
    while (n-- > 0) {
        if (*p++) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    int i, ok;
    unsigned char x;

    def_bestiary_reset();
    check("fresh table is empty", def_bestiary_known(0) == 0 && def_bestiary_known(192) == 0);
    check("layout: record at 0x7800 is where the game's last byte ends (0x200 + 0x100 + 3 * 0x2700)",
          DEF_BESTIARY_RECORD_AT == 0x200 + 0x100 + 3 * 0x2700 &&
              DEF_BESTIARY_RECORD_AT + DEF_BESTIARY_SLOTS * DEF_BESTIARY_RECORD_SIZE <= 0x8000);

    def_bestiary_discover(5, 1 << 0);
    def_bestiary_discover(5, 1 << 3);
    check("discover ORs bits", def_bestiary_known(5) == 0x09);
    def_bestiary_discover(5, 0xFF);
    check("bits above 6 are dropped", def_bestiary_known(5) == 0x7F);
    def_bestiary_discover(-1, 1);
    def_bestiary_discover(193, 1);
    check("invalid index ignored, known(invalid) = 0", def_bestiary_known(-1) == 0 && def_bestiary_known(193) == 0 &&
                                                          def_bestiary_known(1000) == 0);
    check("index: first and last record", def_bestiary_index(1000) == 0 && def_bestiary_index(1192) == 192);
    check("index: a record past 192 and an unknown id are -1", def_bestiary_index(1193) == -1 && def_bestiary_index(7) == -1);

    /* the empty table writes zeros: a card nothing was discovered on is byte-identical to a fresh one */
    def_bestiary_reset();
    memset(tail, 0xAA, sizeof(tail));
    save_slot(0);
    check("empty table writes an all-zero record", all_zero(rec(0), DEF_BESTIARY_RECORD_SIZE));

    /* header and checksum */
    def_bestiary_discover(0, 0x01);
    def_bestiary_discover(100, 0x42);
    def_bestiary_discover(192, 0x7F);
    save_slot(0);
    x = 0;
    for (i = 0; i < DEF_BESTIARY_SPECIES; i++) {
        x ^= rec(0)[8 + i];
    }
    check("record: magic BEST, version, XOR checksum, the 193 bytes",
          memcmp(rec(0), "BEST", 4) == 0 && rec(0)[4] == DEF_BESTIARY_VERSION && rec(0)[5] == x && rec(0)[8] == 0x01 &&
              rec(0)[8 + 100] == 0x42 && rec(0)[8 + 192] == 0x7F);
    check("record: bytes past the table are zero", all_zero(rec(0) + 8 + DEF_BESTIARY_SPECIES,
                                                           DEF_BESTIARY_RECORD_SIZE - 8 - DEF_BESTIARY_SPECIES));
    def_bestiary_reset();
    load_slot(0);
    check("round trip", def_bestiary_known(0) == 0x01 && def_bestiary_known(100) == 0x42 && def_bestiary_known(192) == 0x7F &&
                            def_bestiary_known(1) == 0);

    /* three independent slots */
    def_bestiary_reset();
    memset(tail, 0, sizeof(tail));
    def_bestiary_discover(10, 0x03);
    save_slot(0);
    def_bestiary_reset();
    def_bestiary_discover(11, 0x04);
    save_slot(1);
    def_bestiary_reset();
    save_slot(2); /* slot 2 saved with nothing discovered */
    load_slot(0);
    ok = def_bestiary_known(10) == 0x03 && def_bestiary_known(11) == 0;
    check("slot 0 loads its own", ok);
    load_slot(1);
    ok = def_bestiary_known(10) == 0 && def_bestiary_known(11) == 0x04;
    check("slot 1 loads its own", ok);
    def_bestiary_discover(10, 1);
    load_slot(2);
    check("slot 2 (empty) zeroes the table", def_bestiary_known(10) == 0 && def_bestiary_known(11) == 0);
    def_bestiary_discover(11, 0x7F);
    save_slot(2);
    load_slot(0);
    check("saving slot 2 leaves slot 0 alone", def_bestiary_known(10) == 0x03 && def_bestiary_known(11) == 0);
    /* a save over a slot with a record, nothing discovered: the old record is gone */
    def_bestiary_reset();
    save_slot(0);
    def_bestiary_discover(50, 1);
    load_slot(0);
    check("a new game saved over a slot clears its record", def_bestiary_known(50) == 0 && all_zero(rec(0), 0x100));

    /* ausente / versão errada / checksum ruim / NULL = tudo zero */
    def_bestiary_reset();
    def_bestiary_discover(7, 0x20);
    save_slot(1);
    def_bestiary_discover(8, 0x01);
    def_bestiary_record_read(NULL);
    check("NULL reads as empty", def_bestiary_known(7) == 0 && def_bestiary_known(8) == 0);
    def_bestiary_discover(8, 0x01);
    memset(tail, 0, sizeof(tail));
    load_slot(1);
    check("absent record (zeros) reads as empty", def_bestiary_known(8) == 0);
    def_bestiary_reset();
    def_bestiary_discover(7, 0x20);
    save_slot(1);
    rec(1)[4] = DEF_BESTIARY_VERSION + 1;
    def_bestiary_discover(8, 1);
    load_slot(1);
    check("unknown version reads as empty", def_bestiary_known(7) == 0 && def_bestiary_known(8) == 0);
    def_bestiary_reset();
    def_bestiary_discover(7, 0x20);
    save_slot(1);
    rec(1)[8 + 7] ^= 0x01;
    def_bestiary_discover(8, 1);
    load_slot(1);
    check("bad checksum reads as empty", def_bestiary_known(7) == 0 && def_bestiary_known(8) == 0);
    def_bestiary_reset();
    def_bestiary_discover(7, 0x20);
    save_slot(1);
    rec(1)[0] = 'X';
    load_slot(1);
    check("bad magic reads as empty", def_bestiary_known(7) == 0);
    def_bestiary_reset();
    def_bestiary_discover(7, 0x20);
    save_slot(1);
    rec(1)[8 + 7] = 0xA0;                /* a high bit set... */
    rec(1)[5] = 0xA0 ^ 0x20 ^ rec(1)[5]; /* ...with the checksum fixed up */
    load_slot(1);
    check("bits above 6 on the card are masked", def_bestiary_known(7) == 0x20);

    check("active: on outside a replay script", def_bestiary_active() == 1);
    port_script_active = 1;
    check("active: off in a script", def_bestiary_active() == 0);

    printf("definitive_bestiary_driver: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
