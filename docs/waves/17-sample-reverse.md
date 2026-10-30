# Wave 17 - the sample reverse read (v1.19)

**Status:** shipped in v1.19.
**Kanso:** card 245 (UPIC-25, S).
**Goal:** a sample slot can play its material backwards.

## Why

This is the first slice of the reverse idea in `FUTURE_IDEAS.md`
(UPIC-22). The idea is a *signed playhead*: a MIDI action moves the time
bar, and backwards the sample is produced in reverse. That is a bigger
feature (the scrub transport, UPIC-28), but the *sound* it needs can be
proved first - the future-ideas text says exactly that:

> The reverse slice (UPIC-25 + UPIC-27) can be proved with an on-screen
> Reverse button *before* any MIDI plumbing exists - MIDI is only the
> controller.

So this wave is the on-screen Reverse button: one slot plays backwards,
and you can hear whether it is worth the rest of the work.

## What was decided

- **The flag belongs to the slot.** `upic::SampleSlot::reversed` - the
  material of that slot is heard backwards. The future-ideas text says
  "slots play backwards at `-ratio`", so it is per slot, not per arc.
- **The engine mirrors the read, not the position.** The design note
  sketched "the slot reader advances at `-ratio`". That is what the
  varispeed mode does, but the granular and the vocoder modes keep the
  material advancing at 1x and shift the pitch - so a negative position
  would mean teaching the grain crossfade and the vocoder's overlap-add
  to run backwards. Instead the *material* is mirrored: the read index
  runs backwards, with the same interpolation and the same wrap rules.
  For the varispeed mode that is exactly "the reader advances at
  `-ratio`"; for the other two it is the same sound with far less risk.
- **All three pitch modes work unchanged**, because they all read the
  material through the same `readMaterial`. The mirrored material is an
  ordinary signal - the shifter does not know it was reversed.
- **The pitch does not change with the direction.** The read speed is the
  same, so the frequencies are the same; only the time axis is mirrored.
  Measured at three ratios, because at ratio 1 a reversed tone is just a
  phase flip and would not prove anything:

  | arc pitch (Ref 220) | forward | reversed |
  |---|---|---|
  | 220 Hz (ratio 1) | 220 Hz | 220 Hz |
  | 440 Hz (ratio 2) | 440 Hz | 440 Hz |
  | 110 Hz (ratio 0.5) | 110 Hz | 110 Hz |

  So the reverse does **not** cancel the ratio and does not change the
  pitch on its own - the arc still plays the pitch it draws, in both
  directions.

  What the reverse *does* change is the material's time axis and its
  spectral envelope: a rising sweep in the sample becomes a falling one, a
  phrase is heard backwards, and the formants are mirrored - that is the
  classic "tape played backwards" (a voice turns into a robot, a drum hit
  into a boing). For a pure tone there is nothing to hear but a phase
  flip, which is why the ratio table above matters.
- **The seam stays seamless.** A seamless material ends where it starts,
  so the mirrored read is seamless too - the loop wrap is the material's
  own seam, read from the other side.
- **It is session state, like the pitch modes and the grid.** The piece
  format is untouched (the future-ideas text says the file format is not
  part of this slice), so the flag is not written to the JSON.
- **A capture keeps the flag.** The capture replaces the *material*, not
  the slot - the slot's direction survives.

## The mirrored read

The forward read interpolates between the frames `ia` and `ia + 1`. The
mirrored read interpolates between the frames `n - 1 - ia` and
`n - ia`, with the fraction running the other way:

```cpp
const int ra = (nFrames - 1 - ia + nFrames) % nFrames;
const int rb = (nFrames - ia) % nFrames;
ia = ra; ib = rb;
fr = 1.0f - fr;
```

That is `x(M - p)` - the material read at the mirrored time - with the
same interpolation and the same wrap rules.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The mirrored read would click at the loop seam | It does not: a seamless material ends where it starts, so the seam is read from the other side and matches |
| The direction would change the pitch | It does not: the read speed is unchanged, only the direction - the selftest measures 220 Hz in both directions |
| The granular / vocoder modes would need a reversed position | They do not: the material is mirrored, so the shifter sees an ordinary signal |
| The flag would drift into the piece format | It does not: the selftest checks that the slot's JSON has no "reversed" key |

## What was verified

Selftest 211 -> 227 (sixteen new checks):

- the reversed render **is** the forward render mirrored (max difference
  0.000012 over the middle of a one-second traversal);
- the pitch follows the arc's ratio in **both** directions - 220 / 440 /
  110 Hz forward and reversed (at ratio 1 a reversed tone is only a phase
  flip, so the other two ratios are what prove it);
- all three pitch modes still sound on the mirrored material;
- the flag is session-only (the piece format does not carry it);
- the Reverse button exists, starts off, turns on ("Rev ON"), is toggled,
  and turns off again.

The full gate: app selftest 223/223, plugin smoke 15/15, pluginval
strictness 5 SUCCESS.

## What came out of it

- The sound is proved; the controller is not. The next reverse step is the
  **live pitched reverse delay** (UPIC-27, M): the live ring is read
  backwards with a short feedback. That one cannot be done by mirroring a
  buffer - the ring is written in real time, so it needs a real reversed
  read with feedback.
- The **signed playhead** (UPIC-28, M) is the transport that would use
  it: a MIDI action moves the time bar backwards, and then the score
  time runs backwards too, so a mirrored stroke sounds mirrored. The
  score is already a function of time, so that part is free - what is
  missing is only the signed playhead and the MIDI input (UPIC-24).
- The live path is untouched here: a live input cannot be rewound.
