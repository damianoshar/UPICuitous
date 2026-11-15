#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The files: the piece (load, save, undo, the crash recovery), the samples,
// the PM presets and the exports.

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
// v1.33.4 (M28): the export's model - captured on the message thread, under
// the lock, so the worker thread never touches the component again.
MainComponent::ExportModel MainComponent::captureExportModel() const
{
    ExportModel m;
    {
        const juce::ScopedLock l (dataLock);
        m.page  = curPage();
        m.waves = waveforms;
        m.envs  = envelopes;
        m.slots = sampleSlots;
    }
    m.sr = exportSampleRate();
    m.masterGain = masterGain.load();
    m.samplePitchMode = engine.getSamplePitchMode();
    m.sampleGrainMs = sampleGrainMs;
    m.liveGrainMs = liveGrainMs;
    return m;
}

// The heavy work, on the calling thread (the worker in the app, the
// selftest's thread in the test). The progress callback is called with the
// percentage done and returns false to cancel the job - a cancelled export
// leaves the file that was there before alone.
bool MainComponent::exportPageToFile (const juce::File& target, const ExportModel& model,
                                      const std::function<bool (int percent)>& progress)
{
    return writePageToWavFile (target, model.page, model.waves, model.envs, model.slots,
                               model.sr, model.masterGain, model.samplePitchMode,
                               model.sampleGrainMs, model.liveGrainMs, progress);
}

StemExport MainComponent::exportStemsToDir (const juce::File& dir, const ExportModel& model,
                                            const std::function<bool (int percent)>& progress)
{
    return renderStems (model.page, model.waves, model.envs, model.slots, dir, "stem",
                        model.sr, model.masterGain, model.samplePitchMode,
                        model.sampleGrainMs, model.liveGrainMs, progress);
}

void MainComponent::exportStems()
{
    // v1.33.4 (H11): the chooser can stay open while the window is closed,
    // so the callback carries a safe pointer instead of a raw `this`.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>(
        "Export stems (one WAV per voice) -- choose a folder",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [sp, fc](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the folder can be a document tree (Android).
            // getResult() drops it (it keeps only local files), so the export
            // did nothing at all. A provider folder cannot be written to
            // directly: the stems are rendered into a local temporary folder
            // and then copied into the chosen tree.
            const auto url = c.getURLResult();
            if (! url.isWellFormed()) return;
            const bool isLocal = upic::isPlainFileURL (url);
            juce::File dir;
            if (isLocal)
            {
                dir = url.getLocalFile();
            }
            else
            {
                dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("upic_stems_export");
                dir.deleteRecursively();
                if (! dir.createDirectory())
                {
                    sp->flash ("Could not make a temporary folder for the stems");
                    return;
                }
            }
            // v1.33.4 (M28): the render runs on a worker thread, so the
            // window stays alive and the audio thread is not held up by it.
            // The model is captured here, under the lock.
            auto model = std::make_shared<ExportModel> (sp->captureExportModel());
            sp->startJob ("Rendering the stems",
                [model, dir, url, isLocal] (Job& job)
                {
                    const StemExport res = exportStemsToDir (dir, *model,
                        [&job] (int percent)
                        {
                            job.progress.store (juce::jlimit (0, 100, percent),
                                               std::memory_order_relaxed);
                            return ! job.cancelled.load (std::memory_order_relaxed);
                        });

                    // v1.33.4 (M17): the rendered stems are copied into the
                    // chosen document tree (a local folder holds them already).
                    int copied = 0;
                    juce::String copyError;
                    if (! isLocal)
                    {
                        for (auto& f : res.written)
                        {
                            juce::String error;
                            if (upic::writePickedFolderFile (f, url, f.getFileName(), error))
                                ++copied;
                            else
                                copyError = error;
                        }
                        dir.deleteRecursively();
                    }

                    // v1.33.4 (H11): only the files that were really written
                    // are counted, and a failed render is reported instead of
                    // being silently dropped (the old files are left alone).
                    // v1.33.4 (M23): what was left out (the live arcs) is
                    // said out loud, so a PM-only page is not mistaken for an
                    // empty score.
                    job.ok = ! res.written.empty() && res.failed == 0;
                    if (job.cancelled.load (std::memory_order_relaxed))
                    {
                        job.message = "Stems: cancelled";
                    }
                    else if (res.written.empty())
                    {
                        if (res.failed > 0)
                            job.message = "Stems: the export failed (" + juce::String(res.failed)
                                          + " file(s) could not be written - the old ones were kept)";
                        else if (! res.note.isEmpty())
                            job.message = "Stems: " + res.note;
                        else
                            job.message = "Stems: nothing to render (no audible arcs / voices in this page)";
                    }
                    else
                    {
                        job.message = "Wrote " + juce::String((int) res.written.size()) + " stem file(s) to "
                                    + (isLocal ? dir.getFullPathName() : upic::pickedFileName (url))
                                    + (res.failed > 0 ? " - " + juce::String(res.failed) + " failed" : juce::String())
                                    + (res.note.isEmpty() ? juce::String() : " - " + res.note)
                                    + (copyError.isEmpty() ? juce::String()
                                                           : " - " + juce::String(copied) + " copied, " + copyError);
                    }
                },
                [sp] (Job& job) { sp->flash (job.message); });
        });
}

void MainComponent::updateStatus() {}

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
    m.masterGain = masterGain.load();
    m.samplePitchMode = engine.getSamplePitchMode();
    m.livePitchMode = engine.getLivePitchMode();
    return m;
}

juce::var MainComponent::pieceToVar() const
{
    return upic::pieceToVar (capturePieceModel());
}

bool MainComponent::loadFromVar(const juce::var& v)
{
    // v1.33.4 (M25): the piece is parsed into a temporary model first, and it
    // replaces the current one only when it really is a piece. The old code
    // cleared the model before it parsed, so a file that was not a piece (or
    // a host state that was rubbish) left an empty default page behind and
    // the unsaved work was gone.
    if (v.isVoid())
        return false;
    auto* pagesVar = v.getProperty("pages", juce::var()).getArray();
    if (pagesVar == nullptr || pagesVar->isEmpty())
        return false;

    // --- the piece is parsed into temporary containers (the model is not
    //     touched yet, so a parse that fails leaves the piece alone) ---
    std::vector<upic::Waveform> newWaves;
    std::vector<upic::Envelope> newEnvelopes;
    std::vector<upic::Page> newPages;
    std::vector<upic::SampleSlot> newSlots;
    for (int i = 0; i < upic::kMaxSampleSlots; ++i)
    {
        upic::SampleSlot s;
        s.id = i + 1;
        newSlots.push_back(std::move(s));
    }
    // v1.33.4 (H13): the piece format caps the page, arc, zone and point
    // counts. A piece over a cap used to lose the extra music silently on
    // the next open; the user is told what did not make it in.
    juce::String droppedMsg;
    // v1.33.4 (L13): how many ids had to be changed (a duplicate or an
    // impossible one) and how many material references pointed at nothing.
    int renamed = 0;
    int fixedRefs = 0;
    auto noteDrop = [&droppedMsg] (const juce::String& one, const juce::String& many, int n)
    {
        if (n > 0)
            droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(n) + " "
                            + (n == 1 ? one : many);
    };
    if (auto* arr = v.getProperty("samples", juce::var()).getArray())
    {
        // SECURITY (S-03): the decoded sample data of all the slots together
        // is capped, so a piece with many huge slots cannot exhaust memory.
        size_t totalSampleBytes = 0;
        int stored = 0;
        int droppedFrames = 0;
        for (auto& sv : *arr)
        {
            auto slot = upic::SampleSlot::fromVar(sv, 1);
            // v1.33.4 (M19): the frames the slot lost to the caps (the
            // duration at the material's own rate, the budget) are counted
            // too, so the user hears about the cut material.
            const int rawFrames = (int) sv.getProperty("frames", 0);
            const int keptFrames = slot.data != nullptr ? slot.data->getNumSamples() : 0;
            droppedFrames += juce::jmax (0, rawFrames - keptFrames);
            if (slot.data != nullptr)
                totalSampleBytes += (size_t) slot.data->getNumSamples()
                                  * (size_t) slot.data->getNumChannels() * sizeof(float);
            if (totalSampleBytes > upic::limits::kMaxTotalSampleBytes) break;
            for (auto& s : newSlots)
                if (s.id == slot.id)
                {
                    // the first entry for a slot id wins (a file that
                    // carries the same slot twice must not overwrite the
                    // first one)
                    if (s.data == nullptr) { s = std::move(slot); ++stored; }
                    break;
                }
        }
        noteDrop ("sample", "samples", (int) arr->size() - stored);
        noteDrop ("sample frame", "sample frames", droppedFrames);
    }
    if (auto* arr = v.getProperty("waveforms", juce::var()).getArray())
    {
        int i = 0;
        for (auto& wv : *arr)
        {
            if ((int) newWaves.size() >= upic::limits::kMaxWaveforms) break;
            newWaves.push_back(upic::Waveform::fromVar(wv, i++));
        }
        noteDrop ("wavetable", "wavetables", (int) arr->size() - (int) newWaves.size());
    }
    if (auto* arr = v.getProperty("envelopes", juce::var()).getArray())
    {
        int i = 0;
        for (auto& ev : *arr)
        {
            if ((int) newEnvelopes.size() >= upic::limits::kMaxEnvelopes) break;
            newEnvelopes.push_back(upic::Envelope::fromVar(ev, i++));
        }
        noteDrop ("envelope", "envelopes", (int) arr->size() - (int) newEnvelopes.size());
        // the envelope's own points are capped too
        for (int ei = 0; ei < (int) newEnvelopes.size() && ei < (int) arr->size(); ++ei)
            if (auto* ptsArr = arr->getReference(ei).getProperty("points", juce::var()).getArray())
                noteDrop ("envelope point", "envelope points",
                          (int) ptsArr->size() - (int) newEnvelopes[(size_t) ei].points.size());
    }
    if (auto* arr = v.getProperty("pages", juce::var()).getArray())
    {
        for (auto& pv : *arr)
        {
            if ((int) newPages.size() >= upic::limits::kMaxPages) break;
            // v1.33.4 (L13): the page's parser checks the arc and zone ids
            // (a duplicate id made the selection and the delete hit the
            // wrong arc) and derives the next id from the arcs that are
            // there. `renamed` counts what it had to change.
            newPages.push_back(upic::Page::fromVar(pv, &renamed));
        }
        noteDrop ("page", "pages", (int) arr->size() - (int) newPages.size());
        // the arcs, the sieve zones and the arc points of the pages that
        // were loaded (the page's own parser caps them)
        for (int pi = 0; pi < (int) newPages.size(); ++pi)
        {
            const juce::var pageVar = arr->getReference(pi);
            if (auto* arcsArr = pageVar.getProperty("arcs", juce::var()).getArray())
            {
                noteDrop ("arc", "arcs", (int) arcsArr->size() - (int) newPages[(size_t) pi].arcs.size());
                for (int ai = 0; ai < (int) newPages[(size_t) pi].arcs.size(); ++ai)
                    if (auto* ptsArr = arcsArr->getReference(ai).getProperty("points", juce::var()).getArray())
                        noteDrop ("arc point", "arc points",
                                  (int) ptsArr->size() - (int) newPages[(size_t) pi].arcs[(size_t) ai].points.size());
            }
            if (auto* zonesArr = pageVar.getProperty("zones", juce::var()).getArray())
                noteDrop ("sieve zone", "sieve zones",
                          (int) zonesArr->size() - (int) newPages[(size_t) pi].zones.size());
        }
    }
    if (newWaves.empty()) newWaves = upic::defaultWaveforms();
    if (newEnvelopes.empty()) newEnvelopes = upic::defaultEnvelopes();
    if (newPages.empty()) newPages.push_back(upic::Page());

    // v1.33.4 (L13): the wavetables and the envelopes are keyed by their id
    // too - two wavetables with the same id made the arc list show the
    // wrong name, and the editor's controls edited the wrong one. The first
    // item of an id keeps it (so the arcs that point at the material keep
    // pointing at the same thing), a duplicate gets a free id.
    renamed += upic::uniquifyIds (newWaves);
    renamed += upic::uniquifyIds (newEnvelopes);

    // and the arcs' references to the material are checked: a piece can
    // point at a wavetable or an envelope that is not there (the arc then
    // showed an empty material and played the wrong thing), so the
    // reference is put back onto material that exists.
    for (auto& pg : newPages)
        for (auto& a : pg.arcs)
        {
            if (! newWaves.empty() && ! upic::hasId (newWaves, a.waveformId))
            { a.waveformId = newWaves.front().id; ++fixedRefs; }
            if (! newEnvelopes.empty() && ! upic::hasId (newEnvelopes, a.envelopeId))
            { a.envelopeId = newEnvelopes.front().id; ++fixedRefs; }
            // 0 is the "no envelope of its own" marker for the PM index
            if (a.pmIndexEnv != 0 && ! upic::hasId (newEnvelopes, a.pmIndexEnv))
            { a.pmIndexEnv = 0; ++fixedRefs; }
        }
    if (renamed > 0)
        droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(renamed)
                    + (renamed == 1 ? " item had a duplicate id and has been renumbered"
                                    : " items had duplicate ids and have been renumbered");
    if (fixedRefs > 0)
        droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(fixedRefs)
                    + (fixedRefs == 1 ? " arc pointed at material that was not there"
                                      : " arcs pointed at material that was not there");

    // --- the piece is good: swap it in ---
    {
        juce::ScopedLock l(dataLock);
        stopPlayback();
        // stopPlayback only starts a fade while playing: make the stop immediate
        // here, since the piece (and possibly its pages) is about to be replaced
        isPlaying.store(false);
        wantsStop.store(false);
        waveforms = std::move (newWaves);
        envelopes = std::move (newEnvelopes);
        pages = std::move (newPages);
        sampleSlots = std::move (newSlots);
    }
    {
        juce::ScopedLock l(dataLock);
        // clamp ids
        currentPage = juce::jlimit(0, (int)pages.size() - 1, (int) v.getProperty("currentPage", 0));
    // SECURITY (S-03): the master gain is clamped to the slider's range, so a
    // piece cannot push the mix into clipping or silence.
    masterGain.store(upic::limits::clampGain((float)(double) v.getProperty("master", 0.8)));
    markModelChanged(); // v1.15: the whole piece was replaced -> fade across it
    // v1.1 state (defaults keep old / Python-written files working unchanged)
    seqPages = (bool) v.getProperty("seqPages", false);
    recordPressure = (bool) v.getProperty("recordPressure", false);
    scrubAudition = (bool) v.getProperty("scrubAudition", true);
    muteWhileDragging.store((bool) v.getProperty("muteWhileDragging", true), std::memory_order_relaxed);
    dragSmoothMs.store(juce::jlimit(0, 1000, (int) v.getProperty("dragSmoothMs", 20)), std::memory_order_relaxed);
    scrubGrainMs = juce::jlimit(5, 500, (int) v.getProperty("scrubGrainMs", 40));
    sampleGrainMs = juce::jlimit(5, 500, (int) v.getProperty("sampleGrainMs", 40));
    liveGrainMs = juce::jlimit(5, 500, (int) v.getProperty("liveGrainMs", 40));
    sieveOn = (bool) v.getProperty("sieveOn", false);
    sieveM = juce::jlimit(2, 120, (int) v.getProperty("sieveM", 12));
    sieveR.clear();
    if (auto* arr = v.getProperty("sieveR", juce::var()).getArray())
    {
        for (auto& rv : *arr)
        {
            if ((int) sieveR.size() >= upic::limits::kMaxSieveRemainders) break;
            sieveR.push_back((((int) rv % sieveM) + sieveM) % sieveM);
        }
        noteDrop ("sieve remainder", "sieve remainders", (int) arr->size() - (int) sieveR.size());
    }
    // v1.33.2: an old settings file can carry an empty sieveR (the field was
    // allowed to be empty, and the placeholder made it look like 0,7). That
    // restored a sieve with no remainders, which is inert - the Sieve
    // checkbox then did nothing. Fall back to the octave sieve.
    if (sieveR.empty()) sieveR = { 0 };
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
    // compatible), present -> the piece's mode wins.
    {
        bool changed = false;
        const int samplePm = (int) v.getProperty("samplePitchMode", -1);
        const int livePm = (int) v.getProperty("livePitchMode", -1);
        const int sampleMode = samplePm >= 0 ? juce::jlimit(0, 3, samplePm)
                                             : engine.getSamplePitchMode();
        const int liveMode = livePm >= 0 ? juce::jlimit(0, 2, livePm)
                                         : engine.getLivePitchMode();
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
    // v1.2 state (defaults: full stickiness, no time grid = v1.1 behaviour)
    stickiness = upic::limits::clampFinite((double) v.getProperty("stickiness", 1.0), 0.0, 1.0);
    timeGrid = upic::limits::clampFinite((double) v.getProperty("timeGrid", 0.0), 0.0, 64.0);
    stickSlider.setValue(stickiness, juce::dontSendNotification);
    score.setStickiness(stickiness);
    {
        static const double divs[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        int gi = 0;
        for (int i = 0; i < 7; ++i) if (std::abs(divs[i] - timeGrid) < 1e-9) gi = i;
        gridBox.setSelectedId(gi + 1, juce::dontSendNotification);
        score.setTimeGrid(timeGrid);
    }
    selWave = waveforms.front().id; selEnv = envelopes.front().id;
    selSlot = 0;
    drawMatMode = score.getLayer(); // the tab stays where it is across a load
    syncMaterialTabs();
    drawRef = upic::kDefaultRefFreq;
    refSlider.setValue(drawRef, juce::dontSendNotification);
    seekPlayhead(0.0);
    bumpPageEpoch();
    masterSlider.setValue(masterGain.load(), juce::dontSendNotification);
    refreshBoxes(); refreshScoreBinding();
    // v1.18: the next audio block must already render the loaded piece (the
    // timer alone would be up to 33 ms late - and a host may not run it).
    publishModelSnapshot();
    // v1.33.4 (H13): the piece was over one of the format's caps, so part of
    // it did not make it in. Say so instead of losing the music silently.
    if (! droppedMsg.isEmpty())
        flash ("This piece is bigger than the app supports - not loaded: " + droppedMsg);
    } // the settings scope

    // v1.33.4 (L15): the loaded piece is what the file holds, so there is
    // nothing to save yet (the app must not ask to save a piece that was
    // just opened).
    markPieceSaved();

    return true;
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
            // v1.33.4 (L15): the revision the saved piece came from. If the
            // model moved on while the worker was writing, the piece on the
            // disk is already older than what is on the screen - the app
            // must still ask to save.
            const juce::uint64 savedRevision = sp->modelRevision.load (std::memory_order_relaxed);
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
                [sp, onDone, savedRevision] (Job& job)
                {
                    sp->flash (job.message);
                    // v1.33.4 (L15): the piece is on the disk now, so there
                    // is nothing to ask about when the app closes - unless
                    // the model moved on while the worker was writing.
                    if (job.ok && sp->modelRevision.load (std::memory_order_relaxed) == savedRevision)
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

//==============================================================================
// v1.33.4 (L15, part 2): the undo stack.
//
// The review asked for edit transactions and for the pointer drags to be
// coalesced into one undo action. The stack holds snapshots of the model
// (the piece): the sample audio is shared, so a snapshot costs only the
// pages - the audio is never copied again and again.
//
// An edit captures the state from before it; the snapshot is kept only if
// the model really moved (a click that changed nothing is not an undo
// step). The restore goes through the normal loader, so the same checks
// and the same limits apply to an undone piece as to a loaded one.
static juce::int64 estimateModelBytes (const upic::PieceModel& m)
{
    juce::int64 bytes = 0;
    for (const auto& p : m.pages)
    {
        bytes += (juce::int64) p.arcs.size() * 64;
        for (const auto& a : p.arcs)
            bytes += (juce::int64) a.points.size() * (juce::int64) sizeof (upic::ArcPoint);
        bytes += (juce::int64) p.zones.size() * 64;
    }
    for (const auto& w : m.waveforms)
        bytes += (juce::int64) w.samples.size() * (juce::int64) sizeof (float);
    for (const auto& e : m.envelopes)
        bytes += (juce::int64) e.points.size() * (juce::int64) sizeof (upic::EnvPoint);
    return bytes;
}

void MainComponent::beginEdit (const juce::String& label)
{
    // v1.33.4 (L15, part 2): the state from before the change.
    //
    // A transaction that is still running is closed first: a gesture that
    // was never finished (a mouse-up that was missed) must not block the
    // next one.
    if (pendingEdit != nullptr)
        endEdit();

    pendingEdit = std::make_unique<upic::PieceModel> (capturePieceModel());
    pendingRevision = modelRevision.load (std::memory_order_relaxed);
    pendingLabel = label;
}

void MainComponent::endEdit()
{
    if (pendingEdit == nullptr) return;

    // nothing moved? then the snapshot is not worth keeping (a click that
    // changed nothing is not an undo step).
    if (modelRevision.load (std::memory_order_relaxed) == pendingRevision)
    {
        pendingEdit.reset();
        return;
    }

    UndoEntry e;
    e.model = std::move (*pendingEdit);
    e.label = pendingLabel;
    e.bytes = estimateModelBytes (e.model);
    pendingEdit.reset();

    undoStack.push_back (std::move (e));
    undoBytes += undoStack.back().bytes;

    // a new edit after an undo starts a new branch: the redo stack is gone
    redoStack.clear();
    redoBytes = 0;

    // the bounds: the oldest steps go first
    while ((int) undoStack.size() > kMaxUndoSteps || undoBytes > kMaxUndoBytes)
    {
        undoBytes -= undoStack.front().bytes;
        undoStack.erase (undoStack.begin());
    }
}

juce::String MainComponent::undoLabel() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().label;
}

juce::String MainComponent::redoLabel() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().label;
}

void MainComponent::undo()
{
    if (undoStack.empty()) return;

    // the state we are leaving goes onto the redo stack
    UndoEntry current;
    current.model = capturePieceModel();
    current.label = undoLabel();
    current.bytes = estimateModelBytes (current.model);
    redoStack.push_back (std::move (current));
    redoBytes += redoStack.back().bytes;
    while ((int) redoStack.size() > kMaxUndoSteps || redoBytes > kMaxUndoBytes)
    {
        redoBytes -= redoStack.front().bytes;
        redoStack.erase (redoStack.begin());
    }

    UndoEntry entry = std::move (undoStack.back());
    undoStack.pop_back();
    undoBytes -= entry.bytes;

    // the piece is replaced through the normal loader, so the same checks
    // and the same limits apply as to a loaded piece
    const bool wasPlaying = isPlaying.load (std::memory_order_relaxed);
    if (! loadFromVar (upic::pieceToVar (entry.model)))
    {
        flash ("The undo could not be applied");
        return;
    }

    // the piece on the screen is not what is on the disk any more
    markModelChanged();
    // the transport was running: it starts again, so the undo does not
    // silence the app
    if (wasPlaying) startPlayback();
    flash ("Undo: " + entry.label);
}

void MainComponent::redo()
{
    if (redoStack.empty()) return;

    UndoEntry current;
    current.model = capturePieceModel();
    current.label = redoLabel();
    current.bytes = estimateModelBytes (current.model);
    undoStack.push_back (std::move (current));
    undoBytes += undoStack.back().bytes;
    while ((int) undoStack.size() > kMaxUndoSteps || undoBytes > kMaxUndoBytes)
    {
        undoBytes -= undoStack.front().bytes;
        undoStack.erase (undoStack.begin());
    }

    UndoEntry entry = std::move (redoStack.back());
    redoStack.pop_back();
    redoBytes -= entry.bytes;

    const bool wasPlaying = isPlaying.load (std::memory_order_relaxed);
    if (! loadFromVar (upic::pieceToVar (entry.model)))
    {
        flash ("The redo could not be applied");
        return;
    }

    markModelChanged();
    if (wasPlaying) startPlayback();
    flash ("Redo: " + entry.label);
}

//==============================================================================
// v1.33.4 (L15, part 3): the crash recovery.
//
// The review asked for "recovery snapshots without embedding unchanged audio
// repeatedly". The snapshot is a small piece JSON (the pages) plus the audio
// in sidecar files: a sidecar is only written when the audio really changed,
// so the repeated snapshots carry the pages, not the material.
//
// The snapshot is written while the piece has unsaved changes (on the worker
// thread, like the other long jobs) and it is offered at the next start - the
// app asks whether to recover it.
juce::File MainComponent::recoveryDirectory()
{
    // the user's own data folder (the roaming one on Windows): the snapshot
    // belongs to the user, not to the machine
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("UPICuitous")
                .getChildFile ("recovery");
}

// A hash of the bytes, so a sidecar that is already there is not written
// again (the name of the file carries the hash).
static juce::uint64 hashBytes (const void* data, size_t size)
{
    juce::uint64 h = 1469598103934665603ULL;
    const auto* p = static_cast<const juce::uint8*> (data);
    for (size_t i = 0; i < size; ++i)
    {
        h ^= (juce::uint64) p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

bool MainComponent::writeRecoverySnapshot (const juce::File& dir, const upic::PieceModel& model,
                                            juce::String& error)
{
    error.clear();

    if (! dir.createDirectory())
    {
        error = "the recovery folder could not be created";
        return false;
    }

    // the piece JSON without the audio: the audio goes to the sidecars
    auto piece = upic::pieceToVar (model, false);
    auto* samples = piece.getProperty ("samples", juce::var()).getArray();

    // the sidecars this snapshot uses - the others are removed at the end
    juce::Array<juce::File> keep;

    int arrayIndex = 0;
    for (const auto& slot : model.sampleSlots)
    {
        if (! slot.loaded()) continue;

        const auto pcm = slot.toPcm16();
        if (pcm.getSize() == 0) continue;

        // the name carries the hash of the audio: a sidecar that is already
        // there is the right one and is not written again
        const auto hash = hashBytes (pcm.getData(), pcm.getSize());
        const auto name = "sample-" + juce::String (slot.id) + "-"
                        + juce::String::toHexString ((juce::uint64) hash) + ".pcm";
        const auto f = dir.getChildFile (name);
        keep.add (f);

        if (! f.existsAsFile())
        {
            // the audio is written once: the next snapshots reuse it
            juce::TemporaryFile temp (f);
            {
                auto out = temp.getFile().createOutputStream();
                if (out == nullptr || ! out->write (pcm.getData(), (int) pcm.getSize()))
                {
                    error = "the recovery audio could not be written";
                    return false;
                }
            }
            if (! temp.overwriteTargetFileWithTemporary())
            {
                error = "the recovery audio could not be stored";
                return false;
            }
        }

        // the piece JSON points at the sidecar
        if (samples != nullptr && arrayIndex < samples->size())
        {
            if (auto* obj = (*samples)[arrayIndex].getDynamicObject())
                obj->setProperty ("recoveryFile", name);
        }

        ++arrayIndex;
    }

    // the piece JSON itself (small: the audio is not in it)
    const auto json = juce::JSON::toString (piece, true);
    if (json.getNumBytesAsUTF8() > (juce::int64) 8 * 1024 * 1024)
    {
        error = "the recovery snapshot is too big";
        return false;
    }

    {
        const auto target = dir.getChildFile ("piece.json");
        juce::TemporaryFile temp (target);
        {
            auto out = temp.getFile().createOutputStream();
            if (out == nullptr || ! out->write (json.toRawUTF8(), (int) json.getNumBytesAsUTF8()))
            {
                error = "the recovery snapshot could not be written";
                return false;
            }
        }
        if (! temp.overwriteTargetFileWithTemporary())
        {
            error = "the recovery snapshot could not be stored";
            return false;
        }
    }

    // the sidecars that are not used any more are removed (the folder must
    // not grow with every snapshot)
    for (auto& entry : juce::RangedDirectoryIterator (dir, false))
    {
        const auto f = entry.getFile();
        if (! f.getFileName().endsWith (".pcm")) continue;
        if (keep.contains (f)) continue;
        f.deleteFile();
    }

    return true;
}

bool MainComponent::readRecoverySnapshot (const juce::File& dir, juce::var& pieceOut, juce::String& error)
{
    error.clear();

    const auto pieceFile = dir.getChildFile ("piece.json");
    if (! pieceFile.existsAsFile())
    {
        error = "there is no recovery snapshot";
        return false;
    }
    // the piece JSON is small (the audio is not in it), but the file is read
    // from the disk, so it is capped before anything is read
    if (pieceFile.getSize() > (juce::int64) 8 * 1024 * 1024)
    {
        error = "the recovery snapshot is too big";
        return false;
    }

    const auto json = pieceFile.loadFileAsString();
    if (json.isEmpty())
    {
        error = "the recovery snapshot is empty";
        return false;
    }

    auto parsed = juce::JSON::parse (json);
    if (parsed.isVoid())
    {
        error = "the recovery snapshot is not a piece";
        return false;
    }

    // the audio comes back from the sidecars, in the shape the loader wants
    if (auto* samples = parsed.getProperty ("samples", juce::var()).getArray())
    {
        for (int i = 0; i < samples->size(); ++i)
        {
            // the array hands out copies, but the copy shares the same
            // object, so setting a property on it changes the array's entry
            const auto s = (*samples)[i];
            auto* obj = s.getDynamicObject();
            if (obj == nullptr) continue;
            const auto name = obj->getProperty ("recoveryFile").toString();
            if (name.isEmpty()) continue;

            const auto f = dir.getChildFile (name);
            if (! f.existsAsFile())
            {
                error = "the recovery snapshot lost its audio";
                return false;
            }
            if (f.getSize() > (juce::int64) upic::limits::kMaxPieceBytes)
            {
                error = "the recovery audio is too big";
                return false;
            }
            juce::MemoryBlock bytes;
            {
                juce::FileInputStream in (f);
                if (! in.openedOk() || ! in.readIntoMemoryBlock (bytes) || bytes.getSize() == 0)
                {
                    error = "the recovery audio could not be read";
                    return false;
                }
            }
            // the loader wants the audio as base64
            obj->setProperty ("data", juce::Base64::toBase64 (bytes.getData(), bytes.getSize()));
            obj->removeProperty ("recoveryFile");
        }
    }

    pieceOut = parsed;
    return true;
}

void MainComponent::deleteRecoverySnapshot (const juce::File& dir)
{
    if (! dir.exists()) return;
    for (auto& entry : juce::RangedDirectoryIterator (dir, false))
        entry.getFile().deleteFile();
}

void MainComponent::maybeWriteRecoverySnapshot()
{
    // v1.33.4 (L15, part 3): the periodic snapshot.
    //
    // It runs only when there is something to lose (the piece differs from
    // what was last written) and when the model moved since the last
    // snapshot. The host keeps the piece in the plugin, so there is nothing
    // to recover there.
    if (pluginMode_) return;
    if (! recoveryAllowed.load (std::memory_order_relaxed)) return;
    if (! pieceHasUnsavedChanges()) return;
    if (jobRunning()) return;   // the disk is busy with the user's job

    const auto now = (juce::uint64) juce::Time::getMillisecondCounter();
    const auto rev = modelRevision.load (std::memory_order_relaxed);
    if (now < lastRecoveryMs || now - lastRecoveryMs < 30000) return;   // at most every 30 s
    if (rev == lastRecoveryRevision) return;                            // nothing changed

    lastRecoveryMs = now;
    lastRecoveryRevision = rev;

    // the model is captured on the message thread (under the lock); the
    // worker only writes the files
    const auto model = capturePieceModel();
    const auto dir = recoveryDirectory();

    startJob ("Recovery snapshot",
              [dir, model] (Job& job)
              {
                  juce::String error;
                  job.ok = writeRecoverySnapshot (dir, model, error);
                  job.message = error;
              },
              [this] (Job& job)
              {
                  // a silent job: only a failure is worth a word
                  if (! job.ok && ! job.message.isEmpty())
                      flash ("The recovery snapshot could not be written: " + job.message);
              },
              /* announce */ false);
}

void MainComponent::offerRecovery()
{
    // v1.33.4 (L15, part 3): the offer at the next start. The snapshot is
    // only there when the app did not close cleanly, so it is worth asking.
    if (pluginMode_) return;

    const auto dir = recoveryDirectory();
    juce::var piece;
    juce::String error;
    if (! readRecoverySnapshot (dir, piece, error))
    {
        // nothing to recover (or a broken snapshot: it is thrown away, so it
        // cannot ask again and again)
        deleteRecoverySnapshot (dir);
        return;
    }

    juce::Component::SafePointer<MainComponent> safeMc (this);
    auto options = juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::WarningIcon)
        .withTitle ("Unsaved work recovered")
        .withMessage ("The app was closed without saving last time. A snapshot "
                      "of the piece is here - open it?")
        .withButton ("Recover")
        .withButton ("Discard")
        .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safeMc, piece, dir] (int buttonId)
    {
        if (safeMc == nullptr) return;
        if (buttonId == 1)   // Recover
        {
            if (safeMc->loadFromVar (piece))
            {
                // the recovered piece is not on the disk: the app must ask
                // before it closes
                safeMc->markModelChanged();
                safeMc->flash ("Recovered the piece from the last session");
            }
            else
            {
                safeMc->flash ("The recovery snapshot could not be opened");
            }
        }
        else                 // Discard
        {
            deleteRecoverySnapshot (dir);
        }
    });
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

// The install, on the message thread: the model is read by the audio thread,
// so it is mutated under the lock.
void MainComponent::installSample (int slotIndex, SampleImport&& import)
{
    if (slotIndex < 0 || slotIndex >= (int) sampleSlots.size()) return;
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the material was replaced.
    beginEdit ("Load the material");
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
        markModelChanged(); // v1.15: new material -> fade across it
        publishModelSnapshot(); // the next block must see it (no timer needed)
    }
    score.sampleDurations[sampleSlots[(size_t) slotIndex].id] = sampleSlots[(size_t) slotIndex].durationSec();
    selSlot = slotIndex;
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

//==============================================================================
// v1.33.4 (M28): the page render as a job.
//
// The file chooser calls this, and so does the selftest (the report needs a
// message loop, but the file is written either way). The destination can be
// a document-provider document (Android): it is rendered into a local
// temporary file and then copied into the chosen document.
void MainComponent::exportPageAsJob (const juce::URL& url)
{
    if (! url.isWellFormed()) return;
    const bool isLocal = upic::isPlainFileURL (url);
    juce::File temp;
    if (! isLocal)
    {
        temp = juce::File::createTempFile (".wav");
        if (temp == juce::File())
        {
            flash ("Could not make a temporary file for the render");
            return;
        }
    }

    // the model is captured here, under the lock - the worker never touches
    // the component again
    auto model = std::make_shared<ExportModel> (captureExportModel());
    juce::Component::SafePointer<MainComponent> sp (this);
    startJob ("Rendering the page",
        [model, url, isLocal, temp] (Job& job)
        {
            // v1.33.4 (H8): the page is rendered straight into the file,
            // block by block: the file is as long as the page's own time
            // needs (a warped page used to be cut off at the end) and the
            // memory stays flat. The session's pitch algorithm and grain
            // sizes come with it, so the file sounds like the session.
            const juce::File target = isLocal ? url.getLocalFile() : temp;
            const bool rendered = exportPageToFile (target, *model,
                [&job] (int percent)
                {
                    job.progress.store (juce::jlimit (0, 100, percent),
                                       std::memory_order_relaxed);
                    return ! job.cancelled.load (std::memory_order_relaxed);
                });

            bool ok = rendered;
            juce::String error;
            if (ok && ! isLocal)
                ok = upic::copyLocalFileToPicked (temp, url, error);
            if (! isLocal)
                temp.deleteFile();

            job.ok = ok;
            if (job.cancelled.load (std::memory_order_relaxed))
                job.message = "Render cancelled - " + upic::pickedFileName (url) + " was not written";
            else if (ok)
                job.message = "Rendered " + upic::pickedFileName (url);
            else
                job.message = error.isNotEmpty()
                    ? error
                    : ("Render failed - " + upic::pickedFileName (url) + " was not written");
        },
        [sp] (Job& job) { if (sp != nullptr) sp->flash (job.message); });
}

void MainComponent::renderWav()
{
    // v1.33.4 (H11): the chooser can stay open while the window is closed,
    // so the callback carries a safe pointer instead of a raw `this`.
    juce::Component::SafePointer<MainComponent> sp (this);
    auto fc = std::make_shared<juce::FileChooser>("Render current page to WAV", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("upic-page.wav"), "*.wav");
    fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [sp, fc](const juce::FileChooser& c){
            if (sp == nullptr) return;
            // v1.33.4 (M17): the destination can be a document-provider URL
            // (Android) - getResult() would drop it and the render would do
            // nothing. A local destination is rendered straight into the
            // file; a provider document is rendered into a local temporary
            // file and then copied into the chosen document.
            const auto url = upic::pickedURLWithExtension (c.getURLResult(), "wav");
            if (! url.isWellFormed()) return;
            // v1.33.4 (M28): the render runs on a worker thread, so the
            // window stays alive and the audio thread is not held up by it.
            sp->exportPageAsJob (url);
        });
}

void MainComponent::showAudioSettings()
{
    // v1.7: an in-app overlay instead of a DialogWindow - on Android JUCE
    // has no floating windows, so the dialog had no title bar and no close
    // button there and could never be dismissed.
    // Inputs are allowed: enabling them requests RECORD_AUDIO at runtime
    // (manifest) and falls back to output-only when denied or absent.
    closeSettings();
    if (audioSelector == nullptr)
        audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, false, false, true, false);
    audioOverlay.addAndMakeVisible(*audioSelector);
    // v1.26: keep the OK button above the selector. On some layouts the
    // selector's viewport can otherwise sit over the button and eat its click.
    audioOkBtn.toFront(false);
    audioOverlay.setBounds(getLocalBounds());
    audioOverlay.setVisible(true);
    audioOverlay.toFront(false);
    resized(); // position the selector + OK button inside the card
}

void MainComponent::closeAudioSettings()
{
    // device changes already applied live in the selector; OK / backdrop
    // tap / Esc just dismiss the overlay
    audioOverlay.setVisible(false);
    if (audioSelector != nullptr && audioSelector->getParentComponent() == &audioOverlay)
        audioOverlay.removeChildComponent(audioSelector.get());
}
