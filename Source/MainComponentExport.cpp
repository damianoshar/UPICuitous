#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The exports: the page render, the stems and the WAV file.
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
        m.ampTables = ampTables;
        m.channels = channels;
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
                               model.sampleGrainMs, model.liveGrainMs,
                               &model.ampTables, &model.channels, -1, progress);
}

StemExport MainComponent::exportStemsToDir (const juce::File& dir, const ExportModel& model,
                                            const std::function<bool (int percent)>& progress)
{
    return renderStems (model.page, model.waves, model.envs, model.slots, dir, "stem",
                        model.sr, model.masterGain, model.samplePitchMode,
                        model.sampleGrainMs, model.liveGrainMs, &model.ampTables, &model.channels, -1, progress);
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

