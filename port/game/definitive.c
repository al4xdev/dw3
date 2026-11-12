/* "Definitive" combat (.plan/tasks/definitive-combat-prd.md). Always on in the port, no mod option. Layer 0 only so far:
 * the battle log. The game's C calls the def_* hooks under #ifdef PC_PORT; everything else lives here. */
#include <stdio.h>
#include <stdlib.h>

#include "cdload.h"
#include "common.h"
#include "fightstg.h"
#include "gamestate.h"
#include "records.h"

#include "port_runtime.h"

#include "definitive.h"

s32 fightstg_rules_get_damage(u8 side, s32 id);
s32 fightstg_rules_get_special_damage(u8 side, s32 id);

/* ---- DW3_BATTLE_LOG=<file>: one plain-text line per battle event, appended. Read once; unset = every hook is a no-op.
 * The line order is the game's own, so two runs with the same seed and inputs write the same file. */
static FILE *log_file;
static int log_armed;
static int log_seq;        /* events since the battle began */
static int log_turns[2];   /* turns taken per side, for the "end" line */
static int log_battle = -1;

static int log_on(void) {
    if (!log_armed) {
        const char *path = getenv("DW3_BATTLE_LOG");
        log_armed = 1;
        if (path != NULL && path[0] != '\0') {
            log_file = fopen(path, "a");
        }
    }
    return log_file != NULL;
}

#define LOG(...)                          \
    do {                                  \
        fprintf(log_file, __VA_ARGS__);   \
        fputc('\n', log_file);            \
        fflush(log_file);                 \
    } while (0)

void def_log_begin(void) {
    int i;

    if (!log_on()) {
        return;
    }
    log_seq = 0;
    log_turns[0] = log_turns[1] = 0;
    log_battle = records_state.battle;
    LOG("battle %d kind %d type %d", records_state.battle, records_state.battle_kind,
        fightstg_battle.state.type);
    for (i = 0; i < 3; i++) {
        FightstgMember *m = &fightstg_battle.state.members[0][i];
        if (m->digimon != 0) {
            LOG("  party %d digimon %d hp %d/%d mp %d/%d", i, m->digimon, m->hp, m->max_hp, m->mp, m->max_mp);
        }
    }
    for (i = 0; i < 3; i++) {
        FightstgMember *m = &fightstg_battle.state.members[1][i];
        if (m->digimon != 0) {
            LOG("  enemy %d digimon %d level %d hp %d/%d mp %d scale %d", i, m->digimon,
                records_state.enemies[i].level, m->hp, m->max_hp, m->mp, records_state.enemies[i].stat_scale);
        }
    }
}

void def_log_turn(int side) {
    if (!log_on()) {
        return;
    }
    log_turns[side != 0]++;
    LOG("turn %d side %d n %d member %d", ++log_seq, side != 0, log_turns[side != 0],
        (int)fightstg_battle.state.current[side != 0]);
}

/* kind: for the party the command (0 attack, 1 escape, 2 digivolve, 3 item, 4 technique, 5 switch, 6 team tech); for
 * the enemy the action (1 basic attack, >1 technique ID, <0 escape/other). arg: the technique or item. */
void def_log_action(int side, int kind, int arg) {
    int tech = -1;

    if (!log_on()) {
        return;
    }
    if (side == 0) {
        if (kind == 4) {
            tech = arg;
        }
    } else if (kind > 1) {
        tech = kind;
    }
    if (tech >= 1) {
        RecordsTechnique *rec = &records_techniques[tech - 1];
        LOG("action side %d kind %d arg %d tech %d class %d defstat %d power %d mp %d", side != 0, kind, arg, tech,
            rec->element, rec->defense_stat, rec->power, rec->mp_cost);
    } else {
        LOG("action side %d kind %d arg %d", side != 0, kind, arg);
    }
}

static s32 log_damage(u8 side, s32 id, s32 value, const char *what) {
    FightstgMember *target;

    if (log_on()) {
        target = &fightstg_battle.state.members[side == 0][fightstg_battle.state.current[side == 0]];
        LOG("damage %s side %d tech %d value %d target_hp %d -> %d", what, side != 0, id, value, target->hp,
            target->hp > value ? target->hp - value : 0);
    }
    return value;
}

/* The rules table's get_damage / get_special_damage (fightstg_rules in fightstg_8008D3B4.c). Pass-through: the same
 * calls, in the same order, so the random stream is untouched. */
s32 def_get_damage(u8 side, s32 id) {
    return log_damage(side, id, fightstg_rules_get_damage(side, id), "phys");
}

s32 def_get_special_damage(u8 side, s32 id) {
    return log_damage(side, id, fightstg_rules_get_special_damage(side, id), "magic");
}

void def_log_end(int result) {
    int i;

    if (!log_on()) {
        return;
    }
    LOG("end battle %d result %d party_turns %d enemy_turns %d", log_battle, result, log_turns[0], log_turns[1]);
    for (i = 0; i < 3; i++) {
        FightstgMember *m = &fightstg_battle.state.members[0][i];
        if (m->digimon != 0) {
            LOG("  party %d hp %d/%d", i, m->hp, m->max_hp);
        }
    }
}

/* ---- Text overrides (layer 1). font_get_entry only gets the file's buffer, so the file is found by looking the buffer
 * up in the CD module's loaded entries (no load, no side effect). Only the English sets (records_language 1, 2): the
 * other languages keep the disc's text. Returns the replacement entry or NULL. */
void *def_text_override(void *data, s32 i) {
    int e, k;

    if ((records_language != 1 && records_language != 2) || data == NULL) {
        return NULL;
    }
    for (e = 0; e < 64; e++) {
        const CdloadEntry *ent = &cdload_module.entries[e];
        if (ent->state != 3 || ent->buffer != data) {
            continue;
        }
        for (k = 0; k < def_text_override_count; k++) {
            if (def_text_overrides[k].file + records_language == ent->id && def_text_overrides[k].index == i) {
                {
                    static unsigned char seen[512]; /* one line per entry, not per frame it is drawn */
                    if (k < 512 && !seen[k]) {
                        seen[k] = 1;
                        port_log("text override: file 0x%X entry %d", ent->id, i);
                    }
                }
                return (void *)def_text_overrides[k].bytes;
            }
        }
        return NULL;
    }
    return NULL;
}
