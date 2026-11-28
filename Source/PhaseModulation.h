#pragma once

//==============================================================================
// R5.10 (the review's R052, section 28): the phase modulation panel.
//
// The PM tab's arcs modulate the carrier's phase: the ops, the preset, the
// ratio, the index, the index's envelope, the feedback, and the preset files.
//
// The review asks for the phase modulation controls to be decomposed:
//
//   "| `buildPmControls()`                                      | PhaseModulationPanel |"
//   (section 28)
//
// This header is the panel's description: the controls it owns, the two
// operator counts, the six presets and the voice each one writes, and the
// three knobs' domains and start values.
//
// It holds no widget and no document. The app keeps its widgets (the material
// inspector already says which of them the PM tab shows, Source/
// MaterialInspector.h) and its commands (the preset's assignment, the ops'
// assignment, the preset files); it asks this header what they mean. Before,
// the preset's five rows were if-chains that wrote four locals, and the two
// op counts were "the id is 2 ? 4 : 2" at three call sites.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include <vector>

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

    /** Every control the panel owns. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Ops, Control::Preset, Control::Ratio, Control::Index,
            Control::IndexEnv, Control::Feedback, Control::Import, Control::Export
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The voice: the three knobs' domains. The ratio and the index are
    // skewed low, so the small values (where the timbre changes most) get
    // most of the travel.
    constexpr double kRatioMin = 0.1;
    constexpr double kRatioMax = 32.0;
    constexpr double kRatioInterval = 0.01;
    constexpr double kRatioSkew = 0.4;
    constexpr double kDefaultRatio = 2.0;

    constexpr double kIndexMin = 0.0;
    constexpr double kIndexMax = 20.0;
    constexpr double kIndexInterval = 0.01;
    constexpr double kIndexSkew = 0.4;
    constexpr double kDefaultIndex = 1.5;

    constexpr double kFeedbackMin = 0.0;
    constexpr double kFeedbackMax = 1.0;
    constexpr double kFeedbackInterval = 0.001;
    constexpr double kFeedbackSkew = 0.4;
    constexpr double kDefaultFeedback = 0.0;

    //---------------------------------------------------------------------------
    // The operator counts. The box's item ids are 1 and 2; the counts they
    // name are 2 and 4. The piece carries the count itself (2 or 4), so the
    // two rules have to agree.
    struct Ops
    {
        const char* name = "";
        int count = 2;
    };

    inline const std::vector<Ops>& opsCounts()
    {
        static const std::vector<Ops> counts =
        {
            { "2-op", 2 },
            { "4-op", 4 }
        };
        return counts;
    }

    /** The item id of an operator count. */
    inline int opsItemId (int count)
    {
        return count == 4 ? 2 : 1;
    }

    /** The operator count an item id names. */
    inline int opsCountForItemId (int itemId)
    {
        return itemId == 2 ? 4 : 2;
    }

    //---------------------------------------------------------------------------
    // The presets. The list's first item is manual - it writes nothing, it
    // only leaves the voice where the user put it. The five presets follow:
    // each one writes the ratio, the index, the index's envelope (an
    // envelope id, 0 being the flat amp envelope) and the feedback.
    constexpr int kManualItemId = 1;

    /** The name the manual item carries. */
    inline const char* manualName()
    {
        return "(manual)";
    }

    struct Preset
    {
        int itemId = 2;         // the dropdown's item id
        const char* name = "";
        double ratio = 2.0;
        double index = 1.5;
        int indexEnv = 0;       // the envelope's id
        double feedback = 0.0;
    };

    inline const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> table =
        {
            { 2, "Bell (2.0x, percussive)",      2.0,   2.5, 1, 0.0 },
            { 3, "Brass (1.0x, flat)",           1.0,   1.0, 0, 0.1 },
            { 4, "Pluck (3.0x, percussive)",     3.0,   4.0, 1, 0.0 },
            { 5, "Organ (1.0x, sustain, fb)",    1.0,   0.5, 0, 0.3 },
            { 6, "Metallic (1.414x, perc, fb)",  1.414, 5.0, 1, 0.2 }
        };
        return table;
    }

    /** True when the item writes nothing (the manual item). */
    inline bool isManualItem (int itemId)
    {
        return itemId <= kManualItemId;
    }

    /** The preset an item id names, or nullptr for the manual item and for an
        id the list does not carry. */
    inline const Preset* presetForItemId (int itemId)
    {
        for (auto& preset : presets())
            if (preset.itemId == itemId) return &preset;
        return nullptr;
    }

    //---------------------------------------------------------------------------
    // The wording: the panel's controls have no tooltips of their own today,
    // so every control says nothing. The function stays so the suite can pin
    // the silence, and so a later note has one place to go.
    inline const char* tooltip (Control)
    {
        return "";
    }
}
