# Wave 9 - the live ratio grid (v1.12)

**Status:** shipped in v1.12.
**Kanso:** card 239 (Done).
**Goal:** let the Live tab be read the way the Sample tab is read.

## Why

The Sample tab draws against a *ratio* (`Ref x 2^k`) because a sample is
pitch-shifted by a ratio. The Live tab drew against Hz, even though it
uses the same ratio formula. The same gesture therefore meant two
different things depending on the tab, and the two readings could not be
compared.

## What was decided

- **The Live tab can draw the Sample tab's grid**: the strong lines at
  `Ref x 2^k`, the faint half-octaves, the ratio labels
  (`0.125x ... 1x (Ref) ... 32x`) and the Ref line in orange - the same
  lines in the same places, so the two readings are comparable on one
  canvas.
- **It is a toggle, and the default stays Hz.** The `Ratio grid` button
  appears next to the Live tab (only while the Live tab is active; it
  shrinks or hides on a narrow strip). Nothing changes until the user
  opts in.
- **It is a view preference**, session-only, like the live pitch mode -
  it is not part of the saved piece. A piece must sound and look the
  same on another machine.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The button would break the tab strip's layout | The layout check runs at six sizes, with the button present and hidden |
| Changing the grid would change the piece | It does not - the grid is not saved |
| The Hz grid would be lost | It stays the default; the toggle is explicit |

## What was verified

- The Live tab defaults to the Hz grid.
- The toggle switches the grid on and off (and the value is not saved).
- The button is laid out in the strip.
- The Live tab renders offscreen with the ratio grid.

## What came out of it

- The Hz grid stayed available for comparison - the user asked for that
  specifically, so the ratio grid is opt-in.
- The grids made the *range* difference obvious: the Live tab clamped
  at 3x while the Sample tab reached 16x. That is wave 10.
