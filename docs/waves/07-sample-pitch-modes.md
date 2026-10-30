# Wave 7 - the sample pitch modes (v1.10)

**Status:** shipped in v1.10 (`97d1115`).
**Kanso:** card 92 (Done).
**Goal:** the same three algorithms for the sample material - and fix the
phase vocoder on the way.

## Why

The Live tab could choose between granular, tape and vocoder; the Sample
tab had only varispeed (the original tape behaviour: the material plays
faster or slower, so its *length* changes with the pitch). That is not
what a sample player should do - a pitch-shifted sample should keep its
length.

## What was decided

- **Three modes, varispeed first.** Varispeed (the default, so old
  pieces sound unchanged), Granular and Phase vocoder. The granular and
  vocoder modes keep the material advancing at 1x and shift the pitch
  only.
- **The shifters became source-agnostic.** `LivePitch.h` was refactored
  into the cores (`GrainCore`, `VocoderCore`) plus thin wrappers: the
  live ring wrappers for the microphone and a `SampleVoice` (position +
  grain + vocoder) for material playback. The engine reads the material
  once (`readMaterial`) for every mode.
- **A mode change drops the sample voices**, so each re-anchors.
- **The phase vocoder had a real bug.** Its overlap-add rows accumulated
  across ring wraps, so a stale slot echoed back one ring length (4096
  rows, ~90 ms) later - audible as periodic level dips and lost zero
  crossings at ratios != 1. The frame's fresh hop now assigns its rows,
  so every output row is exactly its four window contributions. The fix
  was modelled in Python first.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| Refactoring the shifters would break the live path | The live path is the same cores with a different wrapper - and its frequency checks stayed green |
| A mode change would click | The sample voices are dropped and re-anchor |
| The vocoder's OLA bug would be hard to see | It was found by listening to the ratio != 1 case, then modelled in Python before the C++ fix |

## What was verified

- The sample granular and vocoder output frequency at r=1, 2 and 0.5,
  plus the output peak.
- The selector round-trip.
- Selftest 153 -> 162 checks.

## What came out of it

- The granular mode leaves a small tick at the grain boundary when the
  ratio moves (~-40 dB) - inherent to granular shifting with a moving
  ratio, and it is written down in `known-issues.yaml` (KI-003).
- The material's length is now independent of the pitch, which is what
  the Live tab's range work (wave 10) needed for the comparison to make
  sense.
