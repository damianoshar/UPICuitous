#include "MainComponent.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

//==============================================================================
// The edit transaction (the undo stack) and the long jobs: the
// page render runs on a worker thread, so the window stays alive.
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

    // Guerrero arcs: if the geometry no longer matches the fingerprint taken
    // when it was generated, the user has edited it. Mark it baked inside this
    // transaction, so the undo snapshot (taken in beginEdit while still
    // unbaked) restores the unbaked state on undo.
#if GUERRERO_INTEGRATION
    markEditedGuerreroGeometry();
#endif

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
    const int waveId = selWave;
    const int envelopeId = selEnv;
    const int ampTableId = selAmpTableId;
    const int sequenceId = selSeqId;
    const int sampleSlotIndex = selSlot;
    const bool wasPlaying = transportActive();
    if (! loadFromVar (upic::pieceToVar (entry.model)))
    {
        flash ("The undo could not be applied");
        return;
    }

    restoreHistorySelections (waveId, envelopeId, ampTableId, sequenceId,
                              sampleSlotIndex);

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

    const int waveId = selWave;
    const int envelopeId = selEnv;
    const int ampTableId = selAmpTableId;
    const int sequenceId = selSeqId;
    const int sampleSlotIndex = selSlot;
    const bool wasPlaying = transportActive();
    if (! loadFromVar (upic::pieceToVar (entry.model)))
    {
        flash ("The redo could not be applied");
        return;
    }

    restoreHistorySelections (waveId, envelopeId, ampTableId, sequenceId,
                              sampleSlotIndex);
    markModelChanged();
    if (wasPlaying) startPlayback();
    flash ("Redo: " + entry.label);
}

void MainComponent::restoreHistorySelections (int waveId, int envelopeId,
                                              int ampTableId, int sequenceId,
                                              int sampleSlotIndex)
{
    const auto hasWave = [this, waveId]
    {
        for (const auto& w : waveforms) if (w.id == waveId) return true;
        return false;
    };
    const auto hasEnvelope = [this, envelopeId]
    {
        for (const auto& e : envelopes) if (e.id == envelopeId) return true;
        return false;
    };
    const auto hasAmpTable = [this, ampTableId]
    {
        for (const auto& t : ampTables) if (t.id == ampTableId) return true;
        return false;
    };
    const auto hasSequence = [this, sequenceId]
    {
        for (const auto& s : sequences) if (s.id == sequenceId) return true;
        return false;
    };

    if (hasWave() && ! waveforms.empty()) selWave = waveId;
    else if (! waveforms.empty()) selWave = waveforms.front().id;
    if (hasEnvelope() && ! envelopes.empty()) selEnv = envelopeId;
    else if (! envelopes.empty()) selEnv = envelopes.front().id;

    if (hasAmpTable()) selAmpTableId = ampTableId;
    else selAmpTableId = ampTables.empty() ? -1 : ampTables.front().id;

    if (hasSequence()) selSeqId = sequenceId;
    else selSeqId = sequences.empty() ? -1 : sequences.front().id;

    selSlot = juce::jlimit (0, juce::jmax (0, (int) sampleSlots.size() - 1),
                            sampleSlotIndex);
    refreshBoxes();
    refreshAmpTableEditor();
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

