# 10 — M1 design: what has to change before a line of GLES is written

Stage 0 said the frame fits. Spike #8 said the context can live offscreen. Reading the runtime for M1 found one thing
that changes the shape of the milestone, and it is worth settling before code.

## The listener is additive, and that is fatal on this device

`psyq/gpu.c` calls `gpu_listener(&ev)` and then rasterises anyway (`gpu.c:1460-1469` for a triangle, `1752` for a
rectangle, `1883/1921/1962/1980` for the transfers). That is right for the reference renderer: on a desktop the
hardware pass exists to upscale, filter and widen a picture the software pass produces.

On the handheld, running both would be the worst of both worlds: the CPU keeps its 18-19 ms *and* pays for a GPU pass.
So M1 needs a new idea in the shim: **a listener that replaces the rasteriser**.

### What that costs

If the listener replaces it, the shim's `gpu_vram` stops being the truth, and everything that reads it has to change
owner:

| reader | today | with the GPU target |
|---|---|---|
| texture fetches while drawing | the CPU span loops | the GPU's VRAM texture (CLUT in the shader) |
| the display (`video_present_direct`) | the CPU's VRAM rows | a readback of the display area (1.7-1.95 ms) |
| the checkpoint / `--save-state` | `gpu_state` copies the array | a readback of the whole VRAM (10.8 ms, only on save) |
| `gpu_block_stamps` / `gpu_touch` (texture cache invalidation) | write stamps | nothing: the GPU samples its own VRAM |
| the golden corpus' comparison | software VRAM vs the target | still valid: the software run is the oracle |

Two of those are per frame (display) and already measured; one is per save; one disappears.

## The flag

`gpu_set_listener` grows a companion:

```c
/* The listener draws every primitive itself: gpu.c emits the event and does not rasterise. Its VRAM copy stays as it
 * was - the display and the checkpoint have to come from the listener (render_gles_read_vram). */
void gpu_set_listener_replaces(int on);
```

With it on, each draw site becomes `emit; if (!replaces) rasterise;`. The transfers (LoadImage, Fill, Copy) keep
writing the CPU VRAM as well *unless* the flag says otherwise, because the CPU copy costs microseconds and gives the
backend something to compare against during bring-up. The plan's validation rule needs exactly that: the software
image stays the oracle.

## Order of work inside M1

1. `render_gles.c`: EGL/GBM offscreen context, the VRAM RGBA8 target, the read copy, and the listener for the three
   transfers (**LOAD, FILL, COPY**) - no polygons yet.
2. `gpu_set_listener_replaces` in the shim, default off so nothing changes for the existing renderer.
3. Validation on the existing harness: `tests/host/gpu_harness.c` grows a `-DGPU_GLES` build and the `H` command
   compares the GLES target against the software VRAM, over the same golden corpus the SDL3 renderer uses. The cases
   that only transfer (`move_*`, and the fixtures' LoadImages) must match exactly; the polygon families are expected to
   differ until M2 and M3, and the harness has to say so instead of failing.
4. Only then the present: the display pass into an RGB565 target, read back (1.83 ms measured), memcpy into `/dev/fb0`.

## What spike #8 settled (measured on the console, ES running)

```
A. gbm device ok (backend 'armsoc')
   EGL 1.4, vendor 'ARM', version '1.4 Bifrost-"r13p0-01rel0"'
   context current: OpenGL ES 3.2 ... | Mali-G31
B. FBO 1024x512 RGBA8: 0x8cd5 (complete)
   draw + readback: program linked, pixel (64,128,191,255) : PASS
C. glReadPixels 1024x512 RGBA: 10.770 ms | 320x240 RGBA: 1.710 ms
```

An EGL context on a GBM device with a pbuffer needs no window, no DRM master and no mode change: the picture can be
rendered offscreen and handed to the existing `/dev/fb0` present, which is the only way to avoid the 17 ms
`SwapWindow`. The 320x240 readback is even slightly cheaper here (1.71 ms) than through SDL (1.95 ms), and a whole-VRAM
readback (10.8 ms) is only paid on a checkpoint.
