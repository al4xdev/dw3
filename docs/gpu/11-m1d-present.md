# 11 — M1d: the picture reaches the panel

`--renderer gles` now has a complete path on the handheld: the offscreen EGL/GBM context draws into the VRAM texture,
the display area is read back as 16-bit pixels, and the present that already exists (`runtime/fbdev_present.c`: NEON
2x nearest plus the 15-to-16-bit expansion) puts it on `/dev/fb0`. The window, the screenshots and the 24-bit modes
are untouched.

## Measured on the console

Field/menu display (320x240), 2000 frames:

| item | gles | software (default) |
|---|---|---|
| `glReadPixels` 320x240 RGBA | 1.41 ms | - |
| 8-to-5-bit conversion | 0.59 ms | - |
| present total | **2.9 ms** | 1.0 ms |
| frame rate | **49.3 fps** | 48.96 fps |

The 1.9 ms difference is the readback, and it is the price of keeping the picture off the window: a KMS swap costs
17 ms (`measurements.txt`, section 4).

## The measurement that mattered: `-O0`

The first run presented at **9.9 ms**, not 2.9. `DW3_GLES_LOG=1` splits it:

```
gles: present readback 8.99 ms (glReadPixels 1.49, 5-bit conversion 7.51)
gles: present readback 2.00 ms (glReadPixels 1.41, 5-bit conversion 0.59)   <- after -O3
```

7.5 ms for a loop of four integer operations per pixel is not the loop: the runtime compiles its sources **without
optimisation** unless a file asks for one (that is why `gpu.c`, `fbdev_present.c`, `mdec.c`, `gte.c` and the SPU
files carry their own `-O3`/`-O2`), so the three `/255` were real divisions. `render_gles.c` now asks for `-O3`, like
every other per-frame file.

This is the same shape of mistake as the `glFinish` baseline in `measurements.txt`: a cost attributed to the wrong
thing. The instrument that found it (`DW3_GLES_LOG`) stays in the file.

## What it looks like

Incomplete, by design. M1 draws the transfers; the ~1060 polygons a battle frame issues are counted
(`render_gles_undrawn`) and not drawn until M2 and M3. The frame rate above is therefore *not* a result about the
game, only about the plumbing: what it proves is that a frame with the software rasteriser off and the GLES present on
fits comfortably inside 20 ms with room for the polygons.

## Where the code is

- `runtime/render_gles.c`: context, VRAM target, read copy, the transfers, `read_vram` (10.8 ms, save time only) and
  `read_display` (1.4 ms, per frame).
- `runtime/video.c` (SDL2 branch): opens the renderer when `--renderer gles` is asked, presents the display readback
  instead of the shim's array, and closes it at exit.
- `runtime/main.c`: `--renderer software|gpu|gles`, with the flag passed through as a name instead of a boolean.
- `cmake/psxstack.cmake`: `PSXSTACK_EGL` (off by default: it needs the EGL, GLES and GBM headers), `-O3` for the
  renderer.
