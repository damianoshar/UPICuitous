# Wave 22 - the signed playhead (and the MIDI scrub that drives it)

**Status:** in progress (step 1 + step 2 landed; plugin MIDI bus landed; host
transport follow and the crossfade polish are next).
**Kanso:** card 248 (MIDI scrub transport) stays in its column; the done
card for the wave will be created when the wave ships.
**Goal:** make the score run backwards, and let a MIDI controller drive it.

## What shipped so far

### Step 1 - the signed playhead (engine + transport + UI)

The engine's `render()` now takes a signed `rate` (default +1). The active
sample range, the per-sample time and the sample anchor all become
direction-aware, and the sample / live / oscillator paths run backwards by
**mirroring the material, not the read position** - the same trick as the
v1.19 sample reverse, so every pitch mode keeps working with no shifter
changes.

The transport advances `audioPlayhead` by `rate * chunk / sr`; page
boundaries go the other way (previous page / loop wrap / stop at 0); a
symmetric terminal fade covers the start of the piece; a `Rev` toggle in
the transport row flips the direction; the time display gets a `REV `
prefix. A direction change no longer re-anchors mid-stream with a click:
it crossfades over ~20 ms (960 samples) - fade out the old direction, reset
the voices, fade in the new one.

Headless test 24 renders a ramp slot forward and backward and checks the
backward render is the forward render mirrored.

### Step 2 - the MIDI scrub

MidiHub gained a `Target::scrub` (and `Target::transport`) binding, plus a
`Target::transport` for play/stop. The scrub CC is read as a **relative
jog** (centre 64 = no motion, scaled to +/- 4x) with exponential smoothing,
decay on the target, and hysteresis at zero so a resting jog does not flap.
The smoothed velocity drives `playRate`; a sign change bumps the direction-
change serial so the crossfade covers the reversal. Note-on starts playback,
note-off stops (or, when a transport binding exists, only that control
toggles play/stop).

The plugin gained a MIDI input bus: `acceptsMidi()` is true and
`processBlock` feeds every host `MidiMessage` through
`MainComponent::injectMidiMessage` into the same handler the stand-alone app
uses, so parameter binding, scrub and transport all work inside a DAW.

### Smoothing constants

`kScrubSmoothOld`, `kScrubSmoothNew`, `kScrubTargetDecay`, `kScrubHysteresis`
are named in `MainComponent.h` with a note that they are first guesses and
need real-controller validation - no controller was available for this pass.

## Open questions (still on the Kanso card, not here)

- plugin mode: scrub vs host transport (an explicit switch). The
  `pluginFollowHost` flag exists and defaults to false (scrub wins); the
  host-transport side is card 249.
- play/stop via a learned transport binding vs. any note: both are
  supported (the binding wins when set, otherwise any note).
- MIDI jitter (timestamp + smooth) and direction-change clicks: addressed
  by the smoothing + the 20 ms crossfade; real-controller validation
  pending.

## Tests

269/269 (the signed-playhead mirror check is test 24). Plugin smoke 15/15,
pluginval strictness 5 SUCCESS. No new selftest checks for the scrub (it
needs a device) - the mechanism is exercised by `injectForTest` in the
existing MIDI tests.

## What is NOT in this wave

- The host transport follow (card 249) - the DAW's transport driving the app.
- Real-controller tuning of the smoothing constants.
- A keyboard shortcut for the Rev toggle.
- Envelope points on the canvas.
