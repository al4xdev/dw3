# Digimon World 2003 — handheld fork (R36S / XF35H)

This is a fork of the [Digimon World 2003 decompilation](https://github.com/gascarcella/dw2003recomp) and its PC port,
tuned to run on the **RK3326 handhelds** — R36S, XF35H and their clones: 4x Cortex-A35 (ARMv8-A in AArch32, 1.3 GHz),
1 GB of DDR3L/LPDDR3, a 640x480 panel and a 32-bit Linux userspace. Everything is rendered in software on the CPU and
written straight to `/dev/fb0`; there is no GPU rendering path, so the picture is byte-identical to the PS1's.
`docs/gpu/` records what the GPU would cost and what the device answered, with the probes that produced the numbers.

> [!IMPORTANT]
> **This repository contains no game data.** You need your own copy of *Digimon World 2003 (Europe) (En,Fr,De,Es,It)*,
> raw `.bin` + `.cue`, MODE2/2352, 692,146,560 bytes, SHA-1 `457cb233349ba841e03b33d8060f8fbcadd45cb3`.
> This project is not affiliated with or endorsed by Bandai, Bandai Namco or Sony.

## State at a glance

The game runs at a fixed pace (50 Hz PAL or 60 Hz). It **does not skip frames**: when a frame takes longer than its
budget (20 ms at 50 Hz, 16.7 ms at 60 Hz) the whole game slows down, audio included. So the goal is not "drop frames",
it is "keep every frame inside the budget".

| Scene | Budget | Measured frame | Verdict |
|---|---|---|---|
| Menus, map, field (2D) | 20 ms | ~8 ms | fits with room (a town frame: gpu 1.2 + audio 4.7 + present 1.0) |
| Opening movie (24-bit) | 20 ms | ~15 ms | fits; plays at full speed |
| Scripted first battle | 20 ms | ~13 ms of drawing | fits |
| **Heaviest battle** (large enemy, ~150k textured px) | 20 ms | **~30 ms** | **over budget: plays at ~33 fps** |

The battle row is the process's own CPU time (`/proc/<pid>/stat`) divided by its frame counter, both read from the
running process; the drawing part of it matches the GPU log's `walk`. For a battle of that kind the split is:

| Part | ms |
|---|---|
| rasteriser (`walk`) | ~18–19 |
| **audio (SPU mix)** | **4.9** |
| present | 1.0 |
| the game's own logic (GTE, models), compiled at `-O0` | ~6–8 |

Two of those were measured directly and are not estimates: the audio with `DW3_NO_AUDIO=1` (4.9 ms → 0.0 ms) and the
drawing with the GPU log. The game's logic is what is left after subtracting them.

**There is no regression to chase.** The same binary measured in the same battle gives the same ~30 ms; an earlier
measurement of 19.7 ms was a lighter fight. What made a session feel slow at 19 fps was the diagnostic `DW3_PHASE`,
which wraps every GTE call (~111k a frame) and costs 23 ms of the 20 ms budget — see
[Measuring performance](#measuring-performance).

## Problem → solution

Every number below was measured on the device, with a cycle counter read from EL0 (`mrrc p15, 1, c14`) and calibrated
against the monotonic clock.

### 1. The screen copy was the biggest single cost

**Problem.** Each frame the 320x240 PS1 framebuffer was expanded pixel by pixel into a 640x480 buffer and then blitted:
10 ms in 15-bit scenes, 23 ms in the 24-bit movie — more than half of the movie's frame.

**Solution.** `Fbdev_PresentVram` (`psxstack/runtime/fbdev_present.c`) expands the PS1 VRAM straight into the XRGB8888
framebuffer with NEON at integer 2x, with no intermediate buffer; 15-bit and 24-bit displays have their own row
expanders, and tall displays (the 576-line title) are cropped to the panel.

**Result.** 15-bit: 10 ms → 1.0 ms. 24-bit: 23 ms → 1.5 ms.

### 2. The vector path only took a case the game never uses

**Problem.** The battle's textured spans were drawn eight pixels at a time with NEON — but only when the primitive was
`GPU_OPAQUE`, and the whole 3D game draws in **`GPU_COPY`** (colour 128,128,128: the texel as it is) or
semi-transparent. Measured in a battle: `neon 152` of `8212` spans. **98% of the frame fell back to the scalar loop**,
and nothing in the logs showed it, because the log counted pixels, not paths.

**Solution.** `gpu_span_flat_neon` handles all three branches `gpu_shade_flat` has — the raw copy, the modulated opaque
case and the four semi-transparent modes (average, add, subtract, add-quarter) — with the same exact 16-bit arithmetic.
The escape hatch `DW3_NO_NEON_FLAT=1` forces the scalar path on the same binary, to time or to isolate a fault.

**Result.** `neon 4439 / scal 3459`, and every one of those scalar spans is an untextured polygon. The time gain was
small (~4%): by then the cost per pixel was the texture fetch, not the arithmetic (see 5).

### 3. The binary was compiled for the wrong CPU

**Problem.** The toolchain targeted generic `-march=armv7-a`. The Cortex-A35 is ARMv8-A in AArch32: it has **SDIV and
UDIV in hardware**, which armv7-a does not, so every integer division became a call into libgcc. `gpu.c` alone had 17
such call sites, and the rasteriser divides once per span row.

**Solution.** `cmake/armhf-linux.cmake` now passes `-mcpu=cortex-a35 -mfpu=neon-fp-armv8`.

**Result.** 17 `__aeabi_idiv` call sites → 0; 15 `sdiv` instructions; the scheduling is the A35's.

### 4. The meter was inside the frame it measured

**Problem.** With `DW3_GPU_LOG` on, the timer read the cycle counter about ten times per span — and the A35 TRM puts
that read at 6–15 cycles (the counter lives in the 24 MHz clock domain and is synchronised into the core). At ~8000
spans per battle frame that is **1–2 ms of the 20 ms budget**, plus a write to the SD card every second.

**Solution.** The meters are off in `dw3.sh` (left as commented lines); a measurement run turns them on by hand. Inside
the meter, the counters accumulate raw cycles and convert once per second, and the order-list walk uses the same
counter instead of two `clock_gettime` calls per list.

**Result.** ~1–2 ms back in a battle — and that is what put the heaviest battle under the budget.

### 5. The VRAM rows aliased the L1 — and still do not fit the caches

**Problem.** A texture fetch steps down `v` one VRAM row per pixel. The row is 1024 pixels = 2048 bytes, a multiple of
32 cache lines, so every row lands in the **same L1 set**: on a 32 KB 4-way L1 (the A35's), four rows fit and each
fetch evicts the one before it.

**Solution.** The rows are padded to 1056 pixels (2112 bytes = 33 cache lines) and 64-byte aligned, so no two rows
share a set and a row stays in one whole line. `psyq_gpu_vram_stride()` is the step; the video output, the framebuffer
expand, the hardware renderer's uploads, the texture dumper, the replay checkpoint dump (which keeps its 1024-wide file
format) and the golden harness all read through it.

**Result, measured on the handheld ("fetch + store", same 4-bit pattern):**

| row stride | ns per pixel |
|---|---|
| 1024 (2048 B, the old layout) | 119 |
| 1032 / 1040 / 1056 (padded) | 31 |
| working set that fits the 32 KB L1 | 14.6 |
| working set of 1 MB (the whole VRAM) | 54.8 |

The padding fixed the set alias (4x in the synthetic test) but barely moved the game, because the real limit is one
level further: the A35's **micro-TLB has 10 entries**, and a 2 KB row stride crosses a 4 KB page every two rows, so a
100-row texture walk overflows it continuously. The A35's hardware prefetcher only follows strides inside a single 4 KB
page, so it cannot see that walk either (`PLD` prefetching measured no gain). Bandwidth is not the problem: at
528 MHz the DDR does ~4.2 GB/s and the game touches ~75 MB/s. It is latency.

### 6. Fast travel did nothing

**Problem.** Pressing Cross on a visited area stored a destination, then the field menu threw it away: the menu is
rebuilt when the map page closes, and its init cleared the pending destination.

**Solution.** The destination survives the rebuild and is consumed when the menu closes. The landing point is the map's
own entry point (remembered the first time the map loads) or the position the player had there.

### 7. Two host-side bugs that hid the tests

**Problem.** `fbdev_present.c` and `gpu.c` no longer compiled for x86_64 (arm-only locals left unused under `-Werror`),
and the GPU golden harness did not link the frame-CSV note. The golden tests had been silently unbuildable.

**Solution.** The arm-only code is properly guarded; the harness stubs the note.

**Result.** `tests/host/replay.py gpu`: **715 cases, 7972 calls, 0 new mismatches** (140 known, the emulator's
fixed-point rounding).

## What was measured and rejected

This is as important as what was done — each of these looked like a win and is not.

| Idea | Why it is out |
|---|---|
| Reciprocal division instead of `/` (exact, Granlund–Montgomery) | all divisions in a battle frame add up to ~0.3 ms; the cost is not arithmetic |
| Direct packet dispatch (skip the command buffer) | the walk outside triangles is 0.6 ms of 19.3 |
| The CLUT copied once per triangle instead of per span | ~0.06 ms |
| Let the display controller scale 320x240 → 640x480 | the RK3326's VOPB Win0 does scale, but only **bilinear/bicubic** — there is no nearest-neighbour mode in hardware or driver, and it needs DRM/KMS instead of fbdev. It would blur the pixels |
| 2 MB huge pages for the micro-TLB | this kernel has THP disabled (`/sys/kernel/mm/transparent_hugepage` does not exist) |
| CPU/DMC governors | already `performance` on both; the DMC's 528 MHz is the DTS ceiling (194/328/450/528 available) |
| Pinning the game away from the interrupts | every IRQ goes to `cpu0`, but the scheduler already ran the game on `cpu1`; the option exists anyway (`DW3_CPU_AFFINITY`) |
| Rendering with the Mali-G31 | would break the byte-identical picture, which is the point of this port. **Done since**: `--renderer gles` (`docs/gpu/`) draws the VRAM on the GPU and in a battle is faster than the software rasteriser (41.9 fps against 37.7 on the same state); the picture differs from the software renderer's in 0.2-0.7% of the VRAM, in a handful of pixels along edges |

## Features of this fork

- **Handheld menu** (`portmaster/dw3/dw3-menu.py`): a framebuffer menu driven only by the gamepad, no keyboard.
- **Direct framebuffer present**: the reason the 2D game and the movies are fluid.
- **Fast travel**: press Cross on a visited area of the map to travel there.
- **Map objective**: the story's next area blinks on the map.
- **Button glyphs and remapping**: text shows the pad letter instead of the PS1 symbol; presets for Xbox, Switch and
  "modern" layouts.
- **XP boost**: battle experience multiplied (5x by default) behind a runtime flag.
- **Fast-forward**: up to 8x where the game has headroom (menus, dialogue, cutscenes). In the field and in battle the
  CPU is the limit, so it only mutes the audio there.
- **A frame meter that adds up**: `DW3_GPU_LOG` breaks the frame into something you can act on (see below).

## Running it

EmulationStation launches `/storage/roms/ports_scripts/Digimon World 3.sh`, which calls
`/storage/roms/ports/dw3/dw3.sh`. That script shows the gamepad menu and then runs the game on `/dev/fb0`.

```
/storage/roms/ports/dw3/
  dw2003          the port binary (ARM 32-bit)
  dw3.sh          menu + game
  dw3-menu.py     the framebuffer menu
  settings.json   settings and mods (video.renderer: "gles" on this machine; "software" reverts)
  pad.map         button letters shown in text
  game/dw2003.bin the disc image (yours)
  card1.mcd       memory card 1
  logs/           game.log (and gpu.log/present.log when the meters are on)
```

### Building

The build runs in Docker with an ARM hard-float cross toolchain:

```sh
./build_arm.sh
```

It configures `dw2003recomp/build/armhf-sdl2` through `cmake/armhf-linux.cmake` (Cortex-A35, NEON), builds with Ninja,
and copies the binary and the mods into `portmaster/dw3/`.

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

## Environment variables

| Variable | Effect |
|---|---|
| `DW3_FEAT_ICONS=0` | no button glyph replacement |
| `DW3_FEAT_TRAVEL=0` | no fast travel |
| `DW3_FEAT_OBJECTIVE=0` | no map objective |
| `DW3_FEAT_XP=0` | no XP boost |
| `DW3_VIDEO=fbdev` | present through `/dev/fb0` |
| `DW3_CPU_AFFINITY=<mask>` | pin the process, e.g. `0x4` for cpu2 |
| `DW3_NO_NEON_FLAT=1` | scalar flat textured span (escape hatch, same binary) |
| `DW3_NO_NEON_GOURAUD=1` | scalar Gouraud span (escape hatch) |
| `DW3_PRESENT_LOG=path` | one line a second with the present cost |
| `DW3_GPU_LOG=path` | one line a second with the whole frame's split (expensive: 1–2 ms in a battle) |
| `DW3_PHASE=path` | per-phase CPU split, windowed runs only. **Do not leave it on to play**: it wraps every GTE call (~111k in a battle) and costs 23 ms of the 20 ms budget |
| `DW3_NO_AUDIO=1` | measurement: play silence and skip the SPU mix (4.9 ms a frame). The voices' state stops advancing, so nothing else can be judged with it |
| `DW3_SKIP_BIG=N` | measurement: draw no triangle (by bounding box) or rectangle over N pixels, to weigh a scene's background |

## Measuring performance

The meters cost real frames, so `dw3.sh` leaves them off. To measure one run, uncomment the two exports (or set them by
hand) and read `logs/gpu.log` while the scene you care about is on screen. One line a second:

```
gpu: 19.3 ms fill 0 flat 0 ftex 130450 fblend 0 gour 1664 gtex 16155 gblend 0 |
depth4 146605 depth8 0 depth15 0 |
walk 19.3 ms pkt 18.8 ms tri 16.7 ms tset 0.9 ms row 1.8 ms flat 7.5 ms gour 2.5 ms mask 0.0 ms pre 0.6 ms |
spans 9891 neon 5734 scal 4157 fopq 5872 gok 0 alias 138 maskpx 0 load 0 | aud N ms
```

| Field | Meaning |
|---|---|
| `walk` | the whole order-list walk: everything the GPU does in a frame |
| `pkt` | the GP0 words inside it, i.e. every drawing command |
| `tri` | the triangles inside those (`tri` ⊂ `pkt` ⊂ `walk`) |
| `tset` | a triangle's setup, before its first row |
| `row` | a row's attribute setup plus the dirty-block stamp |
| `pre` | a row's preamble: the edges' x, the side test, the clipping |
| `flat` / `gour` | the span loops themselves (pixels) |
| `mask` | the mask-checked spans, one pixel at a time |
| `spans` / `neon` / `scal` | rows drawn, and which loop drew them |
| `fopq` / `gok` / `alias` | spans the vector path could take, and how many the self-aliasing check sent back |
| `load` | pixels of CPU-to-VRAM transfers (texture uploads) |
| `aud` | the SPU's per-frame mix: **4.9 ms** in a battle (0.0 with `DW3_NO_AUDIO=1`) |

Two measurements worth knowing by heart:

- **Is the whole frame inside the budget?** Compare the process's CPU time with wall time: at 50 Hz,
  `10 s of CPU in 10 s` is a frame at 100% of the budget. `cut -d" " -f14,15 /proc/<pid>/stat` twice, 20 s apart.
- **Are the pixels right?** Run twice with `--save-state N:/tmp/a.state` and `--save-state-exit`, once with NEON and
  once with `DW3_NO_NEON_FLAT=1`, and compare the files: they must be byte-identical except the stack block at the tail.

## Experiment log, including what failed

Every experiment of the handheld work, with its verdict, so none of it has to be rediscovered and any of it can be
undone. "Reverted" means the commit that tried it is in the history and the tree is back.

| # | Experiment | Result | State |
|---|---|---|---|
| 1 | Expand the VRAM straight into the framebuffer (`Fbdev_PresentVram`) instead of converting then blitting | 10 → 1.0 ms (15-bit), 23 → 1.5 ms (24-bit) | **kept** |
| 2 | NEON span for `GPU_COPY` and the semi-transparent modes (it only took `GPU_OPAQUE`, and 98% of the battle's spans are the other two) | 152 of 8212 spans took the vector path before; 4439 after. Time gain small (~4%): the cost was already the fetch, not the arithmetic. Pixels proven identical (below) | **kept** |
| 3 | `-mcpu=cortex-a35` instead of `-march=armv7-a` | 17 `__aeabi_idiv` call sites → 0, 15 `sdiv`; the scheduling becomes the A35's. No isolated timing (it went in with the rest) | **kept** |
| 4 | Every meter on by default in `dw3.sh` | The GPU log costs 1–2 ms a battle frame (`mrrc` is 6–15 cycles and it was read ~10× per span); `DW3_PHASE` costs **23 ms** a frame in a battle, because it wraps every GTE call (111k of them). A battle ran at 19 fps with it on | **reverted** (meters commented out) |
| 5 | Pad the VRAM rows (1032, then 1056 with 64-byte alignment) | Synthetic benchmark: 119 → 31 ns/px for a fetch that walks `v` one row per pixel. **In the real battle: no measurable gain.** Kept anyway because it is neutral there and removes a known aliasing; the in-game A/B never separated it from the `DW3_PHASE` mistake | **kept, unproven in-game** |
| 6 | `-O2` for the game's own C (it is built at `-O0`) | **Crashes at boot**: `sound_init → sound_update_loading → cdload_get_subfile`, SIGSEGV, 3 runs of 3, core dump with that stack. The decompilation has UB the optimiser exploits (one is documented in `fieldmenu.c`: `fieldmenu_name_pos[6 + i]` reads past a one-element array on purpose) | **reverted**; `PSXSTACK_GAME_OPT` exists for a narrower retry |
| 7 | `DW3_NO_AUDIO=1`: play silence and skip the SPU mix | **The audio costs 4.9 ms per frame** (0.0 with the switch). 24% of the budget | **kept as a switch**, the SPU itself untouched |
| 8 | `DW3_SKIP_BIG=N`: draw no triangle or rectangle over N pixels | **Inconclusive**: nothing in the battle is over 20k pixels, pixel counts identical with and without it. A battle's background is many ordinary polygons, not one big quad | **kept as a switch**; needs a per-texture-page filter to weigh the arena |
| 9 | Microbenchmarks of the span loop (`span_bench*.c`, in `.dumps/`) | Gave the 14.6 ns/px (L1-sized working set) vs 54.8 (the whole VRAM) figure that explains the memory limit, but also **misled experiment 5**: the synthetic walk was not the game's pattern | **reference only** |
| 10 | "The game got slower after a build" | **Not true**: the session log and the restored build show the same ~30 ms in the same battle. The slowdown felt was experiment 4 | **settled** |

## Rolling back

The tree is always left at a commit that produced the binary on the card, and both sides are checked by md5 after every
deploy. The states worth knowing:

| State | `psxstack` | `dw2003recomp` |
|---|---|---|
| the build that was called "the good one" (padding 32, meters off) | `4e17fca` (tag `r36s-good-50fps` is the one before the padding: `26fb162`) | `39128e1` |
| current: that build plus the two measurement switches and no `-O2` | `9201ed8` | `2feb311` |

```sh
# back to a commit, rebuild, deploy, and prove the card has it
git -C dw2003recomp/psxstack checkout <psxstack-commit>
git -C dw2003recomp checkout <dw2003recomp-commit>
./build_arm.sh                      # never pipe it through `head`: SIGPIPE kills the packaging step
md5sum dw2003recomp/build/armhf-sdl2/dw2003 portmaster/dw3/dw2003    # must be equal
rsync -rt portmaster/dw3/dw2003 root@<console>:/storage/roms/ports/dw3/
ssh root@<console> md5sum /storage/roms/ports/dw3/dw2003             # must match the two above
```

Two traps, both hit during this work: `build_arm.sh` runs with `set -euo pipefail`, so `./build_arm.sh | head` aborts
the script before it copies the binary (you then deploy the previous one); and the symbol addresses move between builds,
so reading a state variable out of `/proc/<pid>/mem` needs the address from the binary that is actually running
(`port_frames` was `0x01863750` in one build and `0x017c3750` in the next).

## What is left

The owner's decision is **not to touch the audio**, so the 4.9 ms it costs stays as it is. What is left is the
rasteriser and the game's own code:

1. **The scene's background** (unmeasured): a battle draws ~74k flat pixels — about one full screen — which is where
   the `flat` 5.0 ms goes, and the arena is most of it. To weigh it, filter by texture page (`(tex_y >> 8) << 4 |
   (tex_x >> 6)`), not by polygon size.
2. **A decoded texture cache** (~4 ms of the `flat` time if it works): texture rows decoded into compact blocks that
   fit the L1, so the vertical walk stops paying for the micro-TLB (10 entries, 2 KB rows cross a 4 KB page every two
   rows). The benchmark puts that at 14.6 ns/px against 56 today. The one change that attacks the memory limit itself,
   and the riskiest.
3. **The game's own C at `-O0`** (~6–8 ms): `-O2` for everything crashes (experiment 6), so a retry would have to be
   per module, and only after the UB is found.

## Known limitations

- **The heaviest battles are over budget**: ~30 ms of work against 20, so they play at ~33 fps (measured on the process's
  own CPU time). Light battles fit.
- **The audio costs 4.9 ms a frame**, audible or not. It is the second largest item after the rasteriser, and by the
  owner's decision it is left alone.
- The game's own C is built at `-O0` (`-O2` crashes at boot: undefined behaviour in the decompilation, see the
  experiment log), so its share — roughly 6–8 ms of a battle frame — stays.
- Fast travel lands on the map's entry point (or the remembered position), not on a specific door.
- Widescreen is off on the software renderer: it needs the hardware renderer.
- With fast-forward on, the audio is muted. That is deliberate: at 8x the sound would be eight times faster and
  choppy.
- The boot/title screen is 320x576 and falls back to the converting present (~16 ms a frame there, for under a second).

## Credits and license

The decompilation and the PC port are the work of the upstream project. This fork adds the handheld path: the direct
framebuffer present, the Cortex-A35 codegen, the vector span work, the VRAM row layout, the frame meter and the
handheld extras. Our own work is under the [MIT licence](LICENSE); the game remains the property of its publisher.
