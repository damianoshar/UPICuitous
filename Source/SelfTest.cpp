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