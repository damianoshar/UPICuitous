#include "MainComponent.h"
#include <algorithm>
#include <atomic>
#include <cmath>
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
// R3.2: the stack itself is the document session's now (Source/
// DocumentSession.h): the steps, their labels, the open transaction, the
// two bounds and the size of a snapshot. What is left here is the shell's
// own work: the gain slider's flag, the selection, the transport and the
// messages.
//
// An edit captures the state from before it; the snapshot is kept only if
// the model really moved (a click that changed nothing is not an undo
// step). The restore goes through the normal loader, so the same checks
// and the same limits apply to an undone piece as to a loaded one.

void MainComponent::beginEdit (const juce::String& label)
{
    // A new command owns the transaction from here, so an open gesture's
    // flag cannot outlive it (the gain slider's flag would otherwise make
    // the next gain change look like part of a drag).
    channelGainEditing = false;

    document.beginEdit (label);
}

void MainComponent::endEdit()
{
    if (! document.hasOpenEdit()) return;

    // the gesture is over: the flag cannot outlive the transaction
    channelGainEditing = false;

    document.endEdit();
}

juce::String MainComponent::undoLabel() const
{
    return document.undoLabel();
}

juce::String MainComponent::redoLabel() const
{
    return document.redoLabel();
}

void MainComponent::undo()
{
    // nothing to undo: an open gesture stays open (its step is pushed when
    // the gesture ends, not before)
    if (! document.canUndo()) return;

    // An open gesture (a slider drag) owns a transaction. Closing it first
    // keeps the undo stack honest: the gesture's step is closed and undone
    // like any other.
    endEdit();

    juce::String label;
    switch (document.undo (label))
    {
        case upic::DocumentSession::StepResult::failed:
            flash ("The undo could not be applied");
            break;

        case upic::DocumentSession::StepResult::applied:
            flash ("Undo: " + label);
            break;

        case upic::DocumentSession::StepResult::nothing:
        default:
            break;
    }
}

void MainComponent::redo()
{
    // nothing to redo: an open gesture stays open
    if (! document.canRedo()) return;

    // an open gesture transaction is closed first (see undo())
    endEdit();

    juce::String label;
    switch (document.redo (label))
    {
        case upic::DocumentSession::StepResult::failed:
            flash ("The redo could not be applied");
            break;

        case upic::DocumentSession::StepResult::applied:
            flash ("Redo: " + label);
            break;

        case upic::DocumentSession::StepResult::nothing:
        default:
            break;
    }
}

// R3.2: the document session's restore door: the shell's half of the undo.
// The piece is replaced through the normal loader, so the same checks and
// the same limits apply as to a loaded piece.
bool MainComponent::restorePieceModel (const upic::PieceModel& model)
{
    // the selection and the transport are read before the piece is replaced
    // (the loader puts the selection on the first item of the bank it loads)
    const int waveId = selWave;
    const int envelopeId = selEnv;
    const int ampTableId = selAmpTableId;
    const int sequenceId = selSeqId;
    const int sampleSlotIndex = selSlot;
    const bool wasPlaying = transportActive();

    if (! loadFromVar (upic::pieceToVar (model)))
        return false;

    restoreHistorySelections (waveId, envelopeId, ampTableId, sequenceId,
                              sampleSlotIndex);

    // the piece on the screen is not what is on the disk any more
    markModelChanged();
    // the transport was running: it starts again, so the undo does not
    // silence the app
    if (wasPlaying) startPlayback();
    return true;
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
            // WP10: a page may last an hour now, but a WAV file cannot hold
            // more than 4 GB - at a high sample rate (or warped, which makes
            // the audio longer than the score) the hour does not fit. Say so
            // instead of writing a file whose header cannot describe it.
            const double framesWanted = std::ceil (model->page.audioDurationSec() * model->sr);
            const bool fitsAWav = wavLengthFits (framesWanted, 2, 16);
            const bool rendered = fitsAWav && exportPageToFile (target, *model,
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
            else if (! fitsAWav)
                job.message = "Render refused - the page is too long for a WAV file at "
                            + juce::String (model->sr / 1000.0, 1) + " kHz";
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

