#include "MainComponent.h"
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

        const auto pcm = upic::sampleSlotToPcm16 (slot);
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
    const auto rev = document.revision();
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

