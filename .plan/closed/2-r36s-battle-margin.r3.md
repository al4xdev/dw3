# R36S/XF35H: the battle fits - now it is about margin (revision after closing the frame)

> Written in Portuguese; translated as it stands, with nothing added.

## Where we got to

Measured on the device with the current binary, during the heaviest battle: **19.7 s of CPU in 20 s of wall clock
~ 19.7 ms per frame** at 50 Hz. The budget is 20 ms. **It fits, with ~0.3 ms of headroom.** Before this session the same
kind of battle sat at ~21.5 ms of drawing + audio and presentation, i.e. 1.2x slow motion.

What produced the difference, in order of real impact:

1. **Meters off the game's path** (`dw3.sh`): ~1-2 ms. The A35's TRM confirms `mrrc` at 6-15 cycles, and the meter read
   the counter ~10 times per span.
2. **`-mcpu=cortex-a35`**: 17 calls to `__aeabi_idiv` became 0; 15 `sdiv` in hardware.
3. **NEON for the flat span on `GPU_COPY` and semi-transparent primitives**: 98% of spans fell into the scalar loop
   because the vector path only accepted `GPU_OPAQUE`.
4. **`P2`** (cross product per row), **R7** (meter without a double), the walk sharing the same counter.

## What the investigation ruled out, with a source

| hypothesis | verdict |
|---|---|
| P1 divide by reciprocal | ~0.3 ms in total; it is not arithmetic |
| P3 direct packet dispatch | `walk - tri` = 0.6 ms of 19.3 |
| P4 CLUT per triangle | ~0.06 ms |
| 2x scale in the VOP (the display does the upscale) | the RK3326's Win0 scales **bilinear/bicubic** only, not nearest-neighbour, and only through DRM. It would blur the pixels |
| 2 MB huge pages for the micro-TLB | **THP is off in this kernel** (`/sys/kernel/mm/transparent_hugepage` does not exist) |
| CPU/DMC governor | already on `performance`; DMC at 528 MHz is the DTS ceiling |
| Rasteriser on the Mali | breaks the byte-for-byte match, out of scope |

## Why 55 ns/px (the number that was left)

The Cortex-A35's micro-TLB has **10 entries** and a VRAM row is 2 KB, so every pixel crosses a 4 KB page every two rows:
a 100-row texture blows the micro-TLB continuously. The hardware prefetcher exists but only follows strides **inside the
same 4 KB page** - the 2 KB vertical step defeats it (which is why the manual `PLD` bought nothing). Benchmark on the
console: 14.6 ns/px with a working set that fits in L1, 54.8 with a 1 MB working set. It is not bandwidth (the DDR does
~4.2 GB/s theoretical and the game uses ~75 MB/s): it is latency.

## What was left (all of it margin, not repair)

### 1. Close the measurement (5 minutes)

- Quit the game and read the `window: N frames presented in X s` line in `game.log`: that is the effective fps, the final
  criterion (50/s = no slow motion).
- The `aud` figure (audio per frame) comes from the same place if a run is made with `DW3_GPU_LOG` turned on by hand.

### 2. Audio on NEON (the best gain per effort that was left: ~3 ms)

- The SPU mix is 24 voices x 882 samples per frame. The working set is small (voices and buffers) and fits in L1, so SIMD
  pays here - unlike in the rasteriser.
- Validation: `--wav` before/after **identical** byte for byte; `tests/sound` compares SPU traces.
- Criterion: audio phase <= 1.5 ms, identical WAV. The gain becomes headroom for the boss battles.

### 3. Decoded texture cache (only if 2 is not enough: ~4 ms in the `flat` path)

- Keep the decoded texture rows in compact, **tiled** blocks (8x8 texels = 128 B) so the vertical walk stays inside a few
  pages and the L1; validate it through the write stamps the shim already keeps.
- The ceiling is the benchmark: 14.6 ns/px against 55 today.
- Medium-high risk: it is the hot path and it touches invalidation. Only after the audio.
- Validation: golden replay (0 divergence) + a save state byte-identical on the console + the measured battle.

### 4. Delivery cleanup

- Meters commented out in `dw3.sh` (done), README with the real state and the battle number, and the
  `DW3_NO_NEON_FLAT` / `DW3_NO_NEON_GOURAUD` switches documented as the escape hatch.

## Completion criterion

The heaviest battle holds 50 Hz (effective fps 50, read from `game.log`) with the pixels identical to today's. If the
0.3 ms of headroom does not survive the boss battles, step 2 pays the difference without touching the rasteriser.
