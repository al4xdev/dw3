# 12 — M2: untextured rectangles, batched, and a live VRAM check

## What M2 draws

`GPU_EV_RECT` with `textured == 0 && semi == 0` becomes two triangles in one batch, with:

- the **drawing area as a scissor** (a change of area flushes the batch, since a scissor is draw state);
- the **mask bit as the vertex alpha** (0 or 255), so `set-mask` costs nothing;
- the PS1 coordinate mapping: `x_ndc = (x + 0.5) / 512 - 1`, `y_ndc = (y + 0.5) / 256 - 1`, which puts VRAM row 0 at
  the bottom of the target - the same orientation the upload and the readback already use;
- one draw call per state change, not per primitive: 11.2 µs a call makes that the difference between 1.2 ms and 22 ms
  a frame (`docs/gpu/09`).

Textured rects (sprites, the majority of what this game draws in menus) and semi-transparent ones are still counted and
not drawn: they need M3's CLUT sampling and M4's fragment fetch.

## The self-test, extended

```
   LoadImage 1024x512           : exact
   Fill 64x32                   : exact
   Fill wrapping the edge       : exact
   Copy overlapping             : exact
   Copy with check-mask         : exact
   LoadImage with set-mask      : exact
   Rect 100x50 red              : exact
   Rect clipped to the area     : exact
   Rect with set-mask           : exact
PASS: every transfer and rectangle reproduced the shim's VRAM exactly
```

The rectangle cases are the ones that would catch the coordinate mistakes: a flipped row order or a missing half-pixel
offset is a one-pixel shift, and the whole-VRAM comparison cannot miss it.

## The live check (`DW3_GLES_CHECK`)

`DW3_GLES_REPLACE=0` keeps the CPU drawing the same primitives; `DW3_GLES_CHECK=N` then compares the GLES target with
the shim's VRAM every N frames and logs what differs:

```
gles: vram check 1 (frame 150): exact
gles: vram check 2 (frame 300): 29.3% of the VRAM differs (153600 pixels), first at 0,0 sw 0x3406 hw 0x1084;
   the undrawn counters say what is missing (triangles 1386, rects 12330, segments 0)
```

That is the plan's exactness criterion as a live measure, and it now doubles as the progress meter for M3 and M4: the
number of undrawn primitives is the work that has not landed, and the percentage is where the pixels disagree.

The first check is exact because the boot sequence up to frame 150 is transfers only; from there the title screen is
textured sprites, which is why the counters show 12,330 undrawn rects.

## What is deliberately not done yet

- A rectangle that runs past the VRAM edge is clipped by the scissor instead of wrapping the way the PS1 does. None of
  the cases the game draws inside a page are affected, and the live check would say so (it compares all 524,288
  pixels, not a sample).
- `check-mask` on a drawn rectangle, dithering and interlace are M4's.
