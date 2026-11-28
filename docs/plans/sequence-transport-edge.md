# The sequence transport edge

The architecture note for R5.8. It describes the boundary R5.8 introduced -
the sequence card's description and the performance transport's words - as it
is after that phase, not as it is meant to be at the end of the refactoring.

> **R5.8 gave the sequence card and the performance transport one
> description.** The widgets, the commands and the panels are still the
> shell's; what moved is the knowledge of *what the sequence card holds*,
> *what its switches say*, *what a Record or Play press means* and *what
> domain the tempo has*.

## The boundary

```
Source/SequenceTransport.h   the description: the sequence card's cells, the
                             roles, every control's wording, the two button
                             labels, the press rule, the tempo's domain and
                             the page-duration fallback
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw; the panels read the words
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app binds
Source/MainComponentLayout.cpp     the roles to them and builds the card)
Source/SequencePanel.h            the panel reads the words and the labels
Source/PerformanceView.h          the view reads the words and the range
```

The review asks for the sequence and the performance controllers to be
decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `showPerformanceView()`, `refreshPerformanceTransport()` | PerformanceController |
> | `showSequencePanel()`, `sequenceRecordTick()` | SequenceController |
> | `startSequencePlayback()`, `stopSequencePlayback()` | SequenceTransport |

and, on the panels:

> "Many panel classes already exist. Extend those boundaries instead of
> creating unnecessary duplicate wrappers." (§28)

R5.8 takes the first step of that table, the same step R5.6 and R5.7 took for
the inspectors: the *description* and the *words* become an edge, and the
widgets and the commands stay the shell's. The five functions the table names
are still `MainComponent`'s: they touch the document, the clock and the undo
transaction, which are the shell's. The controllers themselves are a later
phase's business.

## The description

```cpp
namespace upic::sequence_transport
{
    using Kind = upic::inspector::Kind;

    /** A control of the sequence card. */
    enum class Control
    {
        Sequence,   // the performance sequence selector
        Record,     // start / stop the recorder
        Play,       // start / stop the replay
        Loop,       // the selected sequence's loop flag
        Open,       // the sequence panel
        count
    };

    /** One cell of the sequence card. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the sequence card says about itself. */
    struct Description
    {
        const char* name;       // "Sequence"
        std::vector<Cell> card; // the card's cells, in order
    };

    const Description& describe();
    const std::vector<Control>& allControls();

    const char* tooltip (Control control);
    const char* recordButtonText (bool recording);
    const char* playButtonText (bool playing);

    enum class Action { Start, Stop };
    Action recordAction (bool recording);
    Action playAction (bool playing);

    constexpr double kDefaultPageSeconds = 30.0;
    constexpr double kMinTempoBpm = 40.0;
    constexpr double kMaxTempoBpm = 240.0;
    constexpr double kTempoIntervalBpm = 1.0;
    constexpr double kTempoSkew = 0.5;
    const char* tempoTooltip();
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The shared cell vocabulary

The sequence card draws its cells the same way the R5.6/R5.7 edges do, so it
uses their vocabulary - `Source/InspectorCell.h`:

```cpp
namespace upic::inspector
{
    enum class Kind { Plain, Labelled, Inline, Knob };
}
```

`using Kind = upic::inspector::Kind;` keeps `upic::sequence_transport::Kind`
compiling, and the layout's one `addCell` helper handles the card.

## The contract

**The card is written down once.**

| the card | the cells | the kinds | the widths |
|---|---|---|---|
| the sequence card | the sequence selector, Record, Play, Loop, View | Inline, Plain, Plain, Plain, Plain | 30/110, 52, 54, 52, 54 |

The cells' numbers are the numbers the layout had; only their home moved.

**The words are written down once.** The three surfaces that carry the
transport read the same tooltip list and the same label rules:

| the surface | what it shows |
|---|---|
| the toolbar's sequence card | the selector, Record, Play, Loop, View |
| the sequence panel | Record, Play, Loop, plus Trim, Replace, Clear, Close |
| the performance view | Record, Play, Loop, plus its page, scrub, reverse, page-loop and tempo switches |

Before, Record said `Rec` on the card and in the view and `Record` in the
panel; the Record tooltip had three wordings (the card's and the panel's
spelled out "(page + playhead)", the view's did not); the Loop tooltip had
two ("the performance" on the card and in the panel, "the recorded
performance" in the view). Now there is one tooltip list and one pair of
label rules, so the surfaces cannot disagree.

**The press rule is one rule.** Record and Play are toggles: a running switch
stops the run, a stopped one starts it. The test was copied at all three call
sites; now it lives in `recordAction()` and `playAction()`, and the shell runs
it once in `toggleSequenceRecording()` and `toggleSequencePlayback()`. The
card, the panel and the view all call those two functions.

**The tempo's domain is one domain.** 40..240 BPM, 1 BPM apart, with a 0.5
skew. Before, the toolbar's knob offered whole BPM with a skew and the view's
slider offered half BPM without one. The slider now moves in the knob's steps
and carries the knob's skew, and both read the range from the edge.

**The page-duration fallback is one number.** 30 s when a surface has no
duration to show; the sequence panel and the performance view used to spell
it `30.0` separately.

## The deliberate change

Two user-visible things changed on purpose, and the suite pins both:

- the panel's Record button is now labelled `Rec`, the same short form as the
  card and the view. The card's pinned button is 52 px wide, so one short
  form keeps all three inside their cells;
- the view's tempo slider now moves in whole BPM with the knob's skew, and the
  app's tempo knob gained the shared tooltip.

Everything else keeps its words: the shared Record tooltip is the card's and
the panel's old one, and the shared Loop tooltip is the card's and the panel's
old one.

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
sequence card is built:

```cpp
namespace st = upic::sequence_transport;

bindSequenceControl (st::Control::Sequence, &seqLab,     &seqBox);
bindSequenceControl (st::Control::Record,   nullptr,     &seqRecBtn);
bindSequenceControl (st::Control::Play,     nullptr,     &seqPlayBtn);
bindSequenceControl (st::Control::Loop,     nullptr,     &seqLoopBtn);
bindSequenceControl (st::Control::Open,     nullptr,     &seqEditBtn);
```

The layout then builds the card from the description's cells, with the helper
that handles the four kinds:

```cpp
FlowGroup seq { "seq", {} };
const auto& transport = upic::sequence_transport::describe();
for (auto& cell : transport.card)
    addCell (seq, sequenceControls[(size_t) cell.control],
             cell.kind, cell.labelWidth, cell.width, cell.newLine);
```

The tooltips, the button labels and the tempo's range come from the edge too:
the card, the sequence panel and the performance view read the same words.

## The suite

`Source/SelfTestSequenceTransport.cpp` is the sequence transport's contract:
45 checks that drive the real component - they press the card's buttons, the
panel's buttons and the view's buttons and read the app's state.

- the card: the selector, Record, Play, Loop and View, their labels, their
  tooltips and their order;
- the commands: Record starts the recorder (and creates the sequence when the
  bank is empty), Record again stops it, Play needs two points, Loop writes
  the model's flag and View opens the panel;
- the one recorder and the one transport: a recording started on one surface
  stops on another, and a replay started on one surface stops on another;
- the view's own switches: its page, scrub, reverse, loop and tempo are the
  app's commands, and its tempo reaches the page;
- the panel's Clear, and the two panels' Close.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the app's card is the edge's cell list in the edge's order,
the card's tooltips are the edge's wording, the labels follow the edge's rule,
the tempo knob and the tempo slider cover the same range in the same steps and
say the same words, and the edge's own tables are sound (the press rule, the
two labels, the two constants, the fallback).

The suite came first: 33 checks were written against the old code and drove
the real component. The move gave the transport its description, and the
checks that cover the new behaviour (the edge against the app) were added with
the move.

## What is not covered yet

- **The controllers are still the shell's.** The review's §28 wants a
  `PerformanceController`, a `SequenceController` and a `SequenceTransport`.
  `showPerformanceView()`, `refreshPerformanceTransport()`,
  `showSequencePanel()`, `sequenceRecordTick()`, `startSequencePlayback()` and
  `stopSequencePlayback()` still live in `MainComponent`. R5.8 moved the
  description and the words; the controllers are the next step.
- **The sequence panel is still the shell's.** It is a class of its own
  (`Source/SequencePanel.h`), but its buttons are still wired by
  `MainComponent`, and its Trim, Replace and Clear wording is still its own.
- **The performance view's other controls are not described.** Its page
  selector, scrub, reverse, page-loop and tempo switches are the app's
  commands; only the sequence trio's wording and the tempo's domain are the
  edge's.
- **The card engine's rule is not the edge's.** The engine puts the plain
  controls first and the knobs after them, so a card's *reading* order is not
  the description's order. The description carries the logical order, and the
  engine lays the card out. The sequence card has no knob, so here the two
  orders agree.
