# 08 — VRAM format and the present path: what spikes 4-7 decided

Two questions only the device could answer. Raw numbers in `measurements.txt`, sections 7-9.

## Format: RGBA8 wins, even though 5-5-5-1 is cheaper

| format | upload 128 KB | readback 320x240 | bit-exact | fragment fetch |
|---|---|---|---|---|
| `GL_RGB5_A1` (native 16 bits) | 0.66 ms | 0.73 ms | **yes** (0 mismatches in 524288) | **no** |
| `GL_RGBA8` (8-8-8-8) | 1.42 ms (256 KB) | 1.95 ms | through 5551 requantisation | **yes, exact** |

The decisive test was **identity** in spike #4: a shader doing `gl_FragColor = gl_LastFragColorARM` on a 5551 target.
A pixel painted blue read back as `0x003e` (red 30/31) instead of `0x7c00` (blue 31/31): the channels come back
rotated in that format, and the mask bit does not survive.

Since per-pixel semi-transparency and mask are *the* reason to want a GPU, VRAM stays **RGBA8**, exactly as the
reference chose (`gpu_hw.cpp:50`) - and now I know it was not a taste for true colour, it was the need to read the
destination.

Accepted cost: the CPU has to expand 16 to 32 bits before each upload. But uploads are always **dirty rectangles**,
and the expansion is NEON from 2 bytes to 4.

## `gl_LastFragColorARM` is the only exact transparency path here

Recapping what spike #2 proved on RGBA8: destination read exact ((128,0,128) for 50/50), and a pixel with alpha=1
survives a `discard` (`check-mask` semantics).

`GL_EXT_blend_func_extended` (dual source) does not exist in this blob, so the reference's per-primitive blend
(`gpu_hw.cpp:1767-1800`) is out. The two-pass fallback remains available and is plain GLES2 - but it is not needed.

## Present: read 320x240 in the stored format, scale on the CPU

| path | measured |
|---|---|
| today (VRAM in RAM -> NEON 15->16 and 2x -> `memcpy` fb0) | **~1.0 ms** |
| GPU: 320x240 display pass + RGBA8 readback | 2.52 ms |
| GPU: 320x240 pass + **RGB565** readback | 1.83 ms |
| GPU: 640x480 pass + RGB565 readback | 4.38 ms |
| GPU: `SwapWindow` (KMS flip) | 17.0 ms |
| `memcpy` 640x480x2 into `/dev/fb0` | 0.55 ms (0.77 with the frontend alive) |

Choice: **a 320x240 display pass into an RGB565 target, read back as RGB565 (1.83 ms), `memcpy` into `/dev/fb0`
(0.55 ms)** = 2.4 ms. The 2x scale and the conversion happen in the shader (NEAREST, integer), which also guarantees
crisp pixels without depending on the RK3326's VOP, whose scaler is bilinear only (tested and rejected earlier).

No `SwapWindow`: 17 ms is a 60 Hz frame of waiting, and in a single-threaded loop it adds to CPU work instead of
overlapping it.

## The trap that cost 28 ms

`[b]` in spike #6 read 320x240 **RGBA from an RGB565 target** and took **28.1 ms** against 1.7 ms for the same size
in an RGBA8 target. Reading the GPU in a format other than the stored one falls into a very slow conversion path. The
rule that follows: **the type passed to `glReadPixels` must be the target's format.**

## The frame, with the present resolved

| item | ms |
|---|---|
| game logic (C, GTE, model) | 6-8 |
| audio (SPU) | 4.9 |
| GPU rasterisation (150k px) | ~0.6-1.4 |
| VRAM read copy (dirty rectangle) | <=0.42 |
| VRAM uploads (dirty rectangles) | <=0.7 |
| present (pass + readback + fb0 memcpy) | 2.4 |
| draw call overhead (~50 batches x 11 µs) | 0.55 |
| **total** | **~15.6-18.4 ms** |

It fits in 20 ms with 1.6 to 4.4 ms of margin. Today: ~30 ms.

## What is still unknown

- **How many batches** the game issues in a real battle. At 300 batches the overhead becomes 3.3 ms and the margin
  disappears. This is the missing measurement, and it comes out of counters that already exist in `gpu.c`, with no
  backend written.
- **`LoadImage` frequency** per frame: if textures are resent every frame, 0.7 ms per 128 KB rectangle becomes
  several ms.
- Whether the game uses `BG - FG`, which forces one flush per polygon in the reference (`gpu_hw.cpp:4274-4275`).
