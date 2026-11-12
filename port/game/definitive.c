/* "Definitive" combat (.plan/tasks/definitive-combat-prd.md). Always on in the port, no mod option. Layer 0: the battle log;
 * layer 1: the text overrides; layer 2: Inspect, the hit sounds, the "enemy acts twice" test. The game's C calls the def_* hooks under #ifdef PC_PORT; everything else lives here. */
#include <stdio.h>
#include <stdlib.h>

#include "cdload.h"
#include "common.h"
#include "fightstg.h"
#include "sound.h"
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

    port_hud_arm();
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

/* Layer 2: how the last damage the player dealt with an elemental technique met the target (0 none, 1 effective,
 * 2 neutral, 3 resisted); def_hit_feedback plays its sound when the hit's own sound does. */
static int hit_class;
static s32 hit_id;

static void classify(u8 side, s32 id) {
    RecordsTechnique *rec = &records_techniques[id - 1];

    hit_class = 0;
    if (side != 0 || rec->defense_stat < 2 || rec->defense_stat > 8) {
        return;
    }
    {
        int resist = fightstg_rules.stats[1].resists[rec->defense_stat - 2];

        hit_class = resist >= DEF_RESIST_MIN ? 3 : resist <= DEF_PROV_WEAK_MAX ? 1 : 2;
        hit_id = id;
    }
}

void def_hit_feedback(void) {
    static const s32 sound[4] = { 0, DEF_SND_EFFECTIVE, DEF_SND_NEUTRAL, DEF_SND_RESISTED };

    if (fightstg_action.side != 0 || hit_class == 0) {
        return;
    }
    sound_module.play(sound[hit_class]);
    if (log_on()) {
        LOG("hitsound class %d tech %d", hit_class, (int)hit_id);
    }
}

static s32 log_damage(u8 side, s32 id, s32 value, const char *what) {
    FightstgMember *target;

    classify(side, id);
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

/* ---- Inspect and the hit sounds (layer 2). Provisional rule until layer 3's def_weakness_mask: weak at DEF_PROV_WEAK_MAX
 * or below, resisted from DEF_RESIST_MIN, neutral between. */
extern const unsigned char def_str_Fire[], def_str_Water[], def_str_Ice[], def_str_Wind[], def_str_Thunder[],
    def_str_Machine[], def_str_Dark[], def_str_Weak[], def_str_Resist[], def_str_Neutral[];

/* "Enemy acts twice" (layer 2): with the command menu open, E the time left to the enemy's turn, Pe the delay its next
 * turn would take and P the one ours would (fightstg_events.get_delay kind 0: no draw, but it rewrites
 * fightstg_rules.stats, so only here, with the menu open and nothing being calculated), the enemy acts again before
 * we do when E + Pe < P. */
int def_enemy_acts_twice(void) {
    int e = fightstg_events.find(3);
    s32 left, enemy_next, own_next;

    if (e < 0) {
        return 0;
    }
    left = fightstg_events.events[e].delay;
    enemy_next = fightstg_events.get_delay(0x10, 0);
    own_next = fightstg_events.get_delay(0, 0);
    if (log_on()) {
        LOG("twice E %d Pe %d P %d -> %d", (int)left, (int)enemy_next, (int)own_next, left + enemy_next < own_next);
    }
    return left + enemy_next < own_next;
}

const unsigned char *def_twice_label(void) {
    extern const unsigned char def_str_Twice[];
    return def_str_Twice;
}

const unsigned char *def_inspect_element(int e) {
    static const unsigned char *const names[7] = { def_str_Fire, def_str_Water,   def_str_Ice,  def_str_Wind,
                                                    def_str_Thunder, def_str_Machine, def_str_Dark };
    return names[e];
}

const unsigned char *def_inspect_state(int resist, int *palette) {
    if (resist >= DEF_RESIST_MIN) {
        *palette = 7;
        return def_str_Resist;
    }
    if (resist <= DEF_PROV_WEAK_MAX) {
        *palette = 3;
        return def_str_Weak;
    }
    *palette = 0;
    return def_str_Neutral;
}
