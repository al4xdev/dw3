# 09 — Stage 0: what a battle frame asks a GPU to do

The plan's first step was to find out whether the cost of a draw call (11.2 µs, `07-cost-model.md`) would sink the
backend before a line of it was written. Answer: it does not, but only because the texture page and the CLUT travel per
vertex.

## How it was measured

A checkpoint of the game's first battle (FIGHTSTG at frame 19760, `tests/replay/scripts/first_battle_save.json`) is
loaded by the headless host binary, which then runs 1200 more frames with `DW3_GPU_LOG` on:

```sh
# the state is bound to the binary that saved it: regenerate it after any rebuild
./build/test-host/dw2003 --disc "$DISC" --script tests/replay/scripts/first_battle_save.json \
    --save-state battle_start:/tmp/bs.state --save-state-exit
DW3_GPU_LOG=/tmp/battle.log ./build/test-host/dw2003 --disc "$DISC" --load-state /tmp/bs.state \
    --max-frames 20960          # the state resumes at 19760, so this is 1200 frames of battle
```

The counters are game facts (primitives, states, transfers), not device facts: the same game state draws the same
primitives wherever it runs, so the host answers this question as well as the handheld would and about 40x faster.

## The numbers, per frame

| counter | value |
|---|---|
| primitives submitted | **~1060-1080** |
| batches a GPU backend would flush | **~103-116** |
| of which semi-transparent | ~200 |
| of which `BG - FG` (the mode that cannot batch) | **0** |
| primitives checking the mask bit | 0 |
| primitives setting it | 0 |
| `LoadImage` (CPU to VRAM) per frame | **0** |
| VRAM-to-VRAM copies per frame | ~2 |
| textured pixels | fill 0, flat-textured 29.9k, flat-blended 42.1k, Gouraud-textured 15.0k, Gouraud-blended 0.8k |

## What it decides

- **Draw-call overhead: 110 x 11.2 µs = 1.2 ms per frame.** Against the 1.6-4.4 ms of margin in `08-format-and-present`,
  that fits. The plan's stop condition (300+ batches) is not met; it is not even close.
- **No mask bit in this battle**, and no `LoadImage`. Two VRAM-to-VRAM copies are cheap (`glBlitFramebuffer` of a
  rectangle), and the upload path is not on the per-frame critical path at all - which retires the biggest unmeasured
  risk in `07-cost-model.md`. Both still have to work (other scenes use them), but they are not what decides the frame.
- **~88k textured pixels per frame**, not the ~150k estimated from the handheld's counters: this scripted battle has
  one enemy. A three-enemy battle draws more, so the pixel budget stays as measured on the device (1.2-2.8 ms), and
  this number is a floor, not the rule.

## The mistake this step corrected

The first version of the counter included the texture page and the CLUT in the batch key and reported **~300
batches**: twice the plan's stop condition, which would have killed the backend on a wrong number. The reference keeps
those two per vertex (`nointerpolation` attributes in `BatchVertex`, `gpu_hw.h:121-137`), so polygons from different
pages share a batch when depth, transparency mode, dithering, texture window and mask flags agree. With the key fixed,
the same run reports ~110. The lesson is the same one `measurements.txt` keeps recording: a counter is a hypothesis
until it is checked against the implementation it is supposed to model.

The counter lives in the shim (`psyq/gpu.c`, `gpu_log_draw`), gated by `DW3_GPU_LOG`, and prints
`prims batch semi bgfg cmask smask loads copies` after the existing fields.
