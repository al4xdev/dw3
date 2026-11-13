/* "Definitive" combat (.plan/tasks/definitive-combat-prd.md): the constants (rule 6: every number is a constant, with
 * a line saying what it controls) and the hooks the game's C calls. Always on; no mod option, no port_mod_* flag.
 * The hooks' prototypes are in include/port.h (the game's units reach them through common.h). */
#ifndef PORT_DEFINITIVE_H
#define PORT_DEFINITIVE_H

/* Layer 0 has no tunable numbers; layers 3+ add theirs here (the PRD's table). */
#define DEF_FAMILIES 5          /* element families on the grouped screens (Water+Ice, Thunder+Machine as one each) */
#define DEF_OVERFINISH_BITS_PCT 10 /* Bits earned per 100 damage dealt to an enemy already knocked out (OVERFINISH) */
#define DEF_OVERLEVEL_GAP 15       /* levels the party's best must be above the enemies' best for a random battle to offer the Overlevel Rush */
#define DEF_RUSH_TICKS 70          /* ticks (1/60 s) of the rush on the field before the battle's report opens */
#define DEF_WEAK_MAX 80         /* a family is a weakness while one of its values is at most this (modifiers included): the
                                 * table's weak 60, or a neutral 100..115 lowered by Resist Down (80) or Mega Break (100);
                                 * a resisted one lowered stays above it. No cap on the count (owner, 2026-11-13) */
#define DEF_BATON_PASS_MP 0     /* MP Baton Pass costs (offered only while the enemy is open; Persona's is free) */
#define DEF_MULT_ONE 1000       /* the fixed-point unit of every multiplier and affinity below (thousandths) */
#define DEF_MULT_WEAK 500       /* bonus for hitting a weakness */
#define DEF_MULT_STAGGER 200    /* bonus for hitting a target that is open (staggered) */
#define DEF_MULT_RESISTED 500   /* the whole multiplier against a resisted element, no bonuses [owner to confirm] */
#define DEF_AFF_SINGLE_MAX 300  /* affinity ceiling of Fire and of Wind (the player's resist 999) */
#define DEF_AFF_PAIR_MAX 600    /* of Water+Ice and of Thunder+Machine (the pair's sum 1998) */
#define DEF_AFF_DARK_MAX 600    /* of Dark (999) */
#define DEF_PLAYER_REF_RESIST 100 /* the resist the player counts as when an enemy's element hits it: the old formulas' scale */
#define DEF_RELAY_MP 50         /* MP the Relay technique (Mega Boost, remade) costs */
#define DEF_BREAK_SLOW_DELTA 80 /* how far Slow Down (remade) lowers the enemy's resist of the user's best element */
#define DEF_BREAK_MEGA_DELTA 100 /* how far Mega Break lowers each of the user's two best elements */
#define DEF_BREAK_TURNS 3       /* the enemy's turns a lowered resist lasts */
#define DEF_BOSS_DEBUFF_PCT 100 /* strength of a stat debuff in a battle whose boss the original made immune to it */
#define DEF_RESIST_MIN 250      /* resist value from which an element is "resisted" */
#define DEF_SND_EFFECTIVE 0x800450BD /* sound id of a hit on a weakness (the menus' accept chime) */
#define DEF_SND_NEUTRAL 0x4001C      /* of a neutral hit (the menus' confirm blip) */
#define DEF_SND_RESISTED 0x4001B     /* of a resisted hit (the menus' cancel blip) */

/* Layer 3 (definitive_mask.c, pure): the elements `resists[0..6]` (Fire, Water, Ice, Wind, Thunder, Machine, Dark) are weak
 * to, as a bit mask (bit = element index), a family at a time: Water+Ice and Thunder+Machine are one affinity each
 * (owner, 2026-11-13), so a family is weak while either of its values is at most DEF_WEAK_MAX, and then all its bits
 * are set. Dark is a weakness only when `dark_weak` (the species' mark). */
int def_weakness_mask(const short resists[7], int dark_weak);
/* The elements of `e`'s family as a bit mask (Water or Ice: both; Fire: itself); 0 outside 0..6. */
int def_family_bits(int e);
int def_resisted(int resist); /* 1 from DEF_RESIST_MIN up */
/* Layer 5, the damage multiplier of an elemental technique of the player (thousandths, DEF_MULT_ONE = 1.0):
 * 1 + (weak ? 0.5) + (open ? 0.2) + (weak ? affinity) + (open ? affinity); resisted: DEF_MULT_RESISTED alone.
 * `affinity` from def_affinity: the element's share of the attacker's own resists (`resists[0..6]`, 0..999 each). */
int def_affinity(int element, const short resists[7]);
int def_multiplier(int weak, int open, int resisted, int affinity);

/* Layer 4 (definitive_mask.c, pure): stagger, the chain of extra actions and the free swap. Fed by definitive.c with the
 * game's events; tests/port/definitive_driver.c plays the PRD's acceptance sequences on it. */
typedef struct DefChain {
    unsigned char staggered[3];  /* per enemy slot: open */
    unsigned char window_used;   /* weakness families (their elements' bits) that already gave their extra action */
    unsigned char swap_used;     /* the free swap of this window is spent */
    unsigned char extra_pending; /* an extra action is owed: the next turn event is made immediate */
    unsigned char next_extra;    /* the next party turn is that extra action */
    unsigned char cur_extra;     /* the party turn now running is an extra action */
    unsigned char relay;         /* Relay was used: the next swap is free (layer 6) */
} DefChain;
void def_chain_reset(DefChain *c);
/* The player's action resolved on `target`: it `hit` (damage > 0); `elem` the element index of an elemental technique,
 * -1 for a physical blow or a crest's element (they never open); `weak` it hit a weakness. */
void def_chain_player_hit(DefChain *c, int target, int hit, int elem, int weak);
void def_chain_relay_set(DefChain *c);           /* the Relay technique: the next swap is free */
void def_chain_enemy_acts(DefChain *c, int slot); /* an enemy's turn begins: it closes */
void def_chain_party_turn(DefChain *c);           /* a party turn begins: an extra one, or a normal one (new window) */
int def_chain_swap_free(const DefChain *c, int target);
void def_chain_swap_chosen(DefChain *c, int target); /* the player chose Tag: free when allowed, else it costs the turn */
int def_chain_take_extra(DefChain *c);              /* about to take the next event: 1 if the party turn must go first */

/* The port's table of enemy species (definitive_enemies.c, generated by tools/gen_enemy_weaknesses.py and approved by
 * the owner before it goes in): the seven resists a species is given (SDIGIEDT is never edited: the record in memory is
 * overridden when the game asks for it) and its Dark mark (Dark is one of its weaknesses). */
typedef struct DefEnemyRow {
    short id; /* FightstgEnemyRecord.id */
    short resists[7];
    unsigned char dark; /* the Dark mark */
} DefEnemyRow;
extern const DefEnemyRow def_enemy_rows[];
extern const int def_enemy_row_count;

/* Text overrides (layer 1; port/game/definitive_text.c, generated by tools/gen_text_override.py): entry `index` of the
 * text file whose Japanese file ID is `file` (the game adds records_language), already encoded as glyph bytes. */
typedef struct DefTextOverride {
    int file;
    int index;
    const unsigned char *bytes;
} DefTextOverride;
extern const DefTextOverride def_text_overrides[];
extern const int def_text_override_count;

/* Layer 8: MP (port/game/definitive_mp.c). The MP is the rookie's; the forms have none of their own. */
#define DEF_MP_START 140        /* starting MP and max MP of Kotemon, Kumamon, Monmon and Guilmon (Veemon keeps its 150) */
#define DEF_MP_START_VEEMON 150 /* Veemon's starting MP: already the PRD's value, listed so the table is complete */
#define DEF_MP_GAIN 57          /* max MP gain base per level of Kotemon, Kumamon, Monmon and Veemon */
#define DEF_MP_GAIN_GUILMON 60  /* Guilmon's max MP gain base per level: already the PRD's value */

/* One rookie's MP row: its records_digimon index (the gamestate_data.digimon index too), starting MP, gain base. */
typedef struct DefMpRow {
    unsigned char digimon;
    unsigned char start_mp;
    unsigned char gain;
} DefMpRow;
extern const DefMpRow def_mp_rows[];
extern const int def_mp_row_count;

/* Writes the rows into records_digimon (the boot patch; the src/ table stays as the matching build has it). */
void def_mp_apply(void);

/* ---- PRD 2 (definitive_progression.c) ---- */
#define DEF_TP_GAIN_LV_2_20 7     /* TP a level gives, levels 2..20 */
#define DEF_TP_GAIN_LV_21_40 9    /* levels 21..40 */
#define DEF_TP_GAIN_LV_41_50 30   /* levels 41..50 */
#define DEF_TP_GAIN_LV_51_UP 0    /* after 50 only the Train Chip gives TP */
#define DEF_TP_CAP 99             /* the TP ceiling, the original's on purpose: no hoarding for the last gym */
int def_tp_gain(int level);
int def_tp_total(int level);
int def_tp_add(int tp, int level, int *lost);

#ifndef DEF_TRAIN_PITY_AFTER
#define DEF_TRAIN_PITY_AFTER 3 /* non-perfect trainings before the next training hits everything (0 disables) */
#endif
#ifndef DEF_TRAIN_PITY_BONUS
#define DEF_TRAIN_PITY_BONUS 1 /* when 1, the guaranteed training also hits its bonus round when eligible */
#endif
int def_train_pity_count(void);
int def_train_pity_guaranteed(void);
void def_train_pity_record(int perfect);
void def_train_pity_reset(void);

#define DEF_GAUGE_SLOTS 8            /* Digimon slots in records_state.gauges */
#define DEF_GAUGE_MAX 1000           /* maximum valid Blast Gauge value (0..1000) */
#define DEF_GAUGE_SLOT_OFFSET 0x26F0 /* tail offset inside a 0x2700 save slot (after GsRecord + DFV2 marker) */
int def_gauge_clamp(int val);
void def_gauges_encode(const short gauges[DEF_GAUGE_SLOTS], unsigned char out[DEF_GAUGE_SLOTS * 2]);
void def_gauges_decode(const unsigned char in[DEF_GAUGE_SLOTS * 2], short out[DEF_GAUGE_SLOTS]);

#define DEF_EVO_FORM_MAX 512            /* form ID bitset ceiling in RAM (all playable + blast forms) */
#define DEF_EVO_SEEN_WORDS (DEF_EVO_FORM_MAX / 32)
#define DEF_EVO_SKIP_HOLD_FRAMES 4      /* frames Circle or Cross must be held to skip digivolve scene */
void def_evo_seen_reset(unsigned int bits[DEF_EVO_SEEN_WORDS]);
int def_evo_seen_get(const unsigned int bits[DEF_EVO_SEEN_WORDS], int form_id);
void def_evo_seen_set(unsigned int bits[DEF_EVO_SEEN_WORDS], int form_id);
int def_evo_skip_update(int *hold_frames, int skip_pressed);

#ifndef DEF_TRAIN_NO_STAT_LOSS
#define DEF_TRAIN_NO_STAT_LOSS 1 /* resistance trainings (8..14) do not lower secondary stats 1..5 */
#endif
int def_train_no_stat_loss_on(void);

/* ---- PRD 3: the mods the owner played with, made the game's own (party_xp, xp_boost) ---- */
#define DEF_XP_PARTY_SHARE 100   /* % of one fighter's share that a member who sat out (not knocked out) receives */
#define DEF_XP_FORM_MULT 14      /* multiplier of a won battle's form (digivolution) experience; the clamp is skipped */
#define DEF_XP_ALL_FORMS 1       /* every owned form of every party member earns form experience, fought or not */
#define DEF_CARD_BOOST_PACKS 15  /* Booster Packs a battle's Booster Pack drop gives */

#define DEF_EVO_HINT_DEPTH 1     /* Digi-Lab Next Forms: forms shown one step ahead; 0 turns it off, 99 shows every form */

/* The hit callouts (definitive_callout.c): the slanted word that pops over the side hit. def_callout_show starts one
 * (0 clears it) over `side`'s acting member (0 party, 1 enemy); def_callout_frame runs once per battle frame. */
enum { DEF_CALLOUT_NONE, DEF_CALLOUT_WEAK, DEF_CALLOUT_RESIST, DEF_CALLOUT_OVERFINISH, DEF_CALLOUT_RARE, DEF_CALLOUT_COUNT };
void def_callout_show(int kind, int side);
void def_callout_frame(void);

/* PRD 4 layer A: the regional hunt chain and the Rare Target (definitive_hunt.c; the table is
 * definitive_hunt_targets.c, generated by tools/gen_hunt_targets.py and approved by the owner). */
#define DEF_HUNT_CHAIN_MIN 5    /* won random encounters in a row in one region before its Rare Target can appear */
#define DEF_HUNT_CHANCE_BASE 5  /* % chance at the first encounter once the chain reaches the minimum */
#define DEF_HUNT_CHANCE_STEP 5  /* % more per win beyond the minimum */
#define DEF_HUNT_CHANCE_MAX 40  /* the chance's ceiling, % */
#define DEF_HUNT_CHAIN_CAP 255  /* the chain stops counting here (one byte of the card record) */
#define DEF_HUNT_HP_MAX 30000   /* a raised Rare Target's HP never passes this (RecordsEnemy.hp is an s16) */
/* PRD 4 layer B: the Rare Target's rewards. */
#define DEF_HUNT_XP_MULT 10           /* XP: this many times the random encounter the Rare Target replaced */
#define DEF_HUNT_MATERIALS 16         /* Hunt Materials: one u8 counter each in the card record (REC_EXTRA + 10..25) */
#define DEF_HUNT_MATERIAL_CAP 99      /* the most of one material the player can hold */
#define DEF_HUNT_PACK_FIRST 0x186     /* the rare Booster (R-Booster 01..05, items 0x186..0x18A) a Rare Target drops... */
#define DEF_HUNT_PACK_LEVELS 15       /* ...one more per this many levels from level 25 (01 below 25, 05 from 70) */
#define DEF_HUNT_PACKS 1              /* how many of it (owner, 2026-11-13; a plain Booster drop gives DEF_CARD_BOOST_PACKS) */
typedef struct DefHuntTarget {
    unsigned char region; /* ststatus_map_areas's byte: the world map area, bit 7 the server */
    short battle;         /* an existing boss battle: an index into fieldstg_battles */
    short level;          /* the level its enemies fight at */
    unsigned char material; /* its Hunt Material (layer B): one per target battle, the weakest battle's first */
} DefHuntTarget;
extern const DefHuntTarget def_hunt_targets[];
extern const int def_hunt_target_count;
/* The % chance of the Rare Target for a region's chain (0 under the minimum). */
int def_hunt_chance(int chain);
/* A Hunt Material's name (glyph bytes, 0-ended). */
const unsigned char *def_hunt_material_name(int material);
/* PRD 4 layer C: the Hunt Materials species `species` drops on the bestiary screen, those of the Rare Target battles
 * it fights in (a bit per material; 0 for none). */
unsigned int def_hunt_species_materials(int species);
/* The results screen of a Rare Target won (PRD 4 layer B; STFGTREP): the battle whose XP it pays DEF_HUNT_XP_MULT times
 * (-1: none pending), the rare Booster it drops when the battle left no item (0: none), 1 while its material's line has
 * not been shown, that line (glyph bytes, marks it shown), and the end of the screen. */
int def_hunt_reward_battle(void);
int def_hunt_reward_pack(void);
/* The screen gives that Booster (returns it, as def_hunt_reward_pack), and how many packs an item the screen shows is:
 * DEF_HUNT_PACKS for the Booster it gave, 0 for any other item (the PRD 3 count stays theirs). */
int def_hunt_take_pack(void);
int def_hunt_pack_count(int item);
int def_hunt_material_pending(void);
const unsigned char *def_hunt_material_text(void);
void def_hunt_reward_done(void);
/* The hooks the game's C calls (also declared in include/port.h, for the units that do not include this header). */
void def_hunt_map(int map);
int def_hunt_encounter(int battle);
void def_hunt_scale(void);
int def_hunt_active(void);
void def_hunt_skipped(void);
/* The battle's end (def_log_end): a won random encounter adds to the chain; a Rare Target fought resets it. */
void def_hunt_battle_end(int result);
/* The Guardromon teleport resets the chain, whatever region it lands in. */
void def_hunt_chain_reset(void);
struct PortState;
void def_hunt_savestate(struct PortState *s);

#endif /* PORT_DEFINITIVE_H */

/* Layer 9: battle memory and the bestiary (port/game/definitive_bestiary.c). One byte per species, bit = element index. */
#define DEF_BESTIARY_SPECIES 193      /* species in SDIGIEDT (file 0x1CF): the table's size */
#define DEF_BESTIARY_ELEMENTS 7       /* elements in a byte of the table (bits 0..6) */
#define DEF_BESTIARY_SLOTS 3          /* the game's save slots: one record each */
#define DEF_BESTIARY_RECORD_SIZE 0x100 /* bytes of a slot's record on the card (two 0x80 card frames) */
#define DEF_BESTIARY_RECORD_AT 0x7800 /* file offset of slot 0's record: the card file is 0x8000, the game uses up to here */
#define DEF_BESTIARY_PART 5           /* memcard_read/_write part of the record; the slot rides in part >> 8 */
#define DEF_BESTIARY_VERSION 1        /* the record's layout version; an unknown one reads as an empty table */
#define DEF_BESTIARY_ROWS 6           /* species listed per page on the bestiary screen */

void def_bestiary_reset(void);
/* The slot's other port state, kept in the same card record: the Guardromon network's unlocked nodes (a bit each,
 * game_mods.c's table) and the save's flags. Reset with the table (a new game), read and written with it. */
#define DEF_SAVE_HARD 1 /* the save is a Hard Mode one: the party is two Digimon */
unsigned int def_network_mask(void);
void def_network_unlock(int node);
int def_save_flag(int flag);
void def_save_flag_set(int flag, int on);
/* The hunt's chain and the region it counts in (PRD 4 layer A), kept in the same record (REC_EXTRA + 8, + 9). */
void def_record_hunt(int *region, int *chain);
void def_record_set_hunt(int region, int chain);
/* The Hunt Materials held (PRD 4 layer B), same record (REC_EXTRA + 10..25): the count of one, and adding to it
 * (clamped to 0..DEF_HUNT_MATERIAL_CAP). */
int def_record_material(int material);
void def_record_material_add(int material, int n);
void def_bestiary_discover(int index, int element_mask);
int def_bestiary_known(int index);
int def_bestiary_index(int species_id);
struct PortState;
void def_bestiary_savestate(struct PortState *s);
/* The card record of a slot (the host test drives these two): `out`/`in` are DEF_BESTIARY_RECORD_SIZE bytes. An empty
 * table writes all zeros (a card nothing was discovered on stays byte-identical); a missing, wrongly versioned or
 * badly summed record (or NULL) reads as an empty table. */
void def_bestiary_record_write(unsigned char *out);
void def_bestiary_record_read(const unsigned char *in);
/* 1 when the screen and the card record are in use: never in a replay script (shared with the emulator), unless
 * DW3_DEFINITIVE is set, as the rest of the layers (DW3_BESTIARY: this layer alone). */
int def_bestiary_active(void);
/* The record's buffer for the card transfers (a static one: the slot's 0x2700 buffer has no room). */
unsigned char *def_bestiary_buffer(void);
