#pragma once

//==============================================================================
// R3.3: the selection model - what is selected.
//
// The selection is the shell's answer to "what is the user working on?": the
// selected wavetable, envelope, amplitude table, performance sequence,
// group, mixer channel and sample slot, and the arc the canvas points at.
//
// The review asked for a selection model (its R006, "Extract
// SelectionModel"). The document session owns it - the session is the one
// owner of the document's state, its history and its selection - and the
// shell asks it.
//
// The class is plain data: the ids the piece's banks use, and the sample
// slot's index. It needs no JUCE and no app header.
#include <vector>

namespace upic
{
    class SelectionModel
    {
    public:
        // The selected items of the piece's banks. They are the ids the
        // banks use, except for the sample slot: that one is an index into
        // the slot list.
        int wave = 0;
        int envelope = 0;
        int sampleSlot = 0;
        int ampTable = -1;
        int sequence = -1;
        int group = -1;
        int channel = -1;

        // The arc the panel (and the canvas) point at.
        int arc = -1;
        // The canvas selection the panel last showed. The canvas keeps its
        // own selection (the editor owns it); this is the mirror the panel
        // and the toolbar follow.
        std::vector<int> arcSelection;
    };
}
