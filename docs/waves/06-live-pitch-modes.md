# Wave 6 - the live pitch modes (v1.9)

**Status:** shipped in v1.9 (`ca6bd6c`).
**Kanso:** card 89 (Done).
**Goal:** let the user choose how the live input is pitch-shifted.

## Why

The live path had one algorithm (the granular shifter). It is the most
robust one, but it is not the most musical one: a tape-like shifter
keeps the formants, a phase vocoder keeps the spectrum. The toolkit
review asked for the choice.

## What was decided

- **Three shifters behind one interface.** Granular (the v1.5 default),
  Tape and Phase vocoder, all in `LivePitch.h`, all behind a per-voice
  `LiveShifter` so the engine does not know which one is in use.
- **The default stays granular.** A piece that was made with the
  granular shifter must sound as it did - the selector is a session
  preference, not part of the piece.
- **A mode change drops the live voices**, so each re-anchors on its next
  contact instead of dragging the old state along. The engine notices
  the change itself (an atomic compared at the top of the render), so
  the message thread never touches the shifter's state.
- **The selector lives in the audio settings overlay**, next to the
  monitor and capture controls - the place where the live material is
  configured.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The three shifters would sound different in pitch | The selftest measures each shifter's output frequency - they agree |
| A mode change would click | The voices are dropped and re-anchor on the next contact |
| The tape shifter's window would be too long | 40-200 ms, which is what the latency report (wave 13) publishes for it |

## What was verified

- The frequency of each shifter (granular, tape, vocoder) at the
  engine level.
- The selector round-trip: the box, the engine's mode and the value that
  reaches the shifter.
- Selftest 141 -> 153 checks.

## What came out of it

- The sample path wanted the same three algorithms (wave 7), which
  forced the shifters to be source-agnostic (`GrainCore`, `VocoderCore`
  plus thin wrappers).
- The tape shifter reads the mic faster than the mic arrives above
  ~2x - it kept an octave up/down when the live range widened (wave 10).
