# Wave 1 - the composition tools (v1.2)

**Status:** shipped in v1.2 (`d27f250`).
**Kanso:** card 40 (Done).
**Goal:** make the score editable like a score, not a drawing - snap to a
grid, select many arcs, and generate arcs instead of drawing them.

## Why

Drawing a pitch curve with a mouse is slow and imprecise. UPIC (and its
descendants like UPISketch) solved this with *magnetic* quantization: the
drawing snaps to a musical grid, and the grid can change per region of the
page.

## What was decided

- **Sieves are the quantizer.** The Xenakis rule `n mod m` (nine
  remainders) is the default: pitch is quantized to a set of m values
  within an octave. The sieve is a *magnitude* rule, not a grid - that is
  why the UI calls it a sieve and not a quantizer.
- **Zones make the sieve local.** A zone is a region of the page with its
  own sieve, its own grid and its own time grid. Outside every zone the
  global rules apply. A zone is drawn like an arc and is part of the
  saved piece.
- **Stickiness instead of hard snapping.** A slider (0..1) blends
  between "free" and "hard" snap, so the drawing can be exact or loose
  without switching modes.
- **Selection is multi-select.** Shift-click cycles the selection, Ctrl+A
  selects all, Ctrl+D duplicates, the arrow keys nudge, Del deletes.
- **Generators instead of freehand.** Four nUPIC-style generators
  (stochastic, convergent arborescence, echelon construction, ordinal
  spine) stamp arcs with the current voice, so a texture can be produced
  and then edited.
- **The stop is click-free.** The output ramps to silence over 30 ms
  instead of being cut (the "playback now fades out smoothly" behaviour
  of UPISketch).

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The zone rules would fight the global rules | A single precedence rule decides it (zone first, global outside), and it is in the selftest |
| A hard snap would make drawing impossible | Stickiness (0..1) and the per-zone grid fixed it |
| The generators would produce unusable arcs | They stamp the current voice, so the result is immediately editable |

## What was verified

- The sieve quantizer on the MIDI-note axis (nine remainders).
- The zone rules and their precedence over the global ones.
- The zone JSON round-trip.
- The preset library (the UPISketch examples).
- The click-free stop ramp.

## What came out of it

- The tablet (wave 2) inherited the tools - they were the reason the
  instrument needed a pen.
- The grid became per-voice later (wave 5: the Sample tab's speed grid).
- The generators were still bound to one voice each; the tab strip
  (wave 5) made the voice a tab-level choice.
