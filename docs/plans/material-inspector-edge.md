# The material inspector edge

The architecture note for R5.6. It describes the boundary R5.6 introduced -
the material inspector's description - as it is after that phase, not as it is
meant to be at the end of the refactoring.

> **R5.6 gave the inspector its own description.** The widgets, the commands
> and the panels are still the shell's; what moved is the knowledge of *what
> each material shows*.

## The boundary

```
Source/MaterialInspector.h        the description: the four materials, their
                                  names, their tooltips, their hints, the
                                  controls each of them shows and the cells
                                  of its card
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw and what to hide
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app
Source/MainComponentLayout.cpp     binds the roles to them)
Source/MainComponentInput.cpp     the material switch: the hint and the layer
```

The review asks for the inspector to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildMaterialControls()` | MaterialInspector |
> | `buildPmControls()` | PhaseModulationPanel |
> | `buildSampleControls()` | SampleMaterialPanel |
> | `buildLiveInputControls()` | LiveInputPanel |

and

> "Many panel classes already exist. Extend those boundaries instead of
> creating unnecessary duplicate wrappers." (§28)

R5.6 takes the first step of that table: the inspector's *description* becomes
an edge of its own. The three panels the review lists (the phase modulation,
the sample material and the live input) stay the shell's for now - they are
R049's and R052's business.

## The description

```cpp
namespace upic::material_inspector
{
    /** The four materials (the tabs above the canvas). */
    enum class Material
    {
        Osc = 0,      // the wavetable arcs
        Sample = 1,   // the sample slot, transposed
        Live = 2,     // the microphone, transposed
        Pm = 3        // the phase modulation arcs
    };

    /** A control of the material inspector. */
    enum class Control { Wave, Ref, ModUse, /* ... */ count };

    /** How a cell of the material card is drawn. */
    using Kind = upic::inspector::Kind;   // Source/InspectorCell.h

    /** One cell of the material card. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
    };

    /** What the inspector says about one material. */
    struct Description
    {
        Material material;
        const char* name;      // the tab's label
        const char* tooltip;   // the tab's tooltip
        const char* hint;      // the sidebar's explanation
        std::vector<Cell> card;         // the material card's cells, in order
        std::vector<Control> shown;     // the controls the material shows
        bool shows (Control control) const;
    };

    const Description& describe (int mode);
    const Description& describe (Material material);
    const std::vector<Control>& allControls();
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The contract

**The material's contents are written down once.**

| the material | the tab | the card's cells | the controls it shows |
|---|---|---|---|
| Osc | "Osc" | the polyagogy: what the arc's drawn wave does besides being the timbre | the carrier wave and the polyagogy |
| Sample | "Sample" | the slot, its Ref, its level, its pitch mode, its grain, its edits, the input gain, the monitor and the capture | the Ref and the Sample's own controls |
| Live | "Live" | the Ref, the live pitch, the live grain and the reverse | the Ref and the Live's own controls |
| PM | "PM" | the operations, the preset, the ratio, the index, the index envelope, the feedback and the import/export | the carrier wave and the PM's own controls |

Two controls are shared: the Ref belongs to the Sample and the Live tab (it
is the material's reference pitch, and both of those materials are pitched),
and the carrier wave belongs to the Osc and the PM tab (a Sample arc plays
the slot and a Live arc transposes the microphone, so a carrier wave there
was misleading - and left a dead gap in the row).

The carrier wave is shown by two materials but lives in another card - the
voice card, beside the envelope. It is the one control that is not in a
material card.

## The binding

The app owns the widgets, so the app binds the roles to them - once, in
`buildMaterialControls()`:

```cpp
bindMaterialControl (mi::Control::Slot, &slotLab, &sampleBox);
bindMaterialControl (mi::Control::LoadSample, nullptr, &loadSampleBtn);
bindMaterialControl (mi::Control::Ref, &refLab, &refSlider);
```

The controls that live in another card are bound there too (the carrier wave
in the voice card, the polyagogy with the material's own controls): the
binding is about the role, not about who adds the widget.

R5.7 moved the polyagogy's builder: it used to be built with the sieve's
builders, and it is now built with the material's - it is the Osc material's
control, and the material card has always held it.

The layout then walks the edge's controls and asks the description what to
show, and builds the card from the same description's cells:

```cpp
const auto& material = upic::material_inspector::describe (drawMatMode);

for (auto control : upic::material_inspector::allControls())
{
    const auto& binding = materialControls[(size_t) control];
    const bool shown = material.shows (control);
    if (binding.label != nullptr) binding.label->setVisible (shown);
    if (binding.control != nullptr) binding.control->setVisible (shown);
}
```

## The suite

`Source/SelfTestMaterialInspector.cpp` is the inspector's contract: 50 checks
that drive the real component - they click the four tab buttons and read the
card the layout built.

- the four tabs: exactly one is on, and the tab follows the material;
- the hint: each material explains itself in the sidebar;
- the canvas: the material is the canvas's layer;
- the material card: each material's own controls, in the card's reading
  order (the caption above the control and the control's kind) - the Sample
  card holds fifteen cells, the PM card eight, the Live card five and the
  Osc card three;
- the shared controls: the Ref belongs to the Sample and the Live tab, the
  carrier wave to the Osc and the PM tab.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the visibility follows the edge, the card the layout built
is the edge's card, the tab and the hint are the edge's words, and the edge's
own table is sound.

The suite came first: it was written against the old code and drove the
real component. The move gave the inspector a description, and the checks
themselves did not change - the ones that cover the new behaviour (the edge
against the app) were added with the move.

## What is not covered yet

- **The panels are still the shell's.** The review's §28 wants the phase
  modulation, the sample material and the live input in panels of their own
  (`PhaseModulationPanel`, `SampleMaterialPanel`, `LiveInputPanel`). Their
  builders still live in `MainComponentControls.cpp`, and their widgets are
  still the shell's members. R5.6 moved the description; the panels are the
  next step.
- **The material's widgets are still the shell's members.** The description
  names the roles, and the binding names the widgets; the widgets themselves
  are still thirty-one members of `MainComponent`.
- **The card engine's rule is not the edge's.** The engine puts the plain
  controls first and the knobs after them, so the card's *reading* order is
  not the description's order. The description carries the logical order (the
  cell order), and the engine lays the card out.
- **The other cards are described now, but not by this edge.** R5.7 gave the
  sieve card, the magnets card, the page card, the pitch card's frequency
  cell and the acceleration card descriptions of their own
  (`Source/SievePanel.h`, `Source/FrequencyInspector.h`), and the way a cell
  is drawn now lives in one shared vocabulary (`Source/InspectorCell.h`).
  The voice card, the tools card and the rest are still written down in
  `MainComponent::resized()`, one branch each.
- **The fold is still the shell's.** A group the window is too narrow for
  folds into its header, and the fold is applied on top of the material's
  visibility - so a control the material shows can still be hidden by the
  fold.
