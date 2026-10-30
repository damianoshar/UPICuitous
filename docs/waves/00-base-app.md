# Wave 0 - the base app

**Status:** the first commit (`972b998`), never versioned separately.
**Kanso:** card 39 (Done).
**Goal:** Xenakis' UPIC in JUCE - draw pitch curves on a page, give each
arc a wavetable and an envelope, and hear it.

## Why

The UPIC is a drawing instrument: a pen on a tablet draws a curve, the
curve is read as pitch over time, and the result is heard. The clone has
to keep the triptych that makes it work - **Page / Wavetable / Envelope**
- because a single merged sound is a different instrument.

## What was decided

- **The score is a page of arcs.** An arc is a pitch curve over time; the
  page is a timeline (X) against log pitch (Y). Pitch is interpolated in
  the log domain, so a glissando sounds linear in pitch.
- **The voice is a triple.** Every arc points at a wavetable (a drawable
  1024-sample cycle), an envelope (a drawable 0..1 shape over the arc's
  life) and an intensity. The three are edited separately and assigned to
  arcs - that separation is the UPIC idea, not a UI detail.
- **The piece is a file.** `.upic.json` holds the pages, the arcs, the
  wavetables, the envelopes and the material. The Python and the web
  prototypes share the format, so a piece can be moved between them.
- **The engine is polyphonic and per-arc.** Each arc keeps its own
  oscillator phase, so the sound does not restart when another arc
  starts.
- **The sample material was part of the model from the start**, not added
  later: four slots, each a loadable sample that arcs can pitch-shift.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The phase would restart when an arc was re-triggered | It does not - the phase is per arc and free-running |
| The format would tie the three builds together | It did not: the Python and web versions read the same files, and the web sketch stayed a sketch |

## What was verified

- The offline render is finite, audible and bounded (peak in (0.05, 1.0]).
- The pitch interpolation is log-domain (checked against a glissando).
- The JSON round-trips.

## What came out of it

- The composition tools (wave 1): sieves, zones, multi-select, generators.
- The tablet (wave 2): the drawing instrument wants a pen.
- The sample slots stayed unused for a long time - the material that
  plays was the missing half of the instrument.
