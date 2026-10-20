# 05 — What of the reference we do **not** need

The reference's hardware backend is 4887 + 4111 + 3924 lines. Most of it exists to **raise the resolution** and to
serve emulator features we do not have, and drops out of our scope.

## Out of scope, with evidence

| feature | why not | where |
|---|---|---|
| **texture cache** (3924 lines) | it is for upscaling, filters (xBRZ/JINC2), texture replacement and drawn/UV-rect tracking. At native resolution with NEAREST, sampling VRAM directly is *more* exact. Off by default | `settings.cpp:396`, `settings.cpp:1244` |
| **internal upscale** (`resolution_scale`) | the panel is 640x480 and the content 320x240: integer 2x belongs in the *present*, not in drawing | `gpu_hw.cpp:965` |
| **MSAA** | native resolution, no upscale: costs fill rate for nothing | `gpu_hw.cpp:967` |
| **PGXP** (depth buffer, precise geometry) | an emulator hack against PS1 wobble; the game is decompiled and the GTE is ours | `gpu_hw.cpp:1343` |
| **ROV** (`EXT_rasterizer_order_resources`) | desktop class; does not exist on Mali | `gpu_hw.cpp:1336` |
| **SSBO / `samplerBuffer`** | only used to upload VRAM from the CPU; on our device the cheaper route is a dirty rectangle into an RGBA8 texture | `gpu_hw_shadergen.cpp:3547-3571` |
| **downsampling, adaptive blur, internal FXAA, 24-bit chroma smoothing** | desktop emulator features | `gpu_hw.cpp:4680-4760` |
| **separate video thread** | 4 cores and the game already owns one; another thread does not fit a 20 ms budget | `video_thread.cpp` |

## What is left — and it is little

1. **One RGBA8 1024x512 texture plus a read copy** (2 MB + 2 MB) and a `glBlitFramebuffer` of the dirty rectangle
   before each flush.
2. **One batch shader pair** (vertex + fragment) with `#define`s for: 4/8-bit palette, direct texture,
   semi-transparency (4 modes through fragment fetch), dithering, mask test, interlace. On our device this can be
   **one pair with uniforms and `if`s**, because a fragment `if` is irrelevant next to what we save, and it avoids a
   combinatorial explosion of compiled variants.
3. **Screen quads** to fill VRAM, copy VRAM, upload VRAM from the CPU and present.
4. **Batching by state**, with the same key as the reference (`gpu_hw.cpp:4280-4289`).

## Numbers for our case

- Fill rate: a heavy battle draws ~150k textured pixels per frame; with the CLUT that is two fetches per pixel, so
  ~15 M texel/s at 50 Hz. Measured on the device: 307k pixels of two-fetch texture cost 1.2 ms, i.e. roughly
  **250 Mpx/s**. A battle frame is ~0.6-1.4 ms of GPU time, against 18-19 ms of CPU rasterisation.
- Memory: ~5 MB of textures (RT 2 MB + read copy 2 MB + readback 1 MB). The process' RSS today is 8.5 MB of 993 MB.
- CPU freed: the rasteriser is 18-19 ms of a ~30 ms battle frame. Removing it leaves ~11 ms (game C 6-8, audio 4.9,
  present 1.0) — under the 20 ms budget, with margin.

None of this is "it will run at 50 fps". It is "the only part of the budget that does not fit is the part the
hardware is better at".

## The risks that remain

1. **Draw call overhead**: 11.2 µs each, measured. If a battle frame issues hundreds of primitives without batching,
   the cost moves from pixels to calls. This is the one number that can still sink the idea, and it is not measured in
   a real battle yet.
2. **Fragment fetch cost**: 2.84 ms against 1.22 ms for the same geometry without it. Fine, but it is 2.3x.
3. **Uploads**: `glTexSubImage2D` of 256 KB costs 1.42 ms. If the game re-uploads textures every frame, dirty
   rectangles have to be small and rare.
