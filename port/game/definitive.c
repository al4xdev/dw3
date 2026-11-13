/* "Definitive" combat (.plan/tasks/definitive-combat-prd.md). Always on in the port, no mod option. Layer 0: the battle log;
 * layer 1: the text overrides; layer 2: Inspect, the hit sounds, the "enemy acts twice" test. The game's C calls the def_* hooks under #ifdef PC_PORT; everything else lives here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cdload.h"
#include "common.h"
#include "fightstg.h"
#include "sound.h"
#include "gamestate.h"
#include "records.h"
#include "pad.h"

#include "port_runtime.h"
#include "savestate.h"

#include "definitive.h"

s32 fightstg_rules_get_damage(u8 side, s32 id);
s32 fightstg_rules_get_special_damage(u8 side, s32 id);

/* ---- DW3_BATTLE_LOG=<file>: one plain-text line per battle event, appended. Read once; unset = every hook is a no-op.
 * The line order is the game's own, so two runs with the same seed and inputs write the same file. */
/* Layer 5: the terms of the last multiplier, for the log line of the damage it made (0 when none applied). */
static struct {
    int weak, open, resisted, affinity, mult;
} terms;
/* OVERFINISH: a blow that knocks the enemy out and earns an extra action leaves it lying there for that action (its
 * knock-out event waits behind the party's turn), and it can be hit again. Kept as a reward for chaining a weakness
 * into the kill: such a blow shows the OVERFINISH! callout and its damage pays Bits with the battle's
 * (DEF_OVERFINISH_BITS_PCT). `overfinish_hit`: the blow being shown is one; `overfinish_damage`: the battle's total. */
static int overfinish_hit;
static s32 overfinish_damage;
static DefChain chain; /* layer 4: stagger, window, free swap (definitive_mask.c) */
static short resist_mod[2][3][7]; /* layer 6: per side (0 party, 1 enemy) and slot, what Resist Down took off each element */
static unsigned char resist_turns[2][3]; /* and the turns of that side it still lasts */
static int baton_paid; /* Baton Pass was chosen in the switch menu (its MP is paid there) */

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
    def_mp_hook();
    def_chain_reset(&chain);
    def_callout_show(DEF_CALLOUT_NONE, 0);
    def_hunt_reward_done(); /* PRD 4 layer B: a reward a results screen never showed does not carry over */
    if (def_hunt_active()) {
        def_callout_show(DEF_CALLOUT_RARE, 1); /* PRD 4 layer A */
    }
    overfinish_hit = 0;
    overfinish_damage = 0;
    memset(resist_mod, 0, sizeof(resist_mod));
    memset(resist_turns, 0, sizeof(resist_turns));
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
static int hit_elem; /* its element index, valid with hit_class */
static s32 hit_id;

/* Layer 3: the species' Dark mark (the row table), 0 for a species with no row. */
static int dark_mark(int species) {
    int i;

    for (i = 0; i < def_enemy_row_count; i++) {
        if (def_enemy_rows[i].id == species) {
            return def_enemy_rows[i].dark;
        }
    }
    return 0;
}

/* Element `e` against a target whose resists (modifiers included) are `resists`: 1 weak, 2 neutral, 3 resisted. */
static int target_class(int e, const short *resists, int species) {
    if (def_resisted(resists[e])) {
        return 3;
    }
    return (def_weakness_mask(resists, dark_mark(species)) >> e) & 1 ? 1 : 2;
}

static void classify(u8 side, s32 id) {
    RecordsTechnique *rec = &records_techniques[id - 1];

    hit_class = 0;
    overfinish_hit = 0;
    if (side != 0 || rec->defense_stat < 2 || rec->defense_stat > 8) {
        return;
    }
    hit_class = target_class(rec->defense_stat - 2, fightstg_rules.stats[1].resists,
                             fightstg_battle.state.members[1][fightstg_battle.state.current[1]].digimon);
    hit_elem = rec->defense_stat - 2;
    hit_id = id;
}

/* Hard Mode (the save's DEF_SAVE_HARD flag, chosen on New Game): the party is two Digimon. The third of the starting
 * pack is owned but stays out (the lab keeps it), and the third slot takes nobody: this empties it wherever a party
 * was just set (the starting pack, a loaded field), and the lab refuses the slot (def_hard_slot_closed). */
void def_hard_party_fix(void) {
    if (def_save_flag(DEF_SAVE_HARD) && gamestate_data.party[2] >= 0) {
        gamestate_data.party[2] = -1;
    }
}

int def_hard_slot_closed(int slot) {
    return slot >= 2 && def_save_flag(DEF_SAVE_HARD);
}

/* New Game's choice, kept past the new game's own reset of the save's port state (gamestate_init_records). */
static int new_game_hard;

void def_new_game_difficulty(int hard) {
    new_game_hard = hard;
    def_save_flag_set(DEF_SAVE_HARD, hard);
}

void def_new_game_apply(void) {
    def_save_flag_set(DEF_SAVE_HARD, new_game_hard);
    new_game_hard = 0;
}

/* The Bits the battle's OVERFINISH blows earned, given once (the results screen adds them to the battle's money). */
s32 def_overfinish_take_bits(void) {
    s32 bits = def_on() ? overfinish_damage * DEF_OVERFINISH_BITS_PCT / 100 : 0;

    overfinish_damage = 0;
    return bits;
}

void def_hit_feedback(void) {
    static const s32 sound[4] = { 0, DEF_SND_EFFECTIVE, DEF_SND_NEUTRAL, DEF_SND_RESISTED };

    if (fightstg_action.side != 0) {
        return;
    }
    if (overfinish_hit) { /* whatever the blow's element, and over WEAK!: the word of the moment is this one */
        sound_module.play(DEF_SND_EFFECTIVE);
        def_callout_show(DEF_CALLOUT_OVERFINISH, 1);
        return;
    }
    if (hit_class == 0) {
        return;
    }
    sound_module.play(sound[hit_class]);
    def_callout_show(hit_class == 1 ? DEF_CALLOUT_WEAK : hit_class == 3 ? DEF_CALLOUT_RESIST : DEF_CALLOUT_NONE, 1);
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
        if (terms.mult != 0) {
            LOG("  mult %d weak %d open %d resisted %d affinity %d", terms.mult, terms.weak, terms.open, terms.resisted,
                terms.affinity);
        }
    }
    return value;
}

/* The rules table's get_damage / get_special_damage (fightstg_rules in fightstg_8008D3B4.c). Pass-through: the same
 * calls, in the same order, so the random stream is untouched. */
s32 def_get_damage(u8 side, s32 id) {
    terms.mult = 0;
    return log_damage(side, id, fightstg_rules_get_damage(side, id), "phys");
}

s32 def_get_special_damage(u8 side, s32 id) {
    terms.mult = 0;
    return log_damage(side, id, fightstg_rules_get_special_damage(side, id), "magic");
}

void def_log_end(int result) {
    int i;

    def_hunt_battle_end(result); /* PRD 4 layer A: counted whether or not the log is on */
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

/* ---- Inspect and the hit sounds (layer 2), by layer 3's rule (def_weakness_mask, def_resisted). */
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

/* The font's coloured picture of each element (extra glyphs `01 xx` of the 14-pixel font, drawn with palette 0): flame,
 * drop, crystal, wing, bolt, gear, dark star. The same picture stands for the element on every screen. */
const unsigned char *def_element_icon(int e) {
    static const unsigned char icons[7][3] = { { 1, 0x2D, 0 }, { 1, 0x2E, 0 }, { 1, 0x30, 0 }, { 1, 0x31, 0 },
                                               { 1, 0x33, 0 }, { 1, 0x35, 0 }, { 1, 0x36, 0 } };
    return icons[e];
}

/* The five families the bestiary, Inspect and the HUD's Resist Down arrows show the seven elements as: Water with Ice
 * and Thunder with Machine, the pairs the multiplier's affinity already sums, each named by its first. Only the
 * screens group them: the rules still read seven resists, and the technique list and the status screens keep the
 * seven pictures (which of a pair a species is weak to is the player's to find). */
static const signed char family_elems[DEF_FAMILIES][2] = { { 0, -1 }, { 1, 2 }, { 3, -1 }, { 4, 5 }, { 6, -1 } };

int def_element_family(int e) {
    static const unsigned char family[7] = { 0, 1, 1, 2, 3, 3, 4 };

    return family[e];
}

/* The family's pictures: its first element's, and with `second` its other's (empty text for a family of one). */
const unsigned char *def_family_icon(int f, int second) {
    static const unsigned char none[1] = { 0 };
    int e = family_elems[f][second != 0];

    return e >= 0 ? def_element_icon(e) : none;
}

const unsigned char *def_family_name(int f) {
    return def_inspect_element(family_elems[f][0]);
}

/* def_resist_down_mask by family: a lowered element sets its family's bit (Ice the Water's, Machine the Thunder's). */
int def_resist_down_families(int side, int slot) {
    int mask = def_resist_down_mask(side, slot);
    int out = 0;
    int e;

    for (e = 0; e < 7; e++) {
        if ((mask >> e) & 1) {
            out |= 1 << def_element_family(e);
        }
    }
    return out;
}

int def_tech_element(int tech) {
    int stat = tech >= 1 ? records_techniques[tech - 1].defense_stat : 0;

    return stat >= 2 && stat <= 8 ? stat - 2 : -1;
}

/* The label and the 14-pixel font's palette of a target_class (0: not learnt yet): the weakness is the line to find, so
 * it gets the warm colour, a resist the cold one, and what says nothing the grey. */
static const unsigned char *state_label(int cls, int *palette) {
    extern const unsigned char def_str_Unknown[];

    switch (cls) {
    case 0:
        *palette = 7;
        return def_str_Unknown;
    case 3:
        *palette = 1;
        return def_str_Resist;
    case 1:
        *palette = 3;
        return def_str_Weak;
    default:
        *palette = 7;
        return def_str_Neutral;
    }
}

const unsigned char *def_inspect_state(const short *resists, int e, int *palette) {
    int species = fightstg_battle.state.members[1][fightstg_battle.state.current[1]].digimon;

    if (!((def_bestiary_known(def_bestiary_index(species)) >> e) & 1)) { /* not learnt yet: Libra or a hit on it tells */
        return state_label(0, palette);
    }
    return state_label(target_class(e, resists, species), palette);
}

const unsigned char *def_bestiary_state(FightstgEnemyRecord *rec, int index, int e, int *palette) {
    if (!((def_bestiary_known(index) >> e) & 1)) {
        return state_label(0, palette);
    }
    def_enemy_override(rec); /* the field reads SDIGIEDT itself: the record may not have gone through the battle's get */
    return state_label(target_class(e, rec->resists, rec->id), palette);
}

/* A family's line, from what is learnt of its elements (`known`: the bestiary's bits): Weak as soon as one of them is
 * known to be weak (the line to find); with both known, Resist only when both resist, else neutral; and ??? while a
 * part is not learnt and no weakness is. A hit now teaches a pair whole; only a save from before knows half of one. */
static const unsigned char *family_state(int known, const short *resists, int species, int f, int *palette) {
    int k, e, cls, all_known = 1, all_resist = 1;

    for (k = 0; k < 2 && (e = family_elems[f][k]) >= 0; k++) {
        if (!((known >> e) & 1)) {
            all_known = 0;
            continue;
        }
        cls = target_class(e, resists, species);
        if (cls == 1) {
            return state_label(1, palette);
        }
        all_resist &= cls == 3;
    }
    return state_label(!all_known ? 0 : all_resist ? 3 : 2, palette);
}

const unsigned char *def_inspect_family_state(const short *resists, int f, int *palette) {
    int species = fightstg_battle.state.members[1][fightstg_battle.state.current[1]].digimon;

    return family_state(def_bestiary_known(def_bestiary_index(species)), resists, species, f, palette);
}

const unsigned char *def_bestiary_family_state(FightstgEnemyRecord *rec, int index, int f, int *palette) {
    def_enemy_override(rec); /* as def_bestiary_state */
    return family_state(def_bestiary_known(index), rec->resists, rec->id, f, palette);
}

/* ---- Layer 3: the enemy table. Applied each time the game asks for the record (idempotent), so it holds after a reload
 * of SDIGIEDT and a load state alike. */
void def_enemy_override(FightstgEnemyRecord *rec) {
    int i, e;

    if (!def_on()) { /* a replay script keeps the disc's enemies (its state is the emulator's) */
        return;
    }

    for (i = 0; i < def_enemy_row_count; i++) {
        if (def_enemy_rows[i].id == rec->id) {
            for (e = 0; e < 7; e++) {
                rec->resists[e] = def_enemy_rows[i].resists[e];
            }
            return;
        }
    }
}

/* ---- Layer 4: turns, stagger, the chain, the free swap (the rules are in definitive_mask.c's DefChain). Everything here
 * is the game's side of it. A replay script (shared with the emulator) keeps the original rules: def_on is 0 under
 * --script unless DW3_DEFINITIVE is set (the tests that drive the new rules by script). */

int def_on(void) {
    static int on = -1;

    if (on < 0) {
        on = !port_script_active || getenv("DW3_DEFINITIVE") != NULL;
    }
    return on;
}

extern s32 wfightmn_update_idle_anim(u8 side, s32 amount);

static unsigned int def_evo_seen_bits[DEF_EVO_SEEN_WORDS];

void def_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, chain);
    PORT_STATE_VAR(s, baton_paid);
    PORT_STATE_VAR(s, resist_mod);
    PORT_STATE_VAR(s, resist_turns);
    def_bestiary_savestate(s);
    def_hunt_savestate(s);
    PORT_STATE_VAR(s, hit_class);
    PORT_STATE_VAR(s, hit_elem);
    PORT_STATE_VAR(s, hit_id);
    PORT_STATE_VAR(s, overfinish_hit);
    PORT_STATE_VAR(s, overfinish_damage);
    PORT_STATE_VAR(s, records_state.gauges);
    PORT_STATE_VAR(s, def_evo_seen_bits);
}

static int target_slot(void) {
    return fightstg_battle.state.current[1];
}

/* The current target is open: its pose is the tired one, and layer 5 gives the player's next blow its bonus. */
int def_target_staggered(void) {
    return def_on() && chain.staggered[target_slot() % 3];
}

/* A party turn begins (wfightmn_party_turn): an extra action keeps the window, a normal one starts a new one. */
void def_party_turn_begins(void) {
    int m;

    if (!def_on()) {
        return;
    }
    m = fightstg_battle.state.current[0] % 3;
    if (resist_turns[0][m] != 0 && --resist_turns[0][m] == 0) { /* an enemy's Resist Down on us wears off */
        memset(resist_mod[0][m], 0, sizeof(resist_mod[0][0]));
    }
    def_chain_party_turn(&chain);
    if (log_on()) {
        LOG("window %s", chain.cur_extra ? "extra action" : "new (normal action)");
    }
}

/* The player's action was resolved on the current target (wfightmn, before the pose is set): `value` > 0 is a hit. */
void def_action_resolved(u8 side, s32 id, s32 value) {
    int target = target_slot();
    int weak = hit_class == 1 && hit_id == id;
    int was;

    if (!def_on() || side != 0) {
        return;
    }
    if (value > 0 && fightstg_battle.state.members[1][target].hp <= 0) { /* it was already down: an OVERFINISH */
        overfinish_hit = 1;
        overfinish_damage += value;
        if (log_on()) {
            LOG("overfinish damage %d total %d", (int)value, (int)overfinish_damage);
        }
    }
    was = chain.staggered[target];
    def_chain_player_hit(&chain, target, value > 0, weak ? hit_elem : -1, weak);
    if (value > 0 && hit_id == id && (hit_class == 1 || hit_class == 3)) { /* a weakness or a resistance found out */
        def_bestiary_discover(def_bestiary_index(fightstg_battle.state.members[1][target].digimon),
                              def_family_bits(hit_elem)); /* a pair is one affinity: both of it are learnt */
    }
    if (log_on() && value > 0) {
        LOG("hit target %d weak %d stagger %d -> %d extra_pending %d", target, weak, was, chain.staggered[target],
            chain.extra_pending);
    }
}

/* An enemy's turn begins (wfightmn_run_events, event 3): it closes. */
void def_enemy_turn_begins(void) {
    int target = target_slot();

    if (!def_on()) {
        return;
    }
    if (resist_turns[1][target % 3] != 0 && --resist_turns[1][target % 3] == 0) { /* the lowered resists wear off */
        memset(resist_mod[1][target % 3], 0, sizeof(resist_mod[1][0]));
        if (log_on()) {
            LOG("resist down worn off slot %d", target);
        }
    }
    if (chain.staggered[target]) {
        def_chain_enemy_acts(&chain, target);
        wfightmn_update_idle_anim(0x10, 0); /* back to its normal pose */
        if (log_on()) {
            LOG("stagger closed by the enemy's turn (slot %d)", target);
        }
    }
}

/* About to take the next event of the queue (wfightmn_run_events): an owed extra action makes the party's turn the next. */
void def_events_before_take(void) {
    int e;

    if (!def_on() || !def_chain_take_extra(&chain)) {
        return;
    }
    e = fightstg_events.find(2);
    if (e >= 0) {
        fightstg_events.events[e].delay = 0;
        if (log_on()) {
            LOG("extra action granted");
        }
    } else {
        chain.next_extra = 0;
    }
}

void def_baton_paid(void) {
    baton_paid = 1;
}

const unsigned char *def_baton_label(void) {
    extern const unsigned char def_str_BatonPass[];
    return def_str_BatonPass;
}

/* The player chose Tag (party command 5). Owner's rule (2026-11-12): only Baton Pass, offered only while the enemy is
 * open (and once a window), keeps the turn; a plain Switch spends it, unless Relay made the next swap free. */
void def_swap_chosen(void) {
    if (!def_on()) {
        return;
    }
    if (baton_paid) {
        baton_paid = 0;
        def_chain_swap_chosen(&chain, target_slot()); /* spends the window's swap, owes the extra action */
        if (log_on()) {
            LOG("baton pass");
        }
        return;
    }
    if (chain.relay) {
        def_chain_swap_chosen(&chain, target_slot());
        if (log_on()) {
            LOG("relay swap");
        }
    }
}

/* Baton Pass is offered: the enemy is open and this window's swap is unspent (the HUD's Tag picture shows it). */
int def_swap_free(void) {
    int t = target_slot() % 3;

    return def_on() && chain.staggered[t] && !chain.swap_used;
}

/* The pose of a side (wfightmn_update_idle_anim): the tired one for an open enemy, never for the player (HP no longer
 * decides it); the final boss's is its phase or an open one. -1: the original rule holds. */
int def_idle_pose(u8 side) {
    if (!def_on()) {
        return -1;
    }
    if (side != 0x10) {
        return 0;
    }
    if (fightstg_battle.state.type == 6) {
        return fightstg_battle.state.final_phase != 0 || chain.staggered[target_slot() % 3];
    }
    return chain.staggered[target_slot() % 3];
}

/* ---- Layer 6: the remade techniques. Libra (202, was Speed Up) learns the target's weaknesses, Relay (203, was Mega Boost)
 * makes the next swap free, Resist Down / Mega Break (207, 208, were Slow Down / Mega Break) lower the elements the
 * user is best at on the target for a few of its turns; on a boss that blocked the speed-down they replace
 * (records_state.blocked[10]) they are scaled by DEF_BOSS_DEBUFF_PCT, like every stat debuff (PRD 2, layer A). The effects run in fightstg_tech_update (class 5), before the original's own. */

void def_apply_resist_mod(int side, int member, FightstgStats *out) {
    int e;

    side = side != 0;
    if (!def_on() || member < 0 || member > 2 || resist_turns[side][member] == 0) {
        return;
    }
    for (e = 0; e < 7; e++) {
        out->resists[e] += resist_mod[side][member][e];
        if (out->resists[e] < 1) {
            out->resists[e] = 1; /* the old formulas divide by it */
        }
    }
}

/* Bits 0..6 of the elements whose resistance is currently lowered on `side` (0 party, 1 enemy), `slot` (0..2). */
int def_resist_down_mask(int side, int slot) {
    int e, mask = 0;

    side = side != 0;
    if (!def_on() || slot < 0 || resist_turns[side][slot % 3] == 0) {
        return 0;
    }
    slot %= 3;
    for (e = 0; e < 7; e++) {
        if (resist_mod[side][slot][e] < 0) {
            mask |= 1 << e;
        }
    }
    return mask;
}

/* 0: not one of the four; 1: done, `*msg` the message (0 Libra, 1 Relay, 2 resist lowered); 2: no effect (immune). */
int def_tech_effect(u8 side, int tech, int *msg) {
    int enemy = side != 0;
    int victim = enemy ? 0 : 1;                                 /* the side whose resists go down */
    int slot = fightstg_battle.state.current[victim] % 3;
    int e, n, best, k, delta;
    short taken[7] = { 0, 0, 0, 0, 0, 0, 0 };
    FightstgStats *me;

    if (!def_on() || (tech != 202 && tech != 203 && tech != 207 && tech != 208)) {
        return 0;
    }
    if (!enemy && tech == 202) {
        def_bestiary_discover(def_bestiary_index(fightstg_battle.state.members[1][slot].digimon), 0x7F);
        *msg = 0;
        if (log_on()) {
            LOG("libra slot %d", slot);
        }
        return 1;
    }
    if (!enemy && tech == 203) {
        def_chain_relay_set(&chain);
        *msg = 1;
        return 1;
    }
    /* Resist Down / Mega Break, and in an enemy's hands all four (owner's choice: Libra and Relay mean nothing to an
     * enemy): the target's resists of the user's best families go down, both elements of a pair (owner, 2026-11-13: a
     * pair is one affinity). The best is the user's highest affinity as the multiplier counts it (def_affinity: a pair's
     * sum), ties by the family order. On the player they are its affinities. */
    me = fightstg_rules.get_stats(side, 1, fightstg_battle.state.current[enemy]);
    delta = enemy ? (tech == 208 ? DEF_BREAK_MEGA_DELTA : DEF_BREAK_SLOW_DELTA)
                  : def_debuff_scale(10, tech == 208 ? DEF_BREAK_MEGA_DELTA : DEF_BREAK_SLOW_DELTA);
    for (n = tech == 208 ? 2 : 1, k = 0; k < n; k++) {
        best = -1;
        for (e = 0; e < 7; e++) {
            if (!taken[e] && def_affinity(e, me->resists) > 0 &&
                (best < 0 || def_affinity(e, me->resists) > def_affinity(best, me->resists))) {
                best = e;
            }
        }
        if (best < 0) {
            break;
        }
        for (e = 0; e < 7; e++) {
            if ((def_family_bits(best) >> e) & 1) {
                taken[e] = 1;
                resist_mod[victim][slot][e] -= delta;
                if (log_on()) {
                    LOG("resist down side %d slot %d element %d now %d", victim, slot, e, resist_mod[victim][slot][e]);
                }
            }
        }
    }
    resist_turns[victim][slot] = DEF_BREAK_TURNS;
    *msg = 2;
    return 1;
}

const unsigned char *def_message(int i) {
    extern const unsigned char def_str_MsgLibra[], def_str_MsgRelay[], def_str_MsgBreak[];
    static const unsigned char *const m[3] = { def_str_MsgLibra, def_str_MsgRelay, def_str_MsgBreak };

    return m[i];
}

/* The records the four techniques change (written with the MP rows, every time: the reset restores the table). */
static void patch_techniques(void) {
    records_techniques[202 - 1].target = 2;            /* Libra reads the enemy */
    records_techniques[203 - 1].mp_cost = DEF_RELAY_MP;
}

/* Layer 8: the rookies' MP rows (definitive_mp.c) into records_digimon, written every time (the reset restores the table). */
void def_mp_apply(void);

void def_mp_hook(void) {
    if (def_on()) {
        def_mp_apply();
        patch_techniques();
    }
}

/* ---- Layer 5: the damage of an elemental technique, by the multiplier of the PRD (definitive_mask.c). Called from the
 * rules (scale_damage for physical techniques, get_special_damage for magic) with the stats of the call in
 * fightstg_rules.stats ([0] the attacker, [1] the target). `terms` (above) is what the log prints. */

/* The player's element on an enemy: the multiplier. An enemy's element on the player: the player counts as
 * DEF_PLAYER_REF_RESIST, the scale the old formulas had. Returns the damage for the physical formula's step; `power` is
 * the technique's element_power (the old formula's own term, still used against the player). */
int def_scale_physical(u8 side, int element, int value, int power) {
    if (side != 0) {
        return value + value * power * 2 / DEF_PLAYER_REF_RESIST;
    }
    return def_scale_magic(side, element, value);
}

int def_scale_magic(u8 side, int element, int value) {
    const short *own = fightstg_rules.stats[0].resists;
    const short *foe = fightstg_rules.stats[1].resists;
    int species = fightstg_battle.state.members[1][fightstg_battle.state.current[1]].digimon;
    int cls;

    if (side != 0) {
        return value; /* the old bands at the reference resist: 125 - 100 / 4 = 100 percent */
    }
    cls = target_class(element, foe, species);
    terms.weak = cls == 1;
    terms.resisted = cls == 3;
    terms.open = chain.staggered[fightstg_battle.state.current[1] % 3];
    terms.affinity = def_affinity(element, own);
    terms.mult = def_multiplier(terms.weak, terms.open, terms.resisted, terms.affinity);
    return value * terms.mult / DEF_MULT_ONE;
}

/* A blow with no element (a physical technique, a basic attack, a magic with none) on an open target: 1 + 0.2. */
int def_open_bonus(u8 side, int defense_stat, int value) {
    if (!def_on() || side != 0 || defense_stat >= 2 || !chain.staggered[fightstg_battle.state.current[1] % 3]) {
        return value;
    }
    terms.weak = terms.resisted = terms.affinity = 0;
    terms.open = 1;
    terms.mult = DEF_MULT_ONE + DEF_MULT_STAGGER;
    return value * terms.mult / DEF_MULT_ONE;
}

/* ---- PRD 2, layer A: a boss is immune to status (blocked[0..4], [7]), not to stat debuffs (blocked[8..10]: Strength,
 * Defense, Speed down). With def_on the flag no longer blocks; where it would have, the debuff is scaled. */
int def_stat_blocked(int k) {
    if (def_on() && k >= 8 && k <= 10) {
        return 0;
    }
    return records_state.blocked[k];
}

int def_debuff_scale(int k, int amount) {
    if (def_on() && k >= 8 && k <= 10 && records_state.blocked[k] != 0) {
        return amount * DEF_BOSS_DEBUFF_PCT / 100;
    }
    return amount;
}

/* ---- PRD 2, layer B: the stat modifiers count for magic. The engine has none for Spirit; get_special_damage (with
 * fightstg_rules.stats filled: [0] the attacker, [1] the target, both with their modifiers in) asks for these:
 *  - the attack: Spirit scaled as Strength is by its modifier (now / (now - modifier));
 *  - the defence: the player's Defense (already modified: layer 7), or the enemy's Spirit scaled as its Defense is. The
 *    final boss (state.type 6) sets its own Defense and Spirit modifiers: its Spirit is left as it comes. */
static int scaled(int stat, int now, int modifier) {
    int base = now - modifier;

    if (modifier == 0 || base <= 0) {
        return stat;
    }
    stat = stat * now / base;
    return stat < 1 ? 1 : stat;
}

int def_magic_attack(u8 side) {
    FightstgMember *m = &fightstg_battle.state.members[side != 0][fightstg_battle.state.current[side != 0]];
    const FightstgStats *a = &fightstg_rules.stats[0];

    return scaled(a->stats[2], a->stats[0], m->modifiers[0]);
}

int def_magic_defense(u8 side) {
    FightstgMember *m = &fightstg_battle.state.members[side == 0][fightstg_battle.state.current[side == 0]];
    const FightstgStats *t = &fightstg_rules.stats[1];

    if (side != 0) {
        return t->stats[1] < 1 ? 1 : t->stats[1];
    }
    if (fightstg_battle.state.type == 6) {
        return t->stats[2] < 1 ? 1 : t->stats[2];
    }
    return scaled(t->stats[2], t->stats[1], m->modifiers[1]);
}

/* ---- PRD 2, layer D: the game's side of the TP bands (the rule is definitive_progression.c). */
static unsigned char tp_lost[8]; /* per partner: the last level-up lost TP to the ceiling (the report's message) */

s16 def_level_tp(s32 digimon, s16 tp, s16 level) {
    int lost;
    int now = def_tp_add(tp, level, &lost);

    if (lost > 0 && digimon >= 0 && digimon < 8) {
        tp_lost[digimon] = 1;
    }
    return (s16)now;
}

int def_tp_lost_take(s32 digimon) {
    int was;

    if (!def_on() || digimon < 0 || digimon >= 8) {
        return 0;
    }
    was = tp_lost[digimon];
    tp_lost[digimon] = 0;
    return was;
}

const unsigned char *def_tp_full_text(void) {
    extern const unsigned char def_str_LevelTpFull[];
    return def_str_LevelTpFull;
}

/* ---- PRD 2, layer E: training pity label for STGTRAIN. */
static void put_dec(unsigned char *out, int *p, int v, const unsigned char *digits) {
    if (v >= 10) {
        put_dec(out, p, v / 10, digits);
    }
    out[(*p)++] = digits[v % 10];
}

int def_train_pity_bonus_on(void) {
    return DEF_TRAIN_PITY_BONUS != 0;
}

const unsigned char *def_train_pity_label(int *palette) {
    extern const unsigned char def_str_TrainPityPrefix[], def_str_TrainPityMarks[], def_str_TrainPityDigits[],
        def_str_TrainPityReady[];
    static unsigned char buf[64];
    int total = DEF_TRAIN_PITY_AFTER;
    int count = def_train_pity_count();
    int i, p = 0;

    if (!def_on() || total <= 0) {
        return NULL;
    }
    for (i = 0; def_str_TrainPityPrefix[i] != 0 && p < (int)sizeof(buf) - 24; i++) {
        buf[p++] = def_str_TrainPityPrefix[i];
    }
    buf[p++] = def_str_TrainPityMarks[4];
    buf[p++] = def_str_TrainPityMarks[5];
    if (def_train_pity_guaranteed()) {
        for (i = 0; def_str_TrainPityReady[i] != 0 && p < (int)sizeof(buf) - 1; i++) {
            buf[p++] = def_str_TrainPityReady[i];
        }
        *palette = 4;
    } else {
        for (i = 0; i < total && i < 8; i++) {
            int off = i < count ? 0 : 2;
            buf[p++] = def_str_TrainPityMarks[off];
            buf[p++] = def_str_TrainPityMarks[off + 1];
        }
        buf[p++] = def_str_TrainPityMarks[4];
        buf[p++] = def_str_TrainPityMarks[5];
        put_dec(buf, &p, count, def_str_TrainPityDigits);
        buf[p++] = def_str_TrainPityMarks[6];
        buf[p++] = def_str_TrainPityMarks[7];
        put_dec(buf, &p, total, def_str_TrainPityDigits);
        *palette = count > 0 ? 3 : 0;
    }
    buf[p] = 0;
    return buf;
}

/* ---- PRD 2, layer F: persist Blast Gauges in the unused tail of each save slot. */
void def_gauges_slot_write(void *slot_save) {
    if (!def_on() || slot_save == NULL) {
        return;
    }
    def_gauges_encode(records_state.gauges, (unsigned char *)slot_save + DEF_GAUGE_SLOT_OFFSET);
}

void def_gauges_slot_read(const void *slot_save) {
    if (!def_on() || slot_save == NULL) {
        return;
    }
    def_gauges_decode((const unsigned char *)slot_save + DEF_GAUGE_SLOT_OFFSET, records_state.gauges);
}

/* ---- PRD 2, layer G: session RAM tracking of seen digivolution/blast animations. */
int def_evo_form_seen(int form_id) {
    return def_on() && def_evo_seen_get(def_evo_seen_bits, form_id);
}

void def_evo_form_mark(int form_id) {
    if (!def_on()) {
        return;
    }
    def_evo_seen_set(def_evo_seen_bits, form_id);
}

/* ---- PRD 2, layer I: resistance training without secondary stat loss. */
int def_train_no_stat_loss(void) {
    return def_on() && def_train_no_stat_loss_on();
}

/* ---- Overlevel Rush: a random battle against enemies DEF_OVERLEVEL_GAP levels or more under the party's best is
 * offered as a skip (the field asks; fieldstg_rush_update). Taken, the battle is not loaded: the field leaves for the
 * battle's report (STFGTREP, map 0x1400) with the results a win by the first member would have left, so experience,
 * Bits and the drop come from the game's own routine. DW3_RUSH_TEST (a test's): 1 offers it whatever the levels,
 * 2 also takes it without asking. */
int def_rush_test_mode = -1; /* -1: DW3_RUSH_TEST not read yet (a debug session may set it directly) */

static int rush_test(void) {
    if (def_rush_test_mode < 0) {
        const char *v = getenv("DW3_RUSH_TEST");

        def_rush_test_mode = v != NULL ? atoi(v) : 0;
    }
    return def_rush_test_mode;
}

int def_rush_auto(void) {
    return rush_test() == 2;
}

int def_rush_offer(void) {
    int best = 0, worst = 0, i, id;

    if (!def_on()) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        id = gamestate_data.funcs.get_party_member(i);
        if (id >= 0 && gamestate_data.digimon[id].record.stats.values[0] > best) {
            best = gamestate_data.digimon[id].record.stats.values[0];
        }
        if (records_state.enemies[i].digimon != 0 && records_state.enemies[i].level > worst) {
            worst = records_state.enemies[i].level;
        }
    }
    if (rush_test() == 0 && best - worst < DEF_OVERLEVEL_GAP) {
        return 0;
    }
    if (log_on() || rush_test()) {
        port_log("rush: offered (party level %d, enemies' %d)", best, worst);
    }
    return 1;
}

const unsigned char *def_species_name(int species) {
    extern void *font_get_entry(void *data, s32 i);
    FightstgEnemyRecord *recs = (FightstgEnemyRecord *)cdload_module.files.get_file(0x1CF);
    void *names = cdload_module.files.get_file(records_language + 0x4E);
    int i;

    if (recs == NULL || names == NULL || species == 0) {
        return NULL;
    }
    for (i = 0; i < DEF_BESTIARY_SPECIES && recs[i].id != 0; i++) {
        if (recs[i].id == species) {
            return (const unsigned char *)font_get_entry(names, recs[i].name);
        }
    }
    return NULL;
}

/* The results of the battle records_state holds, as won by the party's first member able to fight; no OVERFINISH. */
void def_rush_results(void) {
    FightstgEnemyRecord *recs = (FightstgEnemyRecord *)cdload_module.files.get_file(0x1CF);
    int i, id, lead = 0;

    for (i = 2; i >= 0; i--) {
        id = gamestate_data.funcs.get_party_member(i);
        if (id >= 0 && gamestate_data.digimon[id].record.stats.values[2] > 0) {
            lead = i;
        }
    }
    memset(&records_battle_results, 0, sizeof(records_battle_results));
    records_battle_results.battle = records_state.battle;
    records_battle_results.member = lead;
    records_battle_results.members[lead].took_part = 1;
    if (records_state.has_prize == 1) {
        records_battle_results.item = records_state.prize_item;
    } else if (recs != NULL) {
        for (i = 0; i < DEF_BESTIARY_SPECIES && recs[i].id != 0; i++) { /* the first enemy's drop, at its own rate */
            if (recs[i].id == records_state.enemies[0].digimon) {
                if (recs[i].item > 0 && pad_random.next() % 1024 < recs[i].drop_rate + 1) {
                    records_battle_results.item = recs[i].item;
                }
                break;
            }
        }
    }
    overfinish_damage = 0;
    def_hunt_skipped(); /* PRD 4 layer A: a skipped battle does not count in the hunt's chain */
    port_log("rush: battle %d skipped (member %d, item %d)", (int)records_state.battle, lead,
             (int)records_battle_results.item);
}
