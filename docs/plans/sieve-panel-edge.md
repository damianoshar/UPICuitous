# The sieve panel edge

The architecture note for R5.7. It describes the boundary R5.7 introduced -
the sieve panel's description - as it is after that phase, not as it is meant
to be at the end of the refactoring.

> **R5.7 gave the sieve panel its own description.** The widgets, the commands
> and the panels are still the shell's; what moved is the knowledge of *what
> the sieve holds*, *how its remainder list is written* and *what every
> control says about itself*.

## The boundary

```
Source/SievePanel.h         the description: the sieve card's cells, the
                            global magnets card's cells, the modulus' domain,
                            the time grid's divisions, the remainder list's
                            text format and every control's wording
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app
Source/MainComponentLayout.cpp     binds the roles to them)
Source/MainComponentModel.cpp     the sieve's command: pushSieve()
Source/MainComponentPiece.cpp     the loader: the sieve's state comes back
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
namespace upic::sieve_panel
{
    using Kind = upic::inspector::Kind;

    /** A control of the sieve panel. */
    enum class Control
    {
        On,           // the sieve's switch
        Modulus,      // the m - the octave is divided into m equal steps
        Remainders,   // the R - which of the m divisions are allowed
        Preset,       // the preset library
        Stickiness,   // the global stick
        TimeGrid,     // the global time grid
        count
    };

    /** One cell of one of the sieve panel's cards. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the sieve panel says about itself. */
    struct Description
    {
        const char* name;
        std::vector<Cell> card;     // the sieve card's cells, in order
        std::vector<Cell> magnets;  // the global magnets card's cells
    };

    const Description& describe();
    const std::vector<Control>& allControls();

    constexpr int kMinModulus = 2;
    constexpr int kMaxModulus = 120;

    struct Division { const char* label; double value; };
    const std::vector<Division>& timeGridDivisions();
    int timeGridDivisionIndex (double value);

    std::string formatRemainders (const std::vector<int>& remainders);
    std::vector<int> parseRemainders (const std::string& text, int modulus);

    const char* tooltip (Control control);
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

**The sieve's contents are written down once.**

| the card | the cells | the kinds |
|---|---|---|
| the sieve card | the switch, the preset library, the m, the R | Plain, Plain, Inline, Inline |
| the global magnets card | the stick, the time grid | Knob, Labelled |

**The time grid's divisions are one list.** Before, the values were one array
in the command and one array in the loader, and the dropdown's seven labels
stood beside them:

```
Off    0.5/s   1/s   2/s   4/s   8/s   16/s
0.0    0.5     1.0   2.0   4.0   8.0   16.0
```

Now `timeGridDivisions()` holds the label and the value together, the
dropdown offers them, the command reads them and the loader puts the page's
grid back on the right one (`timeGridDivisionIndex()`).

**The remainder list's text format is one reader and one writer.** The list is
separated by a comma, a semicolon or a space. A token that is not a number is
*skipped*, not refused - "0,4,x,7" is 0, 4 and 7. A remainder is taken modulo
the modulus, so a negative one lands inside the octave. `parseRemainders()`
reads it and `formatRemainders()` writes it; the command, the loader and the
preset library all use them.

**The modulus' domain is one rule.** 2..120. A modulus outside the domain is
refused and the last valid one is kept while the field is being typed.

## The binding

The app owns the widgets, so the app binds the roles to them - once, in
`buildSieveControls()`:

```cpp
bindSieveControl (sp::Control::On,         nullptr,    &sieveBtn);
bindSieveControl (sp::Control::Modulus,    &sieveMLab, &sieveMEdit);
bindSieveControl (sp::Control::Remainders, &sieveRLab, &sieveREdit);
bindSieveControl (sp::Control::Preset,     nullptr,    &sievePresetBox);
bindSieveControl (sp::Control::Stickiness, &stickLab,  &stickSlider);
bindSieveControl (sp::Control::TimeGrid,   &gridLab,   &gridBox);
```

The layout then builds the cards from the description's cells, with the
helper that handles the four kinds:

```cpp
FlowGroup sieve { "sieve", {} };
const auto& panel = upic::sieve_panel::describe();
for (auto& cell : panel.card)
    addCell (sieve, sieveControls[(size_t) cell.control],
             cell.kind, cell.labelWidth, cell.width, cell.newLine);
```

## The builder

`buildSieveControls()` used to hold three things that are not the sieve's:
the polyagogy (the Osc material's own controls), the procedural arc generator
and the keyboard help. R5.7 splits them out:

| the builder | what it holds |
|---|---|
| `buildMaterialControls()` | the material's own controls, and the polyagogy with them |
| `buildSieveControls()` | the sieve and the global magnets |
| `buildToolsControls()` | the procedural arc generator and the keyboard help |

The polyagogy moves because it is the Osc material's control: the material
card has always held it, and the material description names it
(`Control::ModUse`, `Control::ModRate`, `Control::ModDepth`).

## The suite

`Source/SelfTestSievePanel.cpp` is the sieve panel's contract: 42 checks
that drive the real component - they type into the fields, pick from the
dropdowns and read the canvas, which is where the sieve's state goes.

- the sieve card: the switch, the preset library, the m and the R;
- the magnets card: the stick and the time grid;
- the toggle: it reaches the canvas, both ways;
- the modulus: it reaches the canvas, and a modulus below the domain, above
  the domain or not a number at all is refused (the last valid one is kept);
- the remainder list: it reaches the canvas; it takes any separator and a
  space; what is not a number is skipped; a remainder is taken modulo the
  modulus (a negative one too); an empty field keeps the last list; the list
  follows the modulus;
- the presets: the library's eight, a preset sets its m and its R and
  switches the sieve on, and the dropdown goes back to its placeholder;
- the magnets: the stickiness reaches the canvas and runs from 0 to 1 in
  steps of 0.05; the time grid's seven divisions reach the canvas, and the
  grid can be off;
- the wording: every control says what it does, and the fields show their
  shape when they are empty.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the app's cards are the edge's cell lists, the cells'
kinds agree with the widgets, the controls' wording is the edge's wording,
the dropdown's divisions are the edge's list, the edge finds every division
back, the remainder field is the edge's text format, the modulus' domain is
the edge's, and the edge's own reader reads the separators, skips what is
not a number and takes a remainder modulo the modulus.

The suite came first: it was written against the old code and drove the real
component. The move gave the sieve a description, and the checks themselves
did not change - the ones that cover the new behaviour (the edge against the
app) were added with the move.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a `SievePanel`
  class. Its builder still lives in `MainComponentControls.cpp`, and its
  widgets are still the shell's members. R5.7 moved the description; the
  panel is the next step.
- **The command is still the shell's.** `pushSieve()` still reads the
  widgets and writes the model - it just reads the remainder list through the
  edge now.
- **The card engine's rule is not the edge's.** The engine puts the plain
  controls first and the knobs after them, so the magnets card's *reading*
  order is not the description's order (the time grid comes before the
  stick). The description carries the logical order, and the engine lays the
  card out.
- **The zone's own sieve is not described.** A sieve zone carries its own
  m/R and its own time grid; those live in the zone's menu, not in a card.
- **The frequency table's editor is not described.** The button that opens
  it is described (it is the frequency inspector's cell); the editor itself
  is a panel of its own.
