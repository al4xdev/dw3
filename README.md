# Digimon World 2003 — R36S handheld fork

This is a fork of the [Digimon World 2003 decompilation](https://github.com/gascarcella/dw2003recomp) and its PC port,
tuned to run on the **R36S** handheld (Rockchip RK3326, 4x Cortex-A35, 1.3–1.5 GHz, 640x480 panel, Linux ARM 32-bit
userspace). It renders everything in software on the CPU and writes straight to `/dev/fb0`.

> [!IMPORTANT]
> **This repository contains no game data.** You need your own copy of *Digimon World 2003 (Europe) (En,Fr,De,Es,It)*,
> raw `.bin` + `.cue`, MODE2/2352, 692,146,560 bytes, SHA-1 `457cb233349ba841e03b33d8060f8fbcadd45cb3`.
> This project is not affiliated with or endorsed by Bandai, Bandai Namco or Sony.

## State at a glance

The game runs at a fixed pace (50 Hz PAL or 60 Hz). It **does not skip frames**: when a frame takes longer than its
budget (20 ms at 50 Hz, 16.7 ms at 60 Hz), the whole game slows down, audio included. So the goal is not "drop
frames", it is "keep every frame inside the budget".

| Scene | Frame budget | Measured | Verdict |
|---|---|---|---|
| Menus, map, field (2D) | 20 ms | ~10–13 ms | fits; runs at full speed, fast-forward up to 8x works |
| Opening movie (24-bit) | 20 ms | ~15 ms | fits; the movie plays at full speed |
| Light battle | 20 ms | ~26 ms | close; still slightly slow |
| Heavy battle (large enemy) | 20 ms | ~32 ms | above budget; the remaining target |

The whole 2D game, the map, the menus and the movies are fluid. The open problem is the 3D battle: large enemies cover
more screen area, and the cost is per pixel, not per model — so a big enemy is the slow case.

## Problem → solution

Each change below was measured on the device with a per-frame timer; the numbers are from the R36S itself.

### 1. The screen copy was the biggest cost

**Problem.** Every frame, the 320x240 PS1 framebuffer was expanded pixel by pixel into a 640x480 buffer, then blitted.
That cost 10 ms in 15-bit scenes and 23 ms in the 24-bit movie — more than half the movie's frame.

**Solution.** `Fbdev_PresentVram` (`psxstack/runtime/fbdev_present.c`) expands the PS1 VRAM straight into the XRGB8888
framebuffer with NEON, at integer 2x, with no intermediate buffer. 15-bit and 24-bit displays have their own NEON row
expanders; tall displays (the 576-line title) are cropped to the panel.

**Result.** 15-bit: 10 ms → 1.1 ms. 24-bit movie: 23 ms → 1.5 ms.

### 2. The battle's pixels are one kind

**Problem.** The battle draws about 95% of its pixels through a single path: textured, opaque triangles with **4-bit
CLUT textures**. In a heavy fight that is 100k–170k pixels per frame.

**Solution.**
- `gpu_span_flat_neon` (`psxstack/psyq/gpu.c`): the final colour math for eight pixels at a time with NEON; the texel
  fetch stays scalar (ARMv7 NEON has no gather).
- The 16-entry 4-bit CLUT is copied to local memory once per span, unless the span draws into the CLUT's own row (then
  it falls back, so a span can never read a colour it just wrote).

**Result.** About 61 ns per pixel, down from ~155 ns before these two changes.

### 3. Gouraud shading

**Problem.** Gouraud-textured spans cost ~175 ns per pixel, almost three times the flat path.

**Solution.** `gpu_span_gouraud_neon` vectorizes the final colour math the same way.

**Result.** Little or no gain: the cost is the sequential per-pixel interpolation of five attributes (r, g, b, u, v),
which cannot be vectorized without changing results. Left in place; it may help scenes with more colour work.

### 4. A hash that ran for nobody

**Problem.** Every primitive drawn was hashed (a word loop per primitive, every frame) to feed the frame log and the
reference tests — even in a normal run that records nothing.

**Solution.** `psyq_gpu_hash_on` (`psxstack/psyq/libgpu.c`): the hash runs only when a log or a record is open
(`psxstack/runtime/framelog.c`). Tests and replays keep it on.

### 5. Fast travel did nothing

**Problem.** Pressing Cross on a visited area stored a destination, then the field menu threw it away: the menu is
rebuilt when the map page closes, and its init cleared the pending destination.

**Solution.** The destination now survives the rebuild, and is consumed when the menu closes. The landing point is the
map's own entry point (`fieldstg_stage.start_pos`, remembered the first time the map loads) or the position the player
had there; without one, the game's default is kept.

### 6. Measuring without measuring the meter

**Problem.** The first timer called `clock_gettime` per primitive; in a battle that is tens of thousands of calls per
frame, and it showed up as 20 ms of "overhead" in the numbers.

**Solution.** Phase timer (`DW3_PHASE`) for the coarse split, span counter (`DW3_GPU_LOG`) for the pixel mix, and the
ARM virtual counter (`mrrc p15, 1, c14`) for per-span timing — a few cycles instead of a syscall.

## Features of this fork

- **Handheld menu** (`portmaster/dw3/dw3-menu.py`): a framebuffer menu driven only by the gamepad, no keyboard. It
  picks the launcher options and then starts the game.
- **Direct framebuffer present** (see above): the reason the 2D game and the movies are fluid.
- **Fast travel**: press Cross on a visited area of the map to travel there.
- **Map objective**: the story's next area blinks on the map.
- **Button glyphs and remapping**: text shows the pad letter instead of the PS1 symbol; presets for Xbox, Switch and
  "modern" layouts.
- **XP boost**: battle experience multiplied (5x by default) behind a runtime flag.
- **Fast-forward**: works up to 8x where the game has headroom (menus, dialogue, cutscenes). In the field and in
  battle the CPU is the limit, so it only mutes the audio there.

## Running it

On the console, EmulationStation launches `/storage/roms/ports_scripts/Digimon World 3.sh`, which calls
`/storage/roms/ports/dw3/dw3.sh`. That script shows the gamepad menu and then runs the game on `/dev/fb0`.

```
/storage/roms/ports/dw3/
  dw2003          the port binary (ARM 32-bit)
  dw3.sh          menu + game
  dw3-menu.py     the framebuffer menu
  settings.json   settings and mods
  pad.map         button letters shown in text
  game/dw2003.bin the disc image (yours)
  card1.mcd       memory card 1
  logs/           game.log, present.log, gpu.log
```

### Building

The build runs in Docker with an ARM hard-float cross toolchain:

```sh
./build_arm.sh
```

It configures `dw2003recomp/build/armhf-sdl2` with `cmake/armhf-linux.cmake`, builds with Ninja, and copies the binary
and the mods into `portmaster/dw3/`.

### Deploying

```sh
rsync -rt -e ssh portmaster/dw3/dw2003 root@<console>:/storage/roms/ports/dw3/
rsync -rt -e ssh portmaster/dw3/dw3.sh  root@<console>:/storage/roms/ports/dw3/
```

The card's filesystem refuses Unix sockets, so debug sockets must live in `/tmp`.

## Controls

Default layout (the menu can remap it):

| Button | Action |
|---|---|
| D-pad | move |
| Cross (south) | confirm / talk |
| Circle (east) | cancel |
| Square / Triangle | menu |
| Start | field menu |
| Select + Start | quit |

On the map, Cross on a visited area starts a fast travel.

## Mods and environment variables

Mods live in `settings.json` under `mods`. The handheld extras can also be disabled without a rebuild:

| Variable | Effect |
|---|---|
| `DW3_FEAT_ICONS=0` | no button glyph replacement |
| `DW3_FEAT_TRAVEL=0` | no fast travel |
| `DW3_FEAT_OBJECTIVE=0` | no map objective |
| `DW3_FEAT_XP=0` | no XP boost |
| `DW3_VIDEO=fbdev` | present through `/dev/fb0` |
| `DW3_PRESENT_LOG=path` | one line a second with the present cost |
| `DW3_GPU_LOG=path` | one line a second with the pixel mix, the span timing and the texture depth |
| `DW3_PHASE=path` | one line a second with the per-phase frame CPU split |
| `DW3_NO_NEON_GOURAUD=1` | scalar Gouraud span (escape hatch) |

`DW3_GPU_LOG` line, for example:

```
gpu: 21.6 ms fill 0 flat 0 ftex 101556 fblend 2504 gour 1664 gtex 20292 gblend 0 |
depth4 124353 depth8 0 depth15 0 | flat 6.1 ms gour 3.8 ms tri ... spans ...
```

`ftex`/`gtex` are the textured pixels (flat and Gouraud), `depth4/8/15` the texture depth, and `flat`/`gour` the time
inside each span loop.

## Measuring performance

The game logs to `logs/game.log`. For a frame split, run with `DW3_GPU_LOG` and read `logs/gpu.log` while a battle is
on screen. The present cost is in `logs/present.log`.

What we know, from the device:

- The present is no longer a cost (≈1 ms).
- The 2D game and the movies fit the budget.
- In a heavy battle, the drawing is ~21–23 ms: `flat` ~6 ms, `gour` ~4 ms, and ~11–12 ms of list walk plus per-triangle
  setup.
- The open target is that 11–12 ms.

## Known limitations

- Heavy battles are still above the 20 ms budget, so they play in slow motion. This is the active work.
- Fast travel lands on the map's entry point (or the remembered position), not on a specific door.
- Widescreen is off on the software renderer: it needs the hardware renderer.
- With fast-forward on, the audio is muted. That is deliberate: at 8x the sound would be eight times faster and
  choppy.
- The audio costs ~4 ms per frame whether or not anything is audible.

## Credits and license

The decompilation and the PC port are the work of the upstream project. This fork adds the handheld path: the direct
framebuffer present, the NEON span work, the measurement tools and the handheld extras. Our own work is under the
[MIT licence](LICENSE); the game remains the property of its publisher.
