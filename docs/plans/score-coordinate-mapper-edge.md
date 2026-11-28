# The score canvas's coordinate mapper edge

The architecture note for R5.2. It describes the boundary R5.2 introduced -
the score canvas's mapping between the musical coordinates and the screen -
as it is after that phase, not as it is meant to be at the end of the
refactoring.

> **R5.4 gave the drawing modes their own controllers.** The mapper is
> unchanged; the tools ask the canvas, and the canvas asks the mapper. See
> [`score-editing-tools-edge.md`](score-editing-tools-edge.md).

## The boundary

```
Source/ScoreCoordinateMapper.h   the mapper edge: the plot's rectangle, the
                                 page's length and ambitus, the view's zoom
                                 and scroll, and the conversions between the
                                 musical coordinates (seconds, Hz) and the
                                 screen coordinates (pixels)
        |
        |  three users, one mapping: the render draws with it, the editor
        v  maps the cursor and the strokes with it, the hit tester will
           use it too
Source/ScoreRenderer.h     the render: the picture
Source/ScoreEditor.h       the editor: the gesture, the selection, the
Source/ScoreEditor.cpp     commands and the view's state
```

The review asks for the mapping to be a thing of its own (R044, "Introduce
ScoreCoordinateMapper") and says what it is for:

> `ScoreCoordinateMapper` should supply consistent mappings between musical
> and screen coordinates. (§24)

and why (§24):

> Rendering, hit testing and coordinate conversion should agree, but they
> should not be implemented as one operation.

R5.1 gave the render its own edge and left the mapping inside the renderer's
header. R5.2 gives the mapping its own header, so that the render, the
editor and the hit tester can all reach it without reaching into each other.

## What moved

| before | after |
|---|---|
| `upic::score_render::View` (`Source/ScoreRenderer.h`) | `upic::score_coordinates::ScoreCoordinateMapper` (`Source/ScoreCoordinateMapper.h`) |
| the five bodies in `Source/ScoreRenderer.cpp` | the same bodies, inline in the header |
| `ScoreEditor::renderView()` | `ScoreEditor::coordinateMapper()` |
| `State::view` (the render's state) | `State::mapper` |

The bodies are the editor's, body for body. The clamping of a cursor
outside the page, the deliberate *non*-clamping of a pitch outside the
ambitus and the sorting of a zone's corners do not change by a character.

## The conversions

| conversion | the rule |
|---|---|
| `xToTime(x)` | a cursor outside the plot is clamped to the page's ends |
| `timeToX(t)` | the time axis is linear in the zoomed window |
| `yToFreq(y)` | the pitch axis is logarithmic, over the page's ambitus |
| `freqToY(f)` | **not** clamped: a pitch outside the ambitus is drawn off the edge, where the plot clips it - clamping it would draw it as a line along the border |
| `zoneRect(z)` | a zone's times and frequencies are order-independent, so the corners are sorted |

## The mapper is a value; the editor owns the view's state

The mapper carries the *numbers* the conversion needs: where the plot is on
the screen, how long the page is, over which ambitus the pitch runs and where
the view is zoomed and scrolled. It converts and it owns nothing.

The view's *state* is the editor's: the wheel's zoom and pan, the fit, and
the page's current length and ambitus. The editor builds a mapper for every
use (`ScoreEditor::coordinateMapper()`), so the mapper always carries the
live numbers.

That is why the editor's public doors (`timeToX()`, `freqToY()`,
`xToTime()`, `yToFreq()`) stay: they are the app's way in, and they now go
through the mapper.

## What stays in the editor

- the gesture: the mouse and the touch handling, the tools, the
  quantisers, the commit of a stroke and a line, the zone's drag;
- the selection: `selArcs`, the marquee, the cycle on a shift-click;
- the model's commands: the arcs, the zones, the deletion, the duplication;
- the wheel's view commands: the zoom, the pan, the fit. They change the
  view's state and then repaint, so they belong with the editor and not with
  the conversion.

## The suite

`Source/SelfTestCoordinateMapper.cpp` is the mapper's own contract: 14
checks that pin what the mapping does.

- where time 0 and the page's end are on the screen;
- the round trips (a time and a pitch survive the trip out and back);
- the ambitus (its low end is the plot's bottom, its high end the plot's
  top);
- the non-clamping of a pitch above and below the ambitus;
- the clamping of a cursor outside the page;
- a zone's rectangle (its times and its frequencies are order-independent);
- the zoom (it changes the time zoom, it keeps the view's centre where it
  was, and a time survives the round trip through it).

The suite reaches the mapping through the editor's own doors
(`timeToX()`, `xToTime()`, `freqToY()`, `yToFreq()`, `zoneRectForTest()`),
so it pins the agreement between the editor and the mapper as well as the
formulas.

The tolerances are the float's precision, not the last bit: the screen
coordinates are floats, so the round trip is exact to the float's
precision.

The suite came first: it was written against the old code and drove the
editor's doors. The move renamed the type and gave it a header of its own,
and the checks themselves did not change.

## What is not covered yet

- **The hit tester has an edge of its own now** (R045):
  `Source/ScoreHitTester.h`. It asks with this mapper, so the picture and
  the grab agree. See
  [`score-hit-tester-edge.md`](score-hit-tester-edge.md).
- **The drawing modes have controllers of their own now** (R046):
  `Source/ScoreEditingTools.h`. They ask with this mapper too, through the
  canvas. See [`score-editing-tools-edge.md`](score-editing-tools-edge.md).
- **The gesture has an explicit transaction now** (R047):
  `Source/ScoreGestureTransaction.h`. The mapper is unchanged; the
  transaction holds the gesture's command. See
  [`score-gesture-transaction-edge.md`](score-gesture-transaction-edge.md).
- **The view's state is still the editor's fields** (`timeZoom`,
  `timeOffset`, `freqZoom`, `freqOffset`). A `ScoreCamera` (the review's
  §24 file list) would own the zoom and the scroll; until then the editor
  holds them and hands them to the mapper.
- **The mapper does not know about the layers.** The sample and the live
  tabs map the vertical axis to ratios, not to absolute pitches, but the
  *grid* that draws the ratios lives in the render. The mapping itself is
  the same in every layer, so nothing is lost - but the review's
  `ScoreViewport` and `ScoreCamera` would make the distinction explicit.
