#pragma once

//==============================================================================
// R4.3: the recovery snapshot's storage - the edge.
//
// The crash recovery writes a snapshot of the piece while there is unsaved
// work, and offers it at the next start. The snapshot is a small piece JSON
// (the pages, without the audio) plus the audio in sidecar files, so the
// unchanged audio is not embedded again and again.
//
// The storage of that snapshot is a thing of its own: where it lives, how
// it is written, how it comes back and how it is thrown away. The review
// gives that responsibility a name - the "recovery repository" (section 33,
// the table of the storage responsibilities) - and asks for the recovery to
// be a service of its own (R065, "Extract RecoveryService").
//
// This is the storage. The service - the periodic write, the offer at the
// next start and the words the user is told - stays in the shell
// (Source/MainComponentRecovery.cpp), and it calls the edge.
//
// The plan defers the type migration (section 2), so the edge carries the
// JUCE types it needs. The wording of the messages does not change by a
// character: they are the strings the shell showed before, and the suite
// (Source/SelfTestRecoveryRepository.cpp) pins them.
#include <JuceHeader.h>

#include "PieceCodec.h"
#include "PieceLimits.h"
#include "SampleCodec.h"

namespace upic
{
    //================ where the snapshots live ===============================
    //
    // The user's own data folder (the roaming one on Windows): the snapshot
    // belongs to the user, not to the machine.
    inline juce::File recoveryDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("UPICuitous")
                    .getChildFile ("recovery");
    }

    namespace recovery_detail
    {
        // A hash of the bytes, so a sidecar that is already there is not
        // written again (the name of the file carries the hash).
        inline juce::uint64 hashBytes (const void* data, size_t size)
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
    }

    //================ the writer =============================================
    //
    // The piece JSON without the audio: the audio goes to the sidecars. A
    // sidecar is written only when the audio really changed, so the repeated
    // snapshots carry the pages, not the material - and the sidecars that
    // are not used any more are removed at the end, so the folder does not
    // grow with every snapshot.
    inline bool writeRecoverySnapshot (const juce::File& dir, const PieceModel& model,
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
            const auto hash = recovery_detail::hashBytes (pcm.getData(), pcm.getSize());
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

    //================ the reader =============================================
    //
    // The piece JSON comes back, and with it the audio: every sidecar the
    // snapshot points at is read and put into the shape the loader wants
    // (base64). A snapshot that is not there, that is empty, that is not a
    // piece, that is over the cap or that lost its audio is refused, and the
    // reason is said in words.
    inline bool readRecoverySnapshot (const juce::File& dir, juce::var& pieceOut, juce::String& error)
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

    //================ the cleanup ============================================
    //
    // The snapshot is thrown away and the folder is kept: the folder is the
    // app's, and it is written again at the next snapshot.
    inline void deleteRecoverySnapshot (const juce::File& dir)
    {
        if (! dir.exists()) return;
        for (auto& entry : juce::RangedDirectoryIterator (dir, false))
            entry.getFile().deleteFile();
    }
}
