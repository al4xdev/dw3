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
/* The record's spare bytes after the table carry the slot's other port state (the slot's own tail is full): at
 * REC_EXTRA "X1", the flags (DEF_SAVE_HARD), a byte unused, then the Guardromon network's mask (4 bytes, low first).
 * A record without the "X1" (an older save) reads as none of it. */
#define REC_EXTRA 0xD0

static unsigned char extra_flags;
static unsigned int extra_network;

static unsigned char table[DEF_BESTIARY_SPECIES];
static unsigned char buffer[DEF_BESTIARY_RECORD_SIZE];

void def_bestiary_reset(void) {
    memset(table, 0, sizeof(table));
    extra_flags = 0;
    extra_network = 0;
}

unsigned int def_network_mask(void) {
    return extra_network;
}

void def_network_unlock(int node) {
    if (node >= 0 && node < 32) {
        extra_network |= 1u << node;
    }
}

int def_save_flag(int flag) {
    return (extra_flags & flag) != 0;
}

void def_save_flag_set(int flag, int on) {
    extra_flags = on ? extra_flags | flag : extra_flags & ~flag;
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
    PORT_STATE_VAR(s, extra_flags);
    PORT_STATE_VAR(s, extra_network);
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
    if (i == DEF_BESTIARY_SPECIES && extra_flags == 0 && extra_network == 0) {
        return;
    }
    port_log("bestiary: the slot's record written");
    memcpy(out, "BEST", 4);
    out[4] = DEF_BESTIARY_VERSION;
    memcpy(out + REC_TABLE, table, DEF_BESTIARY_SPECIES);
    out[5] = sum(out + REC_TABLE, DEF_BESTIARY_SPECIES);
    if (extra_flags != 0 || extra_network != 0) { /* none of it: the record stays as it always was */
        out[REC_EXTRA] = 'X';
        out[REC_EXTRA + 1] = '1';
        out[REC_EXTRA + 2] = extra_flags;
        for (i = 0; i < 4; i++) {
            out[REC_EXTRA + 4 + i] = (unsigned char)(extra_network >> (8 * i));
        }
    }
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
    if (in[REC_EXTRA] == 'X' && in[REC_EXTRA + 1] == '1') {
        extra_flags = in[REC_EXTRA + 2];
        for (i = 0; i < 4; i++) {
            extra_network |= (unsigned int)in[REC_EXTRA + 4 + i] << (8 * i);
        }
    }
    port_log("bestiary: the slot's record read: %d species known, flags %d, network 0x%X", known, extra_flags,
             extra_network);
}

/* DW3_BESTIARY turns only this layer on in a script: a card test needs the record's extra steps without the combat
 * layers (DW3_DEFINITIVE), which change the battles the script's hashes cover. */
int def_bestiary_active(void) {
    return !port_script_active || getenv("DW3_DEFINITIVE") != NULL || getenv("DW3_BESTIARY") != NULL;
}

unsigned char *def_bestiary_buffer(void) {
    return buffer;
}
