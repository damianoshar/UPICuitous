# Wave 10 - the live path takes the sample path's pitch range (v1.13)

**Status:** shipped in v1.13.
**Kanso:** card 240 (Done).
**Goal:** the Live tab must reach the same pitch as the Sample tab.

## Why

The Live tab was clamped to `0.25x - 3.0x` while the Sample tab shifted
`1/16x - 16x` (four octaves) - the same ratio formula
(`arcPitch / Ref`), a different limit. A live arc simply could not play
the gesture it was drawn for.

## What was decided

- **One range for both: `1/16x .. 16x`.** `kLiveMinRatio` /
  `kLiveMaxRatio` are now the same numbers the sample path uses.
- **The granular grain shrinks above ~5.5x.** The source window (40 ms)
  stays constant, so the read never needs more history than one grain
  and the 0.22 s read lag does not grow with the shift. Below 5.5x the
  old fixed grain is kept, so the sound of the usual range is unchanged.
  `GrainCore::setHop()` keeps the crossfade position when the grain
  length changes, so there is no click.
- **The phase vocoder needed no change** - it already remaps the
  partials and drops what falls outside the spectrum.
- **Tape keeps an octave up and down.** It reads the mic faster than the
  mic arrives, so above ~2x it would have to re-anchor every few
  milliseconds. The range is a property of the algorithm, not a bug.
- **A failing selftest now exits non-zero.** It always exited 0 before,
  so a red check in the log was not a red build.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| A shorter grain would change the sound of the usual range | The threshold is at 5.5x, so the common range is untouched |
| The grain length change would click | The crossfade position is kept when the grain length changes |
| The tape shifter would break at high ratios | It keeps its octave up/down - documented, not silently clamped |

## What was verified

- The live range matches the sample range.
- The grain shifter reaches 4x and clamps at 16x.
- A live arc measures 400 Hz (4x), 1600 Hz (8x) and 25 Hz (1/4x) at the
  engine level.
- Selftest 169 -> 175 checks.

## What came out of it

- The live phase vocoder still buzzed - the block-rate staircase was
  exposed by the wider range (wave 11).
- The live path's delay is now a real number the host has to know about
  (wave 13).
