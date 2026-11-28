# The score canvas's hit tester edge

The architecture note for R5.3. It describes the boundary R5.3 introduced -
the score canvas's answer to "what is under the cursor?" - as it is after
that phase, not as it is meant to be at the end of the refactoring.

> **R5.4 gave the drawing modes their own controllers.** The hit tester is
> unchanged; the tools ask it the same questions, from the tools. See
> [`score-editing-tools-edge.md`](score-editing-tools-edge.md).
>
> **R5.5 gave the gesture an explicit transaction.** The hit tester is
> unchanged; the transaction holds the gesture's command. See
> [`score-gesture-transaction-edge.md`](score-gesture-transaction-edge.md).

## The boundary

```
Source/ScoreHitTester.h   the hit tester edge: the mapper (where the plot is
                          and how the page's time and pitch map onto the
                          screen), the page (the arcs and the zones), the
                          layer, the touch's grab band - and the answers:
                          the arcs near a point, the nearest arc, the zone
                          under a point, the zone's handle under a point
        |
        |  the hit tester answers; the editor acts on the answer
        v
Source/ScoreEditor.h      the editor: the gesture, the selection, the
Source/ScoreEditor.cpp    marquee, the zone's drag, the erase, the tools and
                          the model's commands
```

The review asks for the hit testing to be a thing of its own (R045, "Extract
ScoreHitTester") and says what it is for:

> `ScoreHitTester` should identify arcs, points or handles under a given
> coordinate. (§24)

R5.1 gave the render its own edge, R5.2 gave the coordinate mapping its own
edge, and R5.3 gives the hit testing its own. The three now share the
mapper, so the picture and the grab agree about where a time and a pitch
are - the review's requirement:

> Rendering, hit testing and coordinate conversion should agree, but they
> should not be implemented as one operation. (§24)

## What moved

| before | after |
|---|---|
| `ScoreEditor::arcsNear()` | `upic::score_hit_test::ScoreHitTester::arcsNear()` - the editor's method delegates to it |
| `ScoreEditor::pickArc()` | `ScoreHitTester::pickArc()` |
| `ScoreEditor::pickZone()` | `ScoreHitTester::pickZone()` |
| `ScoreEditor::zoneHandleAt()` | `ScoreHitTester::zoneHandleAt()` |

The bodies are the editor's, body for body. The 12-pixel grab band (20 for
a touch), the nearest-first ordering, the layer rule, the 4-pixel slack on
a zone's border, the topmost zone first and the eight handles do not change
by a character.

## The answers

| question | the answer |
|---|---|
| `arcsNear(pos, maxD)` | every arc of this layer whose polyline passes within `maxD` pixels of `pos`, nearest first. The band is 12 pixels for the mouse and 20 for a touch; it can be given. An arc's *points* and the *segments* between them are both hit, so the arc is grabbed along its line and not only at its points |
| `pickArc(pos)` | the nearest of those arcs, or -1 |
| `pickZone(pos)` | the zone under the point, or -1. The topmost (last drawn) wins. A few pixels of slack let the border itself be grabbed (the exact edge can round to just outside the time/frequency rectangle) |
| `zoneHandleAt(z, pos)` | which edge or corner of the zone is under the point: 0 = none (the whole zone moves), 1 = left, 2 = right, 3 = top, 4 = bottom, 5..8 = the corners (top-left, top-right, bottom-left, bottom-right) |

## The hit tester answers; the editor acts

The hit tester is a value: the editor builds one for every use
(`ScoreEditor::hitTester()`), handing it the mapper, the page, the layer and
the touch's grab band. It reads them and writes nothing.

The editor's private `arcsNear()`, `pickArc()`, `pickZone()` and
`zoneHandleAt()` stay as its way in, and the gesture stays with it:

- the selection: a click with the Select tool selects the arc under the
  cursor, a shift-click adds, a ctrl-click toggles, and a click on empty
  space starts the marquee;
- the marquee: the drag that selects a rectangle of arcs;
- the zone's drag: the zone under the pointer is moved, or resized by the
  edge or corner under the pointer;
- the erase: the arc under the cursor is deleted;
- the tools: the Draw, Line, Sine, Erase and Zone tools ask the hit tester
  what is under the cursor before they act.

The hit tester answers; the editor decides what to do about it.

## The suite

`Source/SelfTestScoreHitTest.cpp` is the hit tester's own contract: 34
checks that pin what the canvas answers.

- the arcs near a point, nearest first, and the nearest arc alone;
- the grab band: inside it, outside it, and given;
- an arc hit along its line, not only at its points, and a point past the
  end of the line misses it;
- the layer rule: the osc arc in the osc layer, the sample arc in the
  sample layer, and neither in the live layer;
- the zone under a point, none outside, the border's slack and no slack
  beyond it;
- the topmost zone, and the one below it once the top one is gone;
- the eight handles and none;
- the end-to-end click: the Select tool selects the arc under the cursor and
  clears the selection on empty space, the Zone tool selects the zone under
  the cursor.

The suite reaches the hit testing through the editor's own doors
(`arcsNearForTest()`, `pickArcForTest()`, `pickZoneForTest()`,
`zoneHandleAtForTest()`), and the last group drives the editor's real mouse
handling with a synthesised click - the way the app's own UI tests do it.

The suite came first: it was written against the old code and drove the
editor's private methods through those doors. The move gave the hit testing
a header of its own, and the checks themselves did not change.

## What is not covered yet

- **The point-level hit testing is not there yet.** The review's sentence
  says "arcs, points or handles"; the canvas has arcs and handles (the
  zone's edges and corners), but no tool edits a single point of an arc, so
  there is nothing to identify yet. The review's `PointEditingTool`
  (R046's family) brings that question with it.
- **The gesture's state is still in the canvas.** R5.4 gave each drawing mode
  its own controller (`Source/ScoreEditingTools.h`): the tools ask the hit
  tester the same questions, from the tools. R5.5 gave the gesture an
  explicit transaction (`Source/ScoreGestureTransaction.h`), which holds the
  gesture's command. What is still the canvas's is the gesture's *state* (the
  render borrows it) and the commands themselves.
- **The selection's rules are still in the canvas' controller** - the
  shift-click's cycle, the ctrl-click's toggle and the marquee. They act on
  the hit tester's answer, but they are the selection tool's rules, not the
  hit tester's.
