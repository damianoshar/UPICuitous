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
    void runLegacyFixtureChecks (SelfTest& t); // the v0.29 file compatibility guard
    void runThreadValueChecks (SelfTest& t);
    void runPieceStateChecks (SelfTest& t);
    void runHostTransportChecks (SelfTest& t);
    void runDrawingChecks (SelfTest& t);
    void runMidiScrubChecks (SelfTest& t);
    void runCursorPathChecks (SelfTest& t);
    void runLivePathChecks (SelfTest& t);
    void runTransformChecks (SelfTest& t);
    void runWP8TransformChecks (SelfTest& t); // WP8: the geometric transforms
    void runWP8CommandChecks (SelfTest& t);   // WP8: the app's transform commands
    void runWP8PerformanceViewChecks (SelfTest& t); // WP8: the performance view
    void runFreqTableChecks (SelfTest& t); // WP1: the drawn conversion tables
    void runAmpTableChecks (SelfTest& t);  // WP2: the amplitude conversion tables
    void runGroupChecks (SelfTest& t);     // WP3: the arc groups
    void runSequenceChecks (SelfTest& t);  // WP4: the performance sequences
    void runGoldenRenderChecks (SelfTest& t); // the fixed offline render
    void runModulationChecks (SelfTest& t);  // WP5: the modulating arcs
    void runModulationSemanticsChecks (SelfTest& t); // WP5 verification targets
    void runChannelModelChecks (SelfTest& t);
    void runChannelUiChecks (SelfTest& t); // WP6.4: the mixer panel
    void runChannelRoutingChecks (SelfTest& t); // WP6.3: the mixer
    void runUntrustedPieceChecks (SelfTest& t);
    void runModelMathChecks (SelfTest& t);
    void runLibraryModelChecks (SelfTest& t); // F2/F3: the banks and their commands
    void runWP7Checks (SelfTest& t);          // WP7: the user library and extraction
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
    void runLibraryUiChecks (SelfTest& t); // F2/F3: the manager rows
    void runInterfaceChecks (SelfTest& t); // F5: the disclosure toggles and the strip
    void runFocusChecks (SelfTest& t);    // F4: the focus views
    void runViewChecks (SelfTest& t);     // F6: the view commands and the precision
    void runKnobChecks (SelfTest& t);     // F11: the knob row
    void runMidiInputChecks (SelfTest& t);
    void runEnvelopeChecks (SelfTest& t);
    void runTransportChecks (SelfTest& t);
    void runTransportStateChecks (SelfTest& t);
    void runChooserChecks (SelfTest& t);

    // Runs every check and returns the number of failures (0 = all passed).
    // The log of the run is written to <temp>/upic_selftest.txt.
    int runSelfTest();
}