# Wave 12 - no more clicks (v1.15)

**Status:** shipped in v1.15 (`260`).
**Kanso:** card 260 (Done).
**Goal:** find and kill every click.

## Why

The user reported: "audio sometimes has clicks, as if the buffer is small
even in large buffers". The second half was the important clue: a
buffer-size problem would *not* happen in large buffers.

## What was decided

Nothing about the DSP changed. The engine renders **bit-identically**
whatever block size the host uses, and the peak second-difference of
every mode is ~0.0002 (the natural curvature of a sine). The clicks came
from the plumbing around it, and there were four sources:

1. **A dropped block was cut, not faded.** The audio thread never blocks
   on the model lock (it try-locks), but when the message thread held it
   (the 30 Hz status timer does, and so does every UI action) the
   callback returned with a **cleared** buffer: the previous block's last
   sample to zero is a step, i.e. a click - and it happens whatever the
   host's buffer size is, which matches the report. The score now
   renders into its own buffer, and a block that cannot be rendered is
   faded out from the last sample over ~2 ms and faded back in
   afterwards.
2. **`engine.reset()` restarted every oscillator's phase.** That is a
   full-scale jump (measured: 0.97 on a 0.4 peak) whenever the engine
   was reset mid-flight: a page change, a loaded sample, a capture
   install, a host state load, a pitch-mode change. The phase is
   free-running state, so `reset()` now only drops the material/ring
   readers (they have to re-anchor); the phase keeps running.
3. **The gains stepped.** The master gain and the monitor (input) gain
   were applied per block, so a slider move or host automation was a
   step in the output - a click. Both are now smoothed per sample (~17 ms
   time constant).
4. **The model changed under the audio thread.** A page change, a
   loaded sample, a capture install, a pitch-mode switch or a host state
   load replaces the sound that is playing - a step, i.e. a click. The
   message thread now bumps a model epoch counter and the audio thread
   fades across the change (the old tail ramps down, the new content
   ramps in).

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The clicks were in the DSP | They were not - the render is bit-identical across block sizes |
| Fading the dropped block would change the sound | It only affects the block that could not be rendered |
| A fade across a model change would hide a real edit | It is a 2 ms cross-fade, not a mute |

## What was verified

A new selftest section (14 checks, 176 -> 191) - a **click hunt**: it
renders every mode (osc / sample varispeed / sample granular / sample
vocoder / live granular / live tape / live vocoder) in 64- and
1024-sample blocks and asserts

- the second-difference peak stays under 0.002, and
- the two renders are identical (block-size independence, measured
  0.00000),

plus the same with a moving pitch curve, plus the phase-continuity
check for `reset()` (it fails with the old code: max diff 0.97).

## What came out of it

- The known and accepted one: the SAMPLE tab's granular mode shifts the
  grain taps while the ratio moves, which leaves a small tick at the
  grain boundary (measured 0.010 on a 0.32 peak, about -40 dB). It is
  inherent to granular pitch shifting with a moving ratio; the guard
  allows it, and it is `known-issues.yaml` KI-003.
- The lock that made the dropped block possible was still there - it
  took until wave 15 to remove it.
