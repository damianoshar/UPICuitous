# Wave 21 - envelope points (+ / -)

**Status:** shipped in v1.24.0.
**Kanso:** the done card for the wave (no card existed; this was a direct
refinement request).
**Goal:** make an arc's envelope shape editable beyond the five presets,
without a mouse and without hunting for gestures: two buttons, a selected
point, and a visible place where the next point will land.

## Why

The envelope editor could already add points (double-click) and delete
them (right-click) - but none of that was documented anywhere, and both
gestures are mouse gestures: on a tablet there is no right-click, and a
double-tap on a small curve is a lottery. So in practice an envelope had
the four or five points of its preset, and a long arc (20-30 s) could not
be shaped at all: the curve between two points is one straight line.

The five presets are still the starting points. The buttons are how you
take one further.

## What it looks like

The editor's header is now:

```
ENVELOPE   5 pts                        [-] [+]
+-----------------------------------------------+
|        /\                                     |
|       /  \                                    |
|      /    \____o_____                         |
+-----------------------------------------------+
```

(o = the selected point; the white ring is what - removes)

- **+** inserts a point and selects it. It sits **on the current curve**,
  so the shape does not jump; drag it to shape the segment.
- **-** removes the selected point (the white ring). Two points must
  remain - a curve needs its ends.
- The header shows the point count, so a customised envelope is obvious
  even though its name still says "Flat".

## The decisions

### 1. Tap the curve, then press +

Where the + inserts, in order:

1. **the spot last tapped on the curve** - a hollow ring with a tick
   shows it; the point lands at that x, on the curve;
2. **after the selected point** - halfway to the next one (for the last
   point, halfway to the previous one);
3. **the widest gap** - when nothing is selected and nothing was tapped,
   so pressing + repeatedly densifies evenly.

The marker is why the button does not need a mode: the tap is the
placement, the button is the commit. It is also the touch story - tap,
press, drag.

### 2. The ends are free, two points must remain

The first and the last point are pinned to x=0 and x=1 *while they sit
there*, but either can be deleted. A curve that starts at x=0.25 holds
its first value from 0 to 0.25 - which is exactly a late onset - and one
that ends early holds its last value to the end. That fell out of
`Envelope::valueAt` (it clamps outside the point range), so no engine
change was needed.

The pinning rule was relaxed to match: a point is pinned only when it
really is at 0 or 1, otherwise the new end can be dragged like any other
point. Without that, deleting the first point and then touching anything
would silently snap the new first point to x=0 and change the attack.

### 3. The cap stays at 32

32 points is more than one per second on a 30 s arc, and `valueAt` does a
linear scan **per sample per arc**, so the cap is what keeps the
worst-case cost bounded. The + button disables itself at the cap.

### 4. The old gestures stay

Double-click still adds at the cursor and right-click still deletes the
nearest point (both now also update the selection). They are simply no
longer the only way in, and the header no longer spends its width on a
hint.

### 5. Session-only editing, no new model fields

The points already live in `Envelope` and are already saved in the
`.upic.json` format (`points: [{x, y}, ...]`, any count). Adding points
is therefore a pure editor change: no format change, no migration, and
the extra points survive a save/load (the selftest proves it).

## Tests

16 new checks (252 -> 268):

- the editor is present and a loaded 4-point envelope keeps its points;
- + on a selected attack point inserts at the segment midpoint, on the
  curve (0.5, 0.6 for the test shape), and selects it;
- the extra points survive a `pieceToVar` / `loadFromVar` round trip;
- - removes the selected point and clears the selection; it refuses with
  no selection and refuses below two points; an end point can be removed;
- a curve without a point at x=0 holds its first value (late onset);
- tap-to-place: `placePointAt(0.9)` then + lands at x=0.9 on the curve;
- the cap: 40 presses stop at 32 and + disables;
- the buttons are in the header, inside the editor's bounds, and -
  follows the selection.

Gate: app 268/268, plugin smoke 15/15, pluginval strictness 5 SUCCESS.

## What came out of it

- The envelope is no longer limited to its preset: long arcs get as many
  shape points as they need, on touch as well as with a mouse.
- The editor's API (`selectPoint`, `placePointAt`, `addPoint`,
  `removePoint`, `pointCount`, `pointAt`) is what the buttons call - the
  tests exercise the same path.
- The header is now the editor's toolbar; the count doubles as the
  "this is customised" cue. A future wave could rename the envelope to
  "Custom" on the first edit if the combo list needs it - not done,
  because it would churn the saved names.
- The canvas still draws the envelope's effect only through the sound;
  showing the points on the arc is not part of this wave.
