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
| a batch per state change | 78 | 2.5 ms | 31.5 (this state) |
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

## What is left

| battle frame, 600 frames from one state | ms |
|---|---|
| the listener (packing ~1,100 primitives) | ~1.4 |
| the flush (one draw) | ~0.7 |
| the read copy (one blit) | ~0.4 |
| the present (glReadPixels 320x240 + the fb0 memcpy) | ~3.5 |
| the game's own CPU (dynarec, GTE, SPU) | the rest |

The present is the largest remaining cost of the renderer's own, and it is a readback whose `glReadPixels` waits for
the frame to finish. A double-buffered PBO would overlap it with the next frame's CPU work at the price of one frame
of display latency - the next measurement.
