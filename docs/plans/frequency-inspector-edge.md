# The frequency inspector edge

The architecture note for R5.7. It describes the boundary R5.7 introduced -
the frequency inspector's description - as it is after that phase, not as it
is meant to be at the end of the refactoring.

> **R5.7 gave the frequency controls their own description.** The widgets, the
> commands and the panels are still the shell's; what moved is the knowledge
> of *what the frequency controls hold*, *how many spaces the page has* and
> *what every control says about itself*.

## The boundary

```
Source/FrequencyInspector.h  the description: the page card's cells, the pitch
                             card's frequency cell, the acceleration card's
                             cells, the page's four spaces, the ambitus's
                             "Custom" item rule, the acceleration's range and
                             every control's wording
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app
Source/MainComponentLayout.cpp     binds the roles to them)
Source/MainComponentModel.cpp     the read-back: refreshBoxes()
Source/MainComponentControls.cpp   the commands: pushAmbitus(), pushWarp(),
                                   pushSpaceDefs()
```

The review asks for the frequency and the sieve controls to be decomposed
(§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildSieveControls()` | SievePanel |
> | `buildFrequencyControls()`, `pushSpaceDefs()` | FrequencyInspector |

and, on the panels:

> "Many panel classes already exist. Extend those boundaries instead of
> creating unnecessary duplicate wrappers." (§28)

R5.7 takes the first step of that table, the same step R5.6 took for the
material inspector: the *description* becomes an edge, and the widgets and the
commands stay the shell's. The panel itself is still R049's and R052's
business.

## The description

```cpp
namespace upic::frequency_inspector
{
    using Kind = upic::inspector::Kind;

    /** A control of the frequency inspector. */
    enum class Control
    {
        Space,          // the arc's own space
        Ambitus,        // the page's ambitus
        Warp,           // the page's acceleration
        SpaceDef1,      // the page's first frequency space
        SpaceDef2,
        SpaceDef3,
        SpaceDef4,
        Table,          // the frequency table of the selected space
        count
    };

    /** One cell of one of the frequency inspector's cards. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the frequency inspector says about itself. */
    struct Description
    {
        const char* name;
        std::vector<Cell> page;   // the page card's cells, in order
        std::vector<Cell> pitch;  // the pitch card's frequency cell
        std::vector<Cell> accel;  // the acceleration card's cells
    };

    const Description& describe();
    const std::vector<Control>& allControls();

    constexpr int kSpaceCount = 4;

    constexpr double kMinWarp = -0.9;
    constexpr double kMaxWarp = 0.9;
    constexpr double kWarpInterval = 0.05;

    int ambitusCustomItemId (int presetCount);

    const char* tooltip (Control control);
    std::string labelTooltip (Control control);
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The shared cell vocabulary

The three panel descriptions draw their cards the same way, so the way a cell
is drawn lives in one place - `Source/InspectorCell.h`:

```cpp
namespace upic::inspector
{
    /** How a cell of a panel's card is drawn. */
    enum class Kind
    {
        Plain,      // the control alone
        Labelled,   // the caption above the control
        Inline,     // the caption beside the control, on its row
        Knob        // the caption above the control, which draws it itself
    };
}
```

Each edge does `using Kind = upic::inspector::Kind;`, so
`upic::material_inspector::Kind::Knob` still compiles and the R5.6 suite is
untouched.

## The contract

**The frequency controls' contents are written down once.**

| the card | the cells | the kinds |
|---|---|---|
| the page card | the ambitus, the four frequency spaces, the table button | Inline, Inline, Inline, Inline, Inline, Plain |
| the pitch card | the arc's own space (on its own row, under the snap and the step) | Labelled |
| the acceleration card | the page's acceleration | Knob |

The pitch card is not the inspector's: it holds the snap, the discrete step
and the step's division knob as well. Only the arc's own space belongs to
the inspector, and only that cell is described.

**The page has four spaces.** The UPIC had four tables, and the page owns one
of them. The count is one constant (`kSpaceCount`), used by the builder, the
command, the read-back and the arc's own dropdown.

**The ambitus's "Custom" item is one rule.** The dropdown offers the six
presets and then "Custom" - the page's own interval, which the dropdown
cannot set. The item's id was written down twice (once when the items were
offered and once when the page's interval was read back); now it is one rule
(`ambitusCustomItemId()`), so the two cannot disagree.

**The acceleration's range is one range.** -0.9..0.9 in steps of 0.05. The
knob offers it and the command clamps to it, so the knob cannot offer a value
the command would refuse.

## The binding

The app owns the widgets, so the app binds the roles to them - once, in
`buildFrequencyControls()`:

```cpp
bindFrequencyControl (fi::Control::Space,   &spaceLab,     &spaceBox);
bindFrequencyControl (fi::Control::Ambitus, &ambitusLab,   &ambitusBox);
bindFrequencyControl (fi::Control::Warp,    &warpLab,      &warpSlider);
bindFrequencyControl (fi::Control::Table,   nullptr,       &spaceTableBtn);
for (int i = 0; i < fi::kSpaceCount; ++i)
    bindFrequencyControl ((fi::Control) ((int) fi::Control::SpaceDef1 + i),
                          &spaceDefLab[i], &spaceDefBox[i]);
```

The layout then builds the cards from the description's cells, with the
helper that handles the four kinds:

```cpp
FlowGroup page { "page", {} };
const auto& freq = upic::frequency_inspector::describe();
for (auto& cell : freq.page)
    addCell (page, frequencyControls[(size_t) cell.control],
             cell.kind, cell.labelWidth, cell.width, cell.newLine);
```

## The suite

`Source/SelfTestFrequencyInspector.cpp` is the frequency inspector's
contract: 29 checks that drive the real component - they pick from the
dropdowns, move the knob and read the page.

- the page card: the ambitus, the four spaces and the table button;
- the pitch card: the arc's own space sits in it;
- the acceleration card: it holds the page's acceleration;
- the ambitus: the six presets plus "Custom", the ambitus reaches the page,
  "Custom" changes nothing, and the ambitus goes back to the default
  interval;
- the four frequency spaces: each offers the space presets, a definition
  reaches the page and goes back to the default, and the arc's own space
  lists the page's four spaces and names the space it is;
- the acceleration: it runs from -0.9 to 0.9 in steps of 0.05, it reaches the
  page and it goes back to the default;
- the table button: "pick space 2, press Table" opens the table of the
  selected space, and it closes again.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the app's cards are the edge's cell lists, the cells'
kinds agree with the widgets, the controls' wording is the edge's wording,
the page's four spaces are the edge's count, the "Custom" id is the edge's
rule and the acceleration's range is the edge's.

The suite came first: it was written against the old code and drove the real
component. The move gave the frequency controls a description, and the checks
themselves did not change - the ones that cover the new behaviour (the edge
against the app) were added with the move.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a
  `FrequencyInspector` class. Its builder still lives in
  `MainComponentControls.cpp`, and its widgets are still the shell's members.
  R5.7 moved the description; the panel is the next step.
- **The commands are still the shell's.** `pushAmbitus()`, `pushWarp()` and
  `pushSpaceDefs()` still read the widgets and write the model - they just
  read the count and the range through the edge now.
- **The frequency table's editor is not described.** The button that opens
  it is described (it is the inspector's cell); the editor itself is a panel
  of its own, and it edits the space the arc's combo points at.
- **The card engine's rule is not the edge's.** The engine puts the plain
  controls first and the knobs after them, so a card's *reading* order is not
  the description's order. The description carries the logical order, and the
  engine lays the card out.
- **The voice card is not described.** It holds the carrier wave and the
  envelope; the carrier wave is the material inspector's control, but it
  lives in another card.
