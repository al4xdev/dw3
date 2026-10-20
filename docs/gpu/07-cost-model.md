# 07 — Cost model: where a GPU backend would fit (and where it would not)

Measured (marked) mixed with estimates (said so). The goal is not to decide here; it is to know where the fat is.

## One frame's budget (20 ms at 50 Hz)

| item | today (software) | with a GPU backend | source |
|---|---|---|---|
| rasterisation (150k textured px, heavy battle) | 18-19 ms | **1.2-2.8 ms** | measured (CPU) / measured (GPU, 307k px) |
| VRAM read copy (dirty rectangle) | - | 0.42 ms (whole VRAM; a rectangle is less) | measured |
| game logic in C (GTE + model) | 6-8 ms | 6-8 ms | measured |
| audio (SPU) | 4.9 ms | 4.9 ms | measured |
| present to the panel | 0.77 ms | 0.77 + **1.95** (readback) = 2.7 ms | measured |
| **total** | **~30 ms (33 fps)** | **~16-19 ms** | sum |

Without decoration: today the frame **does not fit** and the game runs in slow motion in a heavy battle; with
rasterisation on the GPU it fits, with 1-4 ms of margin, which is why the CPU work (GTE, audio) still matters.

## The three traps the numbers exposed

### 1. Batching is not optional (11.2 µs per draw)

4800 individual draws = 53.6 ms. A backend that draws triangle by triangle is *worse* than today's software path. It
has to accumulate vertices and change pipeline only when texpage, transparency mode, dithering, texture window or a
mask flag changes (`gpu_hw.cpp:4280-4289`). With ~50 batches per frame: 0.56 ms of overhead, acceptable.

### 2. You cannot sample the texture you are drawing into

The PS1 draws into VRAM and textures from VRAM, which on a GPU is a feedback loop. The reference keeps **two**
textures and copies the dirty rectangles before each flush (`gpu_hw.cpp:2317-2380`); the full copy costs 0.42 ms and
a rectangle much less. This is the path, already debugged by someone else.

### 3. `SwapWindow` costs 17 ms and is not work

17.0 ms with vsync off is one 60 Hz frame: SDL/KMSDRM always waits for the flip (one buffer in flight). In a
single-threaded loop that **serialises** with CPU work: 10 ms of game plus 17 ms of wait = 27 ms per frame. Design
conclusion: present the way the port already does (`/dev/fb0`, 0.77 ms), reading back only the display area
(1.95 ms). That adds ~1.95 ms and removes 17 ms of waiting.

The trap inside the trap: with EmulationStation running the same swap measures 7.8 ms. The number that applies to the
game is the one with the frontend stopped.

## Unmeasured risk: uploading VRAM from the CPU

`glTexSubImage2D` of 256 KB cost 1.42 ms. If the game uploads VRAM every frame (some PS1 games resend textures per
frame), four of those eat 5.7 ms. **How often this game calls LoadImage is not known yet** - one of the open questions.
Note that uploading into a texture that is also a render target is the expensive case, which is why the reference uses
a texture buffer plus a full-screen quad on the hardware path (`gpu_hw.cpp:3905-3994`).

## What a `DW3_RENDERER=gles` backend would need (a sketch, not a plan)

1. **A 1024x512 RGBA8 target** (VRAM, 2 MB) plus a **read texture** (2 MB) and a dirty-rectangle blit per flush.
2. **One shader pair** with `#define`s for: 4/8-bit palette, direct texture, semi-transparency (4 modes via
   `gl_LastFragColorARM`), dithering, mask test, interlace. On ES 3.2 `texelFetch` removes the precision worry.
3. **Batching by state**, keyed like the reference.
4. **Screen quads** to fill, copy and upload VRAM.
5. **Present**: readback of the display area plus the existing NEON -> RGB565 -> `/dev/fb0` path (measured 0.77 ms).
6. **The software renderer stays the default and the oracle**: every new piece has to match its `--checkpoint`/VRAM
   dump. That is not luxury, it is the only way to know the shader is right.

## Open questions (and the instrument that answers each)

| question | how |
|---|---|
| how many primitives and how many batches does a heavy battle issue per frame? | the counters in `gpu.c` (`pkt tri tset`) on a real battle frame: they exist, the numbers are not written down |
| how often does the game use semi-transparency? | same (`semi` in the log) |
| how often is there a `LoadImage` per frame? | same (`load` in the log) |
| is the battle display area 320x240 or 384x240? | `video_present_stat` already prints it; one battle run |
| 60 Hz panel against a 50 Hz game: does readback + fb0 keep the pace? | measure with the port, not with a spike |
| does Mali's fragment fetch have a hidden cost in large batches? | 2.84 vs 1.22 ms in the spike; measure on a real frame |

None of those requires writing the backend. All of them require running the current port with counters on - and
`DW3_PHASE` was already ruled out for costing 23 ms/frame, so this is a dedicated measurement session, not something
to leave enabled.
