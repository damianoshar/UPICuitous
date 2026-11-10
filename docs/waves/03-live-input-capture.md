# Wave 3 - live input and capture

**Status:** shipped in v1.4 / v1.5 (`b8ffca8`, `5f2bb65`).
**Kanso:** card 42 (Done).
**Goal:** let the instrument be played - the microphone becomes a material.

## Why

Until now the app could only make sound from its own material. UPIC is an
instrument: you draw a gesture, and then you *play* it. A microphone
turns the app into something you can perform with.

## What was decided

- **The input tap is lock-free and lives in the audio callback.** The
  tap copies the device buffer's input channels into a scratch buffer
  *before* the buffer is cleared, and advances the capture ring. No lock
  is taken on the audio thread.
- **Monitoring is a mix, not a mode.** The input gain, the Mon button and
  the capture button sit in one small overlay. The monitor mix is added to
  the output independently of the transport, so you can hear yourself
  while nothing is playing.
- **A capture goes straight into a sample slot.** The take is recorded
  into the selected slot, pre-processed for a seamless loop, and the
  slot's buffer is replaced (never written in place - the audio thread
  may be reading it).
- **The handoff is explicit.** The audio thread fills the ring and hands
  the take over; the message thread installs it (via `callAsync`, so the
  UI is never touched from the audio thread) and only then replaces the
  slot's buffer.
- **A live arc follows its pitch curve (v1.5).** A mode-2 arc reads the
  live input and pitch-shifts it by the same ratio the oscillator arcs
  use (`arcPitch / Ref`). The shifter is the granular one, in
  `LivePitch.h`.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| Installing a take while the audio thread reads the old buffer | The buffer is replaced, never written in place, and the old one stays alive until the reader is done |
| The input tap would fight the output | The tap runs before the clear, and the monitor mix is added afterwards |
| A device change would leave a half-recorded take | The partial take is discarded and the capture is reported as aborted |

## What was verified

- The capture path headlessly: a synthetic take becomes a slot with the
  right id, name, channel count and sample rate.
- The live arc follows the arc's pitch (the engine-level frequency
  check).
- The live voice is silent when the input is off.

## What came out of it

- The granular shifter was the only live shifter - the tape and the phase
  vocoder came with the mode selector (wave 6).
- The live path is delayed (220 ms for the granular shifter), which the
  host could not compensate for until wave 13 reported it.
- The live input was the reason the tab strip (wave 5) needed a Live
  tab: the material and the layer are the same choice.
