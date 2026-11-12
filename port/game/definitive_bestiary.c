/* Definitive combat, layer 9: the bestiary's table (what the player learned of each species' elements) and its card
 * record. The record lives in the 0x800 bytes of the save file the game never writes (file offset 0x7800.., one 0x100
 * record per slot), read and written by memcard.c's part 5; the slot's own tail is taken (global_save's GsRecord at
 * 0x26C4, the MP converter's marker at 0x26EC). Records: "BEST", version, XOR checksum of the table, 193 bytes. */
#include <stdlib.h>
#include <string.h>

#include "psxstack/savestate.h"

#include "definitive.h"

extern int port_script_active;
extern void port_log(const char *fmt, ...);
extern int fightstg_enemy_find_record(int id);

#define REC_TABLE 8 /* offset of the table in the record (magic 4, version 1, checksum 1, 2 unused) */

static unsigned char table[DEF_BESTIARY_SPECIES];
static unsigned char buffer[DEF_BESTIARY_RECORD_SIZE];

void def_bestiary_reset(void) {
    memset(table, 0, sizeof(table));
}

void def_bestiary_discover(int index, int element_mask) {
    if (index >= 0 && index < DEF_BESTIARY_SPECIES) {
        table[index] |= element_mask & ((1 << DEF_BESTIARY_ELEMENTS) - 1);
    }
}

int def_bestiary_known(int index) {
    return index >= 0 && index < DEF_BESTIARY_SPECIES ? table[index] : 0;
}

int def_bestiary_index(int species_id) {
    int i = fightstg_enemy_find_record(species_id);

    return i >= 0 && i < DEF_BESTIARY_SPECIES ? i : -1;
}

void def_bestiary_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, table);
}

static unsigned char sum(const unsigned char *p, int n) {
    unsigned char x = 0;

    while (n-- > 0) {
        x ^= *p++;
    }
    return x;
}

void def_bestiary_record_write(unsigned char *out) {
    int i;

    memset(out, 0, DEF_BESTIARY_RECORD_SIZE);
    for (i = 0; i < DEF_BESTIARY_SPECIES; i++) {
        if (table[i] != 0) {
            break;
        }
    }
    if (i == DEF_BESTIARY_SPECIES) {
        return;
    }
    port_log("bestiary: the slot's record written");
    memcpy(out, "BEST", 4);
    out[4] = DEF_BESTIARY_VERSION;
    memcpy(out + REC_TABLE, table, DEF_BESTIARY_SPECIES);
    out[5] = sum(out + REC_TABLE, DEF_BESTIARY_SPECIES);
}

void def_bestiary_record_read(const unsigned char *in) {
    int i, known = 0;

    def_bestiary_reset();
    if (in == NULL || memcmp(in, "BEST", 4) != 0 || in[4] != DEF_BESTIARY_VERSION ||
        in[5] != sum(in + REC_TABLE, DEF_BESTIARY_SPECIES)) {
        return;
    }
    for (i = 0; i < DEF_BESTIARY_SPECIES; i++) {
        table[i] = in[REC_TABLE + i] & ((1 << DEF_BESTIARY_ELEMENTS) - 1);
        known += table[i] != 0;
    }
    port_log("bestiary: the slot's record read: %d species known", known);
}

/* DW3_BESTIARY turns only this layer on in a script: a card test needs the record's extra steps without the combat
 * layers (DW3_DEFINITIVE), which change the battles the script's hashes cover. */
int def_bestiary_active(void) {
    return !port_script_active || getenv("DW3_DEFINITIVE") != NULL || getenv("DW3_BESTIARY") != NULL;
}

unsigned char *def_bestiary_buffer(void) {
    return buffer;
}
