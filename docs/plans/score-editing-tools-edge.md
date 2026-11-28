# The score canvas's editing tools edge

The architecture note for R5.4. It describes the boundary R5.4 introduced -
the score canvas's drawing modes as explicit controllers - as it is after
that phase, not as it is meant to be at the end of the refactoring.

> **R5.5 gave the gesture an explicit transaction.** The tools below still
> act on the canvas's gesture state, but the transaction they open and close
> has a lifecycle of its own, with a third exit: a cancelled gesture. See
> [`score-gesture-transaction-edge.md`](score-gesture-transaction-edge.md).

## The boundary

```
Source/ScoreEditingTool.h    the tool interface: begin(), update(), end(),
                             cancel() - and the pointer's input (the
                             coordinates, the pressure, the modifiers, the
                             pointer's identity, where the gesture began)
        |
        |  the canvas asks the active tool; the tool acts on the canvas
        v
Source/ScoreEditingTools.h   the tools: the freehand stroke (Draw and
                             SineStroke), the straight line, the selection
                             and the move, the erase, the zone
        |
        |  the tools act on the gesture state and call the canvas's commands
        v
Source/ScoreEditor.h         the canvas: the gesture state (the render
Source/ScoreEditor.cpp       borrows it), the commands (the model) and the
                             canvas's own targets (the playhead's handle, the
                             centre band's scrub target, the pinch)
```

The review asks for the editing tools to be explicit (R046, "Introduce
editing tools", §24):

> "Rather than allowing mouse handlers to accumulate conditions for every
> drawing mode, create an explicit tool controller." (§24)

and sketches the controller it has in mind:

> ```cpp
> class EditingTool
> {
> public:
>     virtual ~EditingTool() = default;
>
>     virtual void begin(const PointerInput&) = 0;
>     virtual void update(const PointerInput&) = 0;
>     virtual void end(const PointerInput&) = 0;
>     virtual void cancel() = 0;
> };
> ```

> "The pointer input should include coordinates, pressure, modifiers,
> pointer identity and relevant gesture information." (§24)

R5.1 gave the render its own edge, R5.2 the coordinate mapping, R5.3 the
hit testing, and R5.4 the drawing modes.

## What moved

| before | after |
|---|---|
| `ScoreEditor::mouseDown()`'s branch per drawing mode | `FreehandTool::begin()`, `LineTool::begin()`, `SelectTool::begin()`, `EraseTool::begin()`, `ZoneTool::begin()` |
| `ScoreEditor::mouseDrag()`'s branch per drawing mode | the same tools' `update()` |
| `ScoreEditor::mouseUp()`'s branch per drawing mode | the same tools' `end()` |
| the canvas's reset of the gesture state | the same tools' `cancel()` |

The bodies are the editor's, body for body. The stroke's resampling, the
line's anchor and its rubber band, the selection's click rules (the
ctrl-click's toggle, the shift-click's add), the marquee's rectangle, the
move's ratio, the erase's pick and delete, the zone's draw and drag - none
of them changes by a character.

## The tools

| tool | what a press means | what a drag means | what a release means |
|---|---|---|---|
| the freehand stroke (Draw, SineStroke) | the stroke starts; the undo transaction "Draw" opens | the pointer's path is sampled (a point every 3 pixels) | the stroke is committed (one arc), and the transaction closes |
| the line | the anchor is set; the transaction "Draw a line" opens | the rubber band follows the pointer | the line is committed (one arc, subdivided along the drag), and the transaction closes |
| the selection and the move | the arc under the cursor is taken (a ctrl-click toggles it, a shift-click adds it); a press on empty space starts the marquee; a press on a selected arc starts the move (the transaction "Move the arc") | the marquee takes the arcs inside it, or the selected arcs follow the pointer | the marquee ends, or the move is published and the transaction closes |
| the erase | the arc under the cursor is deleted (the transaction "Delete") | nothing | nothing |
| the zone | an existing zone is taken (and can be moved or resized), or a new zone is started | the zone's rubber band follows the pointer, or the zone is dragged | the new zone is added (the transaction "Add a sieve zone"), or the drag is published and the transaction closes |

The right button is a tool's business: the Zone tool opens the zone's menu,
every other tool opens the arc's.

## The canvas keeps what is the canvas's

Not every press is a drawing mode. These belong to the canvas, whatever
tool is active, and they stay in the mouse handlers:

- the multi-touch (the first finger behaves like the mouse) and the pinch
  (the second finger takes over the view gesture);
- the playhead's handle and the centre band's scrub target - a press there
  scrubs, and it draws nothing;
- the plot's bounds (a press outside the plot is not a gesture).

The tools never see a JUCE mouse event: the canvas turns the event into a
`PointerInput` (the coordinates, the pressure, the modifiers, the pointer's
identity and where the gesture began) and hands that to the tool.

## The gesture belongs to the tool that began it

Before, every event asked "which tool is active?", so a tool change in the
middle of a drag changed who finished the gesture: a line drawn with the
Line tool was dropped when another tool was picked in between, while a
stroke was committed. Now the canvas remembers the tool that took the
gesture (`gestureTool`) and the drag, the release and the cancel go to it.

This is the review's model made explicit. It cannot be reached by hand - a
tool change needs a toolbar click, which needs the pointer to be released
first - and it is the only behavioural difference the phase brings.

## The tools act; the canvas owns

The tools are the canvas's own controllers, so they act on the canvas's
gesture state and call the canvas's commands:

- the gesture state stays in `ScoreEditor` (the stroke's points, the line's
  anchor, the marquee's rectangle, the move's map, the zone's drag) because
  the render borrows it (R5.1);
- the commands stay in `ScoreEditor` (`commitStroke()`, `commitLine()`,
  `deleteSelected()`, `addZone()`, `beginZoneDrag()`, `dragZoneTo()`,
  `notify()`), because they are the model's business;
- the hit tester (R5.3) answers the tools' questions - what is under the
  cursor - and the mapper (R5.2) is the canvas's mapping.

The canvas declares the tools friends, so they can reach its state without
the app's public API growing.

## The suite

`Source/SelfTestEditingTools.cpp` is the tools' own contract: 45 checks that
pin what each drawing mode does.

- the freehand stroke: one arc per drag, the points sorted by time, the
  release's point part of the stroke, the points on the page, the pressure,
  the stroke temporary until the release, the current material, the layer the
  stroke was drawn in, the simplification (a straight drag is simplified to
  its two ends) and the fine mode's exemption from it, the refusal of a
  stroke that is too short (and the message), and the layer switch that
  cancels the stroke;
- the undo transaction: one labelled transaction per gesture;
- the sine stroke: one arc, and the wobble around the line;
- the line: one arc that follows the drag, its own label, and the refusal of
  a line that is too short (and the message);
- the erase: the arc under the cursor, and nothing on empty space;
- the selection: the click, the ctrl-click, the shift-click, the click on
  empty space;
- the marquee, the move and the zone;
- the scrub band: it scrubs, and it draws nothing.

The suite came first: it was written against the old code and drove the
editor's own mouse handling. The move gave each drawing mode a controller,
and the checks themselves did not change.

## What is not covered yet

- **The point editing is still not a tool.** The review's list of suggested
  tools includes "point editing" (§24); the canvas has no tool that edits a
  single point of an arc yet, so there is nothing to put in a controller.
- **The gesture transactions are still the canvas's.** The undo transaction
  is opened and closed by the tools, but the transaction itself is the
  canvas's `onEditStart`/`onEditEnd` pair. The review's R047 ("Introduce
  gesture transactions") is the next step in that line.
- **The tools are not separate objects yet.** They are the canvas's
  controllers, not the canvas's children: a tool cannot be given a model of
  its own, and the canvas still holds the page, the selection and the
  gesture state. A `ScoreEditingContext` - what a tool is handed - would be
  the next step if the tools ever have to stand on their own.
- **The context menu is still the canvas's.** The right button's menu is
  opened by the tools, but the menu itself and its commands stay in
  `ScoreEditor`.
