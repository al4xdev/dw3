# Audio: going after the SPU's 4.9 ms (carefully)

> Written in Portuguese; translated as it stands, with nothing added. **Kept as written on purpose**: it is the plan as it
> stood before the work. What was actually done, and what it measured, is in `docs/SOUND.md` "What it costs": stages 0,
> 1 and 4 were executed in the other order the profile asked for, taking the field/menu mix from 4.72 to 3.11 ms per vsync
> with the WAV byte-identical, and the battle's `aud` from 4.9 to 3.0-3.3 ms. Stages 2, 3 and 5 were not done.

## Why this plan is still standing (and changed its justification)

When it was written the goal was to give headroom back to the **software rasteriser's** battle frame (~18-19 ms), which was
the game's path. That changed: the **GLES** backend became the default (plan 4) and the measured battle now gives
**41.85 fps against software's 37.70** on the same state. The SPU's mix, though, is paid **the same on both renderers** -
and it became the largest single item left after the game itself:

| battle frame item | ms |
|---|---|
| the game's code (dynarec, GTE) | ~14 |
| **the SPU's mix** | **~4.9** |
| present (transfer + conversion + fb0) | ~3.2 |
| listener + flush + VRAM copies (GLES) | ~1.7 |

So: 4.9 ms is ~25% of the 20 ms budget and twice what the renderer has left. It is no longer about "avoiding slow
motion", it is about the margin that decides whether the boss battles and the effect-heavy scenes hold the cap.

## Measured context

- **The SPU's mix costs 4.9 ms a frame** in the battle, measured with `DW3_NO_AUDIO=1` (4.9 -> 0.0).
- The right instrument is the `aud` field of `DW3_GPU_LOG`, which measures **only** `spu_render` with the cycle counter.
  `DW3_PHASE` is **not** usable here: in a battle it costs 23 ms, it is that intrusive (`gte`/`model` come out inflated).
- What runs per frame: **24 voices x 882 samples** at 44.1 kHz, plus the noise, the capture buffers and the reverb (two
  39-tap FIRs per channel, on half the samples).
- I did **not** yet know the internal split (voices x reverb x the rest): that is stage 0.

## Rules for this front

- **The sound has to come out identical, byte for byte.** The validation is `--wav` before/after, with the same state and
  the same input: `cmp` on the two files. There is no "almost the same" here.
- No changing the rate, cutting a voice, quantising, or reordering operations in a way that changes rounding.
- One commit per stage, `aud` measured before/after, and the previous build's tag kept so we can go back.
- `DW3_NO_AUDIO=1` stays in the binary as a measurement switch (it already exists).
- Measure on the device with `emustation` stopped, like everything else.

## Stage 0 - measure from the inside (without optimising)

- Instrument, behind `DW3_GPU_LOG`, the time of: (a) the 24-voice loop, (b) `spu_reverb_sample`, (c) the noise and the
  capture buffers. One line a second, like the rest.
- Exit criterion: knowing which of those three owns the 4.9 ms. Without that, optimising is a guess - and this session's
  history shows where guesses lead.

## Stage 1 - idle voice (low risk, estimate 1-1.5 ms)

- A voice at `env.level == 0` in the `RELEASE` phase contributes nothing to the mix; today it runs interpolation, envelope
  and volumes anyway.
- Mandatory care: **the side effects have to stay** - the pitch counter advances and the block ends
  (`spu_voice_block_end`) can raise `ENDX` and loop, which the game reads. Only the part that produces a sample may be
  skipped.
- Validation: identical WAV; `aud` before/after; and a battle with effects to confirm no sound went missing.

## Stage 2 - global early out (low risk, estimate 0.3-0.8 ms)

- If no voice is audible, the reverb is off and no CD audio is coming in, the whole mix is silence: filling the buffer
  with zeros is exact.
- Only after stage 1, because it is its degenerate case.

## Stage 3 - NEON in the interpolation and the volume (medium risk, estimate 1-2 ms)

- The 4-tap Gaussian interpolation and the envelope/volume multiply are exact integer arithmetic; the working set
  (24 voices + buffers) is small and **fits in L1** - the opposite of the rasteriser, so SIMD tends to pay here.
- What does **not** vectorise: the ADPCM (serial per voice) and the order of the operations that define rounding.
- Validation: identical WAV.

## Stage 4 - NEON in the reverb's FIR (medium risk, estimate 0.3-0.8 ms)

- Two 39-tap FIRs per channel: products and sums in 64 bits. Exact if the sum happens in the same order, or if the order
  cannot change the result (integers: addition is associative, so it can be reordered safely).
- Only if stage 0 shows the reverb weighs enough.

## Stage 5 - another core (last resort, not recommended)

- The maximum gain is the 4.9 ms itself, but there is synchronisation (the game writes registers in the middle of a
  frame), a risk of glitches and contention for L2/DDR. Only with stages 1-4 done and time still missing.

## Completion criterion

`aud` <= 2.5 ms with the WAV **byte-identical** and no audible glitch. Ideal target <= 1.5 ms. If stage 0 shows the cost
is in the ADPCM or in something serial per voice, the plan stops here and the number is documented as the model's limit.

## What this plan does not solve

The renderer's present (~3.2 ms) and the 0.7% VRAM difference in the battle stay in plan 4, which was left open with those
two items. Here only the SPU is touched.
