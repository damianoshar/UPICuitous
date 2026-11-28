#pragma once

//==============================================================================
// R5.7 (the review's R049, section 28): the frequency inspector.
//
// A frequency space is a range of the frequency axis: the position on the
// page's vertical axis is mapped into it, so the same drawing can be heard
// in another register, with its intervals scaled. The PAGE owns four of them
// (the UPIC's four tables), and an arc names the one it is played in. The
// page also owns its ambitus (the interval its vertical axis covers) and its
// acceleration (a tempo sweep across the page).
//
// The review asks for the frequency controls to be decomposed:
//
//   "| `buildFrequencyControls()`, `pushSpaceDefs()`            | FrequencyInspector |"
//   (section 28)
//
// This header is the frequency inspector's description: which controls it
// holds, in which cards, and what every control says about itself.
//
// It holds no widget and no document. The app binds the roles to its own
// controls (the app owns the widgets), and the layout asks the description
// what to draw.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <cstddef>
#include <string>
#include <vector>

namespace upic::frequency_inspector
{
    using Kind = upic::inspector::Kind;

    /** A control of the frequency inspector. */
    enum class Control
    {
        Space,          // the arc's own space
        Ambitus,        // the page's ambitus
        Warp,           // the page's acceleration
        SpaceDef1,      // the page's first frequency space
        SpaceDef2,
        SpaceDef3,
        SpaceDef4,
        Table,          // the frequency table of the selected space
        count
    };

    /** One cell of one of the frequency inspector's cards. */
    struct Cell
    {
        Control control = Control::Space;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the frequency inspector says about itself. */
    struct Description
    {
        const char* name = "";             // the inspector's name
        std::vector<Cell> page;            // the page card's cells, in order
        std::vector<Cell> pitch;           // the pitch card's frequency cell
        std::vector<Cell> accel;           // the acceleration card's cells
    };

    /** The frequency inspector's description. */
    inline const Description& describe()
    {
        static const Description description =
        {
            "Frequency",
            {
                // the page's ambitus, its four frequency spaces and the
                // table button
                { Control::Ambitus,   Kind::Inline,   58, 170 },
                { Control::SpaceDef1, Kind::Inline,   46, 120 },
                { Control::SpaceDef2, Kind::Inline,   46, 120 },
                { Control::SpaceDef3, Kind::Inline,   46, 120 },
                { Control::SpaceDef4, Kind::Inline,   46, 120 },
                { Control::Table,     Kind::Plain,    0,  66 }
            },
            {
                // the arc's own space - it sits under the snap and the step
                { Control::Space,     Kind::Labelled, 44, 170, true }
            },
            {
                // the page's acceleration - it has a card of its own
                { Control::Warp,      Kind::Knob,     40,   0 }
            }
        };
        return description;
    }

    /** Every control the frequency inspector knows. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Space, Control::Ambitus, Control::Warp,
            Control::SpaceDef1, Control::SpaceDef2,
            Control::SpaceDef3, Control::SpaceDef4,
            Control::Table
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The page's four spaces (the UPIC's four tables). An arc names the one
    // it is played in.
    constexpr int kSpaceCount = 4;

    //---------------------------------------------------------------------------
    // The page's acceleration: the page's own time runs at a varying rate.
    // The range is the knob's and the command's - one rule, so the knob
    // cannot offer a value the command would refuse.
    constexpr double kMinWarp = -0.9;
    constexpr double kMaxWarp = 0.9;
    constexpr double kWarpInterval = 0.05;

    //---------------------------------------------------------------------------
    // The ambitus's items: the presets, and then "Custom" - the page's own
    // interval, which the dropdown cannot set. Before, the item id of
    // "Custom" was written down twice (once when the items were offered and
    // once when the page's interval was read back).
    inline int ambitusCustomItemId (int presetCount)
    {
        return presetCount + 1;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself.
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::Space:
                return "The frequency space this arc is played in.\n"
                       "The page has four (the UPIC's four tables);\n"
                       "the drawing is mapped into the one you pick.";
            case Control::Ambitus:
                return "The interval the page's vertical axis covers.\n"
                       "The UPIC's normal ambitus was six octaves; it\n"
                       "could be ten, or as small as a semitone.";
            case Control::Warp:
                return "The page's acceleration: the page's own time runs\n"
                       "at a varying rate. Positive starts slow and speeds\n"
                       "up, negative starts fast and slows down.";
            case Control::Table:
                return "Draw the frequency table of the space\n"
                       "selected in the Space menu (WP1)";
            default:
                return "";
        }
    }

    /** What a caption says about its control. The four spaces' captions name
        the space they belong to, so their wording carries the number. */
    inline std::string labelTooltip (Control control)
    {
        if (control == Control::Warp)
            return "The page's acceleration.";

        if (control >= Control::SpaceDef1 && control <= Control::SpaceDef4)
        {
            const int index = (int) control - (int) Control::SpaceDef1;
            return "The page's frequency space " + std::to_string (index + 1)
                 + " - the range the page's vertical axis is\n"
                   "mapped into for the arcs that use it.";
        }

        return {};
    }
}
