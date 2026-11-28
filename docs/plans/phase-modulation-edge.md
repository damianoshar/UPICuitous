# The phase modulation edge

The architecture note for R5.10. It describes the boundary R5.10 introduced -
the phase modulation panel's description - as it is after that phase, not as it
is meant to be at the end of the refactoring.

> **R5.10 gave the phase modulation panel its own description.** The widgets and
> the commands are still the shell's; what moved is the knowledge of *what the
> panel's controls are*, *what the operator counts are*, *what the six presets
> write* and *which domain the three knobs have*.

## The boundary

```
Source/PhaseModulation.h   the description: the panel's roles, the two operator
                           counts, the six presets and the voice each one
                           writes, the three knobs' domains and start values
        |
        |  the app binds the roles to its widgets; the two lists are written
        |  and read through the description's rules; the domains come from it
        v
Source/MainComponent.h            the widgets: the ops list, the preset list,
Source/MainComponentControls.cpp   the ratio, the index, the index's envelope,
Source/MainComponentModel.cpp      the feedback, the import and the export
```

The review asks for the phase modulation controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildPmControls()` | PhaseModulationPanel |

R5.10 takes the first step of that table, the same step R5.6, R5.7, R5.8 and
R5.9 took for the inspectors: the *description* and the *words* become an edge,
and the widgets and the commands stay the shell's. `buildPmControls()` is still
`MainComponent`'s: it writes the selected arc, it writes the current voice and
it reads and writes the preset files. The panel itself is a later phase's
business.

The material **card** is not the edge's: where the PM cells go and which tab
shows them is still the material inspector's (`Source/MaterialInspector.h`,
R5.6). The edge says what the controls *mean*, not where they sit.

## The description

```cpp
namespace upic::phase_modulation
{
    /** A control of the phase modulation panel. */
    enum class Control
    {
        Ops,       // the operator count
        Preset,    // the preset library
        Ratio,     // the modulator's ratio
        Index,     // the modulation index
        IndexEnv,  // the envelope that shapes the index
        Feedback,  // the modulator's feedback
        Import,    // a preset file
        Export,    // a preset file
        count
    };

    const std::vector<Control>& allControls();
    const char* tooltip (Control control);

    constexpr double kRatioMin = 0.1, kRatioMax = 32.0;
    constexpr double kRatioInterval = 0.01, kRatioSkew = 0.4;
    constexpr double kDefaultRatio = 2.0;

    constexpr double kIndexMin = 0.0, kIndexMax = 20.0;
    constexpr double kIndexInterval = 0.01, kIndexSkew = 0.4;
    constexpr double kDefaultIndex = 1.5;

    constexpr double kFeedbackMin = 0.0, kFeedbackMax = 1.0;
    constexpr double kFeedbackInterval = 0.001, kFeedbackSkew = 0.4;
    constexpr double kDefaultFeedback = 0.0;

    struct Ops { const char* name; int count; };
    const std::vector<Ops>& opsCounts();
    int opsItemId (int count);
    int opsCountForItemId (int itemId);

    constexpr int kManualItemId = 1;
    const char* manualName();
    struct Preset
    {
        int itemId;
        const char* name;
        double ratio, index;
        int indexEnv;
        double feedback;
    };
    const std::vector<Preset>& presets();
    bool isManualItem (int itemId);
    const Preset* presetForItemId (int itemId);
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The contract

**The operator counts are one rule.** The box offers two counts, and the piece
carries the count itself (2 or 4):

| the item | the piece's count | the box's item id |
|---|---|---|
| "2-op" | 2 | 1 |
| "4-op" | 4 | 2 |

Before, the rule was "the id is 2 ? 4 : 2" at three call sites - where the
count was read for the dropdown, where it was written for the current voice and
where it was written for a new arc. Nothing made the three copies agree.

**The presets are one table.** The list's first item is manual: it writes
nothing, it only leaves the voice where the user put it. The five presets
follow, and each one writes the ratio, the index, the index's envelope and the
feedback:

| the item | the ratio | the index | the envelope | the feedback |
|---|---|---|---|---|
| "(manual)" | - | - | - | - |
| "Bell (2.0x, percussive)" | 2.0 | 2.5 | 1 | 0.0 |
| "Brass (1.0x, flat)" | 1.0 | 1.0 | 0 | 0.1 |
| "Pluck (3.0x, percussive)" | 3.0 | 4.0 | 1 | 0.0 |
| "Organ (1.0x, sustain, fb)" | 1.0 | 0.5 | 0 | 0.3 |
| "Metallic (1.414x, perc, fb)" | 1.414 | 5.0 | 1 | 0.2 |

The envelope is the envelope's id, and 0 is the flat amp envelope. Before, the
five rows were if-chains (`if (sel == 2) { ratio = 2.0; idx = 2.5; envId = 1;
fb = 0.0; }` and so on) that wrote four locals - a new preset meant a new
if-branch, and the list's item text was a second copy of the same names.

**The voice's domains are three domains.** The ratio and the index are skewed
low, so the small values (where the timbre changes most) get most of the
travel:

| the knob | the domain | the step | the skew | the start |
|---|---|---|---|---|
| the ratio | 0.1..32 | 0.01 | 0.4 | 2.0 |
| the index | 0..20 | 0.01 | 0.4 | 1.5 |
| the feedback | 0..1 | 0.001 | 0.4 | 0.0 |

**The panel says nothing.** The PM controls have no tooltips of their own
today, so `tooltip()` returns an empty string for every role and the app
leaves the widgets alone. The function stays so the suite can pin the silence
and so a later phase has one place to put the words.

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
PM controls are built:

```cpp
namespace pm = upic::phase_modulation;
bindPhaseModulationControl (pm::Control::Ops,      &pmOpsLab,       &pmOpsBox);
bindPhaseModulationControl (pm::Control::Preset,   &pmPresetLab,    &pmPresetBox);
bindPhaseModulationControl (pm::Control::Ratio,    &pmRatioLab,     &pmRatioSlider);
bindPhaseModulationControl (pm::Control::Index,    &pmIndexLab,     &pmIndexSlider);
bindPhaseModulationControl (pm::Control::IndexEnv, &pmIndexEnvLab,  &pmIndexEnvBox);
bindPhaseModulationControl (pm::Control::Feedback, &pmFeedbackLab,  &pmFeedbackSlider);
bindPhaseModulationControl (pm::Control::Import,   nullptr,         &pmImportBtn);
bindPhaseModulationControl (pm::Control::Export,   nullptr,         &pmExportBtn);
```

The lists and the domains come from the edge too:

```cpp
pmPresetBox.addItem (pm::manualName(), pm::kManualItemId);
for (auto& preset : pm::presets())
    pmPresetBox.addItem (preset.name, preset.itemId);
...
for (auto& ops : pm::opsCounts())
    pmOpsBox.addItem (ops.name, pm::opsItemId (ops.count));
...
const int ops = pm::opsCountForItemId (pmOpsBox.getSelectedId());
...
const auto* preset = pm::presetForItemId (sel);
```

## The suite

`Source/SelfTestPhaseModulation.cpp` is the panel's contract: 17 checks were
written against the old code and 26 came with the move. They drive the real
component - they pick from the dropdowns the user picks from and read the
app's piece.

They pin:

- the ops list: the two items, their ids and the one it starts on;
- the preset list: the six items, their ids and the manual item it starts on;
- the voice: the three domains, their steps, their skews and their start
  values;
- the preset files: the import's and the export's words;
- the PM arc: a piece with a PM arc loads, the arc is selectable on the PM
  layer, every preset sets the ratio, the index, the envelope and the
  feedback, the manual item leaves the voice alone, and the ops box writes the
  arc's operator count (2 or 4).

The move added the checks that pin the edge against the app: every role is
bound to a widget, the roles point at the panel's own widgets, the tooltips are
the edge's wording (the silence), the ops list and the preset list follow the
edge's items and ids, the voice's domains follow the edge, and each preset
writes the edge's values.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a
  `PhaseModulationPanel`. `buildPmControls()` still lives in `MainComponent`:
  it writes the selected arc, it writes the current voice and it reads and
  writes the preset files. R5.10 moved the description and the words; the
  panel is the next step.
- **The preset does not open an undo transaction.** Picking a preset writes
  the selected PM arc directly, without `runEdit()`, so the voice is not
  undoable. R5.10 pins that as it is - the phase moves the words, it does not
  change the commands - and the suite records it. Making the preset undoable
  is a behaviour change and belongs to its own phase.
- **The index's envelope list is not the edge's.** The list holds the piece's
  envelopes, and their ids come from the model; the edge only says that a
  preset's envelope is an envelope id.
- **The card is still the material inspector's.** Where the PM cells go, which
  tab shows them and how wide they are are still `Source/MaterialInspector.h`.
