# 06 — What the device answered

Measured, not inferred. Raw numbers in `measurements.txt`; sources next to it.

## 1. It is not GLES2. It is GLES 3.2.

```
GL_VERSION : OpenGL ES 3.2 v1.r13p0-01rel0.87...
GL_SL_VER  : OpenGL ES GLSL ES 3.20
```

An ES **2.0** context was requested and the blob returned 3.2 (libmali ignores the version request). That removes a
premise I had been designing around for weeks without measuring it.

With ES 3.2 these are available, to name what matters: `texelFetch` (the exact fetch the reference uses to avoid
point-sampling precision problems, `gpu_hw_shadergen.cpp:2894-2900`), integer textures (`usampler2D`, `R16UI`) if
VRAM should ever be stored as raw 16 bits, VAOs, instancing, `glBlitFramebuffer` for the read copy.

Limitations that still apply (measured):

| extension | present? | consequence |
|---|---|---|
| `GL_ARM_shader_framebuffer_fetch` | **yes** | exact per-pixel semi-transparency and mask, no double pass |
| `GL_EXT_shader_framebuffer_fetch` | no | (the ARM variant covers it) |
| `GL_EXT_blend_func_extended` (dual source) | no | the reference's per-primitive blend path is out |
| `GL_EXT_rasterizer_order_resources` (ROV) | no | expected; desktop only |
| `GL_EXT_unpack_subimage` | **no** | uploading VRAM needs fixed-width rows; no stride in `glTexSubImage2D` |

`precision highp float` in the fragment stage: **23 bits, exponent range 2^127** — real float32. `mediump` is 10 bits,
so forgetting `highp` breaks the 0..1023 texture coordinates. The probe exists because the risk does.

## 2. The CLUT in the shader works, and is exact

```
texel (5,10): index 5 -> palette (80,175,128); read back (80,175,128) : PASS
```

Two dependent fetches into the same 1024x512 texture, with `texture2D` + NEAREST (the ES2 route, no `texelFetch`):
the value came back **byte for byte**. Normalised coordinates in `highp` are precise enough here, so `texelFetch` is
not needed to hit the right texel.

## 3. Fragment fetch works — and that changes the backend's shape

```
blue bg + 50% red fg = expect (128,0,128); got (128,0,128) : PASS
marked pixel survives the fbfetch draw (expect still green 0,255,0): got (0,255,0) : PASS
```

`gl_LastFragColorARM` compiled in a GLSL ES 1.00 shader and did both things that matter:

1. **Exact per-pixel semi-transparency**: it read the destination, blended 50/50 and wrote back, which is PS1 mode 0
   with no approximation.
2. **Per-pixel mask**: a pixel with alpha=1 in the destination survived a `discard` in the next shader — the PS1
   `check-mask` semantics.

Strategy A of `04-mask-and-semi-transparency.md` (the exact one) is available on this device. The reference's two-pass
fallback becomes plan B, not plan A.

## 4. The numbers that define the design

With EmulationStation **stopped** (as it is while a game runs):

| operation | cost |
|---|---|
| one draw of 4800 textured triangles, 2 CLUT fetches/px (307k px) | **1.22 ms** |
| the same with fragment fetch + blend + mask test | **2.84 ms** |
| 4800 individual draws (no batching) | **53.6 ms** (11.2 µs per draw) |
| `glBlitFramebuffer` of the whole VRAM (1024x512) | 0.42 ms |
| `glCopyTexSubImage2D` of the whole VRAM (the ES2 route) | 1.77 ms |
| `glTexSubImage2D` of 256x256 (256 KB) | 1.42 ms |
| `glReadPixels` 320x240 RGBA | 1.95 ms |
| `memcpy` 640x480x2 into `/dev/fb0` (today's present) | 0.77 ms |
| `SDL_GL_SwapWindow` | **17.0 ms** (one 60 Hz frame) |

Three readings that change decisions:

1. **Fill is not the problem.** A whole battle frame (150k textured px) costs ~0.6 ms, ~1.4 ms with transparency,
   against 18-19 ms of CPU rasterisation: two orders of magnitude of headroom.
2. **Draw calls are the problem.** 11.2 µs each. 2000 primitives without batching would be 22 ms of calls alone.
   Batching is a requirement, not an optimisation - which is exactly why the reference does it
   (`gpu_hw.cpp:4280-4289`), and now the reason has a number.
3. **`SwapWindow` costs 17 ms and that is vsync wait**, not work. In a single-threaded work-then-swap loop it
   serialises: 10 ms of game plus 17 ms of wait per frame. Today's `/dev/fb0` path has no such coupling, so a GPU
   backend should render offscreen and **read back** (1.95 ms) to present the way the port already does.

## 5. Where I was wrong for not measuring earlier

- I assumed GLES2. It is 3.2.
- I assumed the swap cost ~7 ms (a run with the frontend alive). With it stopped, 17.0 ms - the conclusion "present is
  expensive" survives, the number I was going to use did not.
- The first blit/upload measurement returned three identical values (1.6 ms), which was the `glFinish`, not the
  operations. Fixed with a baseline and four operations per sync.
