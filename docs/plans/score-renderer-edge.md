# The score canvas's render edge

The architecture note for R5.1. It describes the boundary R5.1 introduced -
the score canvas's render and the editor's gesture - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/ScoreRenderer.h   the render edge: the View (where the plot is and
Source/ScoreRenderer.cpp how time and pitch map onto it) and the State (the
                         page, the waves, the layers, the flags and the
                         gesture), and the paint that draws them
        |
        |  the editor keeps the gesture, the selection and the commands;
        v  it hands the render a snapshot of what it draws
Source/ScoreEditor.h     the editor: the mouse and the touch handling, the
Source/ScoreEditor.cpp   tools, the quantisers, the commit of a stroke, the
                         selection, the model's commands and the wheel's view
                         commands
```

The score canvas draws the page: the frame, the grid, the sieve zones, the
arcs, the gesture in progress, the scrub line, the labels, the playhead, the
workspace's mark and the hint lines.

The review asks for the render to be a thing of its own (R043, "Separate
ScoreRenderer from ScoreEditor") and for the rendering, the hit testing and
the coordinate conversion to agree without being one operation (§24):

> Rendering, hit testing and coordinate conversion should agree, but they
> should not be implemented as one operation.

R5.1 extracts the render. The editor's gesture stays, and the two share one
mapping - the `View` - so they cannot drift apart.

## What moved

| before | after |
|---|---|
| `ScoreEditor::paint()` (380 lines) | `upic::score_render::paint (Graphics&, const State&)` |
| the frame, the grid, the zones, the arcs, the gesture, the labels, the playhead, the hints | the same, in `Source/ScoreRenderer.cpp` |
| `ScoreEditor::xToTime()` / `timeToX()` / `yToFreq()` / `freqToY()` | `upic::score_render::View::xToTime()` / `timeToX()` / `yToFreq()` / `freqToY()` - the editor's methods delegate to them |
| `ScoreEditor::zoneRect()` | `upic::score_render::View::zoneRect()` - the editor's method delegates to it |
| `ScoreEditor::niceStep()` | a static function in the renderer (the grid's step) |
| `ScoreEditor::StrokePt` | `upic::score_render::StrokePoint` (the editor's stroke is the renderer's type) |

The bodies are the editor's, body for body. The colours, the order of the
passes and the wording of the hint lines do not change by a character.

## The order of the passes

The picture is built in a fixed order, and the order is part of the
contract:

1. the background, then the plot's frame;
2. the sieve zones (translucent, under the grid);
3. the grid (the time lines, the pitch lines, the sieve's pitch guides and
   the zone's time grid);
4. the arcs (the stroke, the dots and the tags);
5. the gesture in progress (the stroke, the line's rubber band, the zone's
   rubber rectangle, the selection marquee);
6. the scrub line;
7. the labels and the tags, each on a dark plate, so they stay readable;
8. the playhead (the line and the top triangle), over the labels;
9. the border, the workspace's mark and the hint lines.

## The view

The `View` is a small value: the plot's rectangle, the page's length and
ambitus, and the editor's zoom and scroll. It carries the mapping between
the page's time and pitch and the screen:

- `xToTime()` / `timeToX()` - the time axis;
- `yToFreq()` / `freqToY()` - the pitch axis (logarithmic, over the page's
  ambitus; `freqToY()` is deliberately not clamped, so a pitch outside the
  ambitus is drawn off the edge instead of along the border);
- `zoneRect()` - a zone's rectangle (the times and the frequencies are
  order-independent, so the corners are sorted).

The editor builds one of these for every use of the mapping
(`ScoreEditor::renderView()`), so the view always carries the live plot
rectangle, the page's current length and ambitus and the editor's current
zoom and scroll. The drawing and the hit testing therefore use the same
numbers.

The review gives the mapping a name of its own in R044 ("Introduce
ScoreCoordinateMapper"). Until then it lives next to the render that needs
it, and R044 moves it into a header of its own.

## What stays in the editor

- the gesture: the mouse and the touch handling, the tools, the
  quantisers, the commit of a stroke and a line, the zone's drag;
- the selection: `selArcs`, the marquee, the cycle on a shift-click;
- the model's commands: the arcs, the zones, the deletion, the duplication;
- the wheel's view commands: the zoom, the pan, the fit;
- the public mapping (`timeToX()`, `freqToY()`, …), which now delegates to
  the `View`.

They need the editor's state, the model's lock and the app's hooks. They
are not the picture.

## The suite

`Source/SelfTestScoreRender.cpp` is the render's own contract: 18 checks
that pin what the canvas draws.

- the background outside the plot, the frame inside it and the border;
- the time grid, the A440 line, a C line, and the grid switched off;
- the sample layer's Ref line;
- the arc's dot, the layer rule (an osc arc is not drawn in the sample
  layer) and the selected arc's brighter dot;
- a zone's translucent fill;
- the playhead, and no playhead at zero;
- the workspace's mark;
- the hints (on and off change the image);
- the render is deterministic, and painting mutates neither the model nor
  the view.

The suite paints off-screen into a `juce::Image`, so no window, no device
and no message loop are needed. It reads single pixels at positions the
editor's *own* public mapping gives them, so it pins the agreement between
the drawing and the mapping as well as the colours.

The suite came first: it was written against the old code and drove the
canvas through `ScoreEditor::paint()`. The move turned the paint into the
edge's, and the checks themselves did not change.

## What is not covered yet

- **The hit testing is still in the editor** (`arcsNear()`, `pickArc()`,
  `pickZone()`, `zoneHandleAt()`). The review's R045 ("Extract
  ScoreHitTester") is the next step, and the `View` is what the two will
  share.
- **The coordinate mapper has no header of its own yet.** It is the
  `View` in `Source/ScoreRenderer.h`; the review's R044 gives it a name and
  a home.
- **The grid and the arcs are not separate painters yet.** The review's
  §24 lists `ScoreGridRenderer` and `ArcPainter`; R5.1 extracts the render
  as a whole, and the sub-painters can be split out of it later without
  touching the editor.
- **The render's picture is pinned by pixels, not by a digest.** A digest
  would be stronger, but it would depend on the platform's font rendering;
  the probes pin the geometry and the colours instead.
