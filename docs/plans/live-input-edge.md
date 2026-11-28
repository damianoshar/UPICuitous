# The live input edge

The architecture note for R5.10. It describes the boundary R5.10 introduced -
the live input panel's description - as it is after that phase, not as it is
meant to be at the end of the refactoring.

> **R5.10 gave the live input panel its own description.** The widgets and the
> commands are still the shell's; what moved is the knowledge of *what the
> panel's controls say about themselves*, *what the pitch and the reverse offer*
> and *which domain the input gain and the grain have*.

## The boundary

```
Source/LiveInput.h   the description: the panel's roles, every control's
                     wording, the capture's two words, the input gain's domain,
                     the grain's domain, the live pitch's list and the reverse's
                     buffer list
        |
        |  the app binds the roles to its widgets; the lists are written and
        |  read through the description's rules; the domains come from it
        v
Source/MainComponent.h            the widgets: the monitor, the capture, the
Source/MainComponent.cpp           input gain, the wave page, the pitch list, the
Source/MainComponentControls.cpp   grain and the reverse
Source/MainComponentInput.cpp
```

The review asks for the live input controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `buildLiveInputControls()` | LiveInputPanel |

R5.10 takes the first step of that table, the same step R5.6, R5.7, R5.8 and
R5.9 took for the inspectors: the *description* and the *words* become an edge,
and the widgets and the commands stay the shell's. `buildLiveInputControls()`
and `buildLiveControls()` are still `MainComponent`'s: they prepare the input,
they capture, they edit the pitch and they write the reverse's undo
transaction. The panel itself is a later phase's business.

The Live tab is two pieces: the **input** (the monitor, the capture, the gain
and the wave page - `buildLiveInputControls()`) and the **editor** (the pitch
algorithm, the grain and the reverse - `buildLiveControls()`). The edge
describes both, because they are one panel to the user; the review's row names
the first one.

## The description

```cpp
namespace upic::live_input
{
    /** A control of the live input panel. */
    enum class Control
    {
        Monitor,        // the input is monitored
        Capture,        // the input is captured into a slot
        InputGain,      // the input's gain
        WaveFromSlot,   // the slot becomes a wavetable
        Pitch,          // how the mic is transposed
        Grain,          // the granular grain size
        Reverse,        // the last seconds play backwards
        ReverseLength,  // how many seconds the reverse keeps
        count
    };

    const std::vector<Control>& allControls();
    const char* tooltip (Control control);

    const char* captureText();   // "Capture"
    const char* stopText();      // "Stop"

    constexpr double kInputGainMin = 0.0, kInputGainMax = 1.0;
    constexpr double kInputGainInterval = 0.01, kDefaultInputGain = 0.8;

    constexpr double kGrainMinMs = 5.0, kGrainMaxMs = 500.0;
    constexpr double kGrainIntervalMs = 1.0, kDefaultGrainMs = 40.0;

    struct PitchMode { const char* name; int mode; };
    const std::vector<PitchMode>& pitchModes();
    int pitchItemId (int mode);
    int pitchModeForItemId (int itemId);
    constexpr int kDefaultPitchItemId = 1;

    struct ReverseLength { const char* name; double seconds; };
    const std::vector<ReverseLength>& reverseLengths();
    int reverseLengthItemId (int index);
    double reverseLengthForItemId (int itemId);
    constexpr int kDefaultReverseLengthItemId = 2;
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The contract

**The words are written down once.** The controls that say something say it
here; the ones that say nothing (the monitor, the capture, the input gain)
return an empty string, and the app leaves their own tooltip alone.

| the control | what it says |
|---|---|
| the wave page | "Turn the selected slot into a wavetable (the UPIC's digitised wave page: the take becomes one loop of the oscillator, so its pitch follows the arc)." |
| the pitch | "Live input pitch shift for LIVE-tab arcs. ..." |
| the grain | "Granular grain size for the LIVE tab. Only affects the Granular live pitch mode." |
| the reverse | "Play the microphone's last seconds backwards" |
| the reverse's buffer | "How much of the microphone input the reverse plays back. The longer the buffer, the longer the delay." |

**The capture says two words.** "Capture" while it waits and "Stop" while it
records; the app never writes either one now.

**The input gain's domain is one domain.** 0..1, in steps of 0.01. 0.8 is what
the panel starts on.

**The grain's domain is one domain.** 5..500 ms, in steps of 1 ms - the same
range the Sample tab's grain has, and the live one starts at 40 ms (the hop the
granular shifter used before the control existed).

**The live pitch's items and ids are one rule.** The engine's three algorithms
are the list's three items, and the item id is the engine's mode plus one:

| the item | the engine's mode |
|---|---|
| "Granular (smooth)" | 0 |
| "Tape (varispeed)" | 1 |
| "Phase vocoder" | 2 |

The list starts on granular (item 1).

**The reverse's buffer lengths are one rule.** The list offers three lengths -
0.5 s, 1 s and 2 s - and the item id is the index plus one. The list starts on
one second (item 2), and an id the list does not carry reads as one second -
the delay the reverse has always had.

Before, the items and the ids were written down twice: once where the lists
were built ("Granular (smooth)", "Tape (varispeed)", "Phase vocoder", 1, 2, 3;
"0.5 s", "1 s", "2 s", 1, 2, 3) and once where they were read
("`id - 1`", "`id == 1 ? 0.5 : id == 3 ? 2.0 : 1.0`").

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
material controls are built:

```cpp
namespace li = upic::live_input;
bindLiveInputControl (li::Control::Monitor,        nullptr,       &monBtn);
bindLiveInputControl (li::Control::Capture,        nullptr,       &capBtn);
bindLiveInputControl (li::Control::InputGain,      &inGainLab,    &inGainSlider);
bindLiveInputControl (li::Control::WaveFromSlot,   nullptr,       &waveFromSlotBtn);
bindLiveInputControl (li::Control::Pitch,          &livePitchLab, &livePitchBox);
bindLiveInputControl (li::Control::Grain,          &liveGrainLab, &liveGrainSlider);
bindLiveInputControl (li::Control::Reverse,        nullptr,       &liveRevBtn);
bindLiveInputControl (li::Control::ReverseLength,  &liveRevLab,   &liveRevLenBox);
```

The lists and the domains come from the edge too:

```cpp
for (auto& mode : li::pitchModes())
    livePitchBox.addItem (mode.name, li::pitchItemId (mode.mode));
livePitchBox.setSelectedId (li::kDefaultPitchItemId, juce::dontSendNotification);
...
revBufSec = li::reverseLengthForItemId (liveRevLenBox.getSelectedId());
...
liveGrainSlider.setRange (li::kGrainMinMs, li::kGrainMaxMs, li::kGrainIntervalMs);
...
inGainSlider.setRange (li::kInputGainMin, li::kInputGainMax, li::kInputGainInterval);
inGainSlider.setValue (li::kDefaultInputGain);
```

## The suite

`Source/SelfTestLiveInput.cpp` is the panel's contract: 21 checks were written
against the old code and 31 came with the move. They drive the real component -
they pick from the dropdowns the user picks from and read the app's engine.

They pin:

- the input: the monitor's word, the capture's word and colours, and the input
  gain's domain (0..1 in hundredths) and start value;
- the live pitch: the three algorithms, their ids and the engine's mode, and
  the tooltip;
- the reverse: the three buffer lengths, their ids and the one the box starts
  on, and the switch's and the box's tooltips;
- the grain: the two grain sliders' domain (5..500 ms in steps of 1) and the
  live one's tooltip;
- the digitised wave page: the button's word and tooltip.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the roles point at the panel's own widgets, the tooltips are
the edge's wording, the input's gain and the capture follow the edge, the
pitch list and the reverse's buffer list follow the edge's items and ids, and
both grain sliders follow the edge's domain.

## What is not covered yet

- **The panel is still the shell's.** The review's §28 wants a
  `LiveInputPanel`. `buildLiveInputControls()` and `buildLiveControls()` still
  live in `MainComponent`: they prepare the input, they capture and they write
  the undo transactions. R5.10 moved the description and the words; the panel
  is the next step.
- **The input's device rules are not the edge's.** The sample rate, the
  capture's limits and the audio device's own state belong to the audio setup
  (`Source/MainComponentAudio.cpp`), not to the panel's description.
- **The card is still the material inspector's.** Where the cells go, which tab
  shows them and how wide they are are still `Source/MaterialInspector.h`.
