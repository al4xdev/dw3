/* "Definitive" combat (.plan/tasks/definitive-combat-prd.md): the constants (rule 6: every number is a constant, with
 * a line saying what it controls) and the hooks the game's C calls. Always on; no mod option, no port_mod_* flag.
 * The hooks' prototypes are in include/port.h (the game's units reach them through common.h). */
#ifndef PORT_DEFINITIVE_H
#define PORT_DEFINITIVE_H

/* Layer 0 has no tunable numbers; layers 3+ add theirs here (the PRD's table). */
#define DEF_WEAK_COUNT 2        /* most weaknesses a target has (Dark, when marked, comes on top) */
#define DEF_WEAK_GAP 20         /* how far below the median of the six a value must be to count as a weakness */
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
 * to, as a bit mask (bit = element index). The six non-Dark values give a median (the mean of the 3rd and 4th); the
 * weaknesses are the DEF_WEAK_COUNT lowest values at least DEF_WEAK_GAP below it (ties by the element order; all equal:
 * none). Dark is a weakness only when `dark_weak` (the species' mark), on top of the count. */
int def_weakness_mask(const short resists[7], int dark_weak);
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
    unsigned char window_used;   /* weakness elements (bits) that already gave their extra action in this window */
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
