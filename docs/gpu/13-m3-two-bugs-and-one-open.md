# 13 — M3: two bugs the VRAM check found, and one it has not

M3 (textured sprites and flat triangles, CLUT in the shader) is exact on the self-test and still wrong on the title
screen. The gap between those two statements is the useful part.

## What is exact (self-test, on the console)

```
   Textured 4-bit sprite        : exact (1024 x 512 texels)
```

A 4-bit sprite through a CLUT at `(clut_x, clut_y)`, its index unpacked from the packed texel, modulated by the vertex
colour and with the mask bit from the texel: byte for byte what the shim's `gpu_pixel` produces.

## The two bugs the live check found

**1. The flat path skipped the PS1's 5-bit quantisation.** The shim computes an untextured pixel as `c >> 3`
(`gpu_pixel`), so 31 of 255 becomes 3 of 31. The shader wrote the 8-bit colour straight into the target, and the read
back rounded it to 4. Every pixel was one step off, and a whole screen of one colour read as a uniform block:

```
before: first at 0,0 sw 0x3406 hw 0x1084   (0x1084 = 4,4,4 of 31)
after:  first at 0,0 sw 0x3406 hw 0x0c63   (0x0c63 = 3,3,3 of 31, the shim's value)
```

**2. A transfer landed before the batch queued ahead of it.** Uploads go straight into the target; the drawing
accumulates in a batch flushed later. When the game's order was "clear, then LoadImage", the clear was still queued
when the upload happened, so the clear painted over the frame that had just arrived. Every upload now flushes the
batch first.

Both were invisible to reading the code and obvious to the check. That is the argument for the check existing before
the milestones land, not after.

## What is still open

The title screen reports **29.3% of the VRAM differing (153,600 px = 320x480), no undrawn primitives, and no uploads
after startup** (`uploaded` stays at 1,054,336 px from frame 200 to 600). So whatever draws that area is not a
transfer and not a primitive the listener is counting. The instruments that will settle it are in the tree:

- `DW3_GLES_TRACE=<frame>`: every rectangle of that frame with page, CLUT, window, area and mask flags (the trace
  already showed the title's content: a 320x240 clear with colour 31,31,31, then ~140 textured 48x48 sprites from page
  (640,0) with CLUTs at y 254/255);
- `DW3_GLES_DUMP`: both VRAMs on the first failed check, to compare offline;
- `DW3_GLES_NOFLAT` / `DW3_GLES_NOTEX`: count a path instead of drawing it (this is how the culprit was narrowed to
  the untextured path, and how the flat quantisation bug surfaced);
- the uploaded-pixel counter in the check line.

The next step is mechanical rather than inspired: trace the frames around a check, replay the same events in a script
against the shim's rules, and diff. The hypothesis to beat is that the shim draws that area with something my listener
receives but classifies as undrawn while the counter is zero — which the replay would show immediately.

## Closed: the open one was three bugs, not one

The mechanical path worked, faster than the hypothesis it was meant to test. Three fixes took the title screen from
29.3% of the VRAM differing to **1.0%, with the first check exact**:

1. **The flat path skipped the 5-bit quantisation** (`c >> 3` in the shim, so 31 of 255 is 3 of 31, not 4).
2. **A transfer landed before the batch queued ahead of it**, so a clear painted over the frame just uploaded.
3. **`glBlitFramebuffer` obeys the scissor**, and the scissor is the PS1 drawing area (0,0..319,239 on a title screen).
   The read copy therefore received the display area and *none* of the texture pages - the title's are at x = 640 - so
   every sprite sampled zeros, hit the "texel 0x0000" rule, discarded itself and drew nothing. No error, no counter,
   no warning: the picture was simply the background colour.

The third one is the one worth remembering: a state that belongs to drawing (the scissor) silently constrained a
different operation (the copy), and the observed symptom was as far from the cause as a picture can be.

What is left of the 1.0% is the 344-and-counting semi-transparent or dithered rectangles the listener still counts
instead of drawing, which is M4's scope and is what the counters report.
