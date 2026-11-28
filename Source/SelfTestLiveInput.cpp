#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "LiveInput.h"

#include <algorithm>
#include <cmath>
#include <memory>

//==============================================================================
// R5.10 (the review's R052, section 28): the live input panel.
//
// The Live tab draws arcs that transpose the microphone. Its card has the
// pitch algorithm, the granular grain, the reverse's buffer and its switch;
// the input itself has the monitor, the capture and the input gain, and the
// digitised wave page has its button.
//
// The review asks for the live input controls to be decomposed:
//
//   "| `buildLiveInputControls()`                               | LiveInputPanel     |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the panel does today:
//
// - the input: the monitor's word, the capture's word and colours, and the
//   input gain's domain (0..1 in hundredths) and start value;
// - the live pitch: the three algorithms, their ids and the engine's mode,
//   and the tooltip;
// - the reverse: the three buffer lengths, their ids and the one the box
//   starts on, and the switch's and the box's tooltips;
// - the grain: the two grain sliders' domain (5..500 ms in steps of 1) and
//   the live one's tooltip;
// - the digitised wave page: the button's word and tooltip.
//
// The suite drives the real component: it picks from the dropdowns the user
// picks from and reads the app's engine.

namespace
{
    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    juce::String tooltipOf (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) return t->getTooltip();
        return {};
    }

    juce::Button* findButtonByText (juce::Component& c, const juce::String& text)
    {
        if (auto* b = dynamic_cast<juce::Button*> (&c))
            if (b->getButtonText() == text) return b;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findButtonByText (*c.getChildComponent (i), text)) return r;
        return nullptr;
    }
}

void upic::runLiveInputChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto* mon       = findButtonByText (mc, "Mon");
    auto* cap       = dynamic_cast<juce::TextButton*> (findByName (mc, "capBtn"));
    auto* gain      = dynamic_cast<juce::Slider*>    (findByName (mc, "inGainSlider"));
    auto* waveBtn   = dynamic_cast<juce::TextButton*> (findByName (mc, "waveFromSlotBtn"));
    auto* livePitch = dynamic_cast<juce::ComboBox*>  (findByName (mc, "livePitchBox"));
    auto* revLen    = dynamic_cast<juce::ComboBox*>  (findByName (mc, "liveRevLenBox"));
    auto* liveRev   = dynamic_cast<juce::Button*>    (findByName (mc, "liveRevBtn"));
    auto* liveGrain = dynamic_cast<juce::Slider*>    (findByName (mc, "liveGrainSlider"));
    auto* sampleGrain = dynamic_cast<juce::Slider*>  (findByName (mc, "grainSlider"));

    check (mon != nullptr && cap != nullptr && gain != nullptr && waveBtn != nullptr,
           "R5.10 the live input's controls are there");

    if (mon == nullptr || cap == nullptr || gain == nullptr || waveBtn == nullptr)
        return;

    // --- the input ------------------------------------------------------------
    check (mon->getButtonText() == "Mon",
           "R5.10 the monitor says Mon", mon->getButtonText());

    check (cap->getButtonText() == "Capture",
           "R5.10 the capture says Capture", cap->getButtonText());

    check (cap->findColour (juce::TextButton::buttonColourId)
             == juce::Colour (0xff2a6a3a)
           && cap->findColour (juce::TextButton::buttonOnColourId)
             == juce::Colour (0xff6a2a2a),
           "R5.10 the capture is green, red while it records",
           cap->findColour (juce::TextButton::buttonColourId).toString());

    check (std::abs (gain->getMinimum() - 0.0) < 1e-9
           && std::abs (gain->getMaximum() - 1.0) < 1e-9
           && std::abs (gain->getInterval() - 0.01) < 1e-9,
           "R5.10 the input gain covers 0..1 in hundredths",
           juce::String (gain->getMinimum()) + ".." + juce::String (gain->getMaximum()));

    check (std::abs (gain->getValue() - 0.8) < 1e-9,
           "R5.10 the input gain starts at 0.8", juce::String (gain->getValue()));

    check (tooltipOf (waveBtn) == "Turn the selected slot into a wavetable\n"
                                  "(the UPIC's digitised wave page: the take\n"
                                  "becomes one loop of the oscillator, so its\n"
                                  "pitch follows the arc).",
           "R5.10 the digitised wave page says what it does", tooltipOf (waveBtn));

    check (waveBtn->getButtonText() == "Slot -> Wave",
           "R5.10 the digitised wave page is named", waveBtn->getButtonText());

    // --- the live pitch -------------------------------------------------------
    if (livePitch != nullptr)
    {
        check (livePitch->getNumItems() == 3
               && livePitch->getItemText (0) == "Granular (smooth)"
               && livePitch->getItemText (1) == "Tape (varispeed)"
               && livePitch->getItemText (2) == "Phase vocoder",
               "R5.10 the live pitch list's items",
               livePitch->getItemText (0) + ", " + livePitch->getItemText (1) + ", "
                 + livePitch->getItemText (2));

        check (livePitch->getItemId (0) == 1 && livePitch->getItemId (1) == 2
               && livePitch->getItemId (2) == 3,
               "R5.10 the live pitch list's ids are 1..3");

        check (livePitch->getSelectedId() == 1,
               "R5.10 the live pitch starts at granular",
               juce::String (livePitch->getSelectedId()));

        check (tooltipOf (livePitch) == "Live input pitch shift for LIVE-tab arcs.\n"
                                        "Granular and Phase vocoder cover the full 1/16x - 16x range "
                                        "(like the Sample tab); Tape (varispeed) reads the mic faster "
                                        "than it arrives, so it keeps an octave up/down.",
               "R5.10 the live pitch says what it does", tooltipOf (livePitch));

        struct Mode { int itemId; int engineMode; };
        const Mode modes[] = { { 1, 0 }, { 2, 1 }, { 3, 2 } };
        bool everyMode = livePitch->getNumItems() == 3;
        for (auto& m : modes)
        {
            livePitch->setSelectedId (m.itemId, juce::dontSendNotification);
            livePitch->onChange();
            if (mc.livePitchModeForTest() != m.engineMode)
                everyMode = false;
        }
        check (everyMode,
               "R5.10 the live pitch's item id is the engine's mode + 1",
               juce::String (mc.livePitchModeForTest()));

        livePitch->setSelectedId (1, juce::dontSendNotification);
        livePitch->onChange();
    }

    // --- the reverse ----------------------------------------------------------
    if (revLen != nullptr)
    {
        check (revLen->getNumItems() == 3
               && revLen->getItemText (0) == "0.5 s"
               && revLen->getItemText (1) == "1 s"
               && revLen->getItemText (2) == "2 s",
               "R5.10 the reverse's buffer lengths",
               revLen->getItemText (0) + ", " + revLen->getItemText (1) + ", "
                 + revLen->getItemText (2));

        check (revLen->getItemId (0) == 1 && revLen->getItemId (1) == 2
               && revLen->getItemId (2) == 3,
               "R5.10 the reverse's buffer ids are 1..3");

        check (revLen->getSelectedId() == 2,
               "R5.10 the reverse starts at one second",
               juce::String (revLen->getSelectedId()));

        check (tooltipOf (revLen) == "How much of the microphone input the reverse plays back.\n"
                                     "The longer the buffer, the longer the delay.",
               "R5.10 the reverse's buffer says what it does", tooltipOf (revLen));
    }

    check (liveRev != nullptr && liveRev->getButtonText() == "Rev"
           && tooltipOf (liveRev) == "Play the microphone's last seconds backwards",
           "R5.10 the live reverse says what it does",
           liveRev != nullptr ? tooltipOf (liveRev) : juce::String());

    // --- the grain ------------------------------------------------------------
    check (sampleGrain != nullptr
           && std::abs (sampleGrain->getMinimum() - 5.0) < 1e-9
           && std::abs (sampleGrain->getMaximum() - 500.0) < 1e-9
           && std::abs (sampleGrain->getInterval() - 1.0) < 1e-9,
           "R5.10 the sample grain covers 5..500 ms in milliseconds");

    check (liveGrain != nullptr
           && std::abs (liveGrain->getMinimum() - 5.0) < 1e-9
           && std::abs (liveGrain->getMaximum() - 500.0) < 1e-9
           && std::abs (liveGrain->getInterval() - 1.0) < 1e-9
           && std::abs (liveGrain->getValue() - 40.0) < 1e-9,
           "R5.10 the live grain covers 5..500 ms and starts at 40",
           liveGrain != nullptr ? juce::String (liveGrain->getValue()) : juce::String());

    check (liveGrain != nullptr
           && tooltipOf (liveGrain) == "Granular grain size for the LIVE tab.\n"
                                       "Only affects the Granular live pitch mode.\n40 ms",
           "R5.10 the live grain says what it does",
           liveGrain != nullptr ? tooltipOf (liveGrain) : juce::String());

    // --- the app follows the edge (R5.10) -------------------------------------
    namespace li = upic::live_input;
    check (mc.liveInputControlCountForTest() == (int) li::allControls().size(),
           "R5.10 every role of the edge is bound",
           juce::String (mc.liveInputControlCountForTest()) + " / "
             + juce::String ((int) li::allControls().size()));

    bool rolesBound = true;
    for (auto role : li::allControls())
        if (mc.liveInputControlForTest ((int) role) == nullptr) rolesBound = false;
    check (rolesBound, "R5.10 every role of the edge has a widget");

    check (mc.liveInputControlForTest ((int) li::Control::Capture) == cap
           && mc.liveInputControlForTest ((int) li::Control::InputGain) == gain
           && mc.liveInputControlForTest ((int) li::Control::WaveFromSlot) == waveBtn
           && mc.liveInputControlForTest ((int) li::Control::Pitch) == livePitch
           && mc.liveInputControlForTest ((int) li::Control::Grain) == liveGrain,
           "R5.10 the roles point at the panel's own widgets");

    bool edgeTooltips = true;
    for (auto role : li::allControls())
    {
        const char* tip = li::tooltip (role);
        if (tip == nullptr || *tip == '\0') continue;
        auto* control = mc.liveInputControlForTest ((int) role);
        edgeTooltips = edgeTooltips && control != nullptr
                       && tooltipOf (control).startsWith (tip);
    }
    check (edgeTooltips, "R5.10 the panel's words follow the edge");

    check (std::abs (gain->getMinimum() - li::kInputGainMin) < 1e-9
           && std::abs (gain->getMaximum() - li::kInputGainMax) < 1e-9
           && std::abs (gain->getInterval() - li::kInputGainInterval) < 1e-9
           && std::abs (gain->getValue() - li::kDefaultInputGain) < 1e-9
           && cap->getButtonText() == li::captureText(),
           "R5.10 the input's gain and the capture follow the edge");

    bool edgePitch = livePitch != nullptr
                     && livePitch->getNumItems() == (int) li::pitchModes().size();
    for (int i = 0; livePitch != nullptr && i < (int) li::pitchModes().size()
                    && i < livePitch->getNumItems(); ++i)
        if (livePitch->getItemText (i) != li::pitchModes()[(size_t) i].name
            || livePitch->getItemId (i) != li::pitchItemId (li::pitchModes()[(size_t) i].mode)
            || li::pitchModeForItemId (livePitch->getItemId (i)) != li::pitchModes()[(size_t) i].mode)
            edgePitch = false;
    check (edgePitch, "R5.10 the live pitch list follows the edge's items and ids");
    check (livePitch != nullptr && livePitch->getSelectedId() == li::kDefaultPitchItemId,
           "R5.10 the live pitch starts on the edge's item");

    bool edgeLengths = revLen != nullptr
                       && revLen->getNumItems() == (int) li::reverseLengths().size();
    for (int i = 0; revLen != nullptr && i < (int) li::reverseLengths().size()
                    && i < revLen->getNumItems(); ++i)
        if (revLen->getItemText (i) != li::reverseLengths()[(size_t) i].name
            || revLen->getItemId (i) != li::reverseLengthItemId (i)
            || li::reverseLengthForItemId (revLen->getItemId (i))
                  != li::reverseLengths()[(size_t) i].seconds)
            edgeLengths = false;
    check (edgeLengths, "R5.10 the reverse's buffer list follows the edge");
    check (revLen != nullptr && revLen->getSelectedId() == li::kDefaultReverseLengthItemId,
           "R5.10 the reverse starts on the edge's item");

    check (liveGrain != nullptr && sampleGrain != nullptr
           && std::abs (liveGrain->getMinimum() - li::kGrainMinMs) < 1e-9
           && std::abs (liveGrain->getMaximum() - li::kGrainMaxMs) < 1e-9
           && std::abs (liveGrain->getInterval() - li::kGrainIntervalMs) < 1e-9
           && std::abs (liveGrain->getValue() - li::kDefaultGrainMs) < 1e-9
           && std::abs (sampleGrain->getMinimum() - li::kGrainMinMs) < 1e-9
           && std::abs (sampleGrain->getMaximum() - li::kGrainMaxMs) < 1e-9
           && std::abs (sampleGrain->getInterval() - li::kGrainIntervalMs) < 1e-9,
           "R5.10 both grain sliders follow the edge's domain");
}
