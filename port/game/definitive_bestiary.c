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
 * REC_EXTRA "X1", the flags (DEF_SAVE_HARD), a byte unused, the Guardromon network's mask (4 bytes, low first), then the
 * hunt's region and chain (PRD 4 layer A), then the Hunt Materials, a byte each (layer B), then the Legendary
 * Replicas held, a bit each, low byte first (layer G; the PRD's budget gives the rest of the 48 bytes to the later
 * layers). A record without the "X1" (an older save) reads as none of it, and one written before a layer existed reads
 * its bytes as 0. */
#define REC_EXTRA 0xD0

static unsigned char extra_flags;
static unsigned int extra_network;
static unsigned char extra_region, extra_chain;
static unsigned char extra_materials[DEF_HUNT_MATERIALS];
static unsigned short extra_replicas; /* DEF_REPLICAS bits: two bytes of the record */
/* The second record (PRD 4 layer B): "HUNT", version, XOR checksum of the rest, a byte unused, the rare items' counts,
 * then the bits of the ones ever held (the bestiary's collection); the rest of the 0x100 is the later layers'. */
#define REC2_RARES 8
#define REC2_SEEN (REC2_RARES + DEF_HUNT_RARES)
static unsigned char rares[DEF_HUNT_RARES];
static unsigned char rares_seen[DEF_HUNT_RARES / 8];
static unsigned char buffer2[DEF_RECORD2_SIZE];
/* PRD 4 layer H, after the rares' bits: the DRI challenges unlocked, a bit each (2 bytes, low first), then each one's best
 * stage, 2 bits each (4 bytes, low first). The record's version stays 1: one written before the layer reads them as 0, and
 * the checksum already covered these bytes as zeros. */
#define REC2_DRI (REC2_SEEN + DEF_HUNT_RARES / 8)
typedef char rec2_dri_fits[DEF_DRI_CHALLENGES <= 16 && REC2_DRI + 6 <= DEF_RECORD2_SIZE ? 1 : -1];
static unsigned short dri_unlocked;
static unsigned int dri_best;

static unsigned char table[DEF_BESTIARY_SPECIES];
static unsigned char buffer[DEF_BESTIARY_RECORD_SIZE];

void def_bestiary_reset(void) {
    memset(table, 0, sizeof(table));
    extra_flags = 0;
    extra_network = 0;
    extra_region = 0;
    extra_chain = 0;
    memset(extra_materials, 0, sizeof(extra_materials));
    extra_replicas = 0;
    memset(rares, 0, sizeof(rares));
    memset(rares_seen, 0, sizeof(rares_seen));
    dri_unlocked = 0;
    dri_best = 0;
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

void def_record_hunt(int *region, int *chain) {
    *region = extra_region;
    *chain = extra_chain;
}

void def_record_set_hunt(int region, int chain) {
    extra_region = (unsigned char)region;
    extra_chain = (unsigned char)(chain < 0 ? 0 : chain > 255 ? 255 : chain);
}

int def_record_material(int material) {
    return material >= 0 && material < DEF_HUNT_MATERIALS ? extra_materials[material] : 0;
}

void def_record_material_add(int material, int n) {
    int v;

    if (material < 0 || material >= DEF_HUNT_MATERIALS) {
        return;
    }
    v = extra_materials[material] + n;
    extra_materials[material] = (unsigned char)(v < 0 ? 0 : v > DEF_HUNT_MATERIAL_CAP ? DEF_HUNT_MATERIAL_CAP : v);
}

int def_record_replica(int r) {
    return r >= 0 && r < DEF_REPLICAS && ((extra_replicas >> r) & 1) != 0;
}

void def_record_replica_set(int r, int on) {
    if (r >= 0 && r < DEF_REPLICAS) {
        extra_replicas = (unsigned short)(on ? extra_replicas | (1u << r) : extra_replicas & ~(1u << r));
    }
}

/* 1 when anything of the slot's other port state is set: the record (and its "X1" part) is written only then. */
static int extra_any(void) {
    int i;

    for (i = 0; i < DEF_HUNT_MATERIALS; i++) {
        if (extra_materials[i] != 0) {
            return 1;
        }
    }
    return extra_flags != 0 || extra_network != 0 || extra_region != 0 || extra_chain != 0 || extra_replicas != 0;
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
    PORT_STATE_VAR(s, extra_region);
    PORT_STATE_VAR(s, extra_chain);
    PORT_STATE_VAR(s, extra_materials);
    PORT_STATE_VAR(s, extra_replicas);
    PORT_STATE_VAR(s, rares);
    PORT_STATE_VAR(s, rares_seen);
    PORT_STATE_VAR(s, dri_unlocked);
    PORT_STATE_VAR(s, dri_best);
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
    if (i == DEF_BESTIARY_SPECIES && !extra_any()) {
        return;
    }
    port_log("bestiary: the slot's record written");
    memcpy(out, "BEST", 4);
    out[4] = DEF_BESTIARY_VERSION;
    memcpy(out + REC_TABLE, table, DEF_BESTIARY_SPECIES);
    out[5] = sum(out + REC_TABLE, DEF_BESTIARY_SPECIES);
    if (extra_any()) { /* none of it: the record stays as it always was */
        out[REC_EXTRA] = 'X';
        out[REC_EXTRA + 1] = '1';
        out[REC_EXTRA + 2] = extra_flags;
        for (i = 0; i < 4; i++) {
            out[REC_EXTRA + 4 + i] = (unsigned char)(extra_network >> (8 * i));
        }
        out[REC_EXTRA + 8] = extra_region;
        out[REC_EXTRA + 9] = extra_chain;
        memcpy(out + REC_EXTRA + 10, extra_materials, DEF_HUNT_MATERIALS);
        out[REC_EXTRA + 26] = (unsigned char)extra_replicas;
        out[REC_EXTRA + 27] = (unsigned char)(extra_replicas >> 8);
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
        extra_region = in[REC_EXTRA + 8];
        extra_chain = in[REC_EXTRA + 9];
        for (i = 0; i < DEF_HUNT_MATERIALS; i++) { /* no checksum covers it: a count past the cap reads as the cap */
            extra_materials[i] = in[REC_EXTRA + 10 + i] > DEF_HUNT_MATERIAL_CAP ? DEF_HUNT_MATERIAL_CAP
                                                                               : in[REC_EXTRA + 10 + i];
        }
        extra_replicas = (unsigned short)(in[REC_EXTRA + 26] | in[REC_EXTRA + 27] << 8);
    }
    port_log("bestiary: the slot's record read: %d species known, flags %d, network 0x%X", known, extra_flags,
             extra_network);
}

int def_record_rare(int rare) {
    return rare >= 0 && rare < DEF_HUNT_RARES ? rares[rare] : 0;
}

int def_record_rare_seen(int rare) {
    return rare >= 0 && rare < DEF_HUNT_RARES && ((rares_seen[rare / 8] >> (rare % 8)) & 1);
}

void def_record_rare_add(int rare, int n) {
    int v;

    if (rare < 0 || rare >= DEF_HUNT_RARES) {
        return;
    }
    v = rares[rare] + n;
    rares[rare] = (unsigned char)(v < 0 ? 0 : v > DEF_HUNT_RARE_CAP ? DEF_HUNT_RARE_CAP : v);
    if (n > 0) {
        rares_seen[rare / 8] |= 1 << (rare % 8);
    }
}

void def_record2_write(unsigned char *out) {
    unsigned int i;

    memset(out, 0, DEF_RECORD2_SIZE);
    for (i = 0; i < sizeof(rares_seen) && rares_seen[i] == 0; i++) {
    }
    if (i == sizeof(rares_seen) && dri_unlocked == 0 && dri_best == 0) {
        return; /* no rare ever held (a count needs one) and no DRI challenge: the card stays as it was */
    }
    memcpy(out, "HUNT", 4);
    out[4] = DEF_RECORD2_VERSION;
    memcpy(out + REC2_RARES, rares, sizeof(rares));
    memcpy(out + REC2_SEEN, rares_seen, sizeof(rares_seen));
    for (i = 0; i < 2; i++) {
        out[REC2_DRI + i] = (unsigned char)(dri_unlocked >> (8 * i));
    }
    for (i = 0; i < 4; i++) {
        out[REC2_DRI + 2 + i] = (unsigned char)(dri_best >> (8 * i));
    }
    out[5] = sum(out + REC2_RARES, DEF_RECORD2_SIZE - REC2_RARES);
    port_log("hunt: the slot's second record written");
}

void def_record2_read(const unsigned char *in) {
    int i;

    memset(rares, 0, sizeof(rares));
    memset(rares_seen, 0, sizeof(rares_seen));
    dri_unlocked = 0;
    dri_best = 0;
    if (in == NULL || memcmp(in, "HUNT", 4) != 0 || in[4] != DEF_RECORD2_VERSION ||
        in[5] != sum(in + REC2_RARES, DEF_RECORD2_SIZE - REC2_RARES)) {
        return;
    }
    for (i = 0; i < DEF_HUNT_RARES; i++) {
        rares[i] = in[REC2_RARES + i] > DEF_HUNT_RARE_CAP ? DEF_HUNT_RARE_CAP : in[REC2_RARES + i];
    }
    memcpy(rares_seen, in + REC2_SEEN, sizeof(rares_seen));
    dri_unlocked = (unsigned short)(in[REC2_DRI] | in[REC2_DRI + 1] << 8);
    for (i = 0; i < 4; i++) {
        dri_best |= (unsigned int)in[REC2_DRI + 2 + i] << (8 * i);
    }
    port_log("hunt: the slot's second record read");
}

int def_record_dri_unlocked(int c) {
    return c >= 0 && c < DEF_DRI_CHALLENGES && ((dri_unlocked >> c) & 1);
}

void def_record_dri_unlock(int c) {
    if (c >= 0 && c < DEF_DRI_CHALLENGES) {
        dri_unlocked = (unsigned short)(dri_unlocked | 1u << c);
    }
}

int def_record_dri_best(int c) {
    return c >= 0 && c < DEF_DRI_CHALLENGES ? (int)((dri_best >> (2 * c)) & 3) : 0;
}

void def_record_dri_raise_best(int c, int stage) {
    if (c < 0 || c >= DEF_DRI_CHALLENGES || stage <= def_record_dri_best(c)) {
        return;
    }
    stage = stage > 3 ? 3 : stage;
    dri_best = (dri_best & ~(3u << (2 * c))) | (unsigned int)stage << (2 * c);
}

unsigned char *def_record2_buffer(void) {
    return buffer2;
}

/* DW3_BESTIARY turns only this layer on in a script: a card test needs the record's extra steps without the combat
 * layers (DW3_DEFINITIVE), which change the battles the script's hashes cover. */
int def_bestiary_active(void) {
    return !port_script_active || getenv("DW3_DEFINITIVE") != NULL || getenv("DW3_BESTIARY") != NULL;
}

unsigned char *def_bestiary_buffer(void) {
    return buffer;
}
