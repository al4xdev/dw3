#ifndef PORT_H
#define PORT_H

/* Hooks for the PC port (docs/PORT.md "Hook macros (`include/port.h`)"; DECISIONS "PC port architecture"; the
 * contract with the port stack: psxstack's GAME_CONTRACT.md "2. The hook header").
 *
 * Every macro here has two sides:
 *  - without PC_PORT (the PS1 matching build) it expands to exactly the code the game unit had before, so no byte of
 *    the EXE or of an overlay changes; this side is all here, and needs nothing from the port;
 *  - with -DPC_PORT (the 64-bit host build) the generic macros come from the stack's port/include/psxstack/hooks.h
 *    (included below), which expands them to forms that use the port_* functions and objects it declares, implemented
 *    under port/runtime/ (overlay manager, memory arena, interrupt pump). This file adds the game's own hooks: the
 *    typed slot-function macros (WSTAG_ENTRY, OVERLAY_ENTRY) and the mods' flags.
 *
 * common.h includes this file after its typedefs: no unit includes it directly.
 *
 * Addresses (port/game/game.json, the game's description): the host keeps the PS1's addresses as the *names* of things
 * that live in the overlay slots.
 *  - slot 1 (tier 1): 0x80082CB0 (main_overlay_base), the 19 stage overlays;
 *  - slot 2 (tier 2): 0x800A5DE0 (main_file_base), WFIGHTMN/WFIGHTTS, the 293 WSTAG files and data files;
 *  - heap: 0x800AB800..0x801FF000 (heap.c).
 * A function in a slot is a *tag* on the host: the PS1 address as an integer in a function-pointer variable
 * (SLOT_FUNC), which is a constant expression and so works in the static tables. A tag is never called directly:
 * OVERLAY_FN turns it into the host function at the call. Data in a slot is slot-relative (SLOT_PTR). */

#ifndef PC_PORT
/* ===================================================== PS1 ===================================================== */

/* --- Waits ---
 * PLATFORM_WAIT(): the body of a busy-wait that only an interrupt (vsync, CD, GPU) can end:
 *     while (gfx_frame_pending != 0) { PLATFORM_WAIT(); }
 * PS1: nothing (an empty statement). Host: port_wait() runs the pending "interrupts" (the vsync callback, the CD
 * callbacks, the GPU going idle) so that the condition can change. */
#define PLATFORM_WAIT()

/* PLATFORM_HALT(): the body of a deliberate endless loop (FIELDSTG's fieldstg_find_stage, when the map has no stage):
 *     while (1) { PLATFORM_HALT(); }
 * PS1: nothing. Host: port_halt() reports the place and stops; it does not return. */
#define PLATFORM_HALT()

/* --- The scratchpad stack (heap_run_object) ---
 * PORT_SCRATCHPAD_STACK_ENTER(top): saves $sp at `top` and sets $sp 16 bytes below it (the original's inline asm);
 * PORT_SCRATCHPAD_STACK_LEAVE(): returns to the saved stack. PS1: heap.c's SET_SCRATCHPAD_STACK/RESTORE_STACK, text
 * for text. Host: nothing, the object runs on the normal stack. */
#define PORT_SCRATCHPAD_STACK_ENTER(top)                                                           \
    __asm__ volatile("addu $8, %0, $0\n\tsw $29, 0($8)\n\taddiu $8, $8, -16\n\taddu $29, $8, $0" \
                     :                                                                             \
                     : "r"(top)                                                                    \
                     : "$8", "memory")
#define PORT_SCRATCHPAD_STACK_LEAVE() __asm__ volatile("addiu $29, $29, 16\n\tlw $29, 0($29)" : : : "memory")

/* --- Overlay loads (overlay_load_stage, overlay_load_file) ---
 * OVERLAY_COPY(tier, file, dst, src, size): copies the file `file` (its file ID; `src`, `size` bytes) into the slot
 * of `tier` (1 or 2) at `dst`. PS1: memcpy(dst, src, size) (GCC's built-in, as before). Host: port_overlay_load():
 * the overlay manager makes `file` the tier's current overlay and restores its .data/.bss to the load-time contents;
 * a file that is not code (WSTAG260, ...) is copied into the slot buffer. All five arguments are evaluated once on
 * both sides. */
#define OVERLAY_COPY(tier, file, dst, src, size) memcpy((dst), (src), (size))

/* --- Pointers and integers ---
 * PTR_ADD(type, ofs, base): an offset-table resolve, `ofs` bytes from the pointer `base`, as `type`.
 * PS1: ((type)((ofs) + (s32)(base))): the integer addition the original has, with the offset on the left.
 * Host: pointer arithmetic. */
#define PTR_ADD(type, ofs, base) ((type)((ofs) + (s32)(base)))

/* PTR_TO_S32(p) / S32_TO_PTR(type, v): a pointer kept in an s32 (a struct field, an s32 argument) and back.
 * PS1: plain casts. Host: port_ptr_to_s32()/port_s32_to_ptr(): NULL <-> 0, otherwise a pointer into the port's
 * memory arena <-> its PS1-style address (anything else is a fatal error there). Retyping the field to a pointer is
 * the better fix where it keeps the bytes. */
#define PTR_TO_S32(p) ((s32)(p))
#define S32_TO_PTR(type, v) ((type)(v))

/* PTR_TO_U32(p): the pointer's bits, for the 24-bit ordering-table tags (gfx_compact_ot's `(u32)q & mask`, libgpu.h's
 * setaddr). PS1: (u32)(p). Host: the pointer's word offset in the tag window (port_ptr_to_u32: the game's static data
 * and the arena), which the shim's DrawOTag resolves against the window's base; anything else is a fatal error. */
#define PTR_TO_U32(p) ((u32)(p))

/* --- Things at fixed addresses ---
 * SLOT_FUNC(type, addr): a function of whatever overlay is in a slot, as the function-pointer type `type`.
 * PS1: ((type)(addr)). Host: a tag (see the top of the file); call it through OVERLAY_FN. Valid in static
 * initializers on both sides. */
#define SLOT_FUNC(type, addr) ((type)(addr))

/* WSTAG_ENTRY(addr): a map's entry in the tier 2 slot (fieldstg_stages, fieldstg_stages_2d).
 * OVERLAY_ENTRY(addr): a stage overlay's entry in the tier 1 slot (overlay_entries). */
#define WSTAG_ENTRY(addr) ((void *(*)())(addr))
#define OVERLAY_ENTRY(addr) ((s32 (*)(void))(addr))

/* OVERLAY_FN(tier, fn): the function to call for `fn`, a function pointer that may hold a SLOT_FUNC of `tier`:
 *     data->stage = OVERLAY_FN(2, fieldstg_stage.entry)(obj);
 * PS1: (fn). Host: port_overlay_resolve() looks a tag up in the tier's current overlay; a pointer that is not a tag
 * (a real function: the tables mix both) comes back unchanged. `fn` is evaluated once. */
#define OVERLAY_FN(tier, fn) (fn)

/* LATE_FUNC(tier, addr, ret, name, params): declares `name`, a function at `addr` of whatever overlay is in the slot
 * of `tier`, that this unit calls by name (it is resolved by the PS1 linker through undefined_syms):
 *     LATE_FUNC(1, 0x8008B770, void, func_8008B770, (s32, s32, s32, s32, s32));
 * LATE_CALL(name): the function to call:
 *     LATE_CALL(func_8008B770)(0x700, index, 0, 0, 0);
 * PS1: the plain prototype and the plain name (the same jal). Host: no symbol `name` exists; the call goes through
 * port_overlay_resolve() by `addr`. */
#define LATE_FUNC(tier, addr, ret, name, params) ret name params
#define LATE_CALL(name) name

/* SLOT_PTR(tier, type, addr): data at `addr` in the slot of `tier` (the literal 1 or 2), as the pointer type `type`:
 * the event scripts of FIELDSTG's built-in stage, main_overlay_base, main_file_base.
 * PS1: ((type)(addr)). Host: the same offset into the port's slot buffer. Valid in static initializers on both
 * sides. */
#define SLOT_PTR(tier, type, addr) ((type)(addr))

/* The heap's bounds (heap_init, D_8005CB50).
 * HEAP_START(type) / HEAP_END(type): 0x800AB800 / 0x801FF000 as the pointer type `type`. Host: the arena's heap
 * region; valid in static initializers on both sides.
 * HEAP_SIZE_FROM(first): the bytes from the pointer `first` to the heap's end. PS1: (0x801FF000 - (u32)(first)).
 * HEAP_ADDR(addr): a constant address inside the heap, passed as a pointer (FIELDSTG's free_above(0x8015C674)).
 * PS1: the integer constant, as written. Host: the same offset from the heap's start, as a u8 pointer. */
#define HEAP_START(type) ((type)0x800AB800)
#define HEAP_END(type) ((type)0x801FF000)
#define HEAP_SIZE_FROM(first) (0x801FF000 - (u32)(first))
#define HEAP_ADDR(addr) (addr)

/* BIOS_PTR(type, addr): a pointer into the BIOS ROM (STAGSLCT's version string at 0x1FC0012C).
 * PS1: ((type)(addr)). Host: port_bios_ptr(), the same offset into the port's BIOS image (or a stand-in). */
#define BIOS_PTR(type, addr) ((type)(addr))

#else /* PC_PORT */
/* ===================================================== Host ==================================================== */

#include "psxstack/hooks.h" /* the generic macros and the port_* interface (the stack's) */

void port_audio_resync(void);

/* The game's typed slot functions: a map's entry in slot 2 (fieldstg_stages, fieldstg_stages_2d), a stage overlay's
 * entry in slot 1 (overlay_entries). */
#define WSTAG_ENTRY(addr) SLOT_FUNC(void *(*)(), addr)
#define OVERLAY_ENTRY(addr) SLOT_FUNC(s32 (*)(void), addr)

/* ---- The mods' flags (docs/LAUNCHER.md "Mod runtime"; port/game/game_mods.c sets them, 0 while a mod is off or under
 * --script): read only inside `#ifdef PC_PORT` blocks of the game's C, never in an expression the PS1 build sees.
 * port_mod_skip_dialogues: skip_dialogues is on (docs/LAUNCHER.md "Skip dialogues"): a revealing message window shows its page at once
 * (src/main/message.c), a wait for the confirm button goes on by itself, and so does the battle's message wait
 * (src/fightstg/fightstg_8008D3B4.c). No pad press is made: choices and code-driven prompts still wait. */
extern int port_mod_skip_dialogues;
/* port_mod_battle_animations: battle_animations is on ("Disable battle animations"): fightstg_script_update asks port_battle_cut, at a script's
 * INIT, what script 5 or above becomes: -1 run it as it is, 0 end it at once, 1..4 the target's reaction (the script
 * its child command would have started, results[3] + 1); *sound_id and *sound_arg: the hit sound it would have played
 * (0: none), for fightstg_sound_play. The rules ran before the script started. */
extern int port_mod_battle_animations;
/* port_mod_battle_text: battle_text's mode while the mod is on ("Battle text"): 0 as the disc, 1 skip, 2 async. FIGHTSTG's
 * message box (src/fightstg/fightstg_8008D3B4.c) ends a message at once (skip) or hands its windows to a toast that
 * outlives it (async); the toast's state (arena windows, frames left) is game_mods.c's, named in its save state. */
extern int port_mod_battle_text;
s32 port_battle_cut(const s16 *stream, s32 stage, const s32 *results, s32 hit_sound, s32 *sound_id, s32 *sound_arg);
/* port_mod_global_save: global_save is on ("Save anywhere"): the field menu (src/main/fieldmenu.c) gets a last entry, SAVE (the
 * save screen's title, ?SMEMCRD entry 1), in the field's menu only; chosen, port_global_save_open sends the game to
 * STGMCARD in save mode (the inn's 0xC map for an inn's map, 0xC00 elsewhere). STGMCARD then asks
 * port_global_save_map_name for the slot summary's place name (0, none, off an inn), port_global_save_record puts the
 * map's state that a load loses (the attribute layer, depth and height, the per-visit flags) into the slot's unused tail
 * (slot offset 0x26C4, outside the checksum: a PS1 ignores it), and port_global_save_restore reads it back after a load
 * so FIELDSTG resumes the map as after a Back from the save screen (map_is_new 0). */
extern int port_mod_global_save;
void port_global_save_open(void);
s32 port_global_save_map_name(s32 map_name);
s32 port_global_save_inn(s32 map);
void port_global_save_record(void *slot);
void port_global_save_restore(const void *slot);
/* port_mod_preset_language: preset_language is on ("Preset language"): CNTY_SEL's root object (src/cnty_sel), once the
 * screen's sound bank is in, sets records_language to port_preset_language (0 JPN, 1 USA, 2-6 Europe) and goes on to
 * the opening as the menu would, without drawing the screen. */
extern int port_mod_preset_language;
extern s32 port_preset_language;
/* The party's shared experience (definitive edition, PRD 3; game_mods.c): at the end of every battle WFIGHTMN tells
 * port_party_xp_knocked_out which party slots are at 0 HP, and STFGTREP's report sets each member's experience to
 * port_party_xp_share(slot, took_part, exp). Both only when def_on. */
void port_party_xp_knocked_out(s32 slot, s32 knocked_out);
s32 port_party_xp_share(s32 slot, s32 took_part, s32 exp);

/* port_mod_battle_menu: battle_menu is on ("Battle menu"): FIGHTSTG's main command menu (fightstg_command_menu_create)
 * becomes a menu with one pad button per action, drawn where the list was (docs/LAUNCHER.md "Battle menu"). The
 * buttons are chosen by the place on the player's pad, so the menu follows pad.map (dw3_pad_permute, below): the bit an
 * action listens to is the one the player's button delivers, and its icon is that bit's own glyph (dw3_button_glyph),
 * which is the player's letter.
 * port_battle_menu_bit: the PS1 pad bit (0..15) that triggers action `action` (0 Fight, 1 Tech, 2 Digivolve, 3 Tag,
 * 4 Item, 5 Run: the order of the original list and of its result).
 * port_battle_menu_place: where that bit sits on the player's pad: 12 north, 15 west, 13 east, 14 south for the face
 * buttons, else the shoulder or trigger bit itself (10 L1, 11 R1, 8 L2, 9 R2). */
extern int port_mod_battle_menu;
int port_battle_menu_bit(int action);
int port_battle_menu_place(int bit);
int port_battle_menu_pressed(int action);
char port_battle_menu_letter(int action);
int port_battle_menu_symbol_bit(int action); /* the PS1 button pad.map puts at the action's place (its symbol) */
u8 port_face_symbol_code(int bit); /* the extra glyph of a face button's symbol; 0 for a shoulder or trigger */
void port_battle_menu_chosen(int action, int bit, int refused); /* logs a press on the menu (tests/port/mods.py) */
/* battle_menu's remember_cursor and buffer_input (port/game/game_mods.c, "battle_menu's two helpers"):
 * port_mod_bm_remember: the technique and item menus open on the member's last choice (port_bm_recall/_remember: kind 0
 * technique, 1 item; 0 when none or off); port_mod_bm_buffer: the buffer's window in frames, 0 off. The buffer keeps up to
 * two presses (port_bm_record) made before the command menu opens (port_bm_open) and gives them one at a time
 * (port_bm_take_action to the command menu, port_bm_take_confirm to a technique or item menu); port_bm_message drops what
 * a battle message may have taken, port_bm_clear everything. */
extern int port_mod_bm_remember;
extern int port_mod_bm_buffer;
void port_bm_record(s32 action, u32 confirm);
void port_bm_message(void);
void port_bm_clear(void);
void port_bm_open(void);
s32 port_bm_take_action(void);
u32 port_bm_take_confirm(void);
s32 port_bm_recall(s32 kind, s32 member);
void port_bm_remember(s32 kind, s32 member, s32 value);
void port_bm_log(s32 kind, s32 id, s32 position);
void port_bm_log_last(s32 last);

/* Handheld features. Each is a runtime if (DW3_FEAT_ICONS / _TRAVEL / _OBJECTIVE, default on).
 * A failure is logged once, never per frame. */
extern int dw3_feat_icons;
extern int dw3_feat_travel;
extern int dw3_feat_objective;
void dw3_feats_arm(void);
/* The player's pad map (pad.map's middle column), applied by pad_read_buttons on the raw pad bits: the port keeps the
 * original PS1 positions and does not apply records_language's face-button rotation first. Identity unless
 * DW3_PAD_REMAP is set - the handheld's SDL2 runtime maps the pad itself and ignores input.gamepad, the SDL3 builds
 * apply it there and must not be permuted twice. */
u16 dw3_pad_permute(u16 buttons, int rotated);
/* Face-button text code (1, then 0x1B/0x2F/0x32/0x34) as a main-font letter, or 0. */
s16 dw3_button_glyph(u8 code);
/* The Guardromon network (port/game/definitive_travel.c): talking to a Guardromon (the inn's event script leaving `map`
 * for its 0xC.. save screen `next`) offers SAVE or TELEPORT. dw3_travel_leave returns 1 when the screen takes that
 * transition over (the map is kept until the player picks); dw3_travel_frame runs the screen from FIELDSTG's idle step
 * (fieldstg_manager_update) and returns 1 while it owns the field. */
int dw3_travel_leave(s32 map, s32 next);
int dw3_travel_frame(void);
/* Layout index of the story step's area on this map half, or 0. */
s32 dw3_objective_spot(s32 region);

/* Definitive combat (port/game/definitive.c; .plan/tasks/definitive-combat-prd.md). Layer 0, the battle log:
 * DW3_BATTLE_LOG=<file> appends one line per battle event; unset, every hook returns at once. def_get_damage and
 * def_get_special_damage are fightstg_rules.get_damage / .get_special_damage wrapped: same value, one line logged. */
void def_log_begin(void);
/* Layer 4 (definitive.c): the rules the game's turns follow. def_on is 0 under a replay script (the scripts are shared
 * with the emulator) unless DW3_DEFINITIVE is set. */
int def_on(void);
/* PRD 2, layer A: records_state.blocked[k] as the definitive edition reads it (stat debuffs 8..10 never blocked), and a
 * debuff's amount scaled by DEF_BOSS_DEBUFF_PCT where the original would have blocked it. */
int def_stat_blocked(int k);
/* PRD 2, layer J (definitive_evohint.c): the Digi-Lab's Next Forms panel, opened with Square from the Digimon screen. */
int def_evohint_enabled(void);
void def_evohint_label(s32 layer, s32 show);
void def_evohint_open(s32 layer, s32 slot);
int def_evohint_frame(void);
void def_evohint_tick(void);
s32 def_evohint_slot(void);
/* PRD 2, layer D: a level's TP for a partner (the band's gain, capped; the loss is noted), whether the last level-up
 * lost TP (read once, by the report's message), and that message ("Level N! TP full: train!"). */
s16 def_level_tp(s32 digimon, s16 tp, s16 level);
int def_tp_lost_take(s32 digimon);
const unsigned char *def_tp_full_text(void);
/* PRD 2, layer B: the Spirit a magic attack uses and the stat that defends against it, with the Strength and Defense
 * modifiers counted (get_special_damage, with fightstg_rules.stats filled). */
int def_magic_attack(u8 side);
int def_magic_defense(u8 side);
int def_debuff_scale(int k, int amount);
void def_party_turn_begins(void);
void def_action_resolved(u8 side, s32 id, s32 value);
void def_enemy_turn_begins(void);
void def_events_before_take(void);
void def_swap_chosen(void);
void def_baton_paid(void);                 /* the switch menu's Baton Pass was confirmed and its MP paid */
const unsigned char *def_baton_label(void);
/* Layer 5 (definitive.c), called by the rules when def_on: the damage of an elemental technique (physical: with the old
 * formula's own power term against the player; magic), and the open-target bonus of a blow with no element. */
int def_scale_physical(u8 side, int element, int value, int power);
int def_scale_magic(u8 side, int element, int value);
int def_open_bonus(u8 side, int defense_stat, int value);
struct PortState;
/* Layer 9 (definitive_bestiary.c): what the player has learnt of each enemy species, one byte each (bit = element index,
 * Fire..Dark), saved per slot in the card's spare tail. `index` is the species' record in SDIGIEDT (0..192); the id of
 * a FightstgEnemyRecord goes through def_bestiary_index. */
void def_bestiary_reset(void);
void def_bestiary_discover(int index, int element_mask);
int def_bestiary_known(int index);
int def_bestiary_index(int species_id);
void def_bestiary_savestate(struct PortState *s);
void def_mp_hook(void); /* layer 8: the rookies' MP patch, at a new game and at each battle's start (not under a script) */
/* Layer 6 (definitive.c): the remade techniques. def_tech_effect runs one of Libra / Relay / Resist Down / Mega Break
 * (0: not one of them, 1: done with a message, 2: no effect); def_apply_resist_mod lowers an enemy's resists as
 * get_stats builds them; def_message is the text of message type 23. */
struct FightstgStats;
int def_tech_effect(u8 side, int tech, int *msg);
void def_apply_resist_mod(int side, int member, struct FightstgStats *out);
int def_resist_down_mask(int side, int slot);
int def_resist_down_families(int side, int slot); /* the same, a pair of elements as its family's one bit */
const unsigned char *def_message(int i);
int def_swap_free(void);       /* a swap would be free now: the HUD's Tag button shows it */
int def_target_staggered(void); /* the current target is open */
int def_idle_pose(u8 side);     /* the pose of a side, or -1 for the original rule */
struct PortState;
void def_savestate(struct PortState *s);
void port_hud_arm(void); /* game_mods.c: the battle HUD defaults, at each battle start (not under a replay script) */
void def_log_turn(int side);
void def_log_action(int side, int kind, int arg);
void def_log_end(int result);
/* Layer 1: the replacement for entry `i` of the text file loaded at `data` (font_get_entry), or NULL. */
void *def_text_override(void *data, s32 i);
/* Layer 2, Inspect: the glyph-byte label of element `e` (0..6: Fire, Water, Ice, Wind, Thunder, Machine, Dark), and the
 * label for element `e` of a target with these resists (Weak / Resist / -, ??? while not learnt), with its palette in
 * the 14-pixel font in *palette. */
const unsigned char *def_inspect_element(int e);
const unsigned char *def_inspect_state(const short *resists, int e, int *palette);
/* The screens that show elements (Inspect, the bestiary, the technique list): the element's picture as text of the
 * 14-pixel font; a technique's element index (-1: it has none); and def_inspect_state for the bestiary's species
 * `index`, from its SDIGIEDT record `rec` (the base resists, with the port's table) instead of the battle's. */
const unsigned char *def_element_icon(int e);
/* The element's family (Ice counts as Water, Machine as Thunder: the pairs of the multiplier's affinity) and its picture. */
int def_element_family(int e);
/* The grouped screens (the bestiary, Inspect): family `f` (0..4: Fire, Water+Ice, Wind, Thunder+Machine, Dark), its
 * first picture or with `second` its other one (empty for a family of one), its name (the first element's), and its
 * line's state as def_inspect_state / def_bestiary_state give an element's. */
const unsigned char *def_family_icon(int f, int second);
const unsigned char *def_family_name(int f);
const unsigned char *def_inspect_family_state(const short *resists, int f, int *palette);
int def_tech_element(int tech);
struct FightstgEnemyRecord;
const unsigned char *def_bestiary_state(struct FightstgEnemyRecord *rec, int index, int e, int *palette);
const unsigned char *def_bestiary_family_state(struct FightstgEnemyRecord *rec, int index, int f, int *palette);
/* Layer 2: the sound that tells a hit on a weakness, a neutral one or a resisted one (the player's elemental
 * techniques), played where the hit's own sound plays (fightstg_script_run_sound). */
void def_hit_feedback(void);
/* OVERFINISH: the Bits earned by the blows that hit an enemy already knocked out in the battle just won; given once. */
s32 def_overfinish_take_bits(void);
/* Hard Mode (two Digimon in the party): New Game's choice and its use by the new game's reset; the third slot emptied
 * where a party was set; 1 when the lab must refuse party slot `slot`; 1 when New Game asks (not in a replay script). */
/* Overlevel Rush: 1 when the battle just set up in records_state is far under the party (the field asks to skip it);
 * the results a win would have left, for the battle's report; 1 when a test takes the rush without the question. */
int def_rush_offer(void);
void def_rush_results(void);
int def_rush_auto(void);
/* PRD 4 layer A, the regional hunt (port/game/definitive_hunt.c): a map loaded (a new region resets the chain); a random
 * encounter starting (returns the battle to fight: the region's Rare Target in place of `battle` once the chain allows);
 * after fieldstg_start_battle, the Rare Target's enemies raised to its level; 1 while a Rare Target is being fought (the
 * party can always flee); the Rush took the encounter (it does not count). */
void def_hunt_map(int map);
int def_hunt_encounter(int battle);
void def_hunt_scale(void);
int def_hunt_active(void);
void def_hunt_skipped(void);
/* PRD 4 layer C, the bestiary's drops: the Hunt Materials a species drops (a bit each), a material's name (glyph
 * bytes) and how many of it the player holds. */
unsigned int def_hunt_species_materials(int species);
const unsigned char *def_hunt_material_name(int material);
int def_record_material(int material);
void def_new_game_difficulty(int hard);
void def_new_game_apply(void);
void def_hard_party_fix(void);
int def_hard_slot_closed(int slot);
int def_bestiary_active(void);
/* The hit callouts (port/game/definitive_callout.c): one frame of the word over the target, from the battle's tick. */
void def_callout_frame(void);
/* Text in the callouts' polygon letters (capitals, 2, 3, ! and ?), for screens without the game's font: the width. */
struct GfxLayer;
int def_poly_text(struct GfxLayer *layer, s32 depth, int x, int y, int size, const char *text, const u8 *rgb);
/* Layer 3: overrides the resists of an enemy record (FightstgEnemyRecord) by the port's table, if the species has a row. */
struct FightstgEnemyRecord;
void def_enemy_override(struct FightstgEnemyRecord *rec);
/* Layer 2: 1 when the enemy acts twice before our next turn (call with the command menu open, never mid-calculation),
 * and the label shown then. */
int def_enemy_acts_twice(void);
const unsigned char *def_twice_label(void);
s32 def_get_damage(u8 side, s32 id);
s32 def_get_special_damage(u8 side, s32 id);
int def_train_pity_guaranteed(void);
int def_train_pity_bonus_on(void);
void def_train_pity_record(int perfect);
const unsigned char *def_train_pity_label(int *palette);
void def_gauges_slot_write(void *slot_save);
void def_gauges_slot_read(const void *slot_save);
int def_evo_form_seen(int form_id);
void def_evo_form_mark(int form_id);
int def_evo_skip_update(int *hold_frames, int skip_pressed);
int def_train_no_stat_loss(void);

#endif /* PC_PORT */

#endif /* PORT_H */
