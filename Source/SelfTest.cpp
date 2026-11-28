#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The replacement operator new/delete (v1.33.4, M21).
//
// The selftest counts the allocations of a render through them. The counter
// itself lives in AllocCounter.h, so the checks that use it can live in
// their own files; the operators are global, so only this file defines them.
void* operator new (std::size_t n)
{
    if (upic::allocCounting) ++upic::allocCount;
    void* p = std::malloc (n != 0 ? n : 1);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[] (std::size_t n)
{
    if (upic::allocCounting) ++upic::allocCount;
    return ::operator new (n);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    if (upic::allocCounting) ++upic::allocCount;
    return std::malloc (n != 0 ? n : 1);
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    if (upic::allocCounting) ++upic::allocCount;
    return std::malloc (n != 0 ? n : 1);
}

void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

//==============================================================================
void upic::SelfTest::writeLine (const juce::String& line)
{
    // The log is written as the run goes, not only at the end. A run that
    // dies in the middle (an access violation, a power cut) then leaves the
    // checks it had reached behind, instead of an empty file that says
    // nothing about where it died. The file is the one the gate reads, and
    // the summary line is appended to it at the end.
    if (logStream == nullptr)
    {
        logFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("upic_selftest.txt");
        logFile.deleteFile();
        logStream = std::make_unique<juce::FileOutputStream> (logFile);
        if (logStream != nullptr && ! logStream->openedOk())
            logStream.reset();
    }
    if (logStream != nullptr)
    {
        const auto utf8 = line.toUTF8();
        logStream->write (utf8.getAddress(), (size_t) utf8.sizeInBytes() - 1);
        logStream->flush();
    }
}

void upic::SelfTest::check (bool ok, const juce::String& name, const juce::String& detail)
{
    juce::String line = (ok ? juce::String ("PASS  ") : juce::String ("FAIL  ")) + name;
    if (detail.isNotEmpty()) line << "  [" << detail << "]";
    line << "\n";
    log << line;
    if (! ok) ++failures;

    writeLine (line);
}

void upic::SelfTest::note (const juce::String& text)
{
    juce::String line = text + "\n";
    log << line;
    writeLine (line);
}

//==============================================================================
// The headless verification: no window, no audio device, no screen capture.
// Checks the DSP engine, the envelope and pitch maths, the JSON round-trip
// (including the files written by the Python version), the file I/O, the
// undo/redo, the crash recovery and the offscreen UI snapshots.
//
// The checks themselves live in the SelfTest*.cpp files, by area. They run
// in the order they are listed here - the same order as before the split.
//
// Returns the number of failed checks (0 = everything passed).

int upic::runSelfTest()
{
    upic::SelfTest t;

    // v1.33.4 (L15, part 3): the selftest must not write a recovery
    // snapshot into the user's own data folder (the app would offer it
    // at the next start)
    MainComponent::setRecoveryAllowed (false);

    upic::runDspChecks (t);
    upic::runAudioThreadChecks (t);
    upic::runLoadedPieceChecks (t);
    upic::runLegacyFixtureChecks (t);
    upic::runThreadValueChecks (t);
    upic::runPieceStateChecks (t);
    upic::runHostTransportChecks (t);
    upic::runDrawingChecks (t);
    upic::runMidiScrubChecks (t);
    upic::runCursorPathChecks (t);
    upic::runLivePathChecks (t);
    upic::runTransformChecks (t);
    upic::runWP8TransformChecks (t);
    upic::runWP8CommandChecks (t);
    upic::runWP8PerformanceViewChecks (t);
    upic::runWP9CurveChecks (t);
    upic::runWP9ConvertChecks (t);
    upic::runWP9WorkspaceChecks (t);
    upic::runWP10EnvelopeChecks (t);
    upic::runWP10ArcChecks (t);
    upic::runWP10WaveChecks (t);
    upic::runWP10PageChecks (t);
    upic::runLayoutCharacterizationChecks (t);
    upic::runBottomLayoutCharacterizationChecks (t);
    upic::runDocumentCharacterizationChecks (t);
    upic::runTransportPreviewCharacterizationChecks (t);
    upic::runFreqTableChecks (t);
    upic::runAmpTableChecks (t);
    upic::runGroupChecks (t);
    upic::runSequenceChecks (t);
    upic::runGoldenRenderChecks (t);
    upic::runModulationChecks (t);
    upic::runModulationSemanticsChecks (t);
    upic::runChannelModelChecks (t);
    upic::runChannelUiChecks (t);
    upic::runChannelRoutingChecks (t);
    upic::runUntrustedPieceChecks (t);
    upic::runModelMathChecks (t);
    // R2.1: the domain's own suite - the units, the validation and the
    // lookups, without the app around them.
    upic::runDomainChecks (t);
    // R2.2: the arc - its transformations, its interpolation and its codec.
    upic::runArcChecks (t);
    // R2.3: the arc's geometry - the frame, the bounds and the transforms.
    upic::runArcGeometryChecks (t);
    // R2.4: the page's codec - the file format of a page.
    upic::runPageCodecChecks (t);
    // R2.5: the arc group's data and its file format.
    upic::runArcGroupCodecChecks (t);
    // R2.6: the envelope's codec - the file format of an envelope.
    upic::runEnvelopeCodecChecks (t);
    // R2.7: the wave's codec - the file format of a wave.
    upic::runWaveCodecChecks (t);
    // R2.8: the channel's codec - the file format of a mixer channel.
    upic::runChannelCodecChecks (t);
    // R2.9: the sample slot's codec - the file format of a sample slot.
    upic::runSampleCodecChecks (t);
    // R2.10: the tuning's codec - the file format of a tuning.
    upic::runTuningCodecChecks (t);
    // R2.11: the sieve zone's codec - the file format of a sieve zone.
    upic::runSieveCodecChecks (t);
    // R2.12: the performance sequence's codec - the file format of a sequence.
    upic::runPerformanceCodecChecks (t);
    // R2.13: the frequency space definition's codec - the file format of a space.
    upic::runFreqSpaceCodecChecks (t);
    // R3.1: the document session - the document's revision and its unsaved flag.
    upic::runDocumentSessionChecks (t);
    // R3.2: the undo history - the document's steps.
    upic::runUndoHistoryChecks (t);
    // R3.3: the selection model - what is selected.
    upic::runSelectionChecks (t);
    // R3.4: the command dispatcher - the unit of change.
    upic::runCommandChecks (t);
    // R3.5: the edits that become commands.
    upic::runModelCommandChecks (t);
    // R4.1: the document's codec - the file format of a piece.
    upic::runPieceCodecChecks (t);
    // R4.2: the document's file adapter - the read and the write.
    upic::runDocumentIOChecks (t);
    // R4.3: the recovery snapshot's storage - the write, the read and the cleanup.
    upic::runRecoveryRepositoryChecks (t);
    // R4.4: the user library's storage - the write, the read and the listing.
    upic::runLibraryRepositoryChecks (t);
    // R5.1: the score canvas's render - the frame, the grid, the arcs and the playhead.
    upic::runScoreRenderChecks (t);
    // R5.2: the coordinate mapper - the time axis, the pitch axis and the zoom.
    upic::runCoordinateMapperChecks (t);
    // R5.3: the hit tester - the arcs near a point, the zone under it and its handles.
    upic::runScoreHitTestChecks (t);
    // R5.4: the editing tools - the stroke, the line, the sine, the erase, the selection, the marquee, the move and the zone.
    upic::runEditingToolChecks (t);
    // R5.5: the gesture transactions - one command per gesture, one change per command.
    upic::runGestureTransactionChecks (t);
    // R5.6: the material inspector - the tabs, the hint and the material card.
    upic::runMaterialInspectorChecks (t);
    // R5.7: the sieve panel - the sieve card, the magnets and the sieve's rules.
    upic::runSievePanelChecks (t);
    // R5.7: the frequency inspector - the page card, the spaces and the ambitus.
    upic::runFrequencyInspectorChecks (t);
    // R5.8: the sequence card and the performance transport - one recorder, one transport.
    upic::runSequenceTransportChecks (t);
    // R5.9: the channel controls - the card, the channel list and the routing.
    upic::runChannelControlsChecks (t);
    // R5.9: the modulation controls - the card, the lists and the commands.
    upic::runModulationControlsChecks (t);
    // R5.10: the MIDI settings - the device, the learn rows and the bindings.
    upic::runMidiSettingsChecks (t);
    // R5.10: the sample material panel - the slot, the level and the pitch mode.
    upic::runSampleMaterialChecks (t);
    // R5.10: the live input panel - the input, the pitch, the reverse and the grain.
    upic::runLiveInputChecks (t);
    // R5.10: the phase modulation panel - the ops, the presets and the voice.
    upic::runPhaseModulationChecks (t);
    upic::runLibraryModelChecks (t);
    upic::runWP7Checks (t);
    upic::runExportChecks (t);
    upic::runSieveChecks (t);
    upic::runTuningChecks (t);
    upic::runMaterialChecks (t);
    upic::runEditorChecks (t);
    upic::runSnapshotChecks (t);
    upic::runContinuityChecks (t);
    upic::runRealtimeChecks (t);
    upic::runArcOverrideChecks (t);
    upic::runPanelChecks (t);
    upic::runPmPresetChecks (t);
    upic::runWidgetChecks (t);
    upic::runLibraryUiChecks (t);
    upic::runInterfaceChecks (t);
    upic::runFocusChecks (t);
    upic::runViewChecks (t);
    upic::runKnobChecks (t);
    upic::runMidiInputChecks (t);
    upic::runEnvelopeChecks (t);
    upic::runTransportChecks (t);
    upic::runTransportStateChecks (t);
    upic::runChooserChecks (t);
    // R1.3 last: the overlay suite opens the audio device selector and
    // refreshes the MIDI device list, and nothing after it should see that.
    upic::runOverlayLayoutCharacterizationChecks (t);

    const juce::String summary = "\n" + (t.failures == 0 ? juce::String ("ALL CHECKS PASSED")
                                                            : juce::String (t.failures) + " CHECK(S) FAILED") + "\n";
    t.log << summary;
    if (t.logStream != nullptr)
    {
        const auto utf8 = summary.toUTF8();
        t.logStream->write (utf8.getAddress(), (size_t) utf8.sizeInBytes() - 1);
        t.logStream->flush();
        t.logStream.reset();
    }
    else
    {
        // the stream could not be opened - the whole log is written at once
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("upic_selftest.txt").replaceWithText(t.log);
    }
    DBG(t.log);
    return t.failures;
}