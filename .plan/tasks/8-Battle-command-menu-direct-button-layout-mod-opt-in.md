# Battle command menu: direct-button layout mod (opt-in)

## Goal

Replace the battle's main command list (Fight / Tech / DV / Tag / Item / Run, the vertical list with a cursor) with a
menu where **every action has its own pad button**, drawn as a compact cross-shaped HUD centred where the old list was.
Only this one menu changes. Every submenu (technique list, item list, digivolve, member/tag) keeps its current
confirm/back behaviour. The work ships as a built-in mod, **off by default**; with it off the game is byte-for-byte the
current one (the PS1 build never sees any of this).

The owner does the final on-device test (RP5 / handheld). Everything before that is built and verified here without them.

## Findings that make this small (all verified in the source)

- **One construction site.** `fightstg_command_menu_create(obj->last, &obj->choice)` is called only from
  `fightstg_command_update`, step 2 (`src/fightstg/fightstg_8008D3B4.c:2735`). Nothing else creates or reads a
  `FightstgCommandMenu`.
- **Tiny contract.** The menu only has to write `0..5` into `*result` (0 Fight, 1 Tech, 2 DV, 3 Tag, 4 Item, 5 Run) and
  end itself. `fightstg_command_update` maps that to substeps/`command`; WFIGHTMN reads only
  `FightstgCommand->command/arg/arg2`, which `command_update` fills in afterwards. A replacement object that honours
  this contract needs no change anywhere else.
- **Phases** (`fightstg_command_set_phase`): 0 idle, 1 HUD only, **2 main menu (ours)**, 3 confusion menu (separate
  object `fightstg_confused_menu_*`), 4 Tag-only member menu. `wfightts` (debug battle) only ever sets phase 0.
- **Rules the original enforces and the new menu must copy** (`fightstg_command_menu_update`/`_open`,
  `:3280-3338`): with `member->status & 8` entries 0-2 (Fight, Tech, DV) are greyed (palette 7) and refuse input; with
  `member->status & 0x20` entry 2 (DV) is greyed and refuses input. A refused press does nothing (the original still
  plays the confirm sound first; keep or drop that quirk when reviewing the diff).
- **Labels are localized data**, not literals: `text = cdload_module.files.get_file(records_language + 0x7F)`, strings
  1..6 (`set_text(window, text, i + 1)`). The new menu reuses them so it works in every language.
- **Pad bits** (`src/main/pad.c:257`): `get_pressed(0)` bits are the PS1 layout after the port's own pad map
  (`dw3_pad_permute`, so the player's remap already applies): bit 12 triangle, 13 circle (confirm = `0x2000`), 14 cross,
  15 square, 10 L1, 11 R1, 8 L2, 9 R2, 4-7 d-pad. **The left analog stick also sets the d-pad bits** (0x10/0x20/0x40/
  0x80), so d-pad shortcuts would fire from a stick push too; avoid d-pad for actions.
- **Mod plumbing to copy** (`port/game/game_mods.c`, `port/mods/*/mod.json`, `include/port.h`): a `PortMod` record, a
  `port_mod_<name>` int flag set in the mod's `start`, read in the game's C only inside `#ifdef PC_PORT`; a
  `port/mods/<id>/mod.json` manifest; `tests/port/settings.py` checks every manifest equals the registry
  (`--print-mods`). `battle_animations` is the closest model (flag read in `fightstg_script_update`).

## Design

### Button map (default, all configurable as mod options)

| Action | Button | Why |
|---|---|---|
| Fight | face button (default **Cross**) | most frequent action, easiest thumb |
| Tech | face button (default **Square**) | |
| Item | face button (default **Triangle**) | |
| DV | face button (default **Circle**) | |
| Tag | **L1** | rarer, off the face cluster |
| Run | **R1** | rarer; a shoulder avoids fleeing by accident |

Notes:
- Face buttons are named by **PS1 position**, as the port's pad map does, so the player's own map still applies.
- Do not use d-pad (stick conflict above). Do not use Start/Select.
- Clash check with the submenus: L1/R1 page the technique and item lists, but those are separate objects created
  *after* this one ends, and they read edge-triggered `get_pressed`, so a press that ended the main menu is already
  consumed. If a mapped button equals confirm (`0x2000`) the hand-over frame is the only risk: guard by ignoring input on
  the creation frame and by ending the menu cleanly (`locked`-style). Test this explicitly.
- Mod options: `fight`, `tech`, `item`, `dv`, `tag`, `run` as enums of `cross circle square triangle l1 r1 l2 r2`
  (duplicates are rejected at start with a log line and the defaults are used), plus `confusion_style` (Phase D).

### Layout (Phase C)

Reuse the old list's anchor: windows were created at x=`0x24`, y=`i*19+0x6D` (a 6-row column spanning y 109..204), so
the block's centre is about (0x24 + width/2, 156). Place a cross of four labels around that centre so the on-screen
position mirrors the physical button (Square-mapped action left, Circle-mapped right, Triangle-mapped top, Cross-mapped
bottom), with Tag and Run as small labels at the lower-left/right corners tagged `L1` / `R1`. Everything is drawn with
what the game already uses: `message_create_window(0x1005, 1, x, y)` for text and sprites from `CMFONTCS`/the HUD bank
for button glyphs, all on layer 0x1005 so it sorts with the HUD. Check overlap against the portrait window (right side)
and the HUD bars (y < 0x30).

## Phases

### Phase 0: groundwork (no behaviour change)
1. Add `port/mods/battle_menu/mod.json` and a `PortMod` record in `port/game/game_mods.c` with the options above and
   `int port_mod_battle_menu` (declared in `include/port.h`). Register it with the other built-ins; default off.
2. Update `tests/port/settings.py` expectations if it enumerates mods; run it.
3. Docs: add a "Battle menu" section to `docs/LAUNCHER.md` (the repo's convention; code cites it by heading).

### Phase A: kill the list (the menu becomes invisible but functional)
1. In `fightstg_command_menu_update`, under `#ifdef PC_PORT` and `port_mod_battle_menu != 0`, dispatch to a new
   function and do not create the six windows or the cursor. Keep the object, its INIT/RUN/END states and the `result`
   slot so `command_update` is unaffected.
2. The matching build must stay matching: put the new behaviour in a **separate PC_PORT-only file**
   (e.g. `port/game/battle_menu.c`) with one dispatch line in the original. Do not rewrite the matching C in place (its
   `LOOP_BLOCK` is register-allocation evidence, see `docs/MATCHING.md`).
3. Verify: mod on and no input, the battle waits in the command phase (no crash, no softlock); mod off, the original
   list appears.

### Phase B: one button per action (functional, ugly)
1. The new update reads `pad_state.get_pressed(0)`; for the mapped button of each action it applies the same checks as
   the original (`status & 8`, `status & 0x20`), plays `sound_module.play(0x4001C)`, writes `*obj->result = action` and
   sets `OBJECT_STATE_END`.
2. Ignore input on the creation frame (hand-over guard).
3. Verify on the host with the replay harness (`tests/port`, the `first_battle_save` fixture in `PORT.md`): drive each of
   the six actions by its button and assert `FightstgCommand->command` (Fight 0, Run 1, DV 2, Item 3, Tech 4, Tag 5/6
   after their submenus). Also assert: with `status & 8` a Fight press does nothing; with `status & 0x20` a DV press
   does nothing; a shortcut equal to confirm does not also confirm inside the submenu it opens.

### Phase C: the new UI
1. Draw the cross layout above, labels from `records_language + 0x7F` entries 1..6, greyed (palette 7) for refused
   actions exactly as the original, and a button glyph per entry where an existing font or sprite glyph exists; if none
   is usable, fall back to plain `L1`/`R1` text and coloured dots. No new art assets in this task.
2. Optional press flash of 2-4 frames: it must not change frame counts or RNG consumption of the battle, so flash only
   while the object is already ending, or skip it.
3. Verify by host screenshot at 1x and in the widescreen mod (no overlap with the portrait/HUD, readable at 320x240 on
   the handheld's scaling).

### Phase D: confusion menu (optional, last)
The confused menu (phase 3, `fightstg_confused_menu_*`, `:5364-5524`) is a *different* object with the same six-row
list but shuffled labels (`fightstg_confused_menu_order`), a `roll_confused` check and a message queue. Default: **leave
it as it is** (the shuffle is the whole effect; a button-per-action UI would defeat it). `confusion_style` option:
- `original` (default): unchanged list.
- `symbols`: same behaviour and RNG usage, but each of the six rows is drawn with a confusion marker instead of its
  label.

Glyph question: we cannot know which symbols the game's font draws without looking, and it has no emoji. So this phase
first renders a **test page**: one battle screen with a grid of candidate glyphs (Shift-JIS symbols such as `？！～★☆◎○●×△□`
and the extra glyph range `01 xx`) so the owner can send a screenshot and we pick from what really renders. If nothing
usable shows up, `symbols` is dropped, not faked.

## Risks and handling

| Risk | Handling |
|---|---|
| Matching build breaks | New code only under `#ifdef PC_PORT`, in a new file; one dispatch line in the original. Run the repo's matching checks. |
| Different RNG consumption desyncs replays | The menu consumes no RNG. Mod-off replays must be byte-identical; run all of `tests/port` with the mod off. |
| Shortcut equals a submenu's confirm | Creation-frame guard + explicit test. |
| Left stick fires d-pad bits | d-pad is not offered as a shortcut. |
| Cross layout overlaps portrait/HUD | Screenshot check, normal and widescreen. |
| Confused menu stays a list | By design; documented. |
| Handheld pad map makes the defaults awkward | Options are configurable; owner tests on device. |

## Definition of done (before handing to the owner)

- Mod off: full existing test suite green, replay goldens unchanged.
- Mod on: new test in `tests/port/mods.py` covers all six shortcuts, the two refusal rules and the no-double-confirm case.
- `tests/port/settings.py` (manifest == registry) green.
- Host screenshots of the new menu (normal and widescreen).
- `docs/LAUNCHER.md` section written.
- Hand the owner: the built mod/zip, the default button table, and what to check on device (button feel, overlap, Run on
  R1, Tag on L1, greyed states while paralysed/confused/sealed).

## Out of scope

Submenus (technique, item, digivolve, member, target); the field menu; changing battle rules; new art assets; touch/mouse.
