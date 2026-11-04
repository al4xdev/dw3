# GPU backend study

What it takes to rasterise on the Mali-G31 instead of the CPU, what was measured on the device to decide, and how the
`--renderer gles` backend was built. Notes 01-08 are the study that preceded the backend; 09-16 are its build log; 17 is
an open item. This folder is the evidence, not a plan. The plan is the `gles` backend entry in the project backlog.

## Study (01-08)

| file | subject |
|---|---|
| `01-duckstation-map.md` | where the reference implementation keeps things; the path of one polygon to the GPU |
| `02-vram-as-a-texture.md` | the whole VRAM as one RGBA8 texture; 5551 packing; the mask bit in alpha; the two copies |
| `03-clut-in-the-shader.md` | 4/8-bit textures and CLUTs as two dependent fetches, with no table on the CPU |
| `04-mask-and-semi-transparency.md` | the six strategies (fragment fetch, ARM fetch, ROV, feedback loop, dual source, depth-as-mask, two passes) |
| `05-what-we-do-not-need.md` | which parts of the reference are dead weight here (texture cache, upscale, MSAA, PGXP) |
| `06-what-the-device-answered.md` | measured: GLES 3.2, `GL_ARM_shader_framebuffer_fetch`, CLUT byte-exact, and the costs |
| `07-cost-model.md` | the 20 ms budget with and without a GPU, and the three traps (batching at 11.2 µs/draw, the feedback loop, swap at 17 ms) |
| `08-format-and-present.md` | why VRAM stays RGBA8 instead of native 5-5-5-1, and the present path chosen (320x240 pass + RGB565 readback + fb0 memcpy = 2.4 ms) |
## Build log (09-16)

| file | subject |
|---|---|
| `09-stage0-battle-counters.md` | stage 0: what one battle frame asks a GPU to do, counted from a saved first-battle state; the draw-call cost does not sink the backend because the texture page and CLUT travel per vertex |
| `10-m1-design.md` | M1 design: the GPU listener is additive (the CPU rasterises anyway), which is fatal on this device, and what has to change before any GLES is written |
| `11-m1d-present.md` | M1d: the complete `--renderer gles` path on the handheld, from the offscreen context to `/dev/fb0`, measured on the console |
| `12-m2-rects.md` | M2: untextured rectangles as batched triangles (scissor, mask bit as vertex alpha) and the live VRAM check |
| `13-m3-two-bugs-and-one-open.md` | M3: textured sprites and flat triangles, exact on the self-test and still wrong on the title screen; two bugs the VRAM check found and one it had not |
| `14-2d-artefacts.md` | the broken dialogue text and white boxes a user reported in 2D, and the one-line VRAM check result that named the cause (semi-transparency) |
| `15-battle-first-measurement.md` | the first VRAM-check measurement of the battle: 0.2% of the VRAM differs early, 4.5% once the 3D models appear |
| `16-frame-budget.md` | where a GLES battle frame's milliseconds went (18.75 fps against 37.7 for software on the same state) and the four changes that took them back |

## Open item

| file | subject |
|---|---|
| `17-movie-artefact.md` | the movies' top-right artefact, the two VRAM areas and 24-pixel slices it is made of, every hypothesis tested against it, and what the next attempt should log |

## Raw data

`measurements.txt` is raw output of every probe binary below, including the attempts that failed. It is a log, not prose;
the notes above quote from it.

## The reference

[stenzek/duckstation](https://github.com/stenzek/duckstation), shallow clone, `src/core` and `src/util` only. Every
claim about it carries a `file:line` from that clone. Claims without a reference are marked as opinion.

## The probes

Sources are here; the ARM binaries are build products and are not tracked. To rebuild any of them:

```bash
docker run --rm -v "$PWD:/src" -w /src dw3-armhf-builder:latest bash -c \
  'arm-linux-gnueabihf-gcc -O2 -marm -mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard \
   gpu_spike2.c -o gpu_spike2 $(sdl2-config --cflags --libs) -lGLESv2'
```

| source | what it proves |
|---|---|
| `gl_probe.c` | the GLES version, the extension list, `highp`/`mediump` precision in the fragment stage |
| `gpu_spike2.c` | the CLUT indirection, framebuffer fetch with the mask bit, and the cost of a draw, a blit and an upload |
| `gpu_spike3.c` | swap with and without vsync, the readback, the `/dev/fb0` memcpy |
| `gpu_spike4.c` | VRAM as native 5-5-5-1: bit-exact and cheaper, but it breaks framebuffer fetch |
| `gpu_spike5.c` | an invalid attempt (RGBA8 fed with 16-bit type), kept because it documents a dead end |
| `gpu_spike6.c` | a 640x480 RGB565 display target, and the 28 ms readback trap |
| `gpu_spike7.c` | the 320x240 present targets compared (RGBA8, RGB5_A1, RGB565) |
| `gpu_spike8.c` | an offscreen GLES context (EGL on GBM with a pbuffer, no window, no DRM master), including while EmulationStation owns the display |

Two rules when running them, both learned the hard way (see `measurements.txt`, section 6):

- **Stop `emustation` first** (`systemctl stop emustation`): with the frontend running, the swap measures 7.8 ms
  instead of 17.0 and every other number moves. The game itself runs with the frontend stopped.
- **Measure the `glFinish` round trip first.** Without that baseline, three unrelated operations each "cost" 1.6 ms,
  which is the constant, not the operation.
