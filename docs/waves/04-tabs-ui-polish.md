# Wave 4 - the tabs and the UI polish (v1.6 / v1.7)

**Status:** shipped in v1.6 and v1.7 (`5731ea6`, `7018a3f`, `083ae9f`,
`1e1aac8`, `98bba31`, `952373b`).
**Kanso:** card 43 (Done).
**Goal:** give the live input its own tab, and make the canvas readable.

## Why

The live material and the oscillator material were mixed on one canvas.
Two arcs of different materials in the same place is confusing: which
wavetable does this arc use? Does this arc follow the mic?

## What was decided

- **The Live pitch tab** shows the same page and the same timeline as
  the score, but only the mode-2 arcs, with a grid labelled in Hz
  (the score's grid was already Hz).
- **The layer visibility is strict and two-way.** The score shows only
  the oscillator arcs, the Live tab only the live arcs. An arc belongs to
  exactly one layer, so there is never a "hidden arc" that is still
  sounding.
- **The canvas texts sit above the shapes.** Labels, arc tags and the
  time strip are drawn on dark plates, so they stay readable over the
  arcs. The instruction hints are right-aligned and bold.
- **The view toggles** (Wave / Env / Hints) shrink the bottom area: one
  editor hidden gives the other the full width, both hidden leaves a
  slim voice strip, and Hints removes the instruction lines. They are
  session-only - the piece format does not change.
- **The audio settings are an in-app overlay**, not a `DialogWindow`:
  on Android a JUCE dialog has no title bar and no way to close.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| Switching to the Live tab would open the microphone | It did - and that turned out to be the v1.17 problem (wave 14) |
| Hiding a view would break the layout | The layout check runs at six sizes with the views hidden as well |
| The labels would be unreadable over the arcs | The dark plates fixed it |

## What was verified

- The layer separation: the score shows only its arcs, the Live tab only
  the live ones.
- The layout at six window sizes, with and without the views.
- The view toggles actually hide the right things.

## What came out of it

- Two tabs (score, live) became three materials (wave 5) - the tab
  became the voice.
- The mic-on-tab-switch cost 4.7 s per click (wave 14).
- The canvas got busier, which is what the tab strip (wave 5) solved.
