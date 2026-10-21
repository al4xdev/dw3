## Installing it (no tools needed)

1. Install the port with **PortMaster** on the console (Ports → PortMaster → search for "Digimon World 2003"), or unzip
   `dw3.zip` into the card's `ports/` folder by hand. On ArkOS and EmuELEC that folder is `/storage/roms/ports/`.
2. Put your own disc in `ports/dw3/game/` — the **`.cue` and the `.bin` together**, straight from your own copy of the
   game (Europe / PAL, disc 1, serial `SLES-03975`; a redump-style `.cue` + `.bin` pair is what the launcher expects).
   Copy them the way you copy any ROM: card in the PC, or over the network. The `.bin` is ~660 MB.
3. Launch it from the **Ports** menu. A gamepad menu comes first (that is the port's own, driven only by the pad): the
   bottom row is **PLAY**. Settings, button remapping and the LED bar are the rows above it.

The port contains no game data: without the disc in `game/` it says `NO DISC IN game/` and does not start.

If the game does not start after an update, the two files worth deleting are `settings.json` and `pad.map` in
`ports/dw3/` (they are rebuilt on the next launch).

## Notes

A source port of **Digimon World 2003** (PAL), built from [dw2003recomp](https://github.com/gascarcella/dw2003recomp): the
game's own C, decompiled to a matching build, compiled for 32-bit ARM, with the PlayStation's hardware (CPU, GTE, GPU, SPU,
MDEC, CD) reimplemented on the host. The disc's data is the only thing it reads from the game; nothing of the game is
included here.

It needs your own copy of the PAL disc, in `ports/dw3/game/` as described above.

Two renderers ship in the binary: the software rasteriser (byte-identical to the port's reference) and a GLES one that
draws the PlayStation's VRAM on the Mali. `ports/dw3/settings.json` picks one (`"video": { "renderer": "gles" }` or
`"software"`); on this device GLES is the default and is faster in 3D, the software one is faster in menus.

## What the menu offers

The menu that runs before the game is driven by the pad alone and writes `ports/dw3/settings.json` as you change it.
`Cross` enters a group or changes a value, `Circle` goes back, `Start` plays from anywhere, `Select + Start` quits. The
defaults are the configuration this port was tuned with, so a player who never opens it still gets the good setup:
8x fast-forward with the audio muted, the whole party sharing all the experience, and the XP boost at 2x.

| main row | what it holds |
|---|---|
| `GAME` | `FAST-FORWARD` (speed and off/on), `SKIP DIALOGUES`, `SKIP BATTLE ANIMS` and `HIT REACTION`, `SAVE ANYWHERE` |
| `XP` | `PARTY XP` (percentage of the party that shares), `XP / BITS BOOST` (off or 2x, experience, form experience and money together), `ALL FORMS' XP` |
| `VIDEO` | `HZ` (50 or 60), `RENDERER` (`GLES` for 3D, `SOFTWARE` for 2D-heavy scenes and the byte-exact reference) |
| `LED BAR` | the console's LED bar follows the weakest party member's HP in battle (installs a small daemon) |
| `SAVE LOGS` | writes `dw3-logs-<date>.zip` beside the port: the logs, the crash reports, the settings and the pad map |
| `BUTTONS` | remap the pad: a `LAYOUT` row (Xbox, Switch, PlayStation) for the letters, then one row per action (`CONFIRM`, `BACK`, ...) holding only the button |
| `PLAY` | start the game |

**The two brakes the XP boost removes** (this port's own doing, on the owner's call): the game caps form experience at 10
points a battle before the fifth form-level threshold and 50 after it - that ceiling is what makes a digivolution gain one
level per battle, and it is applied before the multiplier, so at 6x a battle still crept one level. With the mod on the
ceiling is skipped, so a won battle can raise several form levels. `ALL FORMS' XP` goes further: only the forms that
fought earn experience in the original, which leaves the other digivolutions at the lab for ever. Each form keeps its own
requirement table, so nothing is free - a basic form taken to 99 in the late game is a farm the player built.

## Controls

| Button | Action |
|--|--|
| D-Pad | Move / menu |
| A (east) | Confirm (Cross) |
| B (south) | Cancel (Circle) |
| X (north) | Square |
| Y (west) | Triangle |
| L1 / R1 | L1 / R1 |
| Start | Start |
| Select + Start | Quit |

All of it can be rebound in the gamepad menu that runs before the game (`BUTTONS` → `REMAP`); the menu writes the mapping
into `settings.json` and `pad.map`. `Select + Start` quits from anywhere.

## Tested on

| device / CFW | state |
|---|---|
| XF35H (RK3326, Mali-G31, 640x480) — EmuELEC 4.7-Nexus | **tested**: everything in this port was measured here |
| R36S and its clones (same SoC) | **not tested**, but the port was built against exactly that hardware (Cortex-A35, Mali-G31, 640x480, armhf) |
| AmberELEC | **not tested** |
| dArkOS / ArkOS | **not tested** |
| muOS | **not tested** |
| ROCKNIX (Libmali and Panfrost) | **not tested** |
| Knulli | **not tested** |
| Other resolutions (480x320, 720x720, 1024x768) | **not tested**: the present is written for a 640x480 panel and letterboxes what does not fit |

**If you run it on another device, please report what you see** — in the port's thread on the PortMaster Discord, with
the device, the CFW and its version, and the resolution. Two facts make a report useful:

- the frame rate line the game writes at exit: `port: window: N frames presented in X s (Y per second)` in
  `ports/dw3/logs/game.log`;
- whether the **films** show a rectangle of wrong pixels in the top-right corner. That is the one known rendering defect
  (see `docs/gpu/17-movie-artefact.md` upstream); knowing which devices and drivers show it is worth more than a fix
  nobody can test.

## Compile

The port is built with the game repository's ARM cross build (Docker with an armhf toolchain):

```
git clone https://github.com/gascarcella/dw2003recomp --recurse-submodules
cd dw2003recomp
tools/build_arm.sh      # configures with -DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=ON and packages ports/dw3
```

## Thanks

Thanks to the dw2003recomp project for the decompilation and the port, and to the PortMaster crew for the packaging
conventions this folder follows. Thanks also to the DuckStation project, whose VRAM-as-a-texture renderer is the design
the GLES backend here was measured against (`docs/gpu/01`).

## What this port stands on

| project | what it gave this port |
|---|---|
| [gascarcella/dw2003recomp](https://github.com/gascarcella/dw2003recomp) | the decompilation of the game's own C and the PC port — **the base this fork starts from** |
| [gascarcella/psxstack](https://github.com/gascarcella/psxstack) (this fork: [al4xdev/dw3-psxstack](https://github.com/al4xdev/dw3-psxstack)) | the host runtime: arena, overlays, the pump, video, SPU, audio, input, memory cards, settings, the mods engine, the GPU backend, the crash report and the measurement switches |
| [stenzek/duckstation](https://github.com/stenzek/duckstation) | **studied, not linked**: the VRAM-as-a-texture design, the batch key that became per-vertex state here, and the framebuffer-fetch strategy for the mask bit and semi-transparency. `docs/gpu/01`-`05` in the game repository record what was kept and what was rejected, each claim with a `file:line`; the implementation here is our own |
| [psx-spx](https://psx-spx.consoledev.net/) | the hardware reference everything was written from: the SPU's voices, envelopes and reverb, the GPU's commands, the GTE's arithmetic |
| PCSX-Redux | the **oracle** for the tests (SPU traces, replay runners) — never a source of code |
| [SDL2](https://github.com/libsdl-org/SDL) | the window, the gamepad, the audio device and the threads on the host |
| [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails) | the box art in `cover.png` |
| [PortMaster](https://github.com/PortsMaster) | the packaging rules this folder follows, and the multiverse catalogue a decompilation goes to |
