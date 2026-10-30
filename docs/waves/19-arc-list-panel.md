# Wave 19 - the arc list panel

**Status:** shipped in v1.22.0 (phases A, B, C and D).
**Kanso:** card 275 (Next up).
**Goal:** a list of the arcs, on the right of the display, that shows what
each arc is and lets you change it.

## Why

The score is a page of arcs, and the app shows them as shapes. What it
does **not** show is the *inventory*: how many arcs there are, which page
each is on, what pitch range and time span it covers, and which material
it uses. When a piece grows - a generator run, a dozen captured takes -
that inventory is invisible, and the only way to change one arc's
material is to select it on the canvas and use the voice row (which
changes the *default for the next arc*).

The panel makes the contents legible and turns the per-arc settings into
per-arc settings.

## What it looks like

A side column on the right of the display (like the voice editors, but
vertical and narrow), with a **toggle button** next to the other view
toggles (Wave / Env / Hints) - it is a *view*, so its visibility is
session state, not piece state.

The list is **grouped by type**, in the order the tabs use:

```
OSC      3 arcs
  #7  220 - 880 Hz   0.0 - 12.5 s   p1
  #8  330 - 330 Hz   3.0 - 9.0 s    p1
SAMPLE   2 arcs
  #9  110 - 440 Hz   0.0 - 4.0 s    p1   [Rev]  [Varispeed | Granular | Vocoder]
LIVE     1 arc
  #10 220 - 220 Hz   0.0 - 30.0 s   p2   [Rev]  [Granular | Tape | Vocoder]
```

Per row:

| Field | Where it comes from |
|---|---|
| the arc id | `Arc::id` |
| the pitch range | the min and the max of the arc's points (log pitch -> Hz) |
| the time span | `Arc::startTime()` .. `Arc::endTime()` |
| the page | the page the arc is on |
| **Rev** (checkbox) | the arc's material played backwards (the sample and the live arcs) |
| **mode** (dropdown) | the arc's pitch-shift algorithm: Granular / Tape / Phase vocoder (live), Varispeed / Granular / Phase vocoder (sample) |

Clicking a row **selects the arc** on the canvas (the same selection the
ScoreEditor already has), so the two views stay in sync.

## The decisions that have to be made

### 1. The mode and the reverse become per-arc

Today they are **session** state: the Sample tab's Pitch combo and the
Live tab's Pitch combo set one mode for *all* the arcs of that kind, and
the Rev button sets the direction for the whole slot.

The panel wants them **per arc** - and that is the expensive part, the
thing `FUTURE_IDEAS.md` warns about ("the data model is the expensive
part"):

* the arc's JSON gains two optional fields (`"pitchMode"`, `"reversed"`);
  a piece without them keeps today's behaviour (the default), so old
  files, the Python prototype and the web sketch still work;
* the *voice row* (the controls that apply to the next arc you draw)
  becomes the **default** for new arcs - the drawing workflow is
  unchanged ("drawing an arc should have the features' options as is
  currently");
* the engine has to read the mode and the reverse **per arc** instead of
  per kind. The live path's mode is a global atomic today; it would
  become per arc, so the shifter has to be chosen per voice (the shifters
  are already per voice - `liveShift[arc.id]` - so this is mostly
  bookkeeping);
* the sample reverse is already per *slot*; per *arc* means the arc
  decides, and the slot keeps its own default.

**What was actually done (v1.22.0):**

* the arc carries `pitchMode` (-1 = follow the tab) and `reversed`, and
  the **piece format is untouched** - the fields are session state, like
  the pitch modes, the grids and the slot's reverse. So the "expensive
  part" did not happen: no migration, no format change, the old files and
  the prototypes keep working, and the per-arc settings are lost on a
  reload (like the other performance settings).
* the engine reads them per arc: the sample path takes the arc's
  `pitchMode` (and re-anchors the voice when the algorithm changes -
  the grain taps and the vocoder's overlap-add belong to the old one),
  and the live path takes the arc's mode and its `reversed` (the live
  arcs read the mirrored ring instead of the forward one - the engine
  gets both).
* the sample's direction stays the **material's** (the slot's Rev button
  and the panel's Rev box for the sample rows show and edit the slot's
  flag); the live's direction is the **arc's** - the microphone is
  shared, so the direction belongs to the arc that uses it. The global
  live Rev button is a bulk edit over the live arcs (the v1.20 effect
  stays: everything is reversed) and the new arcs inherit it.

### 2. What the panel does NOT do

* it does not edit the arcs' shapes (the canvas is that);
* it does not change the piece format for anything else;
* it does not replace the status line.

### 3. Performance

A page can hold hundreds of arcs. The panel rebuilds its rows when the
model changes - which, with the 30 Hz snapshot timer, would be 30 times a
second -. It must rebuild **on demand**: when the panel is open, on a
model change, but throttled (and the model-change signal has to come
from the *edit* path, not from the timer).

## The phases

| Phase | What | Size | State |
|---|---|---|---|
| A | the panel: a side column, the toggle button, the grouped list with the position info (read-only) | M | done (v1.22) |
| B | the selection sync: clicking a row selects the arc on the canvas, and the canvas selection scrolls the list | S | done (v1.22) |
| C | the per-arc Rev checkbox (the material direction) | M | done (v1.22) |
| D | the per-arc mode dropdown (the algorithm) | M | done (v1.22) |

Phase A is the visible win and it needs no format change: the list is
read-only, so nothing can break. Phases C and D are the ones that touch
the model - and they turned out **not** to need a format change, because
the per-arc settings are session state (like the pitch modes and the
grids).

## What was built (v1.22.0)

* `Source/ArcListPanel.h` (new): the panel draws the rows by hand - each
  row carries its own controls, and a variable number of child components
  would be awkward. The Rev box is a small square on the right, the mode
  a box next to it that opens a popup menu.
* The panel is a **view**: the `Arcs` toggle sits with the Wave / Env /
  Hints toggles, and its visibility is session state.
* The rows are rebuilt **on demand** (a dirty flag set by the model's
  changes), not on every 30 Hz timer tick.
* Clicking a row selects the arc on the canvas - and the tab follows the
  arc's material (an arc can only be selected when it is in the current
  layer). The canvas's selection is followed back into the panel.
* The per-arc overrides reach the engine (the selftest proves the
  direction flips: the output's slope goes from +0.19 to -0.11).

## The risks

| Risk | Mitigation |
|---|---|
| The per-arc mode/reverse would break the old files | They are session state: the piece format is untouched, so nothing can break |
| The Python and the web prototypes would lose the fields | They never had them - the format is unchanged |
| A big page would make the panel slow | The rows are built on demand (a dirty flag), not on the 30 Hz tick |
| The panel and the canvas would disagree about the selection | One selection model, both views read it |
| The voice row and the panel would fight | The voice row is the default for *new* arcs; the panel edits *existing* ones |

## The test plan

* the panel lists every arc with its pitch range, its time span and its
  page, grouped by type;
* clicking a row selects the arc on the canvas;
* a per-arc Rev flips the material of that arc only (the rendered slope
  flips sign);
* a per-arc mode changes that arc only;
* the piece format is unchanged (the per-arc settings are session state);
* the layout still tiles at the six window sizes (the panel takes the
  right column, and never so much that the canvas gets too narrow).

## The priority plan (all the planned features)

Ordered by "what unlocks the most, for the least":

| # | Feature | Size | Depends on | Why here |
|---|---|---|---|---|
| 1 | **The arc list panel** (this wave) | M (A: M, B: S, C: M, D: M) | - | Makes the piece's contents legible; the per-arc controls then have a home |
| 2 | **MIDI input foundation** (UPIC-24) | S | - | **Done in wave 20 (v1.23)** - the controller for everything below |
| 3 | **The signed playhead** (UPIC-28) | M | 1, 2 | The transport that runs the score backwards - a mirrored stroke sounds mirrored | - **Done in wave 22** (step 1 + step 2 + plugin MIDI bus)
| 4 | **The host transport follow** (UPIC-29) | M | - | The DAW's transport drives the app; the score is already a function of time |
| 5 | **The 2-op PM proof** (UPIC-26) | M | - | A new voice: the arc's pitch is the carrier, the ratio is the modulator |
| 6 | **The per-slot Ref + richer sample editing** | M | - | The Sample tab's material work (backlog) |
| 7 | **Scala import** | M | - | The tuning file format (backlog) |
| 8 | The full FM engine (UPIC-30), the OSC wire (UPIC-31), the musical time (UPIC-32) | L | 5 | The research-sized ideas; only after the 2-op proof |

The Future Ideas columns (S / M / L) stay flagged "do not implement yet"
until an item is pulled forward on purpose - which is what this plan
does, one at a time.
