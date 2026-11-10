# Wave 11 - the live vocoder stops buzzing (v1.14)

**Status:** shipped in v1.14.
**Kanso:** card 241 (Done).
**Goal:** the live phase vocoder must not buzz.

## Why

The *live* phase vocoder buzzed while the Sample tab's vocoder (the same
`VocoderCore`) was clean. Same core, same sound, different source - so
the bug was in how the live source was fed, not in the core.

## What was decided

- **The cause was the block-rate staircase.** The live ring is filled per
  block, so the write head only advances every block - and the engine
  passed that *block* position to the shifter for every sample in the
  block. The vocoder reads AT the write head, so `readLiveRing` clamped
  it to the newest frame and it fed the core **the same frame for all
  256 samples**. The vocoder faithfully re-synthesised that staircase.
- **The fix is per-sample positions.** The engine now passes the
  per-sample frame position (`liveWriteEnd - (i1 - n) - 1`) to the live
  shifter, and the vocoder reads the frame that is current at that
  sample instead of running its own read position. The read is
  continuous across block boundaries again, and the tape shifter's
  re-anchor window (which also saw the block steps) is smoother.

## How it was diagnosed

The buzz was not the vocoder - it was the *source* it was given. The
measured spectrum of the live vocoder's output had a sideband at 87.5 Hz
(187.5 - 100) **louder than the 100 Hz fundamental**, plus 287.5 Hz and
475 Hz: the zero-order-hold signature of a staircase, and exactly the
buzz that was heard. The granular shifter reads 220 ms behind the write
head, so it never hit the clamp - which is why only the vocoder buzzed.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The vocoder core would be at fault | It was not - the same core was clean with the sample source |
| The fix would break the granular shifter's timing | It did not - the granular shifter reads behind the write head, so the per-sample position is the same there |
| The tape shifter would change character | Its re-anchor window got smoother, not worse |

## What was verified

- The sidebands collapse: 87.5 Hz 0.0471 -> 0.0026, 287.5 Hz 0.0145 ->
  0.0003, 475 Hz 0.0089 -> 0.0003, while the fundamental gets *stronger*
  (0.0418 -> 0.0715).
- The selftest asserts that the live vocoder's sideband energy stays
  under 25% of the fundamental (it was 169% before the fix).
- Selftest 175 -> 176 checks.

## What came out of it

- The lesson stuck: the live path's frame position is per sample, and a
  block-rate source is a staircase.
- The clicks (wave 15) were the same family of bug - a value that was
  constant for a block where it should have moved per sample.
