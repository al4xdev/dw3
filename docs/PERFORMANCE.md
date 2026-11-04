# Performance

Measured frame costs of the handheld port, in one place. Hardware: RK3326 (4x Cortex-A35 in AArch32 at 1.3 GHz, Mali-G31,
1 GB of RAM, 640x480 panel), one handheld and one firmware, at the machine's 60 Hz setting unless a figure says 50 Hz.
The design notes behind the GPU numbers are in [`gpu/`](gpu/README.md); the audio ones are in [`SOUND.md`](SOUND.md).

- [The budget](#the-budget)
- [Where a frame goes, by scene](#where-a-frame-goes-by-scene)
- [The heaviest battle: software against gles](#the-heaviest-battle-software-against-gles)
- [A gles battle frame](#a-gles-battle-frame)
- [A software battle frame](#a-software-battle-frame)
- [Audio](#audio)
- [Where gles loses](#where-gles-loses)
- [What the handheld work changed](#what-the-handheld-work-changed)
- [Tried and rejected](#tried-and-rejected)
- [Open items](#open-items)
- [Measuring](#measuring)

## The budget

The game runs at a fixed pace, 50 Hz (PAL) or 60 Hz, and does not skip frames. A frame that takes longer than its budget
(20 ms at 50 Hz, 16.7 ms at 60 Hz) slows the whole game down, audio included. The goal is therefore to keep every frame
inside the budget, not to drop frames.

## Where a frame goes, by scene

| Scene | Software | `gles` | Against the 20 ms budget |
|---|---|---|---|
| Menus, map, field (2D) | ~8 ms | ~11 ms | fits |
| Opening movie (24-bit) | ~15 ms | not measured | fits, full speed |
| Scripted first battle | ~13 ms of drawing | not measured | fits |
| Heaviest battle (large enemy, ~150k textured pixels) | ~27 ms (37.70 fps) | ~24 ms (41.85 fps) | over budget |

The 2D figures were recorded with the audio mix at 4.7 ms (a town frame: GPU 1.2 + audio 4.7 + present 1.0); the mix is
3.1 ms in the same scenes now. The battle rows are 600 frames from one saved state, read from the
`window: N frames presented in X s` line of `game.log`, with the same binary for both renderers.

## The heaviest battle: software against gles

The steps that took `gles` from 18.75 fps to 41.85 fps in that battle, each measured on the same state
(details in [`gpu/16-frame-budget.md`](gpu/16-frame-budget.md)):

| Step | fps |
|---|---|
| a read copy per draw batch | 18.75 |
| a read copy per vsync | 31.50 |
| one draw call a frame (copies still uploads) | 29.98 |
| plus GP0 80h as a GPU blit | 31.27 |
| plus the buffered readback | **41.85** |
| software renderer, same state | 37.70 |

The VRAM of the `gles` renderer differs from the software one in 0.2% of its texels before the 3D models appear and 0.7%
once they do (edges and 5-bit rounding). At the frames compared with `--screenshot` the picture that reaches the screen
is byte-identical.

## A gles battle frame

From [`gpu/16-frame-budget.md`](gpu/16-frame-budget.md), "What is left":

| Part | ms |
|---|---|
| the listener (packing ~1,100 primitives) | 1.0 |
| the flush (one draw) | 0.6 |
| the read copy (one blit) | 0.6 |
| the copies (two blits) | 0.0-0.1 |
| the present (PBO memcpy, 5-bit conversion, fb0 write) | 3.2 |
| the game's own CPU (recompiled code, GTE, SPU mix) | the rest, ~14 |

The last row includes the SPU mix (3.0-3.3 ms), which leaves about 11 ms for the game's own code by subtraction. The rows
come from the renderer's per-second meter and do not add up to the ~24 ms the fps figure implies; the note labels the last
row "the rest".

## A software battle frame

This split was taken earlier, in a heavier fight (~30 ms of CPU time per frame, not the 27 ms state above):

| Part | ms |
|---|---|
| rasteriser (the order-list walk) | 18-19 |
| audio mix | 4.9 at the time, 3.0-3.3 now |
| present | 1.0 |
| the game's own logic (GTE, models), left over by subtraction | 6-8 |

Do not combine these rows with the `gles` ones. The two splits come from different fights and different ways of
attributing the remainder; where they disagree on the game's own cost (6-8 ms against ~14 ms), the `gles` figure is the
one backed by the measurements in `gpu/16-frame-budget.md`.

## Audio

The SPU mix is the second largest item of a frame after the game's own code. Per vsync it was 4.7-4.9 ms (4.9 at 50 Hz and
882 samples, 4.7 at 60 Hz and 735 samples). It is now 3.1 ms in the scripted field/menu run (voices 2.27, of which
interpolation 0.31, reverb 0.71, rest 0.13) and 3.0-3.3 ms in a battle. The output is byte-identical to before: the WAV
of every run compared equal. The remaining cost is the voice loop. A sample count must accompany any figure, because it
moves with the rate. See [`SOUND.md`](SOUND.md), "What it costs".

## Where gles loses

In light 2D scenes `gles` is slower than the software renderer: 43.5 fps against 57.7 on the scripted boot and menu run
(1884 frames). Software draws those scenes in a couple of milliseconds and pays a 1 ms present. `gles` pays about 3.2 ms
for the present, and on the boot and menu scenes the readback waits 10-14 ms, because the frame's own GPU work (the boot
re-uploads its art to the VRAM target) is queued before it and the driver drains it there. Giving the read two vsyncs of
slack does not change this. A menu is fine either way, and a player who only uses 2D scenes can run `--renderer software`.

## What the handheld work changed

| Change | Effect |
|---|---|
| `Fbdev_PresentVram` (`psxstack/runtime/fbdev_present.c`) expands the PS1 VRAM straight into the XRGB8888 framebuffer with NEON at integer 2x, with no intermediate buffer | present 10 ms to 1.0 ms (15-bit), 23 ms to 1.5 ms (24-bit) |
| The NEON textured span now covers `GPU_COPY` and the four semi-transparent modes, not only `GPU_OPAQUE` | vector spans 152 of 8212 to 4439 (the rest are untextured polygons); only ~4% faster, because the cost was already the texture fetch |
| `-mcpu=cortex-a35 -mfpu=neon-fp-armv8` in `cmake/armhf-linux.cmake` instead of generic armv7-a | 17 `__aeabi_idiv` call sites to 0; the A35 has hardware SDIV/UDIV |
| Meters off by default: the cycle-counter read costs 6-15 cycles and was done ~10 times a span | 1-2 ms back in a battle |
| VRAM rows padded to 1056 pixels (2112 bytes) and 64-byte aligned, read through `psyq_gpu_vram_stride()` | fixes an L1 set alias (119 ns to 31 ns a pixel in the synthetic fetch test); no measurable gain in the real battle |

The memory limit is latency, not bandwidth: the DDR does ~4.2 GB/s at 528 MHz and the game touches ~75 MB/s. A texture
fetch walks one VRAM row per pixel, and the A35's micro-TLB has 10 entries, so a 2 KB row stride crosses a 4 KB page every
two rows and a 100-row walk overflows it continuously. The hardware prefetcher only follows strides inside one page, and
`PLD` prefetching measured no gain. A working set that fits the 32 KB L1 costs 14.6 ns a pixel; the whole 1 MB VRAM costs
54.8.

The meters were also a trap: `DW3_PHASE` wraps every GTE call (~111k a battle frame) and costs about 23 ms of a 20 ms
budget. A battle ran at 19 fps with it on, which looked like a regression and was not.

## Tried and rejected

| Idea | Why it is out |
|---|---|
| Reciprocal division instead of `/` | all divisions in a battle frame add up to ~0.3 ms |
| Direct packet dispatch, skipping the command buffer | the walk outside triangles is 0.6 ms of 19.3 |
| Copying the CLUT once per triangle instead of per span | ~0.06 ms |
| Letting the display controller scale 320x240 to 640x480 | the RK3326's VOP only scales bilinear or bicubic, with no nearest-neighbour mode, and it needs DRM/KMS instead of fbdev; it would blur the pixels |
| 2 MB huge pages for the micro-TLB | this kernel has transparent huge pages disabled |
| CPU and memory governors | already `performance`; the DMC's 528 MHz is the DTS ceiling |
| Pinning the game away from interrupts | every IRQ goes to `cpu0` but the scheduler already ran the game on `cpu1` |
| `-O2` for the game's own C | crashes at boot (`sound_init`, `sound_update_loading`, `cdload_get_subfile`): the decompilation has undefined behaviour the optimiser exploits. `PSXSTACK_GAME_OPT` allows a narrower retry per module |
| `glBufferSubData` into a reused vertex buffer | 105 us of stall per draw, 60 ms a frame in 2D; the orphaning `glBufferData` stays |
| Reading the `gles` target back as RGB565 | 28 ms a frame; a RGBA8 `glReadPixels` plus the 8-to-5-bit conversion costs 1.7-2.0 ms |
| NEON for the SPU's voice interpolation | the interpolation is only 0.3 ms of the mix, so it would not have paid |

## Open items

In the order of what they would cost to try:

1. **The gles present** (~3.2 ms a frame, the renderer's largest remaining cost): let fb0 take the format the target
   already has, or deliver the picture through a KMS plane the frontend lends. The second is close to the 17 ms swap this
   design exists to avoid.
2. **The audio's last 0.6 ms** (target 2.5 ms, now 3.0-3.3): what is left is the voice loop itself (2.3 ms), which would
   need the voices processed in parallel.
3. **The scene's background**: a battle draws ~74k flat pixels, about one screen, and the arena is most of it. Weighing it
   needs a filter by texture page, not by polygon size.
4. **A decoded texture cache**: texture rows decoded into compact blocks that fit the L1, so the vertical walk stops
   paying for the micro-TLB. The synthetic benchmark puts it at 14.6 ns a pixel against ~56 today. It attacks the memory
   limit itself and is the riskiest change.
5. **The game's own C at `-O0`** (~6-8 ms by the software split): a retry has to go module by module, after the undefined
   behaviour is found.
6. **The 0.7% VRAM difference of `gles`**: copy the software rasteriser's triangle edge rule (a half-pixel bias in clip
   space) and see whether it closes the gap.

## Measuring

The meters cost real frames, so `handheld/Digimon World 3.sh` leaves them off (commented exports). To measure one run,
enable them by hand and read `logs/gpu.log` while the scene is on screen. `DW3_GPU_LOG` prints one line a second:

```
gpu: 19.3 ms fill 0 flat 0 ftex 130450 fblend 0 gour 1664 gtex 16155 gblend 0 |
depth4 146605 depth8 0 depth15 0 |
walk 19.3 ms pkt 18.8 ms tri 16.7 ms tset 0.9 ms row 1.8 ms flat 7.5 ms gour 2.5 ms mask 0.0 ms pre 0.6 ms |
spans 9891 neon 5734 scal 4157 fopq 5872 gok 0 alias 138 maskpx 0 load 0 | aud N ms
```

| Field | Meaning |
|---|---|
| `walk` | the whole order-list walk: everything the GPU does in a frame |
| `pkt` | the GP0 words inside it, that is every drawing command |
| `tri` | the triangles inside those (`tri` within `pkt` within `walk`) |
| `tset` | a triangle's setup, before its first row |
| `row` | a row's attribute setup plus the dirty-block stamp |
| `pre` | a row's preamble: the edges' x, the side test, the clipping |
| `flat`, `gour` | the span loops themselves |
| `mask` | the mask-checked spans, one pixel at a time |
| `spans`, `neon`, `scal` | rows drawn, and which loop drew them |
| `fopq`, `gok`, `alias` | spans the vector path could take, and how many the self-aliasing check sent back |
| `load` | pixels of CPU-to-VRAM transfers (texture uploads) |
| `aud` | the SPU's per-frame mix |

It also prints the SPU's own split per vsync:

```
spu: per vsync (735 samples): total 3.11 ms = voices 2.27 (interpolation 0.31) + reverb 0.71 + rest 0.13
```

With `--renderer gles`, `DW3_GLES_LOG` adds the renderer's split (listener, flush, present). The variables are listed in
[`HANDHELD.md`](HANDHELD.md#environment-variables).

Two checks worth knowing:

- **Is the whole frame inside the budget?** Compare the process's CPU time with wall time. At 50 Hz, 10 s of CPU in 10 s
  is a frame at 100% of the budget: `cut -d" " -f14,15 /proc/<pid>/stat` twice, 20 s apart.
- **Are the pixels right?** Run twice with `--save-state N:/tmp/a.state` and `--save-state-exit`, once with NEON and once
  with `DW3_NO_NEON_FLAT=1`, and compare the files. They must be byte-identical except the stack block at the tail.