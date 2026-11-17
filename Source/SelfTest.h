#pragma once
#include <JuceHeader.h>

//==============================================================================
// The headless verification of UPICuitous (UPICuitous.exe --selftest).
//
// The checks live in the SelfTest*.cpp files, by area: the engine, the audio
// path, the model, the files, the MIDI, the interface and the offscreen
// snapshot. They are kept out of Main.cpp on purpose - the application entry
// point is small enough to read at a glance.
namespace upic
{
    // The shared state of a run: the log and the number of failures so far.
    struct SelfTest
    {
        juce::String log;
        int failures = 0;

        // Records one check: "PASS" or "FAIL", with the detail (if any).
        void check (bool ok, const juce::String& name, const juce::String& detail = {});
    };

    // The checks, by area. They run in the order the runner lists them (the
    // same order they had before the split).
    void runDspChecks (SelfTest& t);
    void runAudioThreadChecks (SelfTest& t);
    void runLoadedPieceChecks (SelfTest& t);
    void runThreadValueChecks (SelfTest& t);
    void runPieceStateChecks (SelfTest& t);
    void runHostTransportChecks (SelfTest& t);
    void runDrawingChecks (SelfTest& t);
    void runMidiScrubChecks (SelfTest& t);
    void runCursorPathChecks (SelfTest& t);
    void runLivePathChecks (SelfTest& t);
    void runTransformChecks (SelfTest& t);
    void runUntrustedPieceChecks (SelfTest& t);
    void runModelMathChecks (SelfTest& t);
    void runExportChecks (SelfTest& t);
    void runSieveChecks (SelfTest& t);
    void runTuningChecks (SelfTest& t);   // M4: the Scala tuning import
    void runMaterialChecks (SelfTest& t); // M5: the per-slot Ref and the material's edits
    void runEditorChecks (SelfTest& t);
    void runSnapshotChecks (SelfTest& t);
    void runContinuityChecks (SelfTest& t);
    void runRealtimeChecks (SelfTest& t);
    void runArcOverrideChecks (SelfTest& t);
    void runPanelChecks (SelfTest& t);
    void runPmPresetChecks (SelfTest& t);
    void runWidgetChecks (SelfTest& t);
    void runMidiInputChecks (SelfTest& t);
    void runEnvelopeChecks (SelfTest& t);
    void runTransportChecks (SelfTest& t);
    void runTransportStateChecks (SelfTest& t);
    void runChooserChecks (SelfTest& t);

    // Runs every check and returns the number of failures (0 = all passed).
    // The log of the run is written to <temp>/upic_selftest.txt.
    int runSelfTest();
}
