# 15 — The battle: 0.2% early, 4.5% once the 3D models appear

The 2D scenes were fixed (see `14`); this is the first measurement of the battle, which is what M3 and M4 exist for.

## How it was measured

A state at the story's first battle (`battle_start`, frame 19760) generated on the console from the scripted run, then
640 frames from it with the check every 60:

```fish
./dw2003 ... --renderer gles --script ./first_battle_save.json \
        --save-state battle_start:/tmp/bs.state --save-state-exit     # ~11 min on the console
./dw2003 ... --renderer gles --load-state /tmp/bs.state --max-frames 20360   # with the check
```

Note: a state is bound to the binary that wrote it, so every rebuild costs another ~11-minute generation. That is the
price of a battle measurement and the reason the 2D loop (90 seconds, no state) is the one to iterate in.

## The numbers

| frames | VRAM differing | undrawn |
|---|---|---|
| 19860 | 0.2% (1,085 px) | 0 |
| 19920 | 0.2% (1,155 px) | 0 |
| 19980 | **4.5%** (23,683 px) | 0 |
| 20100 | 4.5% (23,025 px) | 0 |

Nothing is left undrawn at any point: every triangle, rectangle and segment the listener receives is drawn. So what
remains is inside a primitive.

## What the dump says

From the frame-19980 dump, the 22,818 differing pixels split into two populations:

- **55% have a channel delta of (-1,-1,-1)** (5-bit units): one step darker, in all three channels. That is the shape
  of a rounding difference, not of a wrong primitive.
- **a minority differ completely**: the first differing pixel is `sw 0xe700` (0, 0, 29) against `hw 0xfbc0`
  (0, 30, 31), and neighbours look displaced. That is the shape of geometry.

The floor-the-interpolated-colour change (commit `a33a90a`) addressed the first shape and did not move the number,
which says the dominant population has another cause - most likely the PS1's own edge rule for triangle coverage
(which pixels a half-covered edge includes), since a one-pixel edge difference between two similar neighbours
produces exactly a "one step off" histogram on a shaded model.

## Next, in order

1. Compare a *single* triangle: draw one with known vertices, in both renderers, and diff the covered pixels. The edge
   rule (`gpu_triangle`'s "left/top" convention, P2 cross product with its sign tests) can then be copied into the
   shader's rasterisation policy - which on a GPU means biasing the clip-space positions by half a pixel in the right
   direction, not writing an edge test.
2. Re-run the battle (one more 11-minute state) and see what is left.
3. Only then M5 (CopyVRAM through the GPU, checkpoint read from the target) and a real frame-time measurement of the
   battle with `--renderer gles`.
