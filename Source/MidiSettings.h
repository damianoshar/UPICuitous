#pragma once

//==============================================================================
// R5.10 (the review's R052, section 28): the MIDI settings.
//
// The MIDI input lives in the MIDI + OSC settings overlay (v1.30: it is no
// longer part of the audio settings). The section has a device row, an
// activity line and four learn rows - the master gain, the draw intensity,
// the scrub and the transport. Each learn row is a caption, a Learn button
// and the binding's text.
//
// The review asks for the MIDI controls to be decomposed:
//
//   "| `buildMidiControls()`                                    | MidiSettingsPanel  |"
//   (section 28)
//
// This header is the section's description: the four rows, the words of every
// row and of the learning, the binding's text format, and the device list's
// item ids.
//
// It holds no widget and no document. The app keeps its widgets (the overlay's
// layout engine still places them) and its commands; it asks this header what
// to say. The section before had the same three strings written at three call
// sites - the learn's "Learn"/"Cancel", the binding's "-"/"learning...", and
// the item ids "i + 2" when the device list was written and "id - 2" when it
// was read.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include <string>
#include <vector>

namespace upic::midi_settings
{
    /** The four things the MIDI learn can drive. The order is the section's
        order (and the hub's own). */
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

    /** The four learn rows, in the section's order. */
    inline const std::vector<Row>& rows()
    {
        static const std::vector<Row> table =
        {
            { Target::MasterGain, "Master gain" },
            { Target::Intensity,  "Intensity" },
            { Target::Scrub,      "Scrub" },
            { Target::Transport,  "Transport" }
        };
        return table;
    }

    /** The row a target is, or nullptr when the target is not one of the
        four (the hub's "none"). */
    inline const Row* rowForTarget (Target target)
    {
        for (auto& row : rows())
            if (row.target == target) return &row;
        return nullptr;
    }

    //---------------------------------------------------------------------------
    // The device row and the activity line.
    inline const char* deviceCaption()
    {
        return "MIDI in";
    }

    inline const char* deviceTooltip()
    {
        return "The MIDI input the app listens to";
    }

    /** The activity line before any message has arrived. */
    inline const char* noActivityText()
    {
        return "no MIDI yet";
    }

    //---------------------------------------------------------------------------
    // The learn: the button and the binding's text.
    inline const char* learnText()
    {
        return "Learn";
    }

    /** What the learning button says - it cancels the learn. */
    inline const char* cancelText()
    {
        return "Cancel";
    }

    /** The binding's text while nothing is bound. */
    inline const char* unboundText()
    {
        return "-";
    }

    /** The binding's text while its row is learning. */
    inline const char* learningText()
    {
        return "learning...";
    }

    /** The binding's text: "CC 7 ch 3" or "note 60 ch 1". The channel is the
        app's index (0..15); the user's channel is one more. */
    inline std::string bindingText (bool note, int number, int channel)
    {
        return std::string (note ? "note " : "CC ")
             + std::to_string (number) + " ch " + std::to_string (channel + 1);
    }

    //---------------------------------------------------------------------------
    // The device list. The list's first item is None - the app listens to no
    // device - and the device at the index i follows it. The item ids are one
    // rule: None is 1, and the device at the index i is i + 2.
    constexpr int kNoneItemId = 1;

    /** The name None carries in the list. */
    inline const char* noneItemName()
    {
        return "None";
    }

    /** The item id of the device at the given index in the hub's list. A
        negative index is no device, so it reads as None. */
    inline int itemIdForDeviceIndex (int index)
    {
        return index < 0 ? kNoneItemId : index + 2;
    }

    /** The index in the hub's list of the device an item id names (the app
        bounds it before it opens the device). */
    inline int deviceIndexForItemId (int itemId)
    {
        return itemId - 2;
    }

    /** True when the item id names None. */
    inline bool isNoneItem (int itemId)
    {
        return itemId <= kNoneItemId;
    }
}
