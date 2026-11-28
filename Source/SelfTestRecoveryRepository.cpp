#include <JuceHeader.h>
#include "MainComponent.h"
#include "PieceCodec.h"
#include "RecoveryRepository.h"
#include "SampleCodec.h"
#include "SelfTest.h"

#include <vector>

//==============================================================================
// R4.3: the recovery snapshot's storage - the contract.
//
// The crash recovery writes a snapshot of the piece while there is unsaved
// work, and offers it at the next start. The snapshot is a small piece JSON
// (the pages, without the audio) plus the audio in sidecar files, so the
// unchanged audio is not embedded again and again.
//
// The storage of that snapshot is a thing of its own: where it lives, how
// it is written, how it is read back and how it is thrown away. The review
// asks for the recovery to be a service of its own (R065), and the review's
// own table of the storage responsibilities (section 33) calls it the
// recovery repository. Today it is four static methods of the shell.
//
// This suite is that edge's contract, and it comes before the move, as
// every phase in this wave does. It pins what the storage does today:
//
// - where the snapshots live;
// - the shape of a snapshot: the piece file, the sidecar whose name
//   carries the hash of the audio, and the piece JSON that points at it;
// - the sidecars that are not used any more are removed;
// - every refusal, with the words it uses.
//
// The suite reaches the storage through the shell's own doors
// (upic::writeRecoverySnapshot and its neighbours). The move turns
// them into the edge's, and the checks themselves do not change.
//
// The suite works in its own folders under the temp directory, so it never
// touches the user's own data folder.

namespace
{
    // A fresh work folder for one block of checks.
    juce::File workFolder (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("upic_recovery_repository_test")
                       .getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    // A model with one page, one arc and a little audio. The phase of the
    // tone changes the audio, and with it the hash in the sidecar's name.
    upic::PieceModel modelWithAudio (float phase = 0.0f)
    {
        upic::PieceModel model;

        upic::Page page;
        page.durationSec = 10.0;
        upic::Arc arc;
        arc.id = 1;
        arc.points = { { 0.0, 220.0 }, { 4.0, 880.0 } };
        page.arcs.push_back (arc);
        model.pages.push_back (page);

        juce::AudioBuffer<float> buf (2, 1000);
        for (int i = 0; i < 1000; ++i)
            for (int c = 0; c < 2; ++c)
                buf.setSample (c, i, 0.25f * std::sin ((float) i * 0.01f + phase));

        upic::SampleSlot slot;
        slot.id = 1;
        slot.name = "take.wav";
        slot.sampleRate = 48000.0;
        slot.data = std::make_shared<const juce::AudioBuffer<float>> (buf);
        model.sampleSlots.push_back (slot);
        return model;
    }

    // The sample's entry in a snapshot (a void var when there is none).
    juce::var sampleEntry (const juce::var& snapshot, int index = 0)
    {
        auto* samples = snapshot.getProperty ("samples", juce::var()).getArray();
        if (samples == nullptr || index >= samples->size()) return {};
        return (*samples)[index];
    }

    // The name of the sidecar a snapshot points at (empty when there is
    // none).
    juce::String sidecarName (const juce::File& dir)
    {
        const auto pieceFile = dir.getChildFile ("piece.json");
        if (! pieceFile.existsAsFile()) return {};
        const auto snapshot = juce::JSON::parse (pieceFile.loadFileAsString());
        auto* obj = sampleEntry (snapshot).getDynamicObject();
        return obj != nullptr ? obj->getProperty ("recoveryFile").toString() : juce::String();
    }

    // A file of the given size, filled with 'x'.
    void writeBigFile (const juce::File& f, int megabytes)
    {
        auto out = f.createOutputStream();
        const std::vector<char> block (1024 * 1024, 'x');
        for (int i = 0; i < megabytes && out != nullptr; ++i)
            out->write (block.data(), (int) block.size());
    }
}

void upic::runRecoveryRepositoryChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- where the snapshots live -------------------------------------------
    {
        const auto dir = upic::recoveryDirectory();
        const auto wanted = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("UPICuitous")
                                .getChildFile ("recovery");
        check (dir == wanted,
               "R4.3 the snapshots live in the user's own data folder",
               dir.getFullPathName());
    }

    // --- the shape of a snapshot ---------------------------------------------
    {
        const auto dir = workFolder ("shape");
        juce::String error;
        const bool written = upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        check (written && error.isEmpty(),
               "R4.3 a snapshot is written", error);

        const auto pieceFile = dir.getChildFile ("piece.json");
        check (pieceFile.existsAsFile(),
               "R4.3 the snapshot has its piece file");

        const auto name = sidecarName (dir);
        check (name.startsWith ("sample-1-") && name.endsWith (".pcm"),
               "R4.3 the sidecar's name carries the slot and the hash of the audio",
               name);
        check (dir.getChildFile (name).existsAsFile(),
               "R4.3 the audio is in its own file", name);

        // the piece file does not embed the audio - that is the whole point
        // of the sidecars
        const auto snapshot = juce::JSON::parse (pieceFile.loadFileAsString());
        auto* obj = sampleEntry (snapshot).getDynamicObject();
        const auto data = obj != nullptr ? obj->getProperty ("data").toString() : juce::String ("?");
        check (obj != nullptr && data.isEmpty(),
               "R4.3 the piece file does not embed the audio", data);
    }

    // --- the sidecars that are not used any more are removed -----------------
    {
        const auto dir = workFolder ("sidecars");
        juce::String error;
        const auto model = modelWithAudio();

        upic::writeRecoverySnapshot (dir, model, error);
        const auto first = sidecarName (dir);

        // the same audio again: the same sidecar
        upic::writeRecoverySnapshot (dir, model, error);
        check (sidecarName (dir) == first && dir.getChildFile (first).existsAsFile(),
               "R4.3 the same audio keeps its sidecar", first);

        // different audio: a new sidecar, and the old one is gone (the
        // folder must not grow with every snapshot)
        auto changed = model;
        changed.sampleSlots[0].data = modelWithAudio (1.0f).sampleSlots[0].data;
        upic::writeRecoverySnapshot (dir, changed, error);
        const auto second = sidecarName (dir);
        check (second != first && ! dir.getChildFile (first).existsAsFile()
               && dir.getChildFile (second).existsAsFile(),
               "R4.3 the sidecars that are not used any more are removed",
               first + " -> " + second);
    }

    // --- the refusals of the write -------------------------------------------
    {
        // a folder that cannot be created
        const auto blocked = workFolder ("blocked_folder");
        const auto file = blocked.getChildFile ("a file");
        file.replaceWithText ("not a folder");
        juce::String error;
        const bool written = upic::writeRecoverySnapshot (file.getChildFile ("recovery"),
                                                                   modelWithAudio(), error);
        check (! written && error == "the recovery folder could not be created",
               "R4.3 a folder that cannot be created is refused", error);
    }

    {
        // an audio file that cannot be stored
        const auto dir = workFolder ("blocked_audio");
        juce::String error;
        upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        const auto name = sidecarName (dir);
        dir.getChildFile (name).deleteFile();
        dir.getChildFile (name).createDirectory();
        const bool written = upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        check (! written && error == "the recovery audio could not be stored"
               && dir.getChildFile (name).isDirectory(),
               "R4.3 an audio file that cannot be stored is refused", error);
    }

    {
        // a piece file that cannot be stored
        const auto dir = workFolder ("blocked_piece");
        dir.getChildFile ("piece.json").createDirectory();
        juce::String error;
        const bool written = upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        check (! written && error == "the recovery snapshot could not be stored",
               "R4.3 a piece file that cannot be stored is refused", error);
    }

    // --- the refusals of the read --------------------------------------------
    {
        // there is no snapshot at all
        const auto dir = workFolder ("no_snapshot");
        juce::String error;
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "there is no recovery snapshot",
               "R4.3 there is no recovery snapshot", error);
    }

    {
        // an empty piece file
        const auto dir = workFolder ("empty_snapshot");
        dir.getChildFile ("piece.json").replaceWithText ("");
        juce::String error;
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery snapshot is empty",
               "R4.3 an empty snapshot is refused", error);
    }

    {
        // a piece file that is not JSON
        const auto dir = workFolder ("broken_snapshot");
        dir.getChildFile ("piece.json").replaceWithText ("this is not a piece");
        juce::String error;
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery snapshot is not a piece",
               "R4.3 a snapshot that is not a piece is refused", error);
    }

    {
        // a piece file over the cap (8 MB)
        const auto dir = workFolder ("big_snapshot");
        writeBigFile (dir.getChildFile ("piece.json"), 9);
        juce::String error;
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery snapshot is too big",
               "R4.3 a snapshot over the cap is refused", error);
    }

    {
        // a snapshot that lost its audio
        const auto dir = workFolder ("lost_audio");
        juce::String error;
        upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        dir.getChildFile (sidecarName (dir)).deleteFile();
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery snapshot lost its audio",
               "R4.3 a snapshot that lost its audio is refused", error);
    }

    {
        // an audio file over the cap
        const auto dir = workFolder ("big_audio");
        juce::String error;
        upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        writeBigFile (dir.getChildFile (sidecarName (dir)), 34);
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery audio is too big",
               "R4.3 an audio file over the cap is refused", error);
    }

    {
        // an audio file that cannot be read
        const auto dir = workFolder ("empty_audio");
        juce::String error;
        upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        dir.getChildFile (sidecarName (dir)).replaceWithText ("");
        juce::var recovered;
        check (! upic::readRecoverySnapshot (dir, recovered, error)
               && error == "the recovery audio could not be read",
               "R4.3 an audio file that cannot be read is refused", error);
    }

    // --- the read brings the audio back where the loader wants it -----------
    {
        const auto dir = workFolder ("round_trip");
        const auto model = modelWithAudio();
        juce::String error;
        upic::writeRecoverySnapshot (dir, model, error);

        juce::var recovered;
        const bool read = upic::readRecoverySnapshot (dir, recovered, error);
        check (read, "R4.3 the snapshot reads back", error);

        // the audio is base64 again, and the sidecar's name is gone - the
        // loader does not know that property
        auto* obj = sampleEntry (recovered).getDynamicObject();
        const auto data = obj != nullptr ? obj->getProperty ("data").toString() : juce::String();
        const auto pcm = upic::sampleSlotToPcm16 (model.sampleSlots[0]);
        check (obj != nullptr && ! obj->hasProperty ("recoveryFile")
               && data == juce::Base64::toBase64 (pcm.getData(), pcm.getSize()),
               "R4.3 the recovered audio is the same audio, and the sidecar's name is gone",
               data.substring (0, 40));
    }

    // --- the cleanup ----------------------------------------------------------
    {
        const auto dir = workFolder ("cleanup");
        juce::String error;
        upic::writeRecoverySnapshot (dir, modelWithAudio(), error);
        upic::deleteRecoverySnapshot (dir);

        int left = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false))
        {
            ++left;
        }
        check (dir.exists() && left == 0,
               "R4.3 the cleanup throws the snapshot away and keeps the folder",
               juce::String (left) + " file(s) left");
    }
}
