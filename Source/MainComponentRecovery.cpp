#include "MainComponent.h"
#include "RecoveryRepository.h"
#include "SampleCodec.h"
#include <atomic>

//==============================================================================
// The crash recovery: the snapshot, the restore and the offer.
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
//
// R4.3: the storage of the snapshot is an edge of its own
// (Source/RecoveryRepository.h) - where it lives, how it is written, how it
// comes back and how it is thrown away. The shell keeps the service: when
// the snapshot is written, and how it is offered.

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
    // R3.4: the epoch, not the revision. The epoch is the "the model moved"
    // signal - it moves with every change, so a long gesture is recovered
    // while it is still going on. The revision moves once per command.
    const auto ep = document.epoch();
    if (now < lastRecoveryMs || now - lastRecoveryMs < 30000) return;   // at most every 30 s
    if (ep == lastRecoveryEpoch) return;                                // nothing changed

    lastRecoveryMs = now;
    lastRecoveryEpoch = ep;

    // the model is captured on the message thread (under the lock); the
    // worker only writes the files
    const auto model = capturePieceModel();
    const auto dir = upic::recoveryDirectory();

    startJob ("Recovery snapshot",
              [dir, model] (Job& job)
              {
                  juce::String error;
                  job.ok = upic::writeRecoverySnapshot (dir, model, error);
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

    const auto dir = upic::recoveryDirectory();
    juce::var piece;
    juce::String error;
    if (! upic::readRecoverySnapshot (dir, piece, error))
    {
        // nothing to recover (or a broken snapshot: it is thrown away, so it
        // cannot ask again and again)
        upic::deleteRecoverySnapshot (dir);
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
            upic::deleteRecoverySnapshot (dir);
        }
    });
}

