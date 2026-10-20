# 14 — The 2D artefacts: what the owner saw, what the check said

The owner played with `--renderer gles` and reported broken dialogue text and white boxes in 2D. The question that
matters for the method: could the VRAM check have found it without them playing? It did, and in one line.

## What the check said

Running the same scripted 2D run (registration, dialogue, menus) with the check every 100 frames:

```
vram check 17 (frame 1700): 3.1% of the VRAM differs (16222 pixels), first at 148,43 ...
   the undrawn counters say what is missing (triangles 13366, rects 0, segments 0, uploaded 1570048 px)
gles: undrawn reasons: gouraud 124, semi 22150, dither 124
```

13,366 undrawn triangles, and the reason counters named it: **semi-transparency**. This game draws its 2D text and UI
with semi-transparent primitives, not opaque sprites - so every glyph and every box was missing while everything else
matched.

## The fixes, in the order the numbers asked for them

| change | frame 1700 |
|---|---|
| before | 3.1% (16,222 px), 13,366 undrawn triangles |
| semi-transparent rectangles and triangles through the textured program | **0.4%** (1,918 px), 0 undrawn triangles |
| line segments, enumerated with the shim's `gpu_segment_walk` | 0.2-1.2% across frames 1700-3000, **0 undrawn of any kind** |

The 2D frames now vary between 0.2% and 1.2% of the VRAM differing (19 of 31-41 checks exact), in small regions, with
nothing left undrawn. That is a maths or ordering difference *inside* a primitive, not a missing one.

Two details worth keeping:

- **A textured pixel blends only where its texel's bit 15 says so** (or in mode 0); an untextured one has no texel and
  blends wherever the primitive says. That asymmetry is in `gpu.c`'s `gpu_pixel` and now in the shader.
- **Lines cannot be drawn by a GPU exactly**: the PS1's stepping is a per-pixel rule, so the pixels are enumerated on
  the CPU by the shim's own walker and drawn as points with their per-pixel colour - what the SDL3 renderer does too.
  At a few hundred segments a frame that is a few thousand points, not a span loop.

## The instrument that made this self-service

`render_gles_undrawn_reasons` exists because a count without a reason sends the next person to the wrong corner - it
sent me to the wrong corner once already in M4. With it, "13,366 triangles undrawn" becomes "22,150 semi, 124 Gouraud,
124 dithered" and the work is obvious.

`DW3_GLES_DUMP_FRAME=<frame>` dumps both VRAMs at a chosen frame (the first-failure dump could not reach a
mid-sequence scene): that is how the 502-pixel texel-transparency difference and the boot-scene anomaly were read
offline instead of guessed at.
