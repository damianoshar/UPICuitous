#include "MainComponent.h"
#include "PageCodec.h"
#include "EnvelopeCodec.h"
#include "WaveformCodec.h"
#include "ChannelCodec.h"
#include "SampleCodec.h"
#include "PerformanceCodec.h"
#include "TuningCodec.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The piece: the model it is saved as, the load, the save, the
// samples and the PM presets.
namespace
{
    // The size of a piece, in words a person can read ("12 MB").
    juce::String humanPieceSize (juce::int64 bytes)
    {
        return bytes >= 1024 * 1024 ? (juce::String ((int) (bytes / (1024 * 1024))) + " MB")
                                    : (juce::String ((int) (bytes / 1024)) + " KB");
    }
}

//==============================================================================
// v1.33.4 (M28): the piece as a self-contained model.
//
// The saver used to build the JSON - and with it the base64 of every sample
// - on the message thread, while holding the model lock: a 30 MB piece froze
// the window for a moment, and the audio thread waited for the lock. The
// model is captured on the message thread instead (a copy: the sample audio
// is shared, so the copy is eight pointers), and the worker builds the JSON
// from the copy.
upic::PieceModel MainComponent::capturePieceModel() const
{
    upic::PieceModel m;
    {
        const juce::ScopedLock l (dataLock);
        m.waveforms = waveforms;
        m.envelopes = envelopes;
        m.amplitudeTables = ampTables;
        m.sequences = sequences;
        m.channels = channels;
        m.nextChannelId = nextChannelId;
        m.sampleSlots = sampleSlots;
        m.pages = pages;
        m.currentPage = currentPage;
    }
    m.seqPages = seqPages;
    m.recordPressure = recordPressure;
    m.scrubAudition = scrubAudition;
    m.muteWhileDragging = muteWhileDragging.load();
    m.dragSmoothMs = dragSmoothMs.load();
    m.scrubGrainMs = scrubGrainMs;
    m.sampleGrainMs = sampleGrainMs;
    m.liveGrainMs = liveGrainMs;
    m.sieveOn = sieveOn;
    m.sieveM = sieveM;
    m.sieveR = sieveR;
    m.stickiness = stickiness;
    m.timeGrid = timeGrid;
    m.tuning = tuning;   // M4: the scale the piece was drawn in
    m.masterGain = masterGain.load();
    m.samplePitchMode = engine.getSamplePitchMode();
    m.livePitchMode = engine.getLivePitchMode();
    // WP9: the workspace layout travels with the piece (the pages the
    // canvases show and the split). An ordinary piece has none, and then
    // the key is not written at all.
    if (workspaceOpen)
    {
        m.workspacePages = workspacePages;
        m.workspaceSplit = workspace.getSplit();
    }
    return m;
}

juce::var MainComponent::pieceToVar() const
{
    return upic::pieceToVar (capturePieceModel());
}

bool MainComponent::loadFromVar(const juce::var& v)
{
    // R4.1: the codec reads the document (Source/PieceCodec.h) - it decodes
    // it, migrates it, validates it and repairs what it has to repair - and
    // hands the candidate over. A document that is not a piece is refused
    // here, and the active piece is not touched: the unsaved work stays.
    upic::PieceModel candidate;
    upic::PieceDiagnostics diag;
    if (! upic::pieceFromVar (v, candidate, diag))
        return false;

    commitPieceModel (candidate, diag);
    return true;
}

//==============================================================================
// R4.1: the commit - the candidate is put in force.
//
// Everything below is the shell's work: the swap under the model lock, the
// selection, the controls, the engine, the snapshot the audio thread reads
// and the messages the user is told. The document's *format* is the codec's
// (Source/PieceCodec.h); this is where the format becomes the app.
void MainComponent::commitPieceModel (const upic::PieceModel& m,
                                      const upic::PieceDiagnostics& diag)
{
    // --- the piece is good: swap it in ---
    // An ordinary scrub/freeze request belongs to the piece that is about
    // to be replaced. End it before the swap so the next model snapshot
    // cannot be rendered through the old request. Undo and redo restore
    // through this same loader, so they are covered too.
    cancelOrdinaryPreviewRoutes();
    // WP9: the workspace's canvases point at the pages that are about to be
    // replaced, so the workspace is closed before the swap (and opened again
    // with the layout the piece carries, below).
    if (workspaceOpen) setWorkspaceOpen (false);
    {
        juce::ScopedLock l(dataLock);
        stopPlayback();
        // stopPlayback only starts a fade while playing: make the stop immediate
        // here, since the piece (and possibly its pages) is about to be replaced
        transportState.store ((int) TransportState::stopped, std::memory_order_relaxed);
        waveforms = m.waveforms;
        envelopes = m.envelopes;
        ampTables = m.amplitudeTables;
        sequences = m.sequences;
        channels = m.channels;
        nextChannelId = m.nextChannelId;
        document.selection().channel = channels.empty() ? -1 : channels.front().id;
        document.selection().sequence = sequences.empty() ? -1 : sequences.front().id;
        pages = m.pages;
        sampleSlots = m.sampleSlots;
    }
    // The frequency-table editor captured a page/space in the piece that was
    // just replaced. Close it before any stale callback can use that
    // destination in the new model. Undo and redo restore through this same
    // loader, so they are covered too.
    invalidateFreqTableDestination();
    // WP7: a captured extraction source belongs to the piece that was just
    // replaced. Closing it here also covers undo and redo, which restore a
    // piece through this same path.
    invalidateSampleExtractSource();
    {
        juce::ScopedLock l(dataLock);
        // clamp ids
        currentPage = juce::jlimit(0, (int)pages.size() - 1, m.currentPage);
        // WP9: the workspace's first canvas is the live page, so the page
        // the piece was playing takes the first place in the workspace
        // (the list rotates when it does not). The piece's own current
        // page wins - it is the page the musician left the piece on.
    // SECURITY (S-03): the master gain is clamped to the slider's range, so a
    // piece cannot push the mix into clipping or silence.
    masterGain.store (m.masterGain);
    markModelChanged(); // v1.15: the whole piece was replaced -> fade across it
    // v1.1 state (defaults keep old / Python-written files working unchanged)
    seqPages = m.seqPages;
    recordPressure = m.recordPressure;
    scrubAudition = m.scrubAudition;
    muteWhileDragging.store (m.muteWhileDragging, std::memory_order_relaxed);
    dragSmoothMs.store (m.dragSmoothMs, std::memory_order_relaxed);
    scrubGrainMs = m.scrubGrainMs;
    sampleGrainMs = m.sampleGrainMs;
    liveGrainMs = m.liveGrainMs;
    sieveOn = m.sieveOn;
    sieveM = m.sieveM;
    sieveR = m.sieveR;
    seqBtn.setToggleState(seqPages, juce::dontSendNotification);
    pressBtn.setToggleState(recordPressure, juce::dontSendNotification);
    scrubBtn.setToggleState(scrubAudition, juce::dontSendNotification);
    muteDragBtn.setToggleState(muteWhileDragging.load(), juce::dontSendNotification);
    dragSmoothSlider.setValue((double) dragSmoothMs.load(), juce::dontSendNotification);
    scrubGrainSlider.setValue((double) scrubGrainMs, juce::dontSendNotification);
    grainSlider.setValue((double) sampleGrainMs, juce::dontSendNotification);
    liveGrainSlider.setValue((double) liveGrainMs, juce::dontSendNotification);
    engine.setScrubGrainMs(scrubGrainMs);
    engine.setSampleGrainMs(sampleGrainMs);
    engine.setLiveGrainMs(liveGrainMs);
    scrubEngine.setScrubGrainMs(scrubGrainMs);
    scrubEngine.setSampleGrainMs(sampleGrainMs);
    scrubEngine.setLiveGrainMs(liveGrainMs);
    // v1.33.4 (H7): the global pitch modes come back with the piece. Absent
    // in an older piece -> the session keeps what it had (backward
    // compatible), present -> the piece's mode wins. A negative mode says
    // "the document carried none".
    {
        bool changed = false;
        const int samplePm = m.samplePitchMode;
        const int livePm = m.livePitchMode;
        const int sampleMode = samplePm >= 0 ? samplePm : engine.getSamplePitchMode();
        const int liveMode = livePm >= 0 ? livePm : engine.getLivePitchMode();
        // v1.33.4 (M21): the vocoder's working memory is prepared BEFORE the
        // audio thread can see the new modes
        prepareVocodersIfNeeded (sampleMode, liveMode);
        if (samplePm >= 0)
        {
            const int pm = sampleMode;
            engine.setSamplePitchMode(pm);
            scrubEngine.setSamplePitchMode(pm);
            samplePitchBox.setSelectedId(pm + 1, juce::dontSendNotification);
            score.sampleGranularTab = (pm == 1);
            changed = true;
        }
        if (livePm >= 0)
        {
            const int pm = liveMode;
            engine.setLivePitchMode(pm);
            livePitchBox.setSelectedId(pm + 1, juce::dontSendNotification);
            changed = true;
        }
        if (changed) markModelChanged();  // the voices re-anchor -> fade across it
    }
    sieveBtn.setToggleState(sieveOn, juce::dontSendNotification);
    sieveMEdit.setText(juce::String(sieveM), juce::dontSendNotification);
    {
        juce::String rt;
        for (size_t i = 0; i < sieveR.size(); ++i)
            rt += (i > 0 ? "," : "") + juce::String(sieveR[i]);
        sieveREdit.setText(rt, juce::dontSendNotification);
    }
    score.setRecordPressure(recordPressure);
    score.setSieve(sieveOn, sieveM, sieveR);
    // M4: the tuning the piece was drawn in. An old piece carries none ->
    // 12-tone equal temperament, which is what it was drawn in.
    tuning = m.tuning;
    score.setTuning (tuning);
    // v1.2 state (defaults: full stickiness, no time grid = v1.1 behaviour)
    stickiness = m.stickiness;
    timeGrid = m.timeGrid;
    stickSlider.setValue(stickiness, juce::dontSendNotification);
    score.setStickiness(stickiness);
    {
        static const double divs[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        int gi = 0;
        for (int i = 0; i < 7; ++i) if (std::abs(divs[i] - timeGrid) < 1e-9) gi = i;
        gridBox.setSelectedId(gi + 1, juce::dontSendNotification);
        score.setTimeGrid(timeGrid);
    }
    document.selection().wave = waveforms.front().id; document.selection().envelope = envelopes.front().id;
    document.selection().sampleSlot = 0;
    drawMatMode = score.getLayer(); // the tab stays where it is across a load
    syncMaterialTabs();
    drawRef = upic::kDefaultRefFreq;
    refSlider.setValue(drawRef, juce::dontSendNotification);
    seekPlayhead(0.0);
    bumpPageEpoch();
    masterSlider.setValue(masterGain.load(), juce::dontSendNotification);
    } // the settings scope

    // Control refreshes and the model publication run after the model lock
    // has been released. A refresh can repopulate controls whose callbacks
    // read the model, so it must never run inside that lock.
    refreshBoxes(); refreshScoreBinding();
    // WP9: the workspace opens with the layout the piece carried.
    if (m.workspacePages.size() >= 2)
        setWorkspaceOpen (true, m.workspacePages, m.workspaceSplit);
    // v1.18: the next audio block must already render the loaded piece (the
    // timer alone would be up to 33 ms late - and a host may not run it).
    publishModelSnapshot();
    // v1.33.4 (H13): the piece was over one of the format's caps, so part of
    // it did not make it in. Say so instead of losing the music silently.
    if (! diag.notLoaded.isEmpty())
        flash (diag.notLoaded);
    else if (! diag.adjusted.isEmpty())
        flash (diag.adjusted);

    // v1.33.4 (L15): the loaded piece is what the file holds, so there is
    // nothing to save yet (the app must not ask to save a piece that was
    // just opened).
    markPieceSaved();
}


juce::String MainComponent::pieceStateText()
{
    // v1.33.4 (M28): pieceToVar() captures the model under the lock and the
    // JSON is built from the copy, so the lock is only held for the capture.
    return juce::JSON::toString(pieceToVar(), false);
}

void MainComponent::saveFile (std::function<void (bool)> onDone)
{
    // v1.33.4 (H11): the chooser can stay open while the window is closed.
    // The safe pointer is checked before the body runs, so the raw `this`
    // below is only ever used while the component is alive.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>("Save UPIC piece", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("piece.upic.json"), "*.json");
    fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, sp, fc, onDone](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the destination can be a document-provider URL
            // (Android) - getResult() would drop it and the save would do
            // nothing. A local destination is written in place (through a
            // temporary sibling); a provider document is written through its
            // output stream.
            const auto url = c.getURLResult();
            if (! url.isWellFormed())
            {
                // the chooser was cancelled: the piece is still unsaved
                if (onDone) onDone (false);
                return;
            }
            // v1.33.4 (M28): the save runs on a worker thread - the samples
            // become base64 and the JSON is built there, so a big piece does
            // not freeze the window. The model is captured here, under the
            // lock.
            auto model = std::make_shared<upic::PieceModel> (sp->capturePieceModel());
            const juce::int64 maxBytes = (juce::int64) upic::limits::kMaxPieceBytes;
            // v1.33.4 (L15): the epoch the saved piece came from. If the
            // model moved on while the worker was writing, the piece on the
            // disk is already older than what is on the screen - the app
            // must still ask to save.
            //
            // R3.4: the epoch, not the revision. The epoch moves with every
            // change (a drag in progress moves it with every mouse-move),
            // while the revision moves once per command - a drag that is
            // still open would not be seen, and the app would forget that
            // the piece on the disk is older than the one on the screen.
            const juce::uint64 savedEpoch = sp->document.epoch();
            sp->startJob ("Saving the piece",
                [model, url, maxBytes] (Job& job)
                {
                    const juce::String json = juce::JSON::toString (upic::pieceToVar (*model), true);
                    const juce::int64 bytes = (juce::int64) json.getNumBytesAsUTF8();
                    if (bytes > maxBytes)
                    {
                        job.ok = false;
                        job.message = "Piece too large to save ("
                                    + humanPieceSize (bytes) + ", the limit is "
                                    + humanPieceSize (maxBytes) + ") - shorten the capture or the sample, "
                                    + "or export the audio to a WAV instead";
                        return;
                    }
                    juce::String error;
                    if (! upic::writePickedBytes (url, json.toRawUTF8(), (size_t) bytes, error))
                    {
                        job.ok = false;
                        job.message = error;
                        return;
                    }
                    job.ok = true;
                    job.message = "Saved " + upic::pickedFileName (url);
                },
                [sp, onDone, savedEpoch] (Job& job)
                {
                    sp->flash (job.message);
                    // v1.33.4 (L15): the piece is on the disk now, so there
                    // is nothing to ask about when the app closes - unless
                    // the model moved on while the worker was writing.
                    if (job.ok && sp->document.epoch() == savedEpoch)
                        sp->markPieceSaved();
                    if (onDone) onDone (job.ok);
                });
        });
}

//==============================================================================
// v1.33.4 (H6): the piece saver.
//
// The loader refuses a piece over the file cap, but the saver used to write
// whatever it had: a 30 s stereo capture at 96 kHz is 15.4 MB of base64, so
// the app saved a file it then refused to open ("Piece too large"). The
// saver measures the piece first, refuses it with a message that says what
// to do, and never touches the destination when it refuses - the file that
// was there before is still there. The write itself goes to a temporary
// sibling and replaces the target only when the whole piece is on disk.
bool MainComponent::savePieceToFile (const juce::File& f, juce::int64 maxBytes)
{
    if (f == juce::File()) return false;
    return savePieceToURL (juce::URL (f), maxBytes);
}

// v1.33.4 (M17): the same saver, but the destination can also be a
// document-provider document (Android). Such a document cannot take a
// temporary sibling, so the piece is written through its output stream.
bool MainComponent::savePieceToURL (const juce::URL& url, juce::int64 maxBytes)
{
    if (! url.isWellFormed()) return false;

    // v1.33.4 (M28): the model is captured under the lock and the JSON is
    // built from the copy - the samples become base64, and that is the heavy
    // part. The window's own save path runs this on a worker thread; this
    // one is the synchronous form the selftest drives.
    const auto model = capturePieceModel();
    const juce::String json = juce::JSON::toString (upic::pieceToVar (model), true);

    const juce::int64 bytes = (juce::int64) json.getNumBytesAsUTF8();
    auto humanSize = [] (juce::int64 b) -> juce::String
    {
        return b >= 1024 * 1024 ? (juce::String ((int) (b / (1024 * 1024))) + " MB")
                                : (juce::String ((int) (b / 1024)) + " KB");
    };

    if (bytes > maxBytes)
    {
        flash ("Piece too large to save (" + humanSize (bytes) + ", the limit is "
               + humanSize (maxBytes) + ") - shorten the capture or the sample, "
               "or export the audio to a WAV instead");
        return false;
    }

    juce::String error;
    if (! upic::writePickedBytes (url, json.toRawUTF8(), (size_t) bytes, error))
    {
        flash (error);
        return false;
    }

    flash ("Saved " + upic::pickedFileName (url));
    // v1.33.4 (L15): the piece is on the disk now, so there is nothing to
    // ask about when the app closes.
    markPieceSaved();
    // v1.33.4 (L15, part 3): the piece is safe, so the recovery snapshot is
    // not needed any more (it would offer a stale copy at the next start).
    if (! pluginMode_)
        deleteRecoverySnapshot (recoveryDirectory());
    return true;
}

void MainComponent::loadFile()
{
    // v1.33.4 (H11): the safe pointer is checked before the body runs, so the
    // raw `this` below is only ever used while the component is alive.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>("Load UPIC piece", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, sp, fc](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the chooser can return a document-provider URL
            // (Android) - getResult() drops it (it keeps only local files),
            // so the load did nothing at all. The URL is read instead, and a
            // provider document is copied into a local file first (the copy
            // is bounded by the piece cap).
            upic::PickedFile picked;
            juce::String error;
            if (! upic::readPickedFile (c.getURLResult(), picked,
                                        (juce::int64) upic::limits::kMaxPieceBytes, error))
            {
                sp->flash (error);
                return;
            }
            const auto f = picked.file;
            if (!f.existsAsFile()) return;
            // SECURITY (S-03): a piece is untrusted input. Refuse to read a
            // file that is far bigger than any real piece, so a huge or
            // hostile file cannot exhaust memory before it is even parsed.
            if (f.getSize() > (juce::int64) upic::limits::kMaxPieceBytes)
            {
                // v1.33.4 (H6): the limit is derived from the sample budget
                // now, so the message states the real number.
                flash ("Piece too large (max "
                       + juce::String ((int) (upic::limits::kMaxPieceBytes / (1024 * 1024))) + " MB)");
                return;
            }
            // SECURITY (S-03): the whole load is wrapped, so a piece that
            // exhausts the memory or trips an assertion in the parser takes
            // the app down with a message instead of taking the app down.
            try
            {
                auto txt = f.loadFileAsString();
                if (txt.getNumBytesAsUTF8() > upic::limits::kMaxPieceBytes) return;
                auto v = juce::JSON::parse(txt);
                if (v.isVoid())
                {
                    // v1.33.4 (M25): the file is not a piece at all - the
                    // current piece is kept, so a bad file cannot wipe the
                    // unsaved work.
                    flash ("Not a UPIC piece (the JSON is broken) - the current piece was kept");
                    return;
                }
                if (! loadFromVar(v))
                {
                    flash ("This file is not a UPIC piece - the current piece was kept");
                    return;
                }
            }
            catch (const std::exception& e)
            {
                flash (std::string ("Could not load the piece: ") + e.what());
            }
            catch (...)
            {
                flash ("Could not load the piece");
            }
        });
}

//==============================================================================
// M4: the Scala tuning import.
//
// The tuning is the scale the drawing snaps to. The arcs hold frequencies,
// not pitches, so a tuning does not retune anything that is already drawn:
// it changes the axis the pencil, the magnet and the sieve work on. The
// scale travels with the piece, so a piece is edited the way it was drawn.
void MainComponent::importScalaTuning()
{
    // v1.33.4 (H11): the safe pointer is checked before the body runs, so
    // the raw `this` below is only ever used while the component is alive.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>(
        "Import a Scala tuning (.scl)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.scl;*.SCL;*.txt");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, sp, fc](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the chooser can return a document-provider URL
            // (Android) - getResult() drops it, so the file is read through
            // the URL instead.
            upic::PickedFile picked;
            juce::String error;
            if (! upic::readPickedFile (c.getURLResult(), picked,
                                        (juce::int64) upic::limits::kMaxTuningBytes, error))
            {
                sp->flash (error);
                return;
            }
            const auto f = picked.file;
            if (! f.existsAsFile()) return;
            const auto text = f.loadFileAsString();
            if (text.getNumBytesAsUTF8() > (juce::int64) upic::limits::kMaxTuningBytes)
            {
                sp->flash ("The tuning file is too large");
                return;
            }
            upic::Tuning t;
            juce::String parseError;
            if (! upic::parseScalaTuning (text, t, parseError))
            {
                sp->flash ("Not a Scala tuning - " + parseError);
                return;
            }
            sp->applyTuning (t);
        });
}

// The tuning becomes the piece's tuning: the editor snaps to it, and the
// piece carries it when it is saved.
void MainComponent::applyTuning (const upic::Tuning& t)
{
    runEdit ("Tuning", [this, &t]{
        tuning = t;
        score.setTuning (tuning);
        // the arcs keep their frequencies - the tuning is not part of the audio
        // model, so no snapshot is published. The piece itself has changed, so
        // the app asks to save it.
        markModelChanged();
    });
    flash (tuning.custom
               ? ("Tuning: " + tuning.describe() + " (the sieve now counts the scale's degrees)")
               : "Tuning: 12-tone equal temperament");
}

//==============================================================================
// v1.33.4 (M28): the decode, off the message thread.
//
// The decoder read the whole file and made it seamless on the message
// thread: a 30 s WAV froze the window for a second. The decode now runs on
// the worker thread and the message thread installs the result in the slot.
bool MainComponent::decodeSampleFile (const juce::File& f, SampleImport& out, juce::String& error)
{
    // SECURITY (S-03): the decode and the allocation are wrapped, so a
    // crafted file that exhausts the memory shows a message instead of
    // taking the app down.
    try
    {
        // v1.33.4 (M19): the material keeps the file's own rate. The old
        // code clamped the rate to 96 kHz and kept the frames, so a
        // 192 kHz file was played at half speed. A file above the app's
        // ceiling is refused with a message instead of being retuned, the
        // duration cap is counted at the file's own rate, and the
        // decoder's return value is checked.
        upic::LoadedSample loaded;
        if (! upic::loadSampleFile (f, loaded, error))
            return false;
        out.sample = std::move (loaded);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string ("Could not load the audio file: ") + e.what();
        return false;
    }
    catch (...)
    {
        error = "Could not load the audio file";
        return false;
    }
}

// The selftest installs synthetic material through the same limits as a
// decoded file. A test-only hook must not be able to create state that the
// production loader would refuse.
bool MainComponent::setSampleSlotForTest (int slotIndex,
                                          const juce::AudioBuffer<float>& data,
                                          double sampleRate)
{
    if (slotIndex < 0 || slotIndex >= (int) sampleSlots.size()) return false;
    if (data.getNumChannels() < 1 || data.getNumChannels() > 2) return false;
    if (data.getNumSamples() <= 0
        || data.getNumSamples() > upic::limits::kMaxFramesPerSample) return false;
    if (! (sampleRate >= 1000.0 && sampleRate <= upic::limits::kMaxSampleRate)) return false;
    if ((double) data.getNumSamples() / sampleRate > upic::limits::kMaxSampleSeconds)
        return false;

    for (int c = 0; c < data.getNumChannels(); ++c)
        for (int i = 0; i < data.getNumSamples(); ++i)
            if (! std::isfinite (data.getSample (c, i))) return false;

    const juce::int64 newBytes = (juce::int64) data.getNumSamples()
                               * (juce::int64) data.getNumChannels()
                               * (juce::int64) sizeof (float);
    {
        const juce::ScopedLock l (dataLock);
        juce::int64 totalBytes = 0;
        for (size_t i = 0; i < sampleSlots.size(); ++i)
        {
            if ((int) i == slotIndex || ! sampleSlots[i].loaded()) continue;
            totalBytes += (juce::int64) sampleSlots[i].data->getNumSamples()
                        * (juce::int64) sampleSlots[i].data->getNumChannels()
                        * (juce::int64) sizeof (float);
        }
        if (totalBytes + newBytes > (juce::int64) upic::limits::kMaxTotalSampleBytes)
            return false;

        // WP7: the test hook follows the production replacement path.
        invalidateSampleExtractSource();

        auto& slot = sampleSlots[(size_t) slotIndex];
        slot.data = std::make_shared<const juce::AudioBuffer<float>> (data);
        slot.sampleRate = sampleRate;
        slot.resetEdits();
    }

    document.selection().sampleSlot = slotIndex;
    score.sampleDurations[sampleSlots[(size_t) slotIndex].id] =
        sampleSlots[(size_t) slotIndex].durationSec();
    markModelChanged();
    publishModelSnapshot();
    updateSampleView();
    return true;
}

// The install, on the message thread: the model is read by the audio thread,
// so it is mutated under the lock.
void MainComponent::installSample (int slotIndex, SampleImport&& import)
{
    if (slotIndex < 0 || slotIndex >= (int) sampleSlots.size()) return;
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the material was replaced.
    beginEdit ("Load the material");
    // WP7: the replacement invalidates any analysis captured from the old
    // material before the new buffer enters the model.
    invalidateSampleExtractSource();
    {
        // model is read by the audio thread -> mutate under the lock.
        // v1.18: the buffer is immutable + shared, so the pointer is
        // replaced instead of written; snapshots that still hold the
        // old pointer keep it alive until they are retired.
        juce::ScopedLock l (dataLock);
        auto& slot = sampleSlots[(size_t) slotIndex];
        slot.name = import.name;
        slot.sampleRate = import.sample.sampleRate;
        slot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move (import.sample.data));
        // M5: the material's edits (the trim, the fades, the normalise
        // gain) belong to the material that was there - the new file has
        // none of them. The Ref belongs to the slot, so it stays.
        slot.resetEdits();
        markModelChanged(); // v1.15: new material -> fade across it
        publishModelSnapshot(); // the next block must see it (no timer needed)
    }
    score.sampleDurations[sampleSlots[(size_t) slotIndex].id] = sampleSlots[(size_t) slotIndex].durationSec();
    document.selection().sampleSlot = slotIndex;
    refreshBoxes();
    updateSampleView();
    pushVoice();
    endEdit();
    repaint();
}

void MainComponent::loadSampleIntoSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= (int) sampleSlots.size()) return;

    // v1.33.4 (H11): the safe pointer is checked before the body runs, so the
    // raw `this` below is only ever used while the component is alive.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>(
        "Load audio material (played + pitch-shifted along arcs)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.flac;*.ogg;*.aiff;*.aif");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, sp, fc, slotIndex](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the chooser can return a document-provider URL
            // (Android) - getResult() drops it (it keeps only local files),
            // so the import did nothing at all. The URL is read instead, and
            // a provider document is copied into a local file first (the
            // decoders need a file they can seek in).
            upic::PickedFile picked;
            juce::String error;
            if (! upic::readPickedFile (c.getURLResult(), picked,
                                        (juce::int64) upic::limits::kMaxTotalSampleBytes, error))
            {
                sp->flash (error);
                return;
            }
            const auto f = picked.file;
            const auto materialName = picked.name.isNotEmpty()
                                        ? juce::File (picked.name).getFileNameWithoutExtension()
                                        : f.getFileNameWithoutExtension();

            // v1.33.4 (M28): the decode runs on a worker thread, so the
            // window stays alive while a long file is decoded.
            auto imported = std::make_shared<SampleImport>();
            sp->startJob ("Decoding \"" + materialName + "\"",
                [f, materialName, imported] (Job& job)
                {
                    juce::String err;
                    const bool ok = decodeSampleFile (f, *imported, err);
                    imported->name = materialName;
                    job.ok = ok;
                    job.message = ok ? imported->sample.warning : err;
                },
                [sp, slotIndex, imported, materialName] (Job& job)
                {
                    if (! job.ok)
                    {
                        sp->flash (job.message.isNotEmpty() ? job.message
                                                           : "Could not load the audio file");
                        return;
                    }
                    sp->installSample (slotIndex, std::move (*imported));
                    sp->flash (job.message.isNotEmpty()
                                ? job.message
                                : ("Loaded \"" + materialName + "\" into slot "
                                   + juce::String (sp->sampleSlots[(size_t) slotIndex].id)));
                });
        });
}

//==============================================================================
// v1.33.4 (H12): the PM preset file operations, split out of the file
// dialogs. The dialogs used to build a stack-local FileChooser, which was
// destroyed when the button handler returned - JUCE drops the pending
// callback with the chooser, so neither the import nor the export ever ran.
// The file work lives here, so it can also be driven by the selftest.
juce::String MainComponent::buildPmPresetText()
{
    const int id = score.getSelectedArc();
    double ratio = 0.0, index = 0.0, fb = 0.0;
    int envId = 0;
    bool found = false;
    {
        const juce::ScopedLock sl (dataLock);
        for (auto& a : curPage().arcs)
            if (a.id == id && a.mode == 3)
            {
                ratio  = a.pmRatio;
                index  = a.pmIndex;
                fb     = a.pmFeedback;
                envId  = a.pmIndexEnv;
                found  = true;
                break;
            }
    }
    if (! found) return {};

    std::ostringstream o;
    o << "[preset]\nname=UPIC PM export\nmaster=0.8\ntune=440\n";
    o << "[op0]\nenabled=1\nwave=0\nratio=1\nlevel=0.9\nfb=0\nenv=" << envId << "\nsustain=0\nrelease=0\n";
    o << "[op1]\nenabled=1\nwave=0\nratio=" << ratio << "\nlevel=" << (index / 5.0) << "\nfb=" << fb << "\nenv=" << envId << "\nsustain=0\nrelease=0\n";
    o << "[matrix]\nm1_0=0.8\nout0=1.0\n";
    o << "fb1=" << fb << "\n";
    return juce::String (o.str());
}

void MainComponent::applyPmPresetFile (const juce::File& f)
{
    if (f == juce::File()) return;
    std::ifstream in (f.getFullPathName().toStdString());
    if (! in) { flash ("Could not read preset"); return; }
    std::string s ((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto getField = [&] (const std::string& key) -> std::string {
        auto pos = s.find (key + "=");
        if (pos == std::string::npos) return "";
        auto end = s.find ("\n", pos);
        return s.substr (pos + key.size() + 1, (end == std::string::npos ? s.size() : end) - (pos + key.size() + 1));
    };
    // SECURITY (S-03): std::stod accepts "nan" and "inf", and the
    // engine's jlimit() passes NaN through unchanged - so a crafted
    // preset could put a NaN into the PM parameters and from there
    // into the audio thread. The parsed value is checked here.
    auto parseFloat = [&] (const std::string& k, double d) {
        if (k.empty()) return d;
        try {
            const double v = std::stod (k);
            return std::isfinite (v) ? v : d;
        } catch (...) { return d; }
    };
    std::string op1ratio, op1level, op1fb;
    {
        auto pos = s.find ("[op1]");
        if (pos != std::string::npos) {
            auto next = s.find ("[", pos + 1);
            auto sec = s.substr (pos, next == std::string::npos ? s.size() - pos : next - pos);
            auto gr = [&] (const std::string& key) {
                auto p2 = sec.find (key + "=");
                if (p2 == std::string::npos) return std::string();
                auto e2 = sec.find ("\n", p2);
                return sec.substr (p2 + key.size() + 1, (e2 == std::string::npos ? sec.size() : e2) - (p2 + key.size() + 1));
            };
            op1ratio = gr ("ratio"); op1level = gr ("level"); op1fb = gr ("fb");
        }
    }
    const double r   = parseFloat (op1ratio.empty() ? getField ("ratio") : op1ratio, 2.0);
    const double lvl = parseFloat (op1level.empty() ? "0.8" : op1level, 0.8);
    const double fb  = parseFloat (op1fb, 0.0);
    const int envId  = (int) parseFloat (getField ("pmIndexEnv"), 0.0);
    const juce::ScopedLock sl (dataLock);
    const auto ids = score.getSelectedArcs();
    for (auto& a : curPage().arcs)
    {
        if (! ids.empty() && std::find (ids.begin(), ids.end(), a.id) == ids.end()) continue;
        if (a.mode != 3) continue;
        a.pmRatio = upic::limits::clampFinite (r, 0.25, 16.0);
        a.pmIndex = upic::limits::clampFinite (lvl * 5.0, 0.0, 100.0);
        a.pmFeedback = upic::limits::clampFinite (fb, -1.0, 1.0);
        a.pmIndexEnv = envId;
    }
    pmRatioSlider.setValue (upic::limits::clampFinite (r, 0.25, 16.0), juce::dontSendNotification);
    pmIndexSlider.setValue (upic::limits::clampFinite (lvl * 5.0, 0.0, 100.0), juce::dontSendNotification);
    { int ei = 1; for (size_t j = 0; j < envelopes.size(); ++j) if (envelopes[j].id == envId) { ei = (int) j + 2; break; } pmIndexEnvBox.setSelectedId (ei, juce::dontSendNotification); }
    pmFeedbackSlider.setValue (upic::limits::clampFinite (fb, -1.0, 1.0), juce::dontSendNotification);
    pushVoice();
    publishModelSnapshot();
    flash ("Imported PM preset");
}

