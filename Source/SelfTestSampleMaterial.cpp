#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>

//==============================================================================
// R5.10 (the review's R052, section 28): the sample material panel.
//
// The Sample tab plays the slot: the material's own level, the pitch algorithm
// that transposes it and the material's edits (the normalise, the fades and
// the reset). The tab's card shows the slot, the load button, the reverse, the
// Ref, the level, the pitch mode, the grain and the three edits; this suite is
// about what those controls mean.
//
// The review asks for the sample controls to be decomposed:
//
//   "| `buildSampleControls()`                                  | SampleMaterialPanel |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the panel does today:
//
// - the level: its domain (0..2 in steps of 0.01, skewed low), the value it
//   starts at and its tooltip;
// - the slot list: the eight slots, the item ids that go with them, and the
//   slot a selection names;
// - the pitch mode list: its four items, their ids, the mode each one selects
//   in the engine, and its tooltip;
// - the material's edits: the three buttons' words and tooltips, the
//   reverse's word and tooltip, and the extraction button's.
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
}

void upic::runSampleMaterialChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto* level   = dynamic_cast<juce::Slider*>    (findByName (mc, "levelSlider"));
    auto* pitch   = dynamic_cast<juce::ComboBox*>  (findByName (mc, "samplePitchBox"));
    auto* slot    = dynamic_cast<juce::ComboBox*>  (findByName (mc, "sampleBox"));
    auto* rev     = dynamic_cast<juce::Button*>    (findByName (mc, "revBtn"));
    auto* norm    = dynamic_cast<juce::TextButton*> (findByName (mc, "normMatBtn"));
    auto* fade    = dynamic_cast<juce::TextButton*> (findByName (mc, "fadeMatBtn"));
    auto* reset   = dynamic_cast<juce::TextButton*> (findByName (mc, "resetMatBtn"));
    auto* extract = dynamic_cast<juce::TextButton*> (findByName (mc, "sampleExtractBtn"));

    check (level != nullptr && pitch != nullptr && slot != nullptr && rev != nullptr
           && norm != nullptr && fade != nullptr && reset != nullptr && extract != nullptr,
           "R5.10 the sample panel's controls are there");

    if (level == nullptr || pitch == nullptr || slot == nullptr || rev == nullptr
        || norm == nullptr || fade == nullptr || reset == nullptr || extract == nullptr)
        return;

    // --- the level ------------------------------------------------------------
    check (std::abs (level->getMinimum() - 0.0) < 1e-9
           && std::abs (level->getMaximum() - 2.0) < 1e-9
           && std::abs (level->getInterval() - 0.01) < 1e-9
           && std::abs (level->getSkewFactor() - 0.2) < 1e-9,
           "R5.10 the level covers 0..2 in hundredths, skewed low",
           juce::String (level->getMinimum()) + ".." + juce::String (level->getMaximum())
             + " / " + juce::String (level->getInterval()) + " / " + juce::String (level->getSkewFactor()));

    check (std::abs (level->getValue() - 1.0) < 1e-9,
           "R5.10 the level starts at 1 (as loaded)", juce::String (level->getValue()));

    check (tooltipOf (level) == "The slot's level - how loud the material is "
                                "(1 = as it was loaded)\n1.00",
           "R5.10 the level says what it does", tooltipOf (level));

    // --- the slot list --------------------------------------------------------
    check (slot->getNumItems() == 8
           && slot->getItemId (0) == 1
           && slot->getItemId (7) == 8,
           "R5.10 the slot list is the eight slots, id 1..8",
           juce::String (slot->getNumItems()) + " item(s)");

    slot->setSelectedId (3, juce::dontSendNotification);
    slot->onChange();
    check (mc.selectedSampleSlotForTest() == 2,
           "R5.10 the slot list's item names the slot at index - 1",
           juce::String (mc.selectedSampleSlotForTest()));

    // --- the pitch mode list --------------------------------------------------
    check (pitch->getNumItems() == 4
           && pitch->getItemText (0) == "Varispeed (tape)"
           && pitch->getItemText (1) == "Granular (stretch)"
           && pitch->getItemText (2) == "Phase vocoder"
           && pitch->getItemText (3) == "Granular (no stretch)",
           "R5.10 the pitch mode list's items",
           pitch->getItemText (0) + ", " + pitch->getItemText (1) + ", "
             + pitch->getItemText (2) + ", " + pitch->getItemText (3));

    check (pitch->getItemId (0) == 1 && pitch->getItemId (1) == 2
           && pitch->getItemId (2) == 3 && pitch->getItemId (3) == 4,
           "R5.10 the pitch mode list's ids are 1..4",
           juce::String (pitch->getItemId (0)) + ".." + juce::String (pitch->getItemId (3)));

    check (pitch->getSelectedId() == 1,
           "R5.10 the pitch mode starts at varispeed",
           juce::String (pitch->getSelectedId()));

    check (tooltipOf (pitch) == "Sample-tab arcs: how the slot is transposed.\n"
                                "At 1x (the arc sits on Ref) the pitch is unchanged; "
                                "draw above/below Ref to transpose.\n"
                                "Varispeed and Granular (no stretch) change the slot's "
                                "playback length with pitch; Granular (stretch) fits "
                                "the slot to the arc length; Phase vocoder keeps the "
                                "slot's length and shifts pitch only.",
           "R5.10 the pitch mode says what it does", tooltipOf (pitch));

    struct Mode { int itemId; int engineMode; };
    const Mode modes[] = { { 1, 0 }, { 2, 1 }, { 3, 2 }, { 4, 3 } };
    bool everyMode = true;
    for (auto& m : modes)
    {
        pitch->setSelectedId (m.itemId, juce::dontSendNotification);
        pitch->onChange();
        if (mc.samplePitchModeForTest() != m.engineMode)
            everyMode = false;
    }
    check (everyMode,
           "R5.10 the pitch mode's item id is the engine's mode + 1",
           juce::String (mc.samplePitchModeForTest()));

    pitch->setSelectedId (1, juce::dontSendNotification);
    pitch->onChange();
    check (mc.samplePitchModeForTest() == 0,
           "R5.10 the pitch mode goes back to varispeed",
           juce::String (mc.samplePitchModeForTest()));

    // --- the material's edits -------------------------------------------------
    check (rev->getButtonText() == "Rev"
           && norm->getButtonText() == "Norm"
           && fade->getButtonText() == "Fade"
           && reset->getButtonText() == "Reset"
           && extract->getButtonText() == "Extract",
           "R5.10 the material's edits are named",
           norm->getButtonText() + ", " + fade->getButtonText() + ", " + reset->getButtonText());

    check (tooltipOf (rev) == "Play the selected slot backwards",
           "R5.10 the reverse says what it does", tooltipOf (rev));

    check (tooltipOf (norm) == "Normalize the material: the gain is set so the "
                               "loudest point of the material is just under full scale",
           "R5.10 the normalise says what it does", tooltipOf (norm));

    check (tooltipOf (fade) == "Fade the material in and out over a tenth of its "
                               "length, so the loop does not click",
           "R5.10 the fade says what it does", tooltipOf (fade));

    check (tooltipOf (reset) == "Undo the material's edits (the trim, the fades "
                                "and the normalise gain)",
           "R5.10 the reset says what it does", tooltipOf (reset));

    check (tooltipOf (extract) == "Find the repeating cycle in the selected material,\n"
                                  "preview it, and save it as a wavetable or envelope",
           "R5.10 the extraction says what it does", tooltipOf (extract));
}
