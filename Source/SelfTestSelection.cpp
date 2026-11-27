#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R3.3: the selection model - what is selected.
//
// The selection is the shell's answer to "what is the user working on?": the
// selected wave, envelope, amplitude table, sequence, group, channel and
// sample slot, and the arc the canvas points at. R3.3 moves it behind the
// document session (the review's R006), and this suite is its contract.
//
// The suite reads the selection through the component's own doors (the
// selected*ForTest accessors), so the move does not change the checks.
//
// The components are made in plugin mode: the selection does not depend on
// the window, and the plugin mode does not open an audio device.

namespace
{
    // A piece with two wavetables, two envelopes, two amplitude tables, one
    // performance sequence and two mixer channels.
    upic::PieceModel makeBankPiece()
    {
        upic::PieceModel m;

        upic::Waveform w0 (10, "Wave 10", upic::paletteColour (0));
        w0.makeSine();
        m.waveforms.push_back (w0);

        upic::Waveform w1 (11, "Wave 11", upic::paletteColour (1));
        w1.makeSine();
        m.waveforms.push_back (w1);

        upic::Envelope e0 (20, "Env 20", upic::paletteColour (1));
        e0.presetFlat();
        m.envelopes.push_back (e0);

        upic::Envelope e1 (21, "Env 21", upic::paletteColour (2));
        e1.presetFlat();
        m.envelopes.push_back (e1);

        // WP2: the amplitude conversion tables
        m.amplitudeTables.push_back (
            upic::makeAmplitudeTable (5, "S", juce::Colour (0xff5aa0c8), upic::ampS));
        m.amplitudeTables.push_back (
            upic::makeAmplitudeTable (6, "T", juce::Colour (0xffa0c85a), upic::ampS));

        // WP4: the performance sequence
        upic::PerformanceSequence seq;
        seq.id = 3;
        seq.name = "Seq 3";
        seq.points = { { 0.0, 0, 0.0 }, { 4.0, 1, 1.5 } };
        m.sequences.push_back (seq);

        // WP6: the mixer channels
        upic::Channel c0;
        c0.id = 1;
        c0.name = "Bus";
        c0.colour = juce::Colour (0xff5ac8fa);
        c0.gain = 0.9f;
        m.channels.push_back (c0);

        upic::Channel c1;
        c1.id = 2;
        c1.name = "Bus 2";
        c1.colour = juce::Colour (0xffc8fa5a);
        c1.gain = 0.8f;
        m.channels.push_back (c1);
        m.nextChannelId = 3;

        m.pages.push_back (upic::Page());
        return m;
    }
}

void upic::runSelectionChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- a fresh document ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.selectedWaveformIdForTest() == 0,
               "a fresh document selects the first wavetable",
               juce::String (mc.selectedWaveformIdForTest()));
        check (mc.selectedEnvelopeIdForTest() == 0,
               "a fresh document selects the first envelope",
               juce::String (mc.selectedEnvelopeIdForTest()));
        check (mc.selectedSampleSlotForTest() == 0,
               "a fresh document selects the first sample slot",
               juce::String (mc.selectedSampleSlotForTest()));
        check (mc.selectedAmpTableIdForTest() == -1,
               "a fresh document selects no amplitude table",
               juce::String (mc.selectedAmpTableIdForTest()));
        check (mc.selectedSeqIdForTest() == -1,
               "a fresh document selects no performance sequence",
               juce::String (mc.selectedSeqIdForTest()));
        check (mc.selectedGroupIdForTest() == -1,
               "a fresh document selects no group",
               juce::String (mc.selectedGroupIdForTest()));
        check (mc.selectedChannelIdForTest() == -1,
               "a fresh document selects no mixer channel",
               juce::String (mc.selectedChannelIdForTest()));
        check (mc.selectedArcIdForTest() == -1,
               "a fresh document selects no arc",
               juce::String (mc.selectedArcIdForTest()));
        check (mc.selectedArcIdsCountForTest() == 0,
               "a fresh document has no arc selection",
               juce::String (mc.selectedArcIdsCountForTest()));
    }

    // --- a load --------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        // the selection is on the second wavetable of the fresh bank when
        // the piece is loaded
        mc.selectWaveformForTest (1);
        check (mc.selectedWaveformIdForTest() == 1,
               "the wavetable can be selected before the load",
               juce::String (mc.selectedWaveformIdForTest()));

        check (mc.loadFromVar (upic::pieceToVar (makeBankPiece())),
               "the bank piece loads");

        check (mc.selectedWaveformIdForTest() == 10,
               "the load selects the first wavetable of the piece it loads",
               juce::String (mc.selectedWaveformIdForTest()));
        check (mc.selectedEnvelopeIdForTest() == 20,
               "the load selects the first envelope of the piece it loads",
               juce::String (mc.selectedEnvelopeIdForTest()));
        check (mc.selectedSampleSlotForTest() == 0,
               "the load selects the first sample slot",
               juce::String (mc.selectedSampleSlotForTest()));
        check (mc.selectedChannelIdForTest() == 1,
               "the load selects the first mixer channel",
               juce::String (mc.selectedChannelIdForTest()));
        check (mc.selectedSeqIdForTest() == 3,
               "the load selects the first performance sequence",
               juce::String (mc.selectedSeqIdForTest()));
        check (mc.selectedAmpTableIdForTest() == -1,
               "the load leaves the amplitude table selection alone",
               juce::String (mc.selectedAmpTableIdForTest()));
        check (mc.selectedGroupIdForTest() == -1,
               "the load leaves the group selection alone",
               juce::String (mc.selectedGroupIdForTest()));
    }

    // --- deleting the selected item -------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.loadFromVar (upic::pieceToVar (makeBankPiece()));
        mc.selectWaveformForTest (10);
        check (mc.selectedWaveformIdForTest() == 10,
               "the wavetable is selected before the delete",
               juce::String (mc.selectedWaveformIdForTest()));

        // the delete acts on the selected wavetable and the replacement
        // takes its place in the selection
        check (mc.libraryDeleteWave (11),
               "the selected wavetable is deleted");
        check (mc.selectedWaveformIdForTest() == 11,
               "the delete selects the replacement",
               juce::String (mc.selectedWaveformIdForTest()));
    }
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.loadFromVar (upic::pieceToVar (makeBankPiece()));
        mc.selectEnvelopeForTest (20);
        check (mc.selectedEnvelopeIdForTest() == 20,
               "the envelope is selected before the delete",
               juce::String (mc.selectedEnvelopeIdForTest()));

        check (mc.libraryDeleteEnvelope (21),
               "the selected envelope is deleted");
        check (mc.selectedEnvelopeIdForTest() == 21,
               "the delete selects the replacement",
               juce::String (mc.selectedEnvelopeIdForTest()));
    }

    // --- two documents ----------------------------------------------------------
    {
        auto firstHolder = std::make_unique<MainComponent> (true);
        auto& first = *firstHolder;
        auto secondHolder = std::make_unique<MainComponent> (true);
        auto& second = *secondHolder;

        first.loadFromVar (upic::pieceToVar (makeBankPiece()));
        second.loadFromVar (upic::pieceToVar (makeBankPiece()));

        first.selectWaveformForTest (11);
        first.selectEnvelopeForTest (21);
        check (first.selectedWaveformIdForTest() == 11
               && first.selectedEnvelopeIdForTest() == 21,
               "the first document selects its own items",
               juce::String (first.selectedWaveformIdForTest()) + "/"
               + juce::String (first.selectedEnvelopeIdForTest()));
        check (second.selectedWaveformIdForTest() == 10
               && second.selectedEnvelopeIdForTest() == 20,
               "the second document keeps its own selection",
               juce::String (second.selectedWaveformIdForTest()) + "/"
               + juce::String (second.selectedEnvelopeIdForTest()));
    }

    // --- the canvas selection ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.loadFromVar (upic::pieceToVar (makeBankPiece()));
        check (mc.selectedArcIdForTest() == -1,
               "the load leaves no arc selected",
               juce::String (mc.selectedArcIdForTest()));
    }
}
