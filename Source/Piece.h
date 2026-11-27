#pragma once

#include <JuceHeader.h>
#include <vector>

#include "AmplitudeTable.h"
#include "Channel.h"
#include "Envelope.h"
#include "Page.h"
#include "Performance.h"
#include "Sample.h"
#include "Tuning.h"
#include "Waveform.h"

namespace upic
{
    //================ the piece as a self-contained model (M28) ================
    //
    // The saver used to build the piece's JSON - and with it the base64 of
    // every sample - on the message thread, while holding the model lock: a
    // 30 MB piece froze the window for a moment, and the audio thread waited
    // for the lock. The model is captured on the message thread instead (a
    // copy: the sample audio is shared, so the copy is eight pointers), and
    // the worker builds the JSON from the copy.
    //
    // R4.1: this is the piece as *data*. How it reaches a file, and how it
    // comes back, is the edge's business (Source/PieceCodec.h) - the same
    // split every other type in the app got in R2.
    struct PieceModel
    {
        std::vector<Waveform>   waveforms;
        std::vector<Envelope>   envelopes;
        // WP2: the amplitude conversion tables (their own bank; the arcs
        // name one with amplitudeTableId).
        std::vector<AmplitudeTable> amplitudeTables;
        // WP4: the performance sequences (piece-level: a sequence can move
        // across pages).
        std::vector<PerformanceSequence> sequences;
        // WP6: the mixer channels (empty = the legacy stereo path).
        std::vector<Channel> channels;
        int nextChannelId = 1;
        std::vector<SampleSlot> sampleSlots;
        std::vector<Page>       pages;
        int  currentPage = 0;
        bool seqPages = false;
        bool recordPressure = false;
        bool scrubAudition = true;
        bool muteWhileDragging = true;
        int  dragSmoothMs = 20;
        int  scrubGrainMs = 20;
        int  sampleGrainMs = 40;
        int  liveGrainMs = 40;
        bool sieveOn = false;
        int  sieveM = 12;
        std::vector<int> sieveR;
        double stickiness = 1.0;
        double timeGrid = 0.0;
        float masterGain = 0.8f;
        // R4.1: a negative mode means "the document did not carry one" - an
        // old piece has no mode field, and the session keeps the mode it
        // has. A piece that is saved always has a mode (the engine's).
        int  samplePitchMode = 0;
        int  livePitchMode = 0;
        // M4: the tuning the piece is drawn in. The arcs hold frequencies,
        // so the tuning does not change what is played - it changes what
        // the drawing snaps to, and it travels with the piece so the piece
        // is edited the way it was drawn.
        Tuning tuning;
        // WP9: the workspace layout - the pages the workspace's canvases
        // show and the split of the canvas area. Empty = the single canvas
        // (the ordinary view).
        std::vector<int> workspacePages;
        double workspaceSplit = 0.5;
    };
}
