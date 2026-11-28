# The modulation controls edge

The architecture note for R5.9. It describes the boundary R5.9 introduced -
the modulation card's description - as it is after that phase, not as it is
meant to be at the end of the refactoring.

> **R5.9 gave the modulation card its own description.** The widgets and the
> commands are still the shell's; what moved is the knowledge of *what the
> card holds*, *what its controls say about themselves*, *what the two
> dropdowns offer* and *what domain the depth has*.

## The boundary

```
Source/ModulationControls.h   the description: the modulation card's cells,
                               the roles, every control's wording, the two
                               lists' items and ids and the depth's domain
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw; the lists are written and read through
        |  the description's rules
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app binds
Source/MainComponentLayout.cpp     the roles to them and builds the card)
```

The review asks for the modulation controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `refreshModBox()` | ModulationInspector |

and, on the panels:

> "Many panel classes already exist. Extend those boundaries instead of
> creating unnecessary duplicate wrappers." (§28)

R5.9 takes the first step of that table, the same step R5.6, R5.7 and R5.8
took for the inspectors: the *description* and the *words* become an edge, and
the widgets and the commands stay the shell's. `refreshModBox()` and
`setModOnSelection()` are still `MainComponent`'s: they touch the document and
the undo transaction. The inspector itself is a later phase's business.

## The description

```cpp
namespace upic::modulation_controls
{
    using Kind = upic::inspector::Kind;

    /** A control of the toolbar's modulation card. */
    enum class Control
    {
        Modulator,   // the arc that bends the selection
        Target,      // what it bends: the pitch or the level
        Depth,       // how hard it bends
        count
    };

    /** One cell of the modulation card. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the modulation card says about itself. */
    struct Description
    {
        const char* name;       // "Modulation"
        std::vector<Cell> card; // the card's cells, in order
    };

    const Description& describe();
    const std::vector<Control>& allControls();

    const char* tooltip (Control control);

    constexpr int kNoneItemId = 1;
    const char* noneName();          // "None"
    std::string arcName (int arcId); // "Arc 7"
    int itemIdForArc (int arcId);
    bool isNoneItem (int itemId);
    int arcForItemId (int itemId);

    constexpr int kPitchTarget = 0, kAmpTarget = 1;
    constexpr int kPitchItemId = 1, kAmpItemId = 2;
    const char* targetName (int target);
    int itemIdForTarget (int target);
    int targetForItemId (int itemId);

    constexpr double kMinDepth = 0.0, kMaxDepth = 1.0, kDepthInterval = 0.001;
    constexpr double kDefaultDepth = 0.5;
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The shared cell vocabulary

The modulation card draws its cells the same way the R5.6/R5.7/R5.8 edges do,
so it uses their vocabulary - `Source/InspectorCell.h`:

```cpp
namespace upic::inspector
{
    enum class Kind { Plain, Labelled, Inline, Knob };
}
```

`using Kind = upic::inspector::Kind;` keeps `upic::modulation_controls::Kind`
compiling, and the layout's one `addCell` helper handles the card.

## The contract

**The card is written down once.**

| the card | the cells | the kinds | the widths |
|---|---|---|---|
| the modulation card | the modulator, the target, the depth | Inline, Inline, Inline | 30/100, 26/66, 36/110 |

The cells' numbers are the numbers the layout had; only their home moved.

**The words are written down once.** The three controls say what they do:

| the control | what it says |
|---|---|
| the modulator | "The arc that modulates the selection (its contour bends the pitch or its level)" |
| the target | "What the modulator drives: pitch or amplitude" |
| the depth | "The modulation depth (1 = +/- one octave at the contour extremes)" |

**The modulator list's items and ids are one rule.** None is the list's first
item, and the page's arcs follow it:

- None is item 1;
- the arc with the id `n` is item `n + 1`;
- an arc's name in the list is "Arc " and its id.

**The target list's items and ids are one rule.** The model says 0 for the
pitch and 1 for the amplitude; the list offers "Pitch" (item 1) and "Amp"
(item 2), and the two ids map back to the model's two values.

Before, the items and the ids were written down twice: once where the list
was built ("None", "Arc " + id, "Pitch", "Amp", 1, 2, id + 1) and once where
it was read ("i <= 1 ? -1 : i - 1", "target == 1 ? 2 : 1"). Nothing made the
two copies agree.

**The depth's domain is one domain.** 0..1, in steps of 0.001 - one is a full
octave at the contour's extremes. The slider's range, the command's clamp and
the value the card shows when there is no arc to show a depth for (0.5) are
the same numbers.

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
modulation card is built:

```cpp
namespace moc = upic::modulation_controls;

bindModulationControl (moc::Control::Modulator, &arcModLab, &arcModBox);
bindModulationControl (moc::Control::Target, &arcModTgtLab, &arcModTgtBox);
bindModulationControl (moc::Control::Depth, &arcModDepthLab, &arcModSlider);
```

The layout then builds the card from the description's cells, with the helper
that handles the four kinds:

```cpp
FlowGroup mod { "mod", {} };
const auto& modulation = upic::modulation_controls::describe();
for (auto& cell : modulation.card)
    addCell (mod, modulationControls[(size_t) cell.control],
             cell.kind, cell.labelWidth, cell.width, cell.newLine);
```

The lists are written and read through the edge's rules:

```cpp
arcModBox.addItem (moc::noneName(), moc::kNoneItemId);
for (auto& a : curPage().arcs)
    arcModBox.addItem (juce::String (moc::arcName (a.id)), moc::itemIdForArc (a.id));
...
arcModBox.setSelectedId (moc::itemIdForArc (cur), juce::dontSendNotification);
arcModTgtBox.setSelectedId (moc::itemIdForTarget (target), juce::dontSendNotification);
arcModSlider.setValue (depth, juce::dontSendNotification);
```

## The one rule behind the two dropdowns

The card's two dropdowns read the same way and write the same command, and
that reading was copied at both call sites. Now one method does it:

```cpp
void MainComponent::applyModulationControls()
{
    const int item = arcModBox.getSelectedId();
    setModOnSelection (moc::isNoneItem (item) ? -1 : moc::arcForItemId (item),
                       moc::targetForItemId (arcModTgtBox.getSelectedId()),
                       arcModSlider.getValue());
}
```

The slider keeps its own path: it writes the depth of the selected arcs in
place, so a drag is one transaction (the slider opens the transaction on the
drag's start and closes it on the drag's end).

## The suite

`Source/SelfTestModulationControls.cpp` is the modulation card's contract: 38
checks that drive the real component - they pick from the card's dropdowns,
move the card's slider and read the app's state.

- the card: the modulator, the target and the depth, the captions and the
  wording;
- the lists: None and the page's arcs, Pitch and Amp, and their item ids;
- the depth: its domain, and the value it falls back to;
- the commands: picking a modulator assigns it, picking a target writes it,
  picking None clears it, the slider writes the depth, a cycle is refused,
  and each of those is one undo step.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the app's card is the edge's cell list in the edge's
order, the card's tooltips are the edge's wording, the app's lists follow the
edge's ids and names, the depth slider covers the edge's domain, and the
edge's own tables are sound.

The suite came first: 22 checks were written against the old code and drove
the real component. The move gave the card its description, and the checks
that cover the new behaviour (the edge against the app) were added with the
move.

## What is not covered yet

- **The inspector is still the shell's.** The review's §28 wants a
  `ModulationInspector`. `refreshModBox()` still lives in `MainComponent`.
  R5.9 moved the description and the words; the inspector is the next step.
- **The modulation's own rules are not the edge's.** The cycle check
  (`modWouldCycle()`), the assignment's refusal and the model's own fields
  (`modulatorArcId`, `modTarget`, `modArcDepth`) belong to the model
  (`Source/Arc.h`, `Source/Page.h`), not to the card.
- **The card engine's rule is not the edge's.** The engine puts the plain
  controls first and the knobs after them, so a card's *reading* order is not
  the description's order. The description carries the logical order, and the
  engine lays the card out. The modulation card has no knob, so here the two
  orders agree.
