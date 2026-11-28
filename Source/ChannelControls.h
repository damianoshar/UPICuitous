#pragma once

//==============================================================================
// R5.9 (the review's R051, section 28): the channel controls.
//
// The mixer is where an arc's voice is routed: every arc sits on a channel -
// Main, or one of the mixer's own. The toolbar's "ch" card carries two
// controls: the channel selector and the button that opens the mixer.
//
// The review asks for the channel controls to be decomposed:
//
//   "| `showChannelPanel()`, `refreshChannelBox()`              | ChannelController |"
//   (section 28)
//
// This header is the first step, the same step R5.6, R5.7 and R5.8 took for
// the inspectors: the *description* and the *words* leave the shell. The
// widgets stay the app's (the app owns them and binds the roles), and so do
// the commands: the routing, the undo transaction and the mixer panel stay in
// MainComponent.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <vector>

namespace upic::channel_controls
{
    using Kind = upic::inspector::Kind;

    /** A control of the toolbar's channel card. */
    enum class Control
    {
        Channel,   // the mixer channel selector
        Open,      // the mixer
        count
    };

    /** One cell of the channel card. */
    struct Cell
    {
        Control control = Control::Channel;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the channel card says about itself. */
    struct Description
    {
        const char* name = "";    // the card's name
        std::vector<Cell> card;   // the card's cells, in order
    };

    /** The channel card's description. */
    inline const Description& describe()
    {
        static const Description description =
        {
            "Channel",
            {
                // the channel selector and the button that opens the mixer
                { Control::Channel, Kind::Inline, 26, 100 },
                { Control::Open,    Kind::Plain,   0,  62 }
            }
        };
        return description;
    }

    /** Every control the card carries. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Channel, Control::Open
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself.
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::Channel:
                return "The mixer channel the selected arcs play on";
            case Control::Open:
                return "The mixer: channel levels, mute/solo and routing";
            default:
                return "";
        }
    }

    /** The button that opens the mixer. */
    inline const char* openButtonText()
    {
        return "Mix...";
    }

    //---------------------------------------------------------------------------
    // The channel list's items.
    //
    // Main is the first item - the channel every arc sits on when the piece
    // has no mixer of its own - and the mixer's own channels follow it. The
    // item ids are one rule: Main is 1, and the channel at index i is i + 2.
    // The list is written in one place and read in another, so the rule lives
    // here; before, "index + 2" and "item - 2" were written down twice.
    constexpr int kMainItemId = 1;

    /** The name Main carries in the list. */
    inline const char* mainName()
    {
        return "Main";
    }

    /** The item id of the channel at the given index in the mixer's bank. A
        negative index is a channel the mixer does not have, so it reads as
        Main. (The list's first item is Main, so the channel at index i sits
        at position i + 1.) */
    inline int itemIdForChannelIndex (int index)
    {
        return index < 0 ? kMainItemId : index + 2;
    }

    /** True when the item id names Main. */
    inline bool isMainItem (int itemId)
    {
        return itemId <= kMainItemId;
    }

    /** The index in the mixer's bank of the channel an item id names (the
        mixer bounds it). */
    inline int channelIndexForItemId (int itemId)
    {
        return itemId - 2;
    }
}
