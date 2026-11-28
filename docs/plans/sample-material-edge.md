# The sample material edge

The architecture note for R5.10. It describes the boundary R5.10 introduced -
the sample material panel's description - as it is after that phase, not as it
is meant to be at the end of the refactoring.

> **R5.10 gave the sample material panel its own description.** The widgets and
> the commands are still the shell's; what moved is the knowledge of *what the
> panel's controls say about themselves*, *what the pitch list offers* and
> *which domain the level has*.

## The boundary

```
Source/SampleMaterial.h   the description: the panel's roles, every control's
                           wording, the pitch algorithm's list and its item ids,
                           the level's domain and the slot list's item ids
        |
        |  the app binds the roles to its widgets; the lists are written and
        |  read through the description's rules; the domains come from it
        v
Source/MainComponent.h            the widgets: the slot list, the load button,
Source/MainComponentControls.cpp   the reverse, the level, the pitch list and
Source/MainComponentModel.cpp      the material's three edits and the extract
Source/MainComponentLayout.cpp
```

The review asks for the sample controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildSampleControls()` | SampleMaterialPanel |

R5.10 takes the first step of that table, the same step R5.6, R5.7, R5.8 and
R5.9 took for the inspectors: the *description* and the *words* become an edge,
and the widgets and the commands stay the shell's. `buildSampleControls()` is
still `MainComponent`'s: it loads the slot, it edits the material and it writes
the undo transaction for the level. The panel itself is a later phase's
business.

The material **card** is not the edge's: where the cells go and which tab shows
them is still the material inspector's (`Source/MaterialInspector.h`, R5.6). The
edge says what the controls *mean*, not where they sit.

## The description

```cpp
namespace upic::sample_material
{
    /** A control of the sample material panel. */
    enum class Control
    {
        Slot,        // the slot selector
        LoadSample,  // load the slot's material
        Reverse,     // the slot plays backwards
        Level,       // the slot's level
        Pitch,       // how the slot is transposed
        Normalise,   // the material's gain
        Fade,        // the material's fades
        Reset,       // the material's edits
        Extract,     // the cycle extraction
        count
    };

    const std::vector<Control>& allControls();
    const char* tooltip (Control control);

    constexpr double kLevelMin = 0.0, kLevelMax = 2.0;
    constexpr double kLevelInterval = 0.01, kLevelSkew = 0.2;
    constexpr double kDefaultLevel = 1.0;

    struct PitchMode { const char* name; int mode; };
    const std::vector<PitchMode>& pitchModes();
    int pitchItemId (int mode);
    int pitchModeForItemId (int itemId);
    constexpr int kDefaultPitchItemId = 1;
    bool usesGranularTab (int itemId);

    int itemIdForSlotIndex (int index);
    int slotIndexForItemId (int itemId);
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The contract

**The words are written down once.** The controls that say something say it
here; the ones that say nothing (the slot list, the load button) return an
empty string, and the app leaves their own tooltip alone.

| the control | what it says |
|---|---|
| the reverse | "Play the selected slot backwards" |
| the level | "The slot's level - how loud the material is (1 = as it was loaded)" |
| the pitch | "Sample-tab arcs: how the slot is transposed. ..." |
| the normalise | "Normalize the material: the gain is set so the loudest point of the material is just under full scale" |
| the fade | "Fade the material in and out over a tenth of its length, so the loop does not click" |
| the reset | "Undo the material's edits (the trim, the fades and the normalise gain)" |
| the extract | "Find the repeating cycle in the selected material, preview it, and save it as a wavetable or envelope" |

**The pitch list's items and ids are one rule.** The engine's four algorithms
are the list's four items, and the item id is the engine's mode plus one (a
`ComboBox` item id has to be greater than zero):

| the item | the engine's mode |
|---|---|
| "Varispeed (tape)" | 0 |
| "Granular (stretch)" | 1 |
| "Phase vocoder" | 2 |
| "Granular (no stretch)" | 3 |

The list starts on varispeed (item 1), and the granular (stretch) mode is the
one the Sample tab shows the grain size for.

**The slot list's item ids are one rule.** The list has the piece's eight slots
in the bank's order, and the slot at the index `i` is the list's item `i + 1`.

**The level's domain is one domain.** 0..2, in steps of 0.01, skewed low so the
quiet end has most of the travel. 1 is the level the material was loaded with,
and it is the level the panel falls back to.

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
material controls are built:

```cpp
namespace sm = upic::sample_material;
bindSampleMaterialControl (sm::Control::Slot,        &slotLab,        &sampleBox);
bindSampleMaterialControl (sm::Control::LoadSample,  nullptr,         &loadSampleBtn);
bindSampleMaterialControl (sm::Control::Reverse,     nullptr,         &revBtn);
bindSampleMaterialControl (sm::Control::Level,       &levelLab,       &levelSlider);
bindSampleMaterialControl (sm::Control::Pitch,       &samplePitchLab, &samplePitchBox);
bindSampleMaterialControl (sm::Control::Normalise,   nullptr,         &normMatBtn);
bindSampleMaterialControl (sm::Control::Fade,        nullptr,         &fadeMatBtn);
bindSampleMaterialControl (sm::Control::Reset,       nullptr,         &resetMatBtn);
bindSampleMaterialControl (sm::Control::Extract,     nullptr,         &sampleExtractBtn);
```

The binding sets the widget's tooltip when the edge has words for it, and
leaves it alone when it has not.

The lists and the domains come from the edge too:

```cpp
for (auto& mode : sm::pitchModes())
    samplePitchBox.addItem (mode.name, sm::pitchItemId (mode.mode));
samplePitchBox.setSelectedId (sm::kDefaultPitchItemId);
...
const int pm = sm::pitchModeForItemId (samplePitchBox.getSelectedId());
...
levelSlider.setNormalisableRange (juce::NormalisableRange<double> (sm::kLevelMin, sm::kLevelMax,
                                                                     sm::kLevelInterval, sm::kLevelSkew));
...
sampleBox.addItem (..., upic::sample_material::itemIdForSlotIndex ((int) i));
```

## The suite

`Source/SelfTestSampleMaterial.cpp` is the panel's contract: 18 checks were
written against the old code and 26 came with the move. They drive the real
component - they pick from the dropdowns the user picks from and read the
app's engine.

They pin:

- the level: its domain (0..2 in steps of 0.01, skewed low), the value it
  starts at and its tooltip;
- the slot list: the eight slots, the item ids that go with them, and the slot
  a selection names;
- the pitch mode list: its four items, their ids, the mode each one selects in
  the engine, and its tooltip;
- the material's edits: the three buttons' words and tooltips, the reverse's
  word and tooltip, and the extraction button's.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the roles point at the panel's own widgets, the tooltips are
the edge's wording, the level covers the edge's domain, the pitch list and the
slot list follow the edge's items and ids.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a
  `SampleMaterialPanel`. `buildSampleControls()` still lives in
  `MainComponent`: it loads the slot, it edits the material and it writes the
  undo transaction for the level. R5.10 moved the description and the words;
  the panel is the next step.
- **The material's own edits are not the edge's.** The normalise's gain, the
  fades and the trim are read parameters of the model (`Source/SampleCodec.h`),
  and the commands that write them stay the shell's.
- **The card is still the material inspector's.** Where the cells go, which tab
  shows them and how wide they are are still `Source/MaterialInspector.h`.
