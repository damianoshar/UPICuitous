# Wave 18 - the live reverse (v1.20)

**Status:** shipped in v1.20.
**Kanso:** card 247 (UPIC-27, M).
**Goal:** the Live tab can play the microphone backwards.

## Why

The reverse idea in `FUTURE_IDEAS.md` (UPIC-22) has two halves: the
sample reverse (wave 17, done) and the **live** one - "the live input
becomes a pitched reversed delay with short feedback". The user asked for
the simpler shape of it: **small buffers, reversed, with the buffer
duration chosen by the user**.

So the Live tab gets a reverse mode whose *buffer duration* is a control:
the microphone's last 0.5 / 1 / 2 seconds are captured and played back
mirrored, pitch-shifted by the arc as usual.

## What was decided

- **The capture is mirrored, not the read.** The same trick as the sample
  path (wave 17): the capture's write head moves **backwards**, so the
  buffer holds the last D seconds mirrored in time, and reading it forward
  plays the input backwards. The shifters (granular, tape, phase vocoder)
  are **not changed at all** - they read a ring forward, and a mirrored
  ring is an ordinary signal that happens to run backwards.

  The alternative - making the shifter read backwards - would have meant
  teaching the grain crossfade, the tape re-anchor and the vocoder's
  overlap-add to run in the opposite direction. The future-ideas text
  knew this was the expensive half.

- **Two buffers, and they swap roles.** One is captured, the other is the
  frozen segment the shifter reads. When the capture is full, the two
  swap: the fresh segment becomes the playback and the buffer that was
  playing becomes the new capture. Both take exactly D seconds, so the
  swap lands when the playback ends.

  Nothing is ever **copied**, so there is no spike in the audio thread -
  a copy of a 2 s stereo segment would be a ~0.3 ms stall, which is
  audible as a dropout in a small block.

- **The segment's ends are faded** (5 ms), so the loop is click-free: the
  capture fades in at its oldest end and out at its newest end, and in the
  playback that is a fade out at the start and a fade in at the end. This
  is the "fade the ring ends" of the future-ideas note.

- **The playback head starts after the shifter's lag** (250 ms - more than
  the granular shifter's 220 ms), because every live shifter reads
  *behind* the head. The head then runs at 1x and stops at the segment's
  end.

- **The message thread never touches the audio thread's state.** The
  buffers are allocated once, at the maximum duration (2 s); the chosen
  duration only uses the first N frames, so nothing is reallocated while
  the audio thread may be reading. The duration and the restart are
  passed as atomics, and the audio thread does the resetting.

- **The pitch is the arc's, as usual.** The reverse changes the direction
  of the material, not the ratio.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The mirrored read would change the pitch | It does not: the read speed is the same, only the direction - the same property the sample reverse has (and the same measurement) |
| The segment swap would click | The segment's ends are faded (5 ms), and the live voices re-anchor on the new segment |
| Copying the segment would stall the audio thread | Nothing is copied - the two buffers swap roles |
| The message thread could reallocate under the audio thread | The buffers are allocated once (at the maximum duration); only the frame count changes |
| A duration shorter than the shifter's lag would be silent | The smallest choice is 0.5 s, and the playback starts 250 ms in |

## What was verified

Selftest 230 -> 237 (seven new checks):

- the live reverse button is there and turns the effect on;
- the reverse buffer has the chosen length;
- **the captured segment is mirrored**: a rising ramp comes out falling
  (the quarter holds 0.750, the three quarters 0.250);
- the segment keeps the input level;
- **a forward segment plays the input forwards and a mirrored segment
  plays it backwards** (the slope of the rendered output flips sign:
  +0.1495 -> -0.1425) - the load-bearing property, measured end to end
  through the shifter.

The full gate: app selftest 237/237, plugin smoke 15/15, pluginval
strictness 5 SUCCESS.

## What came out of it

- The live reverse is a **reverse delay**: the output is the input's last
  D seconds, backwards, delayed by D plus the shifter's lag. The delay
  is the duration the user picks.
- The **feedback** of the original idea (30-50% back into the write head)
  is not implemented - the segment simply loops. With the fades, the loop
  is clean, but the repeats are separated instead of overlapping.
- The **signed playhead** (UPIC-28) is still the transport that would
  make a mirrored *stroke* sound mirrored: the score itself running
  backwards. This wave is the material running backwards, which is the
  half that can be heard on its own.
- The MIDI input foundation (UPIC-24) is still missing - the reverse is
  on a button for now.
