# The MIDI settings edge

The architecture note for R5.10. It describes the boundary R5.10 introduced -
the MIDI settings section's description - as it is after that phase, not as it
is meant to be at the end of the refactoring.

> **R5.10 gave the MIDI settings section its own description.** The widgets and
> the commands are still the shell's; what moved is the knowledge of *what the
> section says*, *what each learn row is called*, *how a binding reads* and
> *which item is which MIDI device*.

## The boundary

```
Source/MidiSettings.h   the description: the four learn rows, the device row's
                        caption and tooltip, the activity line's first text, the
                        learn's words, the binding's text format and the device
                        list's item ids
        |
        |  the app writes and reads the section through the description's
        |  rules: the labels take their text from it, the learn's buttons take
        |  their word from it, the binding's text is formatted by it and the
        |  device list's ids are its rule
        v
Source/MainComponent.h            the widgets: the device list, the activity
Source/MainComponentControls.cpp   line, the four captions, the four Learn
Source/MainComponentMidi.cpp       buttons and the four binding labels
```

The review asks for the MIDI controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildMidiControls()` | MidiSettingsPanel |

R5.10 takes the first step of that table, the same step R5.6, R5.7, R5.8 and
R5.9 took for the inspectors: the *description* and the *words* become an edge,
and the widgets and the commands stay the shell's. `buildMidiControls()` is
still `MainComponent`'s: it opens the device, it starts the learn and it drives
the parameter the binding names. The panel itself is a later phase's business.

## The description

```cpp
namespace upic::midi_settings
{
    /** The four things the MIDI learn can drive. */
    enum class Target
    {
        MasterGain = 0,   // the master gain
        Intensity = 1,    // the drawing's intensity
        Scrub = 2,        // the scrub (the jog)
        Transport = 3,    // the transport
        count
    };

    /** One learn row: what it drives and what it is captioned. */
    struct Row
    {
        Target target = Target::MasterGain;
        const char* caption = "";
    };

    const std::vector<Row>& rows();
    const Row* rowForTarget (Target target);

    const char* deviceCaption();   // "MIDI in"
    const char* deviceTooltip();   // "The MIDI input the app listens to"
    const char* noActivityText();  // "no MIDI yet"

    const char* learnText();       // "Learn"
    const char* cancelText();      // "Cancel"
    const char* unboundText();     // "-"
    const char* learningText();    // "learning..."
    std::string bindingText (bool note, int number, int channel);

    constexpr int kNoneItemId = 1;
    const char* noneItemName();    // "None"
    int itemIdForDeviceIndex (int index);
    int deviceIndexForItemId (int itemId);
    bool isNoneItem (int itemId);
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The contract

**The four rows are one table.** The section has four learn rows - the master
gain, the draw intensity, the scrub and the transport - and the edge names them
in one place:

| the row | what it drives | what it is captioned |
|---|---|---|
| the master gain | the master volume | "Master gain" |
| the intensity | the drawing's intensity | "Intensity" |
| the scrub | the jog | "Scrub" |
| the transport | the play and the stop | "Transport" |

**The words are written down once.** The section says what it is, what the
learn says and what a binding reads:

| the thing | what it says |
|---|---|
| the device row | "MIDI in" |
| the device list | "The MIDI input the app listens to" |
| the activity line (before anything arrived) | "no MIDI yet" |
| the learn button | "Learn", and "Cancel" while it learns |
| the binding (nothing bound) | "-" |
| the binding (learning) | "learning..." |
| the binding (a CC) | "CC 7 ch 3" |
| the binding (a note) | "note 60 ch 1" |

The channel in the binding's text is the user's channel: the hub's own index
(0..15) plus one.

**The device list's item ids are one rule.** None is the list's first item, and
the devices follow it:

- None is item 1;
- the device at the hub's index `i` is item `i + 2`;
- an item id reads back to the hub's index with `item - 2`.

Before, the ids were written down twice: once where the list was built
("None", 1, `i + 2`) and once where it was read (`id <= 1`, `id - 2`). Nothing
made the two copies agree.

## The binding

The app owns the widgets, so the app asks the edge what to say:

```cpp
namespace ms = upic::midi_settings;

midiDevLab.setText (ms::deviceCaption(), juce::dontSendNotification);
midiActLab.setText (ms::noActivityText(), juce::dontSendNotification);
{
    juce::Label* captions[] = { &midiGainLab, &midiIntLab, &midiScrubLab, &midiTransportLab };
    for (auto& row : ms::rows())
        captions[(int) row.target]->setText (row.caption, juce::dontSendNotification);
}
midiDevBox.setTooltip (ms::deviceTooltip());
```

The learn's words and the binding's text follow the hub's state:

```cpp
auto describe = [this] (MidiHub::Target t) -> juce::String
{
    if (midi.isLearning() && midi.learningTarget() == t)
        return ms::learningText();
    const auto b = midi.binding (t);
    if (! b.bound) return ms::unboundText();
    return juce::String (ms::bindingText (b.note, b.number, b.channel).c_str());
};
```

and the device list is written and read through the edge's rule:

```cpp
midiDevBox.addItem (ms::noneItemName(), ms::kNoneItemId);
...
midiDevBox.addItem (infos[i].name, ms::itemIdForDeviceIndex (i));
...
const int devIndex = ms::deviceIndexForItemId (id);
const juce::String devId = (ms::isNoneItem (id)
                             || devIndex < 0 || devIndex >= midiDeviceIds.size())
                            ? juce::String() : midiDeviceIds[(size_t) devIndex];
```

## The suite

`Source/SelfTestMidiSettings.cpp` is the section's contract: 23 checks were
written against the old code and 28 came with the move. They drive the real
component - they press the Learn buttons, inject the hub's messages and read
the labels the user reads.

They pin:

- the section: the device row's caption and tooltip, the activity line's first
  text, the four captions and the four binding labels;
- the device list: "None" is the first item, and its id is the edge's;
- the learn: the buttons say "Learn", the wired ones start a learn on their
  own target and say "Cancel" while they do, the row's binding says
  "learning...", and the buttons say "Learn" again after the cancel;
- the four rows: each of the four bindings follows the hub's learning state,
  and the two wired buttons are the gain's and the transport's;
- the binding's text: a CC and a note, with the app's channel index shown as
  the user's channel;
- the cleared binding.

The move added the checks that pin the edge against the app: the device row
follows the edge, the four captions follow the edge's rows, the learn's words
follow the edge, the binding's text follows the edge and the device item ids
follow the edge's rule.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a
  `MidiSettingsPanel`. `buildMidiControls()` still lives in `MainComponent`:
  it opens the device, it starts the learn and it drives the parameter. R5.10
  moved the description and the words; the panel is the next step.
- **Two of the four Learn buttons are not wired.** The master gain's and the
  transport's buttons have a handler; the intensity's and the scrub's do not
  (their `onClick` is empty), so pressing them does nothing today. R5.10 pins
  that as it is - the phase moves the words, it does not add handlers - and the
  suite records it. Wiring them is a behaviour change and belongs to its own
  phase.
- **The hub is not the edge's.** The binding's *storage*, the learn's state and
  the device's identifier belong to `Source/MidiHub.h`; the edge only says how
  they read.
