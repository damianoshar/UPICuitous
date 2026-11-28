# The score canvas's gesture transaction edge

The architecture note for R5.5. It describes the boundary R5.5 introduced -
the score canvas's gesture transactions - as it is after that phase, not as it
is meant to be at the end of the refactoring.

> **R5.4 gave the drawing modes their own controllers.** The gesture's state
> below is still the canvas's (the render borrows it), but each mode's
> behaviour lives in its own tool. See
> [`score-editing-tools-edge.md`](score-editing-tools-edge.md).

## The boundary

```
Source/ScoreGestureTransaction.h   the transaction's lifecycle: begin (with
                                   the label), commit (the command is kept)
                                   and cancel (the command is dropped)
        |
        |  the canvas opens a transaction; the app's hooks carry it
        v
Source/ScoreEditor.h               the canvas: the three doors
Source/ScoreEditor.cpp             (beginGesture, commitGesture,
Source/ScoreEditingTools.h         cancelGesture) and the gesture state
        |
        |  the app fills the hooks in; the transaction is the document's
        v
Source/CommandDispatcher.h         the document's transaction: the snapshot,
Source/DocumentSession.h           the undo step, the change report - and the
                                   third exit, cancelCommand()
```

The review asks for the gesture's transaction to be explicit (R047,
"Introduce gesture transactions", §25):

> "A stroke should have an explicit lifecycle: during drawing, temporary
> geometry should normally remain in the editor. On release, produce one
> validated editing command." (§25)

and for the live audition to be a bounded preview rather than hundreds of
document changes:

> "For live audition, publish a bounded transient preview rather than
> committing hundreds of intermediate document changes." (§25)

R5.1 gave the render its own edge, R5.2 the coordinate mapping, R5.3 the hit
testing, R5.4 the drawing modes, and R5.5 the transaction that holds them
together.

## The lifecycle

```cpp
namespace upic::score_gesture
{
    class GestureTransaction
    {
    public:
        using Open  = std::function<void (const juce::String& label)>;
        using Close = std::function<void()>;   // the command is kept
        using Drop  = std::function<void()>;   // the command is dropped

        void setDoors (Open open, Close close, Drop drop);
        void begin (const juce::String& label);
        void commit();
        void cancel();
        bool isOpen() const noexcept;
        const juce::String& label() const noexcept;
    };
}
```

The class owns no model and no document: the doors are the canvas's hooks
(`onEditStart`, `onEditEnd`, `onEditCancel`), and the class only remembers
whether a transaction is open and what it was called.

The rules:

- `begin()` opens the transaction. A transaction that is still open is closed
  first - a gesture that was never finished must not swallow the next one
  (the document's dispatcher does the same);
- `commit()` closes it and keeps it: the command is one undo step, if the
  model really changed;
- `cancel()` closes it and drops it: no undo step, and the document's
  revision does not move;
- `commit()` and `cancel()` with nothing open do nothing.

## The contract

**A gesture is one transaction, and it ends in exactly one of two ways: a
command or nothing.**

| the gesture | the transaction | the document |
|---|---|---|
| the stroke | opens at the press ("Draw"), closes at the release | one undo step, and the revision moves once |
| the line | opens at the press ("Draw a line"), closes at the release | one undo step |
| the move | opens at the press ("Move the arc"), closes at the release | one undo step - and only if the arcs really moved |
| the zone's drag | opens at the press ("Move the sieve zone"), closes at the release | one undo step - and only if the zone really moved |
| the erase | opens and closes inside the command ("Delete") | one undo step |
| a new zone | opens and closes inside the command ("Add a sieve zone") | one undo step |
| a click that only takes an arc or a zone | opens and closes, and produces nothing | no step, and the revision does not move |
| a cancelled gesture | opened, and dropped | no step, no open transaction, the model as it was |

The document's side is the dispatcher's third exit:

```cpp
void cancelCommand()
{
    if (! open_) return;

    const bool changed = changed_;

    pending_.reset();      // the step is not kept

    open_ = false;
    changed_ = false;
    label_.clear();

    if (changed && modelMoved_) modelMoved_ (false);
}
```

The epoch moves when the gesture had changed the model: the gesture undid
itself, so the model moved back, and the audio must see the state the
gesture left behind.

## The live audition

The gesture's temporary geometry stays in the editor, and the model follows
the pointer without the document changing: the epoch moves with every change
during the drag, and the status timer publishes one snapshot per tick - the
bounded transient preview §25 asks for. The revision (the document's change)
waits for the release, so a drag is one change, not hundreds.

## The suite

`Source/SelfTestGestureTransactions.cpp` is the transaction's own contract:
45 checks that drive the editor with synthesised mouse events and watch the
document's own transaction (`Source/CommandDispatcher.h`).

- the stroke: the transaction opens at the press with its label, it closes at
  the release, the temporary geometry does not touch the model, the release is
  one step, and the document changed once;
- the line and the move: the same, each with its own label;
- the move's drag: the model moves while the pointer moves, and the document
  has not changed yet;
- the refused stroke: a transaction that opens and closes, and no step;
- the erase and the zone: one command each;
- the click that only takes an arc, and the click that only takes a zone: no
  step, and the model is left alone;
- the move that ends where it started: no step;
- the cancelled gesture: no step, no open transaction, the model as it was,
  and a cancelled move puts the arc back;
- the command after a cancelled gesture: a command of its own.

The suite came first: it was written against the old code and drove the
editor's own mouse handling. The move gave the transaction a lifecycle, and
the checks themselves did not change - the ones that cover the new behaviour
(the click that only takes an arc, the cancelled gesture's closed
transaction, the command after it) were added with the move.

## The measurement

§25 asks for the mouse-release behaviour to be profiled. The suite measures
the press, the drag and the release of a stroke on an empty page, on a page
with a large curve (1,000 points) and on a page at the arc limit (4,000
arcs), and records the numbers as notes:

| the page | the press | the drag | the release |
|---|---|---|---|
| an empty page | 0.026 ms | 0.001 ms | 0.033 ms |
| a page with a large curve (1,000 points) | 0.025 ms | 0.001 ms | 0.037 ms |
| a page at the arc limit (4,000 arcs) | 0.934 ms | 0.002 ms | 0.611 ms |

The press is the snapshot the transaction takes (a copy of the piece); the
release is the commit and the undo step's measurement. The numbers are
notes, not thresholds - they are the baseline a later phase compares
against.

## What is not covered yet

- **The other editors have their own transactions.** The envelope editor and
  the waveform editor open a transaction of their own (`setOnEditStart()`,
  `setOnEditEnd()`), and the envelope editor's refusal closes it by hand.
  They do not use this edge: the edge is the score canvas's.
- **The dirty flag is not restored by a cancel.** A gesture that changed the
  model marks the piece as changed while it happens, and a cancel that puts
  the model back does not clear the flag - the piece stays marked as unsaved
  even though it ends where it started. The flag belongs to the document's
  session, and the session does not remember the state it had when the
  command opened.
- **The capture happens at the press.** A gesture that changes nothing still
  takes a snapshot of the piece when it opens its transaction (0.026 ms on an
  empty page, 0.934 ms on a page at the arc limit). A transaction that opens
  when the first change happens would not pay that - but then the move's
  snapshot would be taken after the model moved.
- **The other profiles are not measured yet.** §25 also asks for the release
  with simultaneous playback, with an active preview, with the undo history
  growing and with the snapshot compilation. The suite measures the three
  page sizes; the rest belongs to the audio's own suites.
