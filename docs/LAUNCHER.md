# The launcher and the mods: this game's part

This page is mostly the reference for this game's built-in mods (including the LED bar) and its 60 Hz mode. Two
programs are called "the launcher" in this repository:

- The desktop launcher is psxstack's (`psxstack/launcher/`), built for this game from `port/game/game.json`
  (`cmake -S psxstack/launcher -B build/launcher -G Ninja -DPSXSTACK_GAME_JSON=$PWD/port/game/game.json
  -DPSXSTACK_VERSION_ROOT=$PWD -DPSXSTACK_TOOLS_DIR=$PWD/tools`; `build/launcher/dw2003-launcher`). The contract
  between it and the game, the settings directory and file (schema 1), the mod manifest and runtime, its screens and
  self-test, and the crash report are psxstack's `docs/LAUNCHER.md` and `launcher/README.md`.
- The handheld launcher is the shell script `handheld/Digimon World 3.sh`, with the framebuffer menu `handheld/dw3-menu.py`
  (`handheld/README.md`). Where it touches the mods (the DEBUG group, the LED bar), this page says so.

The decisions behind the design: DECISIONS "Launcher and mods".

Code cites this file as `docs/LAUNCHER.md "<heading>"`; keep the headings stable.

## The game's side of the contract
- `dw2003 --config FILE` and `--print-settings` are psxstack's runtime (`psxstack/runtime/settings.c`, `input.c`,
  `mods.c`); `tests/port/settings.py` checks the round trip, the defaults, the overrides and the errors against this
  game's build, and that every manifest under `psxstack/mods/` and `port/mods/` equals the game's registry
  (`--print-mods`).
- The disc check is the game's description's: `port/game/game.json` names the unpatched EU disc (SLES-03936) by its
  SHA-1 and size; the launcher and the game both check against it.
- The environment variables keep their names (`DW3_CONFIG_DIR`, `DW3_GAME`, `DW3_SELFTEST_*`, `DW3_PORT_*`): the
  description's `env_prefix` is `DW3`.
- Texture packs are data mods in the settings directory's `mods/` (psxstack `docs/LAUNCHER.md` "Data mods"; the
  launcher passes `--mods-dir`); `tests/port/settings.py` checks `mod_order` and the data mods' settings, and
  `tests/port/textures.py` the packs found that way.
- The self-test with the real disc and game: `DW3_SELFTEST_DISC=$PWD/iso/dw2003.cue
  DW3_SELFTEST_GAME=$PWD/build/port-sdl/dw2003 SDL_VIDEO_DRIVER=offscreen build/launcher/dw2003-launcher --self-test DIR`.

## Built-in mods

### Fast-forward

`fast_forward` (runtime only, no game C). Options: `hold` (binding, default `Tab`), `toggle` (binding, unbound),
`speed` (enum `2x 3x 4x 6x 8x unlimited`, default `4x`), `mute` (bool, default true).

While it is on (the hold binding held, or after the toggle) the pace is the nominal rate times the speed (`4x`: 200
vsyncs a second at 50 Hz, 240 at 60; `unlimited`: no pace), the window presents at most 60 images a second (every vsync
is still drawn), and with `mute` the audio device's queue is cleared and nothing is queued (the SPU keeps rendering,
since LIBSND reads its envelopes; a `--wav` is unchanged). The window's title says it is on. The game, its log and its
record are those of an unthrottled run, which are byte-identical to a paced run's.

Test hook: `DW3_PORT_FAST_FORWARD=ON:OFF` (only while the mod is enabled) turns it on for ON vsyncs and off for OFF,
repeating (`tests/port/settings.py`).

### Skip dialogues

`skip_dialogues`. Options: `toggle` (binding, default `F2`), `hold` (binding, unbound), `fast_forward_waits` (bool,
default false). While it is on, `port_mod_skip_dialogues` is 1 and three hooks act:

- `message_window_update` (`src/main/message.c`), RUN case: a revealing window (`speed > 0`, not done) shows its whole
  page at once (`message_find_page_end`). This covers every message window, the overlays' too.
- Its DONE step 1: a wait for button 13 (confirm) goes on by itself. Every wait in the disc's text tables uses that
  button; any other wait is left to the player.
- `fightstg_message_step` step 3 (`src/fightstg/fightstg_8008D3B4.c`): the battle's message wait goes on as a press
  would (without the press's sound).

No pad press is synthesised, so choices, menus, name entry, shops, the inn and the save prompts still wait, and a
closed dialogue does not talk to the NPC again. Not covered: the code-driven prompts of the card game, the status
screen and other overlays.

With `fast_forward_waits`, the mod also asks for fast-forward (with `fast_forward`'s speed and mute, or their defaults
when that mod is off) while FIELDSTG is the current overlay and `fieldstg_stage.event_running` is set: a cutscene's
scripted waits, walks and bubble animations. Free walking is not sped up.

Test hook: `DW3_PORT_SKIP_DIALOGUES=1` (only while the mod is enabled) starts it on. `tests/port/mods.py` runs
`first_battle_save`'s route with no press through the scenes.

### Disable battle animations

`battle_animations`. Option: `hit_reaction` (bool, default true). No hotkey: it acts whenever enabled
(`port_mod_battle_animations`).

The battle's rules (every roll, damage, HP) run before an action's animation script starts; the scripts only present
them. The hook sits in `fightstg_script_update`'s INIT step 0 (`src/fightstg/fightstg_8008B630.c`), after the script's
stream is found and before script 12's sound-bank load. For scripts 5 and up (attacks, techniques, items) it asks
`port_battle_cut` (`port/game/game_mods.c`), which scans the stream (`port/game/battle_scan.c`) and returns:

- **-1, run it:** a stream that does not scan to its end (none on the disc).
- **1-4, the target's reaction** (`results[3] + 1`: flinch, heavy hit, KO, dodge) for a script with a child command
  when `hit_reaction` is on. The hook restarts the same object as that reaction (side flipped; results, stage, effect
  and hit sound zeroed; step 0 again), as the child command would have.
- **0, end it at once:** a script with no child command, or every script when `hit_reaction` is off, except that **a
  knock-out keeps its KO reaction** so the model ends on its KO pose.

In each case it plays the script's first hit sound (`fightstg_script_run_sound`'s choice). Reaction scripts 1-4 created
directly (the defeat camera, `fightstg_player_reaction`) and the scenes outside the scripts (entrances,
digivolutions, the intro and defeat cameras) are left alone: they carry functional writes. `tests/port/battle.py`
checks the scanner against every script on the disc; `tests/port/mods.py` runs the first story battle with the mod on.

### Battle text

`battle_text`. Option: `mode` (enum `off` / `skip` / `async`, default `async`; read every vsync, so it applies live).
Flag: `port_mod_battle_text` (0, 1, 2).

The battle's message box (`FightstgMessage`, `src/fightstg/fightstg_8008D3B4.c`) is presentation only: the rules ran
before it is shown, but every action waits for it to be dismissed with a confirm press. The hook sits in
`fightstg_message_update`'s RUN, the frame a message is ready (`fightstg_message_port_take`). Each message has a class,
set by `fightstg_message_show` from its type and text ID (`ESMFIGHT`):

- **keep:** the victory (0x52) and defeat (0x53) texts. They are the game's pacing (`wfightmn_victory_wait` closes the
  victory text after 100 frames), so they run as on the disc in every mode.
- **drop:** what the animation already says: a plain "Attack" (8), "Evade" (0x1D), "Focus on attack/guard" (0x3C, 0x3D),
  "Special effect up" (0x3E), "Blast gauge up" (0x3F), "Excited" (0x40). Ended at once in `skip` and `async`.
- **normal** (technique and item names, damage, healing, statuses, switches...): `skip` ends it at once; `async` hands
  its two windows to a **toast** and ends it, so the action goes on (the animation starts with the technique's name
  still up, and the damage replaces it when it arrives). A chain (a queued follow-up: regeneration, the counter's
  damage) shows only its last page. The toast stays 90 frames unless a newer text replaces it.

The toast is owned by no object: the battle's slots object (`fightstg_slots_update`) runs and draws the windows each
frame, and `fightstg_slots_create` clears it for a new battle. Its state (`port_battle_text_windows`,
`port_battle_text_frames`, in `port/game/game_mods.c`) is named in the save state. No random number is drawn and no
rule changes, but the frame count of a battle does, so the scripted replays of the disc's own battles do not apply with
the mode on.

The handheld's menu (`handheld/dw3-menu.py`, GAME > BATTLE TEXT) cycles OFF / SKIP / ASYNC and ships it on, in
`async`. The stack has no "on unless the settings say otherwise" for a mod: on a desktop it is off until switched on.

### Save anywhere

`global_save`. Option: `restore_map_state` (bool, default true). No hotkey: it acts whenever enabled
(`port_mod_global_save`).

The game only saves at an inn: the inn's event script changes the map to STGMCARD's 0xC01..0xC19 (the inn's name for
the slot summary) with entry -1 (save mode), and Back returns to the previous map. With the mod on, the field menu
(START; `src/main/fieldmenu.c`) gets a last entry, **SAVE** (the save screen's title, `?SMEMCRD` entry 1, so it is in
every language), in the field's menu only (not in the status screen's copy of the menu). Chosen, it makes the same map
change (`port_global_save_open`: the inn's 0xC map when the map is an inn's, else 0xC00), so STGMCARD, its card
prompts and its Back work as at an inn; the slot summary shows the area (`stgmcard_map_areas` covers every field map)
and, off an inn, no place name. Six entries use the bank's 6-entry panel; the seventh (with the card case) stretches
it by one row.

A load returns to the saved map at the saved position but as a fresh entry (`map_is_new`): the attribute layer, the
player's depth and height, the per-visit flags and a few more fields after the slot's 0x26C4 bytes are reset, which is
right at an inn but can put the player on the wrong floor of a multi-layer map. So the save also writes those
(`GsRecord`, `port/game/game_mods.c`) into the slot's unused tail (slot offset 0x26C4, 0x28 bytes, written to the card with
the slot and outside the game's checksum: a PS1 or an emulator ignores it and loads the save as a fresh entry), and a
load with `restore_map_state` puts them back and marks the map as revisited (`field_last_map`), so FIELDSTG resumes it
as after a Back from the save screen. A save without the record (an inn save of the plain game) loads as before. With
the mod off, the binary is the plain game: the menu has no SAVE and a load restores nothing.

`tests/port/mods.py` saves from the lab (map 0x206), returns, reboots and loads, with `restore_map_state` on and off,
and with the mod off; `tests/saves/run.py` loads the card it writes in the emulator.

### Preset language

`preset_language`. Option: `language` (enum `english french german italian spanish usa japanese`, default `english`,
`applies: restart`). No hotkey: it acts at boot whenever enabled (`port_mod_preset_language`).

The game boots into CNTY_SEL (map 0x1600), whose menu sets `records_language` (the offset of every localized text
file ID: 0 JPN, 1 USA, 2 ENG, 3 FRA, 4 ITA, 5 GER, 6 SPN) and goes on to the opening, map 0xE02 (0xE01 for Japanese).
With the mod on, CNTY_SEL's root object (`src/cnty_sel/cnty_sel_80082CE8.c`), once the screen's sound bank is in, sets
`records_language` to the option's code and makes that map change itself: the screen's image, menu, music and fade
are never loaded. Nothing else on the EU disc reads the choice; the title's New Game builds the new game's records
(`gamestate_init_records`) in that language. A reset goes through CNTY_SEL again, so the language stays. The screen
offers only the five EU languages, but the USA and Japanese text sets are on the disc too (`docs/DISC_LAYOUT.md`) and
the EU code handles them: Japanese takes the opening's 0xE01 path, with the Japanese release's intro art (both
play-tested to the memory card screen).

`tests/port/mods.py` boots each language (the opening's map and `records_language`, and the mod off still at the
screen) and runs `new_game`'s route from the opening in French, whose new game has the French deck names.

### Logs (the DEBUG group)

The game's commentary is one switch, `debug.logs` in `settings.json`: `off` (the default) prints nothing, `soft` prints
the startup lines and the events, `heavy` adds the periodic measurements (the per-second `present:` line among them).
`handheld/Digimon World 3.sh` reads the value and passes it as `DW3_LOG`; the runtime's `port_log` honours it (`runtime/main.c`,
`port_log_verbose()` for the periodic lines). **With the logs off the game log stays empty** - the file is still created,
because the launcher appends to it - and the crash report keeps its last lines either way, so a report is possible
without them. The separate one-off meters (`DW3_PRESENT_LOG`, `DW3_GPU_LOG`, `DW3_PHASE`) are commented in that launcher and
are not the same thing: they write their own files and some cost real frames.

`SAVE LOGS`, in the same group, writes `dw3-logs-<date>.zip` beside the port: `logs/*`, `crash-*.txt`, `settings.json`,
`pad.map`, `tui.state` and a `system.txt` with the renderer, `uname` and the date. The cards and the disc are left out.

### Widescreen battles

`widescreen`. No options, no hotkey: while enabled, the battle (FIGHTSTG) is shown 16:9 through the GPU renderer's wide
canvas (psxstack `runtime/render_gpu_wide.c`; issue #71). The game is unchanged: FIGHTSTG already sends the 3D a 16:9
frame needs (its only screen-space cull, `fightstg_model_mesh_is_visible`, keeps a mesh with a bounding point within
the clip plus 64 pixels: 224 from the centre, against the 213.5 of a 16:9 frame), and the GPU clipped it to the
320-wide drawing area. The mod's frame callback (`port/game/game_mods.c`) calls `port_video_widescreen(1)` while FIGHTSTG
is the tier-1 overlay; its start asks for the canvas and a 16:9 window (`port_video_widescreen_enable`).

- **What widens:** the battle's stage and models (more of the arena at both sides); the background clear, fades and
  flashes cover the whole width. The HUD, the menus and the portrait window stay as drawn, in the middle 4:3.
- **What stays 4:3:** the field, the menus and every other overlay (320-wide 2D whose art ends at the screen's edge;
  the field's widening is issue #78), the movies, and everything with the software renderer (logged once).
- **The window** keeps its size; the picture switches between 4:3 and 16:9 inside it. A new window opens 16:9
  (`video.scale` x 427 x 240) when the mod is on and `video.renderer` is `gpu`.
- **Known limits:** the digivolution's 2D effects are 320-wide art and may show their edges at the sides; a stage
  modelled only at the front (stage kind 4) may show its back at the sides. Both are for the play-test.

`tests/port/render_gpu.py` (a GPU device): a battle frame's middle 320 columns equal the software image byte for byte,
its sides are drawn, the present is 16:9, the field stays 4:3, the frame log equals the run without the mod.
`tests/port/mods.py` (headless): the frame log unchanged and the software path's 4:3 logged once.

### LED bar

`led_bar`. No options: the on/off switch is the option. **Only the RK3326 handhelds have the hardware** (R36S, XF35H
and their clones: an MCU on `/dev/ttyS2`, `docs/XF35H.md` "The LED bar is on a UART, not in sysfs"), so everywhere
else the mod is a no-op that logs one line. While enabled and in a battle (FIGHTSTG is the tier-1 overlay) its frame
callback (`port/game/led.c`) reads the acting member's health and sends **one message per change** to `ledd`, the
package's daemon: the Digimon in front picks one of six readings, so a switch updates the bar at once.

| the acting member | the bar |
|---|---|
| 100-81% | green |
| 80-61% | green, breathing |
| 60-41% | lilac, breathing |
| 40-21% | red, breathing |
| 20-1% | red, blinking (200 ms on, 200 ms off) |
| 0 | the bar's own rotating mode, every colour |

Outside a battle the bar goes dark and its power rail drops: nothing is being watched there.

- **Why a daemon and not the game writing the bar:** what costs in that path is not the byte (1 ms on the wire at 9600
  8N1) but paying the setup every time. Measured on the console: the vendor script 66 ms a change, its binary 18 ms,
  opening and closing the tty with no fork at all ~14 ms, the same fd held open 0.15-0.9 ms. One process owns the line,
  takes an `flock` so there is only ever one, coalesces the requests of callers that queue up behind each other, and
  runs the animation's own clock. A change then costs the game ~2 ms, a handful of times a fight.
- **The animation does not outlive the game:** every reading is sent with the game's pid, and the daemon turns the bar
  off within a second of that process going away - a clean exit, a crash and a `kill` alike.
- **What it does not do:** it never changes the console's own LED setting (`ee_led.mode`); EmulationStation applies
  that at boot and when its LED screen is used, and a writer a lock cannot reach is left alone. There is no brightness
  in the hardware - a mode byte and nothing else - so the reading is carried by colour and rhythm.
- **Known limits:** the palette is the measured one, not the names of the vendor script, and the breathing modes were
  judged by eye on one console (`docs/XF35H.md`). Mode 25 and the `times` argument are untested.

The daemon's own tests are the device (`/.dumps/led-test/`): the palette map, the cost of each path and the singleton
lock. On the host there is nothing to see: `tests/port/mods.py` keeps the mod's silent no-op honest.

### Battle menu

`battle_menu`. **Always on** (the definitive combat: `.plan/tasks/definitive-combat-prd.md`); the mod's record only carries
its options (the buttons, `remember_cursor`, `buffer_input`), which apply while it is enabled and otherwise hold their
defaults. The one exception is a replay script (`--script`), which keeps the original list. A seventh button, **Inspect**
(L2, or R2 and then the free places when the player put an action there), opens a panel with the enemy's seven elements
as Weak / Resist / - and spends no turn; "Enemy x2" shows over the arena when the enemy acts twice before our next turn.
Redesigns **only the battle's main command menu** (Fight, Tech, Digivolve, Tag, Item,
Run): the list and its cursor are gone, each action has its own button, and a compact 3x3 grid shaped like the pad is
drawn where the list was: the up and down arms (Y, B) are columns, the left and right ones (X, A) and the shoulders
(L1, R1) are cells. Each action is one of the font's coloured pictures (fist, bolt, figure, arrows, book, wing) beside its
button's symbol, on a dark panel, with a short slide-in and press feedback (`fightstg_battle_menu_update`,
`src/fightstg/fightstg_8008D3B4.c`).
The submenus (techniques, items, Digivolve, Tag), the confusion menu and the rest of the battle are the game's own.
With the mod off nothing runs: the dispatch is one `#ifdef PC_PORT` test where the menu is created.

| option | default | |
|---|---|---|
| `fight` | right button | the place of each action on the pad: `south`, `west`, `north`, `east` (the face buttons by position), `l1`, `r1`, `l2`, `r2` |
| `tech` | L1 | |
| `digivolve` | top button | |
| `tag` | bottom button | |
| `item` | R1 | |
| `run` | left button | |
| `remember_cursor` | `true` | the technique and item menus open on what that Digimon used last (by member ID and kind); the command menu keeps its cursor between turns |
| `buffer_input` | `short` | `off`, `short` (1.0 s) or `long` (1.5 s): presses made in the window right before the command menu opens are given to it as it opens (up to 2: e.g. R1 then X to confirm the same skill before the menu appears) |

- **It follows the button mapping.** The options name a *place* on the player's pad, not a PS1 button. `pad.map`
  (`dw3_pad_permute`) says which PS1 bit the player's button at that place delivers (`port_battle_menu_bit`), so
  remapping the pad moves the action with the button, and the icon beside each label is that button's glyph, which
  `dw3_button_glyph` already turns into the player's own letter (A/B/X/Y on the handhelds). L1/R1/L2/R2 are drawn as text.
  The symbol uses the 9-pixel font, the pictures the 14-pixel one.
- **Same contract as the list:** the menu's `result` is -1, then 0-5 (Fight, Tech, Digivolve, Tag, Item, Run); a Digimon
  that cannot Fight, Tech or Digivolve (status 8) or Digivolve (0x20) gets those labels greyed and the press refused, as
  the list does. The confirm sound plays on a press, nothing else in the battle changes, and no random number is drawn.
- **Two actions on one button** cannot both work: the default buttons are used and one line is logged.
- **Known limits:** the D-pad is not offered as a shortcut (the left stick also sets its bits). Two actions on the left
  or right shoulder row stack one under the other. The confusion menu keeps its list.

`tests/port/mods.py` (headless): the mod on, each button opens its submenu and nothing else does, a refused action does
nothing, the mod off gives the original's frame log.

## 50/60 Hz

A setting, not a mod: `video.refresh: 60` or `--refresh 60` (default 50, PAL). It sets, before `port_overlay_init()`
(so the reset's snapshot holds it):

- `records_60hz = 1`, the flag the NTSC patch sets (`0x8005CCAC`). The game then runs its own 60 Hz mode: the video
  mode, the time step per vsync, actor speeds, the sound tick and the fade rate are compensated, as on a patched PS1.
- `port_rate` and the pace 60 (735 audio samples a vsync).
- `psyq_cd_set_vsync_hz(60)`: the drive's rate stays per second (2.5 sectors and 735 XA frames a tick; seeks keep their
  milliseconds).

`main_screen_pos` (the patch's second word, `0x8005CCB0`) stays 1 (PAL): it only adds the PAL screen offset, which the
port's video ignores, and picks the card game's PAL layout. `--fps 60` alone is not the 60 Hz mode: it speeds
everything up, music and the play-time clock included.

`tests/port/hz60.py` compares the port at 60 Hz with the patched game in the emulator (`tests/port/ntsc_patch.lua`
writes the two words at `main`'s entry; references in `tests/port/hz60/`).
