#pragma once

#include <JuceHeader.h>
#include <vector>

#include "Page.h"
#include "PieceLimits.h"
#include "Sample.h"
#include "Tuning.h"

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
    struct PieceModel
    {
        std::vector<Waveform>   waveforms;
        std::vector<Envelope>   envelopes;
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
        int  samplePitchMode = 0;
        int  livePitchMode = 0;
        // M4: the tuning the piece is drawn in. The arcs hold frequencies,
        // so the tuning does not change what is played - it changes what
        // the drawing snaps to, and it travels with the piece so the piece
        // is edited the way it was drawn.
        Tuning tuning;
    };

    // The piece as a var. The samples become base64 here, and that is the
    // heavy part of a save - so the caller can run this on a worker thread
    // (v1.33.4 / M28).
    //
    // v1.33.4 (L15, part 3): the piece without the sample audio. The
    // recovery snapshot keeps the audio in a sidecar file, so the piece JSON
    // stays small and the unchanged audio is not embedded again and again.
    inline juce::var pieceToVar (const PieceModel& m, bool includeSampleAudio)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("app", "UPICuitous");
        o->setProperty("version", 1);
        o->setProperty("master", m.masterGain);
        o->setProperty("currentPage", m.currentPage);
        // v1.1 (absent in older / Python-written files -> defaults applied)
        o->setProperty("seqPages", m.seqPages);
        o->setProperty("recordPressure", m.recordPressure);
        o->setProperty("scrubAudition", m.scrubAudition);
        o->setProperty("muteWhileDragging", m.muteWhileDragging);
        o->setProperty("dragSmoothMs", m.dragSmoothMs);
        o->setProperty("scrubGrainMs", m.scrubGrainMs);
        o->setProperty("sampleGrainMs", m.sampleGrainMs);
        o->setProperty("liveGrainMs", m.liveGrainMs);
        o->setProperty("sieveOn", m.sieveOn);
        o->setProperty("sieveM", m.sieveM);
        // v1.33.4 (H7): the global pitch modes change the sound, so they
        // travel with the piece (and with the host state)
        o->setProperty("samplePitchMode", m.samplePitchMode);
        o->setProperty("livePitchMode", m.livePitchMode);
        juce::Array<juce::var> srArr;
        for (int r : m.sieveR) srArr.add (r);
        o->setProperty("sieveR", srArr);
        // v1.2 (same rule: defaults keep old files working unchanged)
        o->setProperty("stickiness", m.stickiness);
        o->setProperty("timeGrid", m.timeGrid);
        // M4: the tuning. An old piece has none -> 12-tone equal
        // temperament, which is what it was drawn in.
        if (m.tuning.custom)
            o->setProperty("tuning", m.tuning.toVar());
        juce::Array<juce::var> wa, ea, pa, sa;
        for (auto& w : m.waveforms) wa.add (w.toVar());
        for (auto& e : m.envelopes) ea.add (e.toVar());
        for (auto& s : m.sampleSlots) if (s.loaded()) sa.add (s.toVar (includeSampleAudio));
        for (auto& p : m.pages) pa.add (p.toVar());
        o->setProperty("waveforms", wa);
        o->setProperty("envelopes", ea);
        o->setProperty("samples", sa);
        o->setProperty("pages", pa);
        return juce::var (o);
    }

    // The piece with the audio in it (the normal one).
    inline juce::var pieceToVar (const PieceModel& m)
    {
        return pieceToVar (m, true);
    }
}
