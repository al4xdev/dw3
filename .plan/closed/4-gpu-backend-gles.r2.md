# GPU backend (`gles`): done, all the way to being the default; the present is what is left

> Written in Portuguese; translated as it stands, with nothing added. The table entries and the numbers are the ones the
> plan recorded at the time; the renderer's own account, with the final measurements, is `docs/gpu/16-frame-budget.md`.

A follow-up revision of the GPU plan. Stages 0-5 are executed and the renderer **became the device's default by the
owner's decision** (it was an option in the original plan). What is left is in the last section, with numbers.

## Where we were, where we are

| | battle (600 frames of one state, same binary) |
|---|---|
| software, same state | 37.70 fps |
| `gles`, first measurement | 18.75 fps |
| `gles`, today | **41.85 fps** |

And the scripted replay's battle window (frames 19760-20800) runs at the **50 Hz cap** - the whole run gives 49.90 fps
against the software renderer's 49.67 on the same script. In 2D the two are level at the cap (48.99 fps) and the VRAM
check is **exact in 17 of 18 comparisons**.

VRAM against software, battle: 0.2% before the models, **0.7%** with them (it was 4.5%), zero undrawn primitives.

## Stages

- [x] **Stage 0 - measure the real game.** ~1060-1080 primitives a frame, **~110 batches**, 0 `LoadImage`, ~2 VRAM
  copies, ~88k textured pixels. The continuation criterion (<=150 batches) passed.
- [x] **M1 - context, VRAM and present.** EGL on GBM, offscreen, no window and no DRM master; an RGBA8 1024x512 target +
  a read copy; no swap (17 ms), no mode change; the picture leaves through the fb0 present that already existed.
  Changed against the plan: the present is **not** an RGB565 pass + native readback (reading the target as RGB565 cost
  **28 ms**) - it is a `glReadPixels` of the RGBA8 target and an 8-to-5-bit conversion, now through a pair of PBOs.
- [x] **M2 - rectangles and sprites**, **M3 - triangles with the CLUT in the shader** (4/8/15-bit, the texture window,
  `texelFetch`, integer interpolation like the shim's), **M4 - semi-transparency (4 modes), mask and dithering** through
  `gl_LastFragColorARM` and flat varyings. DuckStation's batch key became **per-vertex state** after the measurement: the
  four modes, the mask, the Gouraud flag, dither and the drawing area were *batch* state and each cost a draw.
- [x] **M5 - VRAM copy and checkpoint.** GP0 80h became a blit (6.5 ms -> 0.06 ms a copy); a save state reads the target
  back before writing (the `gpu_vram` block of a state saved on `gles` is byte-identical to one saved on software, and
  after a load the check is exact from the first comparison).

## Decisions that changed on the way (and why)

- **`gles` is the device's default**, not an option: `settings.json` with `"renderer": "gles"`, and the handheld menu
  writes the same. Reverting is `"software"` in the file or `--renderer software`; the software renderer's source is the
  `software-closed` tag. Software **stays the oracle**: the VRAM check compares against it.
- **Selection by `--renderer`/`video.renderer`**, not by `DW3_RENDERER` as the plan said (it is the vocabulary the stack
  already had). On the way it turned out that the file's `video.renderer` was **ignored** (`"gpu"` ran software).
- **One VRAM read per vsync, not per batch** (the plan did not foresee it): it was the difference between 18.75 and
  31.5 fps.
- **`glBlitFramebuffer` obeys the scissor** - the read copy's blit runs with the scissor off; the drawing area became a
  fragment test.

## What is still open (the TODO from here)

- [ ] **The present: ~3.2 ms a frame**, the largest cost the renderer has left. It is a 320x240 RGBA transfer from the
  PBO + `memcpy` (mapped memory is uncached: converting straight out of it cost 7.1 ms) + the 8-to-5-bit conversion + the
  fb0 present. Ideas, in order: let **fb0** take the format the target already has, or deliver the picture through KMS
  instead of reading it back - the second was the 17 ms swap this design exists to avoid, so it would need the plane the
  frontend already owns.
- [ ] **The 0.7% difference in the battle** (it was 4.5%). The populations now are: 55% of the pixels with a (-1,-1,-1)
  delta in 5-bit units (rounding/edge) and a minority with displaced geometry. The next step is still the
  **single-triangle** experiment to copy `gpu_triangle`'s edge rule - a half-pixel bias in clip space, not an edge test in
  the shader.
- [ ] **The 2D check at frame 1700**: 1,918 pixels (0.4%) that already existed before this round, in the region
  269,195..312,492.
- [ ] **`DW3_GLES_LOG`/`DW3_GLES_CHECK`/`DW3_STATE_ANY` are measurement instruments**, not game options: they stay in the
  code (they cost a counter read per event when on) and must not be left on in the owner's session.
- [ ] **Audio (4.9 ms a frame)** and the game's own C stay outside this plan (plan 3 in the backlog).

## Completion criterion (revised)

The original asked for the heavy battle at <=20 ms a frame. Measured today: **23.9 ms** on the first battle's heaviest
state (41.85 fps) and **20 ms** in the replay's window, which already holds the 50 Hz cap. ~4 ms are missing, and the
present is where they are.

## Evidence

`docs/gpu/01`-`16` in the game repository, with `docs/gpu/16-frame-budget.md` as this round's account (the four changes,
the dead ends that were measured - `glBufferSubData` at 105 us a draw, reading the target as RGB565 at 28 ms - and the
final table). Commits: `b0d17d3`, `494e60a`, `c2ce59d` (renderer), `ec234a3`, `2968e84` (`docs/gpu/16`), `876749c`,
`0d5a41b` (default), `1796e2c`, `b28ff2b` (save state).

## What failed, or did not happen as written

Added when the plan was closed with what it has. Stages 0-5 exist and the renderer is the default; these are the failures.

- **The completion criterion is not met.** It asked for the heavy battle at <=20 ms a frame; it measures **23.9 ms**
  (41.85 fps). The replay window holds the cap, but that is the lighter half of the same battle.
- **The renderer lost somewhere, and the plan never considered it.** In light 2D scenes (menus, boot) this renderer is
  **slower** than the software one: 43.5 fps against 57.7 on the scripted run, because the present surfaces the frame's
  GPU work (the boot's texture uploads) once per vsync. The plan's premise was that a GPU backend is faster, full stop.
- **The present's 3.2 ms was not attacked**, and the two ideas that could (fb0 in the target's format, or a KMS plane) are
  either untested or the 17 ms swap this design exists to avoid.
- **The 0.7% VRAM difference is still there** and the single-triangle edge-rule experiment that would explain it was never
  run. The 0.2% figure before the models was never explained either.
- **The 24-bit display path was fixed by reading the code, not by watching a movie.** No FMV was ever run on this renderer
  end to end; if the fix is wrong, nobody has seen it yet.
- **A bug was found and left**: `--input-test` hangs on this device (no output, immune to `timeout`), and the `dw2003`
  processes it left behind were only killed by hand.
- **Measurement discipline failed twice**: numbers were compared across two different rates (50 Hz vs the machine's
  60 Hz), and one "no regression" conclusion needed an A/B against an older build to be trusted. Both cost a round.
