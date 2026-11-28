#pragma once

//==============================================================================
// R5.9 (the review's R051, section 28): the modulation controls.
//
// A modulation is one arc bending another: the modulator's contour bends the
// carrier's pitch or its level, by the depth the slider says. The toolbar's
// "mod" card carries three controls: the modulator, its target and the depth.
//
// The review asks for the modulation controls to be decomposed:
//
//   "| `refreshModBox()`                                        | ModulationInspector |"
//   (section 28)
//
// This header is the first step, the same step R5.6, R5.7 and R5.8 took for
// the inspectors: the *description* and the *words* leave the shell. The
// widgets stay the app's (the app owns them and binds the roles), and so do
// the commands: the assignment, the undo transaction and the cycle check stay
// in MainComponent.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <string>
#include <vector>

namespace upic::modulation_controls
{
    using Kind = upic::inspector::Kind;

    /** A control of the toolbar's modulation card. */
    enum class Control
    {
        Modulator,   // the arc that bends the selection
        Target,      // what it bends: the pitch or the level
        Depth,       // how hard it bends
        count
    };

    /** One cell of the modulation card. */
    struct Cell
    {
        Control control = Control::Modulator;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the modulation card says about itself. */
    struct Description
    {
        const char* name = "";    // the card's name
        std::vector<Cell> card;   // the card's cells, in order
    };

    /** The modulation card's description. */
    inline const Description& describe()
    {
        static const Description description =
        {
            "Modulation",
            {
                // the modulator, its target and the depth
                { Control::Modulator, Kind::Inline, 30, 100 },
                { Control::Target,    Kind::Inline, 26,  66 },
                { Control::Depth,     Kind::Inline, 36, 110 }
            }
        };
        return description;
    }

    /** Every control the card carries. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Modulator, Control::Target, Control::Depth
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself.
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::Modulator:
                return "The arc that modulates the selection "
                       "(its contour bends the pitch or its level)";
            case Control::Target:
                return "What the modulator drives: pitch or amplitude";
            case Control::Depth:
                return "The modulation depth "
                       "(1 = +/- one octave at the contour extremes)";
            default:
                return "";
        }
    }

    //---------------------------------------------------------------------------
    // The modulator list: None first, then the page's arcs. The item ids are
    // one rule: None is 1, and the arc with the id n is n + 1. The list is
    // written in one place and read in another, so the rule lives here.
    constexpr int kNoneItemId = 1;

    /** The name None carries in the list. */
    inline const char* noneName()
    {
        return "None";
    }

    /** The name an arc carries in the list. */
    inline std::string arcName (int arcId)
    {
        return "Arc " + std::to_string (arcId);
    }

    /** The item id of an arc (None when the arc id is negative: there is no
        modulator). */
    inline int itemIdForArc (int arcId)
    {
        return arcId < 0 ? kNoneItemId : arcId + 1;
    }

    /** True when the item id names None. */
    inline bool isNoneItem (int itemId)
    {
        return itemId <= kNoneItemId;
    }

    /** The arc an item id names (the page bounds it). */
    inline int arcForItemId (int itemId)
    {
        return itemId - 1;
    }

    //---------------------------------------------------------------------------
    // The target list: Pitch and Amp. The model says 0 for the pitch and 1
    // for the amplitude; the list's ids are 1 and 2. The list is written in
    // one place and read in another, so the rule lives here.
    constexpr int kPitchTarget = 0;
    constexpr int kAmpTarget = 1;
    constexpr int kPitchItemId = 1;
    constexpr int kAmpItemId = 2;

    /** The name a target carries in the list. */
    inline const char* targetName (int target)
    {
        return target == kAmpTarget ? "Amp" : "Pitch";
    }

    /** The item id of a target. */
    inline int itemIdForTarget (int target)
    {
        return target == kAmpTarget ? kAmpItemId : kPitchItemId;
    }

    /** The target an item id names. */
    inline int targetForItemId (int itemId)
    {
        return itemId == kAmpItemId ? kAmpTarget : kPitchTarget;
    }

    //---------------------------------------------------------------------------
    // The depth's domain: 0..1, in steps of 0.001 - one is a full octave at
    // the contour's extremes. The slider's range, the command's clamp and the
    // value the card shows when there is no arc to show a depth for are the
    // same numbers.
    constexpr double kMinDepth = 0.0;
    constexpr double kMaxDepth = 1.0;
    constexpr double kDepthInterval = 0.001;
    constexpr double kDefaultDepth = 0.5;
}
