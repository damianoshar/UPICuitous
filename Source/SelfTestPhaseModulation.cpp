#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>

//==============================================================================
// R5.10 (the review's R052, section 28): the phase modulation panel.
//
// The PM tab's arcs modulate the carrier's phase: the ops, the preset, the
// ratio, the index, the index's envelope, the feedback and the preset files.
//
// The review asks for the phase modulation controls to be decomposed:
//
//   "| `buildPmControls()`                                      | PhaseModulationPanel |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the panel does today:
//
// - the ops: the two counts, their ids and the count each one names;
// - the presets: the six items, their ids, and the ratio, index, index
//   envelope and feedback each preset writes;
// - the voice: the three knobs' domains and the values the tab starts on;
// - the preset files: the two buttons' words;
// - the selected arc: a preset writes the arc, and the ops box writes the
//   arc's ops.
//
// The suite drives the real component: it picks from the dropdowns the user
// picks from and reads the piece the app saves.

namespace
{
    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    juce::ComboBox* findComboByFirstItem (juce::Component& c, const juce::String& first)
    {
        if (auto* b = dynamic_cast<juce::ComboBox*> (&c))
            if (b->getNumItems() > 0 && b->getItemText (0) == first) return b;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findComboByFirstItem (*c.getChildComponent (i), first)) return r;
        return nullptr;
    }

    // The PM buttons are direct children of the component (the library's
    // Import button lives deeper, inside its editor).
    juce::TextButton* findDirectButton (juce::Component& c, const juce::String& text)
    {
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* b = dynamic_cast<juce::TextButton*> (c.getChildComponent (i)))
                if (b->getButtonText() == text) return b;
        return nullptr;
    }

    int arcPropertyInt (const juce::var& piece, int arcIndex, const char* name)
    {
        auto* pages = piece.getProperty ("pages", juce::var()).getArray();
        if (pages == nullptr || pages->isEmpty()) return -1;
        auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray();
        if (arcs == nullptr || arcIndex < 0 || arcIndex >= arcs->size()) return -1;
        return (int) arcs->getReference (arcIndex).getProperty (name, -1);
    }

    double arcPropertyDouble (const juce::var& piece, int arcIndex, const char* name)
    {
        auto* pages = piece.getProperty ("pages", juce::var()).getArray();
        if (pages == nullptr || pages->isEmpty()) return -1.0;
        auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray();
        if (arcs == nullptr || arcIndex < 0 || arcIndex >= arcs->size()) return -1.0;
        return (double) arcs->getReference (arcIndex).getProperty (name, -1.0);
    }
}

void upic::runPhaseModulationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto* ops      = dynamic_cast<juce::ComboBox*>  (findByName (mc, "pmOpsBox"));
    auto* preset   = dynamic_cast<juce::ComboBox*>  (findByName (mc, "pmPresetBox"));
    auto* ratio    = dynamic_cast<juce::Slider*>    (findByName (mc, "pmRatioSlider"));
    auto* index    = dynamic_cast<juce::Slider*>    (findByName (mc, "pmIndexSlider"));
    auto* feedback = dynamic_cast<juce::Slider*>    (findByName (mc, "pmFeedbackSlider"));
    auto* indexEnv = findComboByFirstItem (mc, "(amp env)");
    auto* import   = findDirectButton (mc, "Import");
    auto* exportBtn= findDirectButton (mc, "Export");

    check (ops != nullptr && preset != nullptr && ratio != nullptr && index != nullptr
           && feedback != nullptr && indexEnv != nullptr
           && import != nullptr && exportBtn != nullptr,
           "R5.10 the PM panel's controls are there");

    if (ops == nullptr || preset == nullptr || ratio == nullptr || index == nullptr
        || feedback == nullptr || indexEnv == nullptr
        || import == nullptr || exportBtn == nullptr)
        return;

    // --- the ops --------------------------------------------------------------
    check (ops->getNumItems() == 2
           && ops->getItemText (0) == "2-op"
           && ops->getItemText (1) == "4-op",
           "R5.10 the ops list is 2-op and 4-op",
           ops->getItemText (0) + ", " + ops->getItemText (1));

    check (ops->getItemId (0) == 1 && ops->getItemId (1) == 2
           && ops->getSelectedId() == 1,
           "R5.10 the ops list starts at 2-op",
           juce::String (ops->getSelectedId()));

    // --- the presets ----------------------------------------------------------
    check (preset->getNumItems() == 6
           && preset->getItemText (0) == "(manual)"
           && preset->getItemText (1) == "Bell (2.0x, percussive)"
           && preset->getItemText (2) == "Brass (1.0x, flat)"
           && preset->getItemText (3) == "Pluck (3.0x, percussive)"
           && preset->getItemText (4) == "Organ (1.0x, sustain, fb)"
           && preset->getItemText (5) == "Metallic (1.414x, perc, fb)",
           "R5.10 the preset list's items",
           preset->getItemText (0) + ", " + preset->getItemText (1) + ", "
             + preset->getItemText (2) + ", " + preset->getItemText (3) + ", "
             + preset->getItemText (4) + ", " + preset->getItemText (5));

    check (preset->getItemId (0) == 1 && preset->getItemId (1) == 2
           && preset->getItemId (2) == 3 && preset->getItemId (3) == 4
           && preset->getItemId (4) == 5 && preset->getItemId (5) == 6
           && preset->getSelectedId() == 1,
           "R5.10 the preset list's ids are 1..6 and it starts manual",
           juce::String (preset->getSelectedId()));

    // --- the voice ------------------------------------------------------------
    check (std::abs (ratio->getMinimum() - 0.1) < 1e-9
           && std::abs (ratio->getMaximum() - 32.0) < 1e-9
           && std::abs (ratio->getInterval() - 0.01) < 1e-9
           && std::abs (ratio->getSkewFactor() - 0.4) < 1e-9
           && std::abs (ratio->getValue() - 2.0) < 1e-9,
           "R5.10 the ratio covers 0.1..32 and starts at 2",
           juce::String (ratio->getValue()));

    check (std::abs (index->getMinimum() - 0.0) < 1e-9
           && std::abs (index->getMaximum() - 20.0) < 1e-9
           && std::abs (index->getInterval() - 0.01) < 1e-9
           && std::abs (index->getSkewFactor() - 0.4) < 1e-9
           && std::abs (index->getValue() - 1.5) < 1e-9,
           "R5.10 the index covers 0..20 and starts at 1.5",
           juce::String (index->getValue()));

    check (std::abs (feedback->getMinimum() - 0.0) < 1e-9
           && std::abs (feedback->getMaximum() - 1.0) < 1e-9
           && std::abs (feedback->getInterval() - 0.001) < 1e-9
           && std::abs (feedback->getSkewFactor() - 0.4) < 1e-9
           && std::abs (feedback->getValue() - 0.0) < 1e-9,
           "R5.10 the feedback covers 0..1 and starts at 0",
           juce::String (feedback->getValue()));

    check (import->getButtonText() == "Import"
           && exportBtn->getButtonText() == "Export",
           "R5.10 the preset files are Import and Export",
           import->getButtonText() + " / " + exportBtn->getButtonText());

    // --- a preset writes the voice and the selected arc -----------------------
    check (mc.loadFromVar (juce::JSON::parse (
               "{\"pages\":[{\"duration\":4,\"nextArcId\":2,\"arcs\":["
               "{\"id\":1,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,"
               "\"pmFeedback\":0.25,\"pmOps\":2,"
               "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]}]}]}")),
           "R5.10 the PM piece loads");

    // the score's layer is the material tab: a PM arc is only selectable on
    // the PM layer (ScoreEditor::setSelectedArcs filters by the layer)
    auto* sc = dynamic_cast<ScoreEditor*> (findByName (mc, "score"));
    check (sc != nullptr, "R5.10 the score editor is there");
    if (sc != nullptr) sc->setLayer (3);

    mc.selectArcsAndSyncForTest ({ 1 });
    check (mc.selectedArcCountForTest() == 1,
           "R5.10 the PM arc is selected on the PM layer",
           juce::String (mc.selectedArcCountForTest()));

    struct PresetCase
    {
        int itemId;
        double ratio;
        double index;
        int indexEnvItem;
        double feedback;
    };
    // the index envelope ids: env 0 is "Flat" (item 2), env 1 is
    // "Percussive" (item 3) - the bank's own order + 2
    const PresetCase cases[] =
    {
        { 2, 2.0,   2.5, 3, 0.0 },   // Bell: percussive, no feedback
        { 3, 1.0,   1.0, 2, 0.1 },   // Brass: flat, a little feedback
        { 4, 3.0,   4.0, 3, 0.0 },   // Pluck: percussive
        { 5, 1.0,   0.5, 2, 0.3 },   // Organ: flat, sustained feedback
        { 6, 1.41,  5.0, 3, 0.2 }    // Metallic: percussive, feedback (1.414 snaps to 1.41)
    };

    bool everyPreset = true;
    juce::String presetDetail;
    for (auto& c : cases)
    {
        preset->setSelectedId (c.itemId, juce::dontSendNotification);
        preset->onChange();

        if (std::abs (ratio->getValue() - c.ratio) > 1e-9
            || std::abs (index->getValue() - c.index) > 1e-9
            || std::abs (feedback->getValue() - c.feedback) > 1e-9
            || indexEnv->getSelectedId() != c.indexEnvItem)
        {
            everyPreset = false;
            presetDetail += juce::String (c.itemId) + " -> "
                          + juce::String (ratio->getValue()) + "/"
                          + juce::String (index->getValue()) + "/"
                          + juce::String (feedback->getValue()) + "/"
                          + juce::String (indexEnv->getSelectedId()) + "; ";
        }
    }
    check (everyPreset,
           "R5.10 every preset sets the ratio, the index, the envelope and the feedback",
           presetDetail.isNotEmpty() ? presetDetail
                                     : juce::String ((int) (sizeof (cases) / sizeof (cases[0])))
                                         + " presets");

    // the selected arc carries the preset: read it back out of the piece
    {
        const auto piece = mc.pieceToVar();
        check (std::abs (arcPropertyDouble (piece, 0, "pmRatio") - 1.414) < 1e-9
               && std::abs (arcPropertyDouble (piece, 0, "pmIndex") - 5.0) < 1e-9
               && std::abs (arcPropertyDouble (piece, 0, "pmFeedback") - 0.2) < 1e-9
               && arcPropertyInt (piece, 0, "pmIndexEnv") == 1,
               "R5.10 the preset writes the selected PM arc",
               "ratio " + juce::String (arcPropertyDouble (piece, 0, "pmRatio"))
                 + ", index " + juce::String (arcPropertyDouble (piece, 0, "pmIndex"))
                 + ", feedback " + juce::String (arcPropertyDouble (piece, 0, "pmFeedback"))
                 + ", env " + juce::String (arcPropertyInt (piece, 0, "pmIndexEnv")));
    }

    // "(manual)" leaves the voice alone
    {
        const double beforeRatio = ratio->getValue();
        const double beforeIndex = index->getValue();
        const double beforeFeedback = feedback->getValue();
        preset->setSelectedId (1, juce::dontSendNotification);
        preset->onChange();
        check (std::abs (ratio->getValue() - beforeRatio) < 1e-9
               && std::abs (index->getValue() - beforeIndex) < 1e-9
               && std::abs (feedback->getValue() - beforeFeedback) < 1e-9,
               "R5.10 (manual) leaves the voice alone",
               juce::String (ratio->getValue()) + "/" + juce::String (index->getValue()));
    }

    // --- the ops box writes the selected arc ----------------------------------
    ops->setSelectedId (2, juce::dontSendNotification);
    ops->onChange();
    check (arcPropertyInt (mc.pieceToVar(), 0, "pmOps") == 4,
           "R5.10 4-op writes the arc's four operators",
           juce::String (arcPropertyInt (mc.pieceToVar(), 0, "pmOps")));

    ops->setSelectedId (1, juce::dontSendNotification);
    ops->onChange();
    check (arcPropertyInt (mc.pieceToVar(), 0, "pmOps") == 2,
           "R5.10 2-op writes the arc's two operators",
           juce::String (arcPropertyInt (mc.pieceToVar(), 0, "pmOps")));
}
