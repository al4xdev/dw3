# 16 — Where a GLES frame's milliseconds went, and the four changes that took them

The battle is the scene that matters and the one that was slowest: 18.75 fps against the software renderer's 37.7 on
the same saved state. This is the account of finding the 20 ms and taking most of them back, in the order the
measurements asked for it.

The rule the whole page follows: an instrument first, a change second. Every entry below is a number off the device.

## 1. One read copy per vsync, not one per batch (18.75 → 31.5 fps)

The textured program samples the read copy, so it has to be current before a textured draw. That was done inside the
flush - once per batch, ~110 times a battle frame - and each `glBlitFramebuffer` waits for the drawing queued before
it. Moving it to the end of the frame (`render_gles_frame`) is enough as long as nothing samples what it drew in the
same frame, which the scripted 2D run says nothing does: its VRAM check is byte-identical before and after.

| battle, 600 frames from the state | fps |
|---|---|
| a read copy per batch | 18.75 |
| a read copy per vsync | 31.50 |

## 2. A timer that was measuring itself

The next split was commissioned with `port_clock_ns`. It reported the listener (the primitive packing) at 14.5 ms a
frame - 8.8 us for what is 250-600 ns of work - and the split by event kind showed the same per-call cost for tri, rect
and every other kind. That is the signature of the instrument, not the code: on this handheld `clock_gettime` is not
answered by the vDSO, so a read costs ~3 us, and the listener took two per event, 3300 a frame.

`port_gpu_cycles` - `mrrc p15, 1, c14`, the ARM generic counter read from EL0, which `DW3_GPU_LOG` already used -
gives the same numbers without the artefact: a triangle costs 250-600 ns, and the listener was never the problem.

The first report of that instrument is worth keeping, because it is the shape of the mistake:

```
gles: per frame over 28 vsyncs: listener 14.56 ms (46261 calls), flush 3.95 ms, read copy 0.40 ms
gles: the listener by kind: tri 3.4 ms (48005), rect 0.13 ms (3640), copy 13.08 ms (58)
```

## 3. Vertex array objects, and one dead end (2D 36.2 → 49.0 fps)

The flush re-issued six vertex attribute pointers and their enables for every batch - ~14 of the ~25 API calls a batch
cost - and it re-allocated the vertex buffer with `glBufferData` every time. The attribute setup moved into a vertex
array object per pipeline; the game never changes it.

`glBufferSubData` into a reused buffer, which looked like the obvious next saving, is a trap on this driver: it makes
the write wait for every draw still reading the buffer. A 2D scene went to 60 ms a frame - 105 us per draw, all stall.
The orphaning `glBufferData` stayed.

## 4. One draw call a frame (the scissor and the uniforms were the batch)

With the flush down to 2.5 ms the batch count was the cost: 78 draws a frame at 32 us each. Every one of them existed
because state that the shader reads was state of the *batch*:

- the four PS1 blend modes, the mask test, Gouraud shading and dithering were uniforms -
- the drawing area was the GL scissor -

so any primitive that changed one of them ended the draw. But this renderer's fragment shader already decides all of
it per pixel through `gl_LastFragColorARM`; the values only needed to arrive per vertex, which is exactly what a
`flat` varying is for. The area became four more floats and a `discard` at the top of the shader.

The flat program - the "a little cheaper" path for opaque untextured primitives - went with it; the textured program
computes the same 5-bit value for depth 3, and it is the only program the frame now uses. (It stays, unused for
primitives, for the line segments' points.)

| battle, 600 frames from the state | draws/frame | flush | fps |
|---|---|---|---|
| a batch per state change | 78 | 2.5 ms | 31.5 |
| one batch | 1 | 0.7 ms | see 5 |

A real bug fell out of the same reading: for an untextured primitive without dither the shader took its foreground
from the (zero) texel and drew black where the shim draws `c >> 3`. The depth-3 path now uses the vertex colour, and
the 2D VRAM check went from "most frames differ by a little" to **17 of 18 checks byte-exact** (the eighteenth is the
1,918-pixel difference at frame 1700 that predates this work).

## 5. GP0 80h on the GPU (M5): the copies were the battle's real cost

With the batches gone, the split named the last item immediately:

```
gles: per frame over 29 vsyncs: listener 14.45 ms (47760 calls), flush 0.71 ms (87 flushes, 29 draws)
gles: the listener by kind: tri 1.20 ms (44454), rect 0.17 ms (3248), copy 13.08 ms (58)
```

Two copies a frame, 6.5 ms each. A copy was an upload of the shim's rectangle, and an upload has to be ordered after
the drawing before it - so each one waited for the whole frame's geometry, then converted and pushed 5-5-5-1 to RGBA8
on the CPU.

It does not have to be an upload at all. The target already holds the true VRAM - the drawing the shim's copy sees,
plus every primitive the CPU never drew - so a copy is a blit from the read copy to the target, translated. A blit is
ordered after the drawing in the command stream and does not stall. Both ends wrap, so it splits into at most four
pieces, the same split the upload uses; a copy with the mask rules still goes through the CPU's answer, because those
need a per-pixel pass and are rare.

That removed the copy's stall but the frame did not get faster - the time reappeared in the present, which reads the
target with `glReadPixels` and therefore waits for everything queued before it, the new blits included:

```
gles: present readback 11.47 ms (glReadPixels 10.91, 5-bit conversion 0.56)
```

## 6. The readback off the critical path (31.3 → 41.9 fps)

The present's wait is for the *current* frame's drawing, and the CPU has nothing to do until it returns. But it does
not need this frame's pixels: it needs a picture, and a frame is 20 ms. So the readback is issued into a pixel buffer
object and read one vsync later - the wait then lands on work that finished 20 ms ago - with the two buffers
alternating so the frame being read is never the one being filled.

Mapped pixel memory is uncached, so converting 320x240 out of it byte by byte cost 7.1 ms where the old
`glReadPixels` into ordinary memory cost 0.06: one `memcpy` out of the map first, then the same conversion, and the
whole readback is 2.4 ms. The first frame still takes the blocking path, so the picture is never late at startup.

`glReadPixels` at 0.04 ms is the same transfer it always was, now without the wait.

## The battle, in four steps

Each row is 600 frames from one saved battle state, `--renderer gles` against the software renderer on the same state
and the same binary:

| battle | fps |
|---|---|
| a read copy per batch | 18.75 |
| a read copy per vsync | 31.50 |
| one draw a frame (copies still uploads) | 29.98 |
| + GP0 80h as a blit | 31.27 |
| + the buffered readback | **41.85** |
| the software renderer, same state | 37.70 |

And the VRAM check on the same battle: 0.2% differing before the models appear, 0.7% once they do - it was 4.5%
before the untextured-colour fix and the copy change - with no undrawn primitive of any kind.

## What is left

| battle frame | ms |
|---|---|
| the listener (packing ~1,100 primitives) | 1.0 |
| the flush (one draw) | 0.6 |
| the read copy (one blit) | 0.6 |
| the copies (two blits) | 0.0-0.1 |
| the present (the PBO's memcpy and 5-bit conversion, then the fb0 present) | 3.2 |
| the game's own CPU (dynarec, GTE, SPU) | the rest, ~14 |

M5's other half came with it: a save state saved on this renderer used to carry only the transfers, because the shim's
array stops being the truth once the listener draws. The save reads the target back first now (row by row into the
shim's padded stride) and a load restores it through the listener's power-on upload; the `gpu_vram` block of a GLES
state is byte-identical to the software renderer's at the same frame, and the VRAM check is exact from the first check
after a load.

The present is now the renderer's largest cost and it is all CPU work on the picture - a 320x240 RGBA transfer and its
conversion - not waiting. The next thing to try there is doing the picture in the GPU's own format (the fb0 present
already knows 15-bit), or handing the display to KMS instead of reading it back at all; the second is the 17 ms swap
this design was built to avoid, so it would need the plane the frontend already owns.
