#pragma once

//==============================================================================
// R5.10 (the review's R052, section 28): the sample material panel.
//
// The Sample tab plays the slot: the material's own level, the pitch algorithm
// that transposes it, and the material's edits (the normalise, the fades and
// the reset). The tab's card shows the slot, the load button, the reverse, the
// Ref, the level, the pitch mode, the grain and the three edits; this header
// says what those controls mean.
//
// The review asks for the sample controls to be decomposed:
//
//   "| `buildSampleControls()`                                  | SampleMaterialPanel |"
//   (section 28)
//
// This header is the panel's description: the controls it owns, the words of
// each one, the pitch algorithm's list and item ids, the level's domain, and
// the slot list's item ids.
//
// It holds no widget and no document. The app keeps its widgets (the material
// inspector already says which of them the Sample tab shows, Source/
// MaterialInspector.h) and its commands (the level's undo transaction, the
// pitch's edit, the slot's load); it asks this header what they mean. Before,
// the four pitch items were written where the list was built and the mode was
// read as "id - 1", the level's range was in the widget, and the slot's
// "index + 1" was written down twice.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include <vector>

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

    /** Every control the panel owns. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Slot, Control::LoadSample, Control::Reverse, Control::Level,
            Control::Pitch, Control::Normalise, Control::Fade, Control::Reset,
            Control::Extract
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself. A control that says
    // nothing returns an empty string (the app gives it no tooltip).
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::Reverse:
                return "Play the selected slot backwards";
            case Control::Level:
                return "The slot's level - how loud the material is "
                       "(1 = as it was loaded)";
            case Control::Pitch:
                return "Sample-tab arcs: how the slot is transposed.\n"
                       "At 1x (the arc sits on Ref) the pitch is unchanged; "
                       "draw above/below Ref to transpose.\n"
                       "Varispeed and Granular (no stretch) change the slot's "
                       "playback length with pitch; Granular (stretch) fits "
                       "the slot to the arc length; Phase vocoder keeps the "
                       "slot's length and shifts pitch only.";
            case Control::Normalise:
                return "Normalize the material: the gain is set so the "
                       "loudest point of the material is just under full scale";
            case Control::Fade:
                return "Fade the material in and out over a tenth of its "
                       "length, so the loop does not click";
            case Control::Reset:
                return "Undo the material's edits (the trim, the fades "
                       "and the normalise gain)";
            case Control::Extract:
                return "Find the repeating cycle in the selected material,\n"
                       "preview it, and save it as a wavetable or envelope";
            default:
                return "";
        }
    }

    //---------------------------------------------------------------------------
    // The level's domain. It is the slot's own knob - the material's loudness
    // - and 1 is the level the material was loaded with.
    constexpr double kLevelMin = 0.0;
    constexpr double kLevelMax = 2.0;
    constexpr double kLevelInterval = 0.01;
    constexpr double kLevelSkew = 0.2;
    constexpr double kDefaultLevel = 1.0;

    //---------------------------------------------------------------------------
    // The pitch algorithm. The list is ONE list: the dropdown offers it, the
    // command reads the engine's mode from it, and the loader puts the piece's
    // mode back on the right item. The item ids are the engine's modes + 1
    // (the engine's modes start at 0, and a ComboBox item id must be > 0).
    struct PitchMode
    {
        const char* name = "";
        int mode = 0;
    };

    inline const std::vector<PitchMode>& pitchModes()
    {
        static const std::vector<PitchMode> modes =
        {
            { "Varispeed (tape)",       0 },
            { "Granular (stretch)",     1 },
            { "Phase vocoder",          2 },
            { "Granular (no stretch)",  3 }
        };
        return modes;
    }

    /** The item id of a pitch mode. */
    inline int pitchItemId (int mode)
    {
        return mode + 1;
    }

    /** The engine's mode an item id names. */
    inline int pitchModeForItemId (int itemId)
    {
        return itemId - 1;
    }

    /** The item the box starts on (varispeed). */
    constexpr int kDefaultPitchItemId = 1;

    /** True when the item is the granular (stretch) mode - the mode whose
        grains are fitted to the arc, so the Sample tab shows the grain size
        for it. */
    inline bool usesGranularTab (int itemId)
    {
        return itemId == pitchItemId (1);
    }

    //---------------------------------------------------------------------------
    // The slot list. The list has the piece's eight slots in the bank's order;
    // the slot at the index i is the list's item i + 1 (a ComboBox item id must
    // be > 0). The list is written in one place and read in another, so the
    // rule lives here.
    inline int itemIdForSlotIndex (int index)
    {
        return index + 1;
    }

    /** The slot index an item id names (the app bounds it). */
    inline int slotIndexForItemId (int itemId)
    {
        return itemId - 1;
    }
}
