# Wave 5 - one tab strip (v1.8)

**Status:** shipped in v1.8 (`81a3750`, `00b34d3`, `fe7ce67`).
**Kanso:** card 44 (Done).
**Goal:** three materials, three layers, one strip.

## Why

There were two strips: the top one switched between the score and the
live pitch, the bottom one switched the material. Two switches for one
concept is one too many, and the material and the layer could disagree
(the score could show a live arc's material).

## What was decided

- **The tab is the material.** `Osc | Sample | Live`: each tab is a
  material *and* its own canvas layer. There is one entry point
  (`setMaterial`), so the layer, the drawing mode, the editor content
  and the assign/generator material can never disagree.
- **The strips merged.** The old score/live strip and the bottom
  material strip became one strip above the canvas, and the bottom area
  is the voice editor.
- **The Sample tab got its own grid.** The oscillator's pitch is read
  against Hz; a sample is pitch-shifted by a *ratio*, so the Sample tab's
  grid is the speed grid (`Ref x 2^k`) with the ratio labels. The grid
  follows the Ref slider live.
- **The tabs are per-voice choices, not piece state.** They are session
  preferences like the pitch modes - a loaded piece sounds as it always
  did.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The layer and the material could disagree | One entry point (`setMaterial`) makes it impossible |
| The Sample grid would be confusing next to the Hz grid | The two grids are the same lines in the same places, so they can be compared - the Live tab got the same treatment in wave 9 |
| A new tab would need a new canvas layer | The layer is a filter on the arcs (mode), not a separate canvas |

## What was verified

- The tab wiring: each tab sets the layer, the drawing mode, the editor
  content and the material.
- The Sample tab renders offscreen with the speed grid.
- The layout at six sizes with the tab strip present.

## What came out of it

- The live tab needed its own pitch-mode selector (wave 6) and the
  sample tab the same (wave 7).
- The Hz grid and the ratio grid wanted to be comparable on the same
  canvas - that is wave 9.
