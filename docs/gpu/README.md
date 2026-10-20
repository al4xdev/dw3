# GPU backend study

What it would take to rasterise on the Mali-G31 instead of the CPU, and what was measured on the device to decide.
Written before any backend exists: this folder is the evidence, not a plan. The plan is the `gles` backend entry in
the project backlog.

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
| `measurements.txt` | raw output of every binary below, including the attempts that failed |

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
| `gpu_spike5.c` | an invalid attempt (RGBA8 fed with 16-bit type) — kept because it documents a dead end |
| `gpu_spike6.c` | a 640x480 RGB565 display target, and the 28 ms readback trap |
| `gpu_spike7.c` | the 320x240 present targets compared (RGBA8, RGB5_A1, RGB565) |

Two rules when running them, both learned the hard way (see `measurements.txt`, section 6):

- **Stop `emustation` first** (`systemctl stop emustation`): with the frontend running, the swap measures 7.8 ms
  instead of 17.0 and every other number moves. The game itself runs with the frontend stopped.
- **Measure the `glFinish` round trip first.** Without that baseline, three unrelated operations each "cost" 1.6 ms,
  which is the constant, not the operation.
