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

        // The log is written as the run goes (a run that dies leaves the
        // checks it had reached behind), so the file stream is opened with
        // the first check and closed with the last.
        juce::File logFile;
        std::unique_ptr<juce::FileOutputStream> logStream;

        // Records one check: "PASS" or "FAIL", with the detail (if any).
        void check (bool ok, const juce::String& name, const juce::String& detail = {});

        // Records a line that is not a check: the numbers a benchmark
        // measured. It goes into the log file like a check line, so the
        // evidence a run gathered is still there after the run.
        void note (const juce::String& text);

    private:
        // Opens the log file on the first line and writes one line to it.
        void writeLine (const juce::String& line);
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
    void runWP9CurveChecks (SelfTest& t);   // WP9: the generalized curve
    void runWP9ConvertChecks (SelfTest& t); // WP9: the Convert As commands
    void runWP9WorkspaceChecks (SelfTest& t); // WP9: the multi-page workspace
    void runWP10EnvelopeChecks (SelfTest& t); // WP10: the envelope bank at 128
    void runWP10ArcChecks (SelfTest& t);       // WP10: the page at 4000 arcs
    void runWP10WaveChecks (SelfTest& t);      // WP10: the wavetable at 4096 samples
    void runWP10PageChecks (SelfTest& t);      // WP10: the page at an hour
    void runLayoutCharacterizationChecks (SelfTest& t); // R0: the layout, before it moves
    void runBottomLayoutCharacterizationChecks (SelfTest& t); // R1.2: the panels below the toolbar
    void runOverlayLayoutCharacterizationChecks (SelfTest& t); // R1.3: the three overlay cards
    void runDocumentCharacterizationChecks (SelfTest& t); // R0: the document, before it moves
    void runTransportPreviewCharacterizationChecks (SelfTest& t); // R0: the transport + preview
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
    void runDomainChecks (SelfTest& t);  // R2.1: the domain's numeric core, on its own
    void runArcChecks (SelfTest& t);     // R2.2: the arc - its rules and its codec
    void runArcGeometryChecks (SelfTest& t); // R2.3: the arc's geometry, on its own
    void runPageCodecChecks (SelfTest& t);   // R2.4: the page's codec - the file format
    void runArcGroupCodecChecks (SelfTest& t); // R2.5: the arc group's data and its file format
    void runEnvelopeCodecChecks (SelfTest& t);  // R2.6: the envelope's codec - the file format
    void runWaveCodecChecks (SelfTest& t);      // R2.7: the wave's codec - the file format
    void runChannelCodecChecks (SelfTest& t);  // R2.8: the channel's codec - the file format
    void runSampleCodecChecks (SelfTest& t);   // R2.9: the sample slot's codec - the file format
    void runTuningCodecChecks (SelfTest& t);   // R2.10: the tuning's codec - the file format
    void runSieveCodecChecks (SelfTest& t);    // R2.11: the sieve zone's codec - the file format
    void runPerformanceCodecChecks (SelfTest& t); // R2.12: the performance sequence's codec - the file format
    void runFreqSpaceCodecChecks (SelfTest& t);   // R2.13: the frequency space definition's codec - the file format
    void runDocumentSessionChecks (SelfTest& t);  // R3.1: the document session - the revision and the unsaved flag
    void runUndoHistoryChecks (SelfTest& t);      // R3.2: the undo history - the document's steps
    void runSelectionChecks (SelfTest& t);        // R3.3: the selection model - what is selected
    void runCommandChecks (SelfTest& t);          // R3.4: the command dispatcher - the unit of change
    void runModelCommandChecks (SelfTest& t);     // R3.5: the edits that become commands
    void runPieceCodecChecks (SelfTest& t);       // R4.1: the document's codec - the file format
    void runDocumentIOChecks (SelfTest& t);       // R4.2: the document's file adapter - the read and the write
    void runRecoveryRepositoryChecks (SelfTest& t); // R4.3: the recovery snapshot's storage - the write, the read and the cleanup
    void runLibraryRepositoryChecks (SelfTest& t);  // R4.4: the user library's storage - the write, the read and the listing
    void runScoreRenderChecks (SelfTest& t);        // R5.1: the score canvas's render - the frame, the grid, the arcs and the playhead
    void runCoordinateMapperChecks (SelfTest& t);   // R5.2: the coordinate mapper - the time axis, the pitch axis and the zoom
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