#pragma once

//==============================================================================
// R5.7: the vocabulary the panel descriptions share.
//
// A panel's card is a list of cells, and each cell says how it is drawn:
// the control alone, the caption above it, the caption beside it on its row,
// or the caption above a knob (which draws the caption itself).
//
// The material inspector (R5.6), the sieve panel and the frequency inspector
// (R5.7) all describe their cards this way, so the kind lives in one place:
// the app's layout reads it and draws the cell.
//
// The header holds no widget and no document, and it is free of JUCE: a cell
// is a drawing rule, and the rule can be checked without a window.

namespace upic::inspector
{
    /** How a cell of a panel's card is drawn. */
    enum class Kind
    {
        Plain,      // the control alone
        Labelled,   // the caption above the control
        Inline,     // the caption beside the control, on its row
        Knob        // the caption above the control, which draws it itself
    };
}
