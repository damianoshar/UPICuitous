#include <JuceHeader.h>
#include "SelfTest.h"
#include "UserLibrary.h"
#include "LibraryRepository.h"

#include <utility>
#include <vector>

//==============================================================================
// R4.4: the user library's storage - the contract.
//
// The library is a per-user folder of assets that outlive the piece. An asset
// is one JSON file: the kind, the library's own id, the name, the colour and
// the asset's own payload. The folder is the library's storage: where the
// files live, how a name becomes a file name, how a file is written (a temp
// file and a rename), how it is read back (validated against the piece's
// limits), how the folder is listed, and how a file is revealed.
//
// The review gives the storage a name of its own - the "library repository"
// (section 34, Persistence/Libraries) - and the reusable assets a format of
// their own - the "library asset codec" (section 33). The format lives in
// Source/UserLibrary.h; the storage lives in Source/LibraryRepository.h.
//
// This suite is the edge's contract, and it came before the move, as every
// phase in this wave does. It pins what the storage does:
//
// - where the library lives and how the kinds map to folders;
// - how a name becomes a file name;
// - the envelope a published asset is written in, and the atomic write;
// - what a read refuses, with the words it uses;
// - the folder's listing: the valid assets, the refused ones and the
//   shared table folder;
// - the first free library id;
// - the reveal target's guard.
//
// The suite reaches the storage through the repository's own doors
// (upic::library_repository's functions). Before the move they were the
// shell's (upic::UserLibrary's static methods); the checks themselves did
// not change.
//
// The library root is redirected to a temp folder for the whole run, so the
// user's own library is never touched.

namespace
{
    // A fresh work folder for one block of checks.
    juce::File workFolder (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("upic_library_repository_test")
                       .getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    // Puts the library root back where it was, even when a check fails.
    struct RootGuard
    {
        juce::File previous;
        bool wasOverridden;

        RootGuard()
            : previous (upic::library_repository::rootFolder()),
              wasOverridden (upic::library_repository::rootIsOverriddenForTest())
        {
        }

        ~RootGuard()
        {
            if (wasOverridden)
                upic::library_repository::setRootForTest (previous);
            else
                upic::library_repository::clearRootForTest();
        }
    };

    // The payloads the library's five kinds carry. They are hand-built here:
    // this suite pins the storage and the envelope, not the codecs of the
    // assets themselves (their own suites do that).
    juce::var wavePayload()
    {
        juce::Array<juce::var> samples;
        samples.add (0.0);
        samples.add (0.5);
        samples.add (-0.5);

        auto* o = new juce::DynamicObject();
        o->setProperty ("samples", samples);
        return juce::var (o);
    }

    juce::var envelopePayload()
    {
        juce::Array<juce::var> points;
        for (const auto& xy : { std::pair<double, double> { 0.0, 1.0 },
                                std::pair<double, double> { 1.0, 0.0 } })
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("x", xy.first);
            p->setProperty ("y", xy.second);
            points.add (juce::var (p));
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("points", points);
        return juce::var (o);
    }

    juce::var freqTablePayload()
    {
        juce::Array<juce::var> points;
        for (const auto& xh : { std::pair<double, double> { 0.0, 100.0 },
                                std::pair<double, double> { 1.0, 1000.0 } })
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("x", xh.first);
            p->setProperty ("hz", xh.second);
            points.add (juce::var (p));
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("editable", true);
        o->setProperty ("points", points);
        return juce::var (o);
    }

    juce::var sequencePayload()
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("t", 0.0);
        p->setProperty ("pos", 0.5);
        p->setProperty ("page", 0);

        juce::Array<juce::var> points;
        points.add (juce::var (p));

        auto* o = new juce::DynamicObject();
        o->setProperty ("loop", true);
        o->setProperty ("points", points);
        return juce::var (o);
    }

    // The envelope a library file carries.
    juce::var assetObject (const juce::String& kind, int id, const juce::String& name,
                           int colour, const juce::var& payload, int version = 1)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", kind);
        o->setProperty ("id", id);
        o->setProperty ("version", version);
        o->setProperty ("name", name);
        o->setProperty ("colour", colour);
        o->setProperty ("payload", payload);
        return juce::var (o);
    }

    bool writeAssetFile (const juce::File& file, const juce::var& object)
    {
        return file.replaceWithText (juce::JSON::toString (object, true));
    }

    // The shell's read path: the selected file name becomes a file in the
    // kind's own folder, and the storage reads it. The edge now takes the
    // name directly (the shell builds no path).
    bool readByName (upic::UserAssetKind kind, const juce::String& fileName,
                     upic::UserAsset& asset, juce::String& error)
    {
        return upic::library_repository::read (kind, fileName, asset, error);
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

void upic::runLibraryRepositoryChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    const RootGuard rootGuard;
    const auto realRoot = upic::library_repository::rootFolder();

    // --- where the library lives --------------------------------------------
    {
        const auto wanted = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("UPICuitous")
                                .getChildFile ("Library");
        check (realRoot == wanted,
               "R4.4 the library lives in the user's own data folder",
               realRoot.getFullPathName());
    }

    {
        check (upic::library_repository::folderFor (upic::UserAssetKind::Wave)
                   == realRoot.getChildFile ("waves")
               && upic::library_repository::folderFor (upic::UserAssetKind::Envelope)
                   == realRoot.getChildFile ("envelopes")
               && upic::library_repository::folderFor (upic::UserAssetKind::AmpTable)
                   == realRoot.getChildFile ("tables")
               && upic::library_repository::folderFor (upic::UserAssetKind::FreqTable)
                   == realRoot.getChildFile ("tables")
               && upic::library_repository::folderFor (upic::UserAssetKind::Sequence)
                   == realRoot.getChildFile ("sequences"),
               "R4.4 the five kinds live in four folders");
        check (upic::library_repository::folderFor (upic::UserAssetKind::AmpTable)
                   == upic::library_repository::folderFor (upic::UserAssetKind::FreqTable),
               "R4.4 the two table kinds share one folder");
    }

    const auto root = workFolder ("root");
    upic::library_repository::setRootForTest (root);
    check (upic::library_repository::rootIsOverriddenForTest()
           && upic::library_repository::rootFolder() == root,
           "R4.4 the root can be redirected", upic::library_repository::rootFolder().getFullPathName());
    upic::library_repository::clearRootForTest();
    check (! upic::library_repository::rootIsOverriddenForTest()
           && upic::library_repository::rootFolder() == realRoot,
           "R4.4 the redirect can be cleared");
    upic::library_repository::setRootForTest (root);

    // --- a name becomes a file name -----------------------------------------
    {
        check (upic::library_repository::slugFor ("My Wave 01!") == "my_wave_01"
               && upic::library_repository::slugFor ("A_B-C") == "a_b-c",
               "R4.4 a name becomes a file name",
               upic::library_repository::slugFor ("My Wave 01!"));
        check (upic::library_repository::slugFor ("") == "asset"
               && upic::library_repository::slugFor ("!!!") == "asset"
               && upic::library_repository::slugFor ("Name___") == "name",
               "R4.4 a name with nothing usable becomes 'asset'");
        check (upic::library_repository::slugFor (juce::String::repeatedString ("a", 60)).length() == 40,
               "R4.4 a file name is at most 40 characters",
               juce::String (upic::library_repository::slugFor (juce::String::repeatedString ("a", 60)).length()));
    }

    // --- the write -----------------------------------------------------------
    {
        juce::String error;
        const bool ok = upic::library_repository::publish (upic::UserAssetKind::Wave, 5, "My Wave",
                                                    juce::Colour (0xff123456), wavePayload(), error);
        check (ok && error.isEmpty(), "R4.4 a wave publishes", error);

        const auto file = upic::library_repository::folderFor (upic::UserAssetKind::Wave)
                              .getChildFile ("my_wave.json");
        check (file.existsAsFile(), "R4.4 the file is named after the asset", file.getFileName());

        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        auto* o = parsed.getDynamicObject();
        check (o != nullptr && o->getProperties().size() == 6
               && o->getProperty ("kind").toString() == "wave"
               && (int) o->getProperty ("id") == 5
               && (int) o->getProperty ("version") == 1
               && o->getProperty ("name").toString() == "My Wave"
               && (int) o->getProperty ("colour") == (int) 0xff123456
               && o->getProperty ("payload").getProperty ("samples", juce::var()).getArray() != nullptr,
               "R4.4 the file is the library's envelope",
               juce::JSON::toString (parsed, false).substring (0, 80));
    }

    {
        juce::String error;
        const bool ok = upic::library_repository::publish (upic::UserAssetKind::Wave, 6, "My Wave",
                                                    juce::Colours::white, wavePayload(), error);
        const auto folder = upic::library_repository::folderFor (upic::UserAssetKind::Wave);
        check (ok
               && folder.getChildFile ("my_wave.json").existsAsFile()
               && folder.getChildFile ("my_wave-2.json").existsAsFile(),
               "R4.4 the same name publishes beside the first", error);
    }

    {
        juce::String error;
        const bool env = upic::library_repository::publish (upic::UserAssetKind::Envelope, 7, "My Envelope",
                                                     juce::Colours::white, envelopePayload(), error);
        const bool amp = upic::library_repository::publish (upic::UserAssetKind::AmpTable, 4, "My Amp",
                                                     juce::Colours::white, envelopePayload(), error);
        const bool freq = upic::library_repository::publish (upic::UserAssetKind::FreqTable, 0, "Page 1 space 1",
                                                      juce::Colours::white, freqTablePayload(), error);
        check (env && amp && freq
               && upic::library_repository::folderFor (upic::UserAssetKind::Envelope)
                      .getChildFile ("my_envelope.json").existsAsFile()
               && upic::library_repository::folderFor (upic::UserAssetKind::AmpTable)
                      .getChildFile ("my_amp.json").existsAsFile()
               && upic::library_repository::folderFor (upic::UserAssetKind::FreqTable)
                      .getChildFile ("page_1_space_1.json").existsAsFile(),
               "R4.4 the other kinds publish into their folders", error);
    }

    // --- the refusals of the write ------------------------------------------
    {
        juce::String error;
        check (! upic::library_repository::publish (upic::UserAssetKind::Wave, 1, "   ",
                                             juce::Colours::white, wavePayload(), error)
               && error == "The asset needs a name",
               "R4.4 a publish without a name is refused", error);

        error.clear();
        check (! upic::library_repository::publish (upic::UserAssetKind::Wave, 1,
                                             juce::String::repeatedString ("a", 129),
                                             juce::Colours::white, wavePayload(), error)
               && error == "The asset's name is too long",
               "R4.4 a name over the limit is refused", error);

        error.clear();
        const bool negative = upic::library_repository::publish (upic::UserAssetKind::Wave, -1, "Negative",
                                                          juce::Colours::white, wavePayload(), error);
        const auto negativeError = error;
        error.clear();
        const bool tooBig = upic::library_repository::publish (upic::UserAssetKind::Wave,
                                                        upic::limits::kMaxItemId + 1, "Too Big",
                                                        juce::Colours::white, wavePayload(), error);
        check (! negative && ! tooBig
               && negativeError == "The asset's id is outside the valid range"
               && error == "The asset's id is outside the valid range",
               "R4.4 an id outside the range is refused", negativeError + " / " + error);
    }

    {
        // The payload is validated before the folder is made: a bad file
        // cannot leave an empty folder behind.
        const auto folder = upic::library_repository::folderFor (upic::UserAssetKind::Sequence);
        check (! folder.isDirectory(), "R4.4 the sequence folder starts absent");

        juce::String error;
        auto bad = sequencePayload();
        if (auto* points = bad.getProperty ("points", juce::var()).getArray())
            if (! points->isEmpty())
                (*points)[0].getDynamicObject()->setProperty ("page", -1);

        check (! upic::library_repository::publish (upic::UserAssetKind::Sequence, 1, "Bad Sequence",
                                             juce::Colours::white, bad, error)
               && error == "The sequence holds a malformed point"
               && ! folder.isDirectory(),
               "R4.4 an invalid payload is refused before the folder is made", error);
    }

    {
        // A root that is a file: the kind's folder cannot be made.
        const auto blocked = workFolder ("blocked_root");
        const auto file = blocked.getChildFile ("root file");
        file.replaceWithText ("not a folder");
        upic::library_repository::setRootForTest (file);

        juce::String error;
        const bool ok = upic::library_repository::publish (upic::UserAssetKind::Wave, 1, "Blocked",
                                                    juce::Colours::white, wavePayload(), error);
        check (! ok && error.startsWith ("The library folder could not be created: "),
               "R4.4 a folder that cannot be created is refused", error);
        upic::library_repository::setRootForTest (root);
    }

    {
        // The temp file cannot be written: the target is never touched, and
        // no asset is left behind. A read-only temp file is the block (a
        // directory would be removed by the write's own cleanup).
        const auto folder = upic::library_repository::folderFor (upic::UserAssetKind::Wave);
        const auto temp = folder.getChildFile ("blocked_write.json.tmp");
        temp.replaceWithText ("in the way");
        temp.setReadOnly (true);

        juce::String error;
        const bool ok = upic::library_repository::publish (upic::UserAssetKind::Wave, 1, "Blocked Write",
                                                    juce::Colours::white, wavePayload(), error);
        check (! ok && error.startsWith ("The asset could not be written to ")
               && ! folder.getChildFile ("blocked_write.json").exists(),
               "R4.4 a temp file that cannot be written is refused, and no asset is left", error);

        temp.setReadOnly (false);
        temp.deleteFile();
    }

    // --- the read ------------------------------------------------------------
    {
        upic::UserAsset asset;
        juce::String error;
        const bool ok = readByName (upic::UserAssetKind::Wave, "my_wave.json", asset, error);
        check (ok && asset.kind == upic::UserAssetKind::Wave
               && asset.id == 5
               && asset.fileName == "my_wave.json"
               && asset.name == "My Wave"
               && asset.colour.getARGB() == 0xff123456u
               && asset.payload.getProperty ("samples", juce::var()).getArray() != nullptr,
               "R4.4 a wave reads back as it was written", error);
    }

    {
        struct Wanted
        {
            upic::UserAssetKind kind;
            const char* fileName;
            int id;
            const char* name;
        };

        const Wanted wanted[] =
        {
            { upic::UserAssetKind::Envelope, "my_envelope.json", 7, "My Envelope" },
            { upic::UserAssetKind::AmpTable, "my_amp.json", 4, "My Amp" },
            { upic::UserAssetKind::FreqTable, "page_1_space_1.json", 0, "Page 1 space 1" }
        };

        bool all = true;
        juce::String detail;
        for (const auto& w : wanted)
        {
            upic::UserAsset asset;
            juce::String error;
            if (! readByName (w.kind, w.fileName, asset, error)
                || asset.id != w.id || asset.name != w.name)
            {
                all = false;
                detail = juce::String (w.fileName) + ": " + error;
                break;
            }
        }
        check (all, "R4.4 the other kinds read back", detail);
    }

    {
        // A file name is resolved in the kind's own folder, not in the root.
        upic::UserAsset asset;
        juce::String error;
        check (! readByName (upic::UserAssetKind::Envelope, "my_wave.json", asset, error)
               && error == "The asset file is gone",
               "R4.4 a file name is resolved in its kind's own folder", error);
    }

    // --- the listing ---------------------------------------------------------
    {
        // The listing runs before the refusal files below: it counts the
        // folder's refusals, so the folder must hold only the two assets and
        // the one broken file it is meant to report.
        const auto folder = upic::library_repository::folderFor (upic::UserAssetKind::Wave);
        folder.getChildFile ("broken.json").replaceWithText ("this is not a library asset");
        folder.getChildFile ("notes.txt").replaceWithText ("not a library asset");

        int skipped = 0;
        juce::StringArray failures;
        const auto assets = upic::library_repository::list (upic::UserAssetKind::Wave, &skipped, &failures);
        check (assets.size() == 2 && skipped == 1 && failures.size() == 1
               && failures[0] == "broken.json: The asset file is not a valid library asset",
               "R4.4 the folder's assets are listed, and the broken ones are counted",
               juce::String ((int) assets.size()) + " assets, "
               + juce::String (skipped) + " skipped, "
               + (failures.isEmpty() ? juce::String ("no failure") : failures[0]));
    }

    {
        // The two table kinds share a folder: a valid table of the other
        // kind is not a failure.
        int skipped = 0;
        juce::StringArray failures;
        const auto amps = upic::library_repository::list (upic::UserAssetKind::AmpTable, &skipped, &failures);
        check (amps.size() == 1 && skipped == 0 && failures.isEmpty(),
               "R4.4 a valid table of the other kind is not a failure",
               juce::String ((int) amps.size()) + " amps, " + juce::String (skipped) + " skipped");

        const auto freqs = upic::library_repository::list (upic::UserAssetKind::FreqTable);
        check (freqs.size() == 1 && freqs[0].name == "Page 1 space 1",
               "R4.4 the shared folder lists the right kind",
               juce::String ((int) freqs.size()) + " tables");
    }

    // --- the refusals of the read -------------------------------------------
    {
        upic::UserAsset asset;
        juce::String error;
        check (! readByName (upic::UserAssetKind::Wave, "missing.json", asset, error)
               && error == "The asset file is gone",
               "R4.4 a missing file is refused", error);

        const auto folder = upic::library_repository::folderFor (upic::UserAssetKind::Wave);
        writeBigFile (folder.getChildFile ("big.json"), 5);
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "big.json", asset, error)
               && error == "The asset file is too large (the limit is 4096 kB)",
               "R4.4 a file over the cap is refused", error);

        folder.getChildFile ("empty.json").replaceWithText ("");
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "empty.json", asset, error)
               && error == "The asset file is empty",
               "R4.4 an empty file is refused", error);

        folder.getChildFile ("broken.json").replaceWithText ("this is not a library asset");
        error.clear();
        const bool broken = readByName (upic::UserAssetKind::Wave, "broken.json", asset, error);
        const auto brokenError = error;
        folder.getChildFile ("array.json").replaceWithText ("[]");
        error.clear();
        const bool array = readByName (upic::UserAssetKind::Wave, "array.json", asset, error);
        check (! broken && ! array
               && brokenError == "The asset file is not a valid library asset"
               && error == "The asset file is not a valid library asset",
               "R4.4 a file that is not a library asset is refused", brokenError + " / " + error);

        auto unknown = assetObject ("wave", 1, "Unknown", 0, wavePayload());
        unknown.getDynamicObject()->setProperty ("extra", 1);
        writeAssetFile (folder.getChildFile ("unknown.json"), unknown);
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "unknown.json", asset, error)
               && error == "The asset file has an unknown property: extra",
               "R4.4 an unknown property is refused", error);

        writeAssetFile (folder.getChildFile ("version.json"),
                        assetObject ("wave", 1, "Version", 0, wavePayload(), 2));
        error.clear();
        const bool version2 = readByName (upic::UserAssetKind::Wave, "version.json", asset, error);
        const auto versionError = error;
        auto noVersion = assetObject ("wave", 1, "No Version", 0, wavePayload());
        noVersion.getDynamicObject()->removeProperty ("version");
        writeAssetFile (folder.getChildFile ("no_version.json"), noVersion);
        error.clear();
        const bool noVersionRead = readByName (upic::UserAssetKind::Wave, "no_version.json", asset, error);
        check (! version2 && ! noVersionRead
               && versionError == "The asset file has an unsupported version"
               && error == "The asset file has an unsupported version",
               "R4.4 an unsupported version is refused", versionError + " / " + error);

        writeAssetFile (folder.getChildFile ("wrong_kind.json"),
                        assetObject ("envelope", 1, "Wrong Kind", 0, envelopePayload()));
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "wrong_kind.json", asset, error)
               && error == "The file does not hold a \"wave\" asset",
               "R4.4 a file of the wrong kind is refused", error);

        writeAssetFile (folder.getChildFile ("bad_id.json"),
                        assetObject ("wave", upic::limits::kMaxItemId + 1, "Bad Id", 0, wavePayload()));
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "bad_id.json", asset, error)
               && error == "The asset's id is outside the valid range",
               "R4.4 an id outside the range is refused (read)", error);

        auto noNameFile = assetObject ("wave", 1, "Not a string", 0, wavePayload());
        noNameFile.getDynamicObject()->setProperty ("name", 42);
        writeAssetFile (folder.getChildFile ("no_name.json"), noNameFile);
        error.clear();
        const bool noName = readByName (upic::UserAssetKind::Wave, "no_name.json", asset, error);
        const auto noNameError = error;
        writeAssetFile (folder.getChildFile ("empty_name.json"),
                        assetObject ("wave", 1, "   ", 0, wavePayload()));
        error.clear();
        const bool emptyName = readByName (upic::UserAssetKind::Wave, "empty_name.json", asset, error);
        const auto emptyNameError = error;
        writeAssetFile (folder.getChildFile ("long_name.json"),
                        assetObject ("wave", 1, juce::String::repeatedString ("a", 129), 0, wavePayload()));
        error.clear();
        const bool longName = readByName (upic::UserAssetKind::Wave, "long_name.json", asset, error);
        check (! noName && ! emptyName && ! longName
               && noNameError == "The asset has no valid name"
               && emptyNameError == "The asset has no name"
               && error == "The asset's name is too long",
               "R4.4 a name that is missing, empty or too long is refused",
               noNameError + " / " + emptyNameError + " / " + error);

        auto noColour = assetObject ("wave", 1, "No Colour", 0, wavePayload());
        noColour.getDynamicObject()->removeProperty ("colour");
        writeAssetFile (folder.getChildFile ("no_colour.json"), noColour);
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "no_colour.json", asset, error)
               && error == "The asset has no valid colour",
               "R4.4 a colour that is missing is refused", error);

        auto noData = assetObject ("wave", 1, "No Data", 0, wavePayload());
        noData.getDynamicObject()->removeProperty ("payload");
        writeAssetFile (folder.getChildFile ("no_data.json"), noData);
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "no_data.json", asset, error)
               && error == "The asset has no data",
               "R4.4 a file without data is refused", error);

        juce::Array<juce::var> oneSample;
        oneSample.add (0.0);
        auto* badPayloadObject = new juce::DynamicObject();
        badPayloadObject->setProperty ("samples", oneSample);
        writeAssetFile (folder.getChildFile ("bad_payload.json"),
                        assetObject ("wave", 1, "Bad Payload", 0, juce::var (badPayloadObject)));
        error.clear();
        check (! readByName (upic::UserAssetKind::Wave, "bad_payload.json", asset, error)
               && error == "The wavetable's sample count is outside the valid range (2 to 4096 samples)",
               "R4.4 a payload that is invalid is refused", error);
    }

    // --- the next library id -------------------------------------------------
    {
        const auto secondRoot = workFolder ("second");
        upic::library_repository::setRootForTest (secondRoot);

        int skipped = 0;
        juce::StringArray failures;
        const auto none = upic::library_repository::list (upic::UserAssetKind::Sequence, &skipped, &failures);
        check (none.empty() && skipped == 0 && failures.isEmpty()
               && upic::library_repository::nextLibraryId (upic::UserAssetKind::Sequence) == 0,
               "R4.4 an empty library starts at id 0");

        juce::String error;
        check (upic::library_repository::publish (upic::UserAssetKind::Sequence, 0, "Seq A",
                                           juce::Colours::white, sequencePayload(), error)
               && upic::library_repository::publish (upic::UserAssetKind::Sequence, 2, "Seq B",
                                              juce::Colours::white, sequencePayload(), error),
               "R4.4 two sequences publish", error);
        check (upic::library_repository::nextLibraryId (upic::UserAssetKind::Sequence) == 1,
               "R4.4 the next id is the first free one",
               juce::String (upic::library_repository::nextLibraryId (upic::UserAssetKind::Sequence)));

        check (upic::library_repository::publish (upic::UserAssetKind::Sequence, 0, "Seq C",
                                           juce::Colours::white, sequencePayload(), error)
               && upic::library_repository::nextLibraryId (upic::UserAssetKind::Sequence) == 1,
               "R4.4 a duplicate id does not move the next id");
    }

    // --- the reveal target ---------------------------------------------------
    {
        // The sequence folder is not there yet in the second root? It is: the
        // publish above made it. Use a fresh root for the missing-folder case.
        const auto emptyRoot = workFolder ("empty");
        upic::library_repository::setRootForTest (emptyRoot);
        juce::String error;
        check (! upic::library_repository::reveal (upic::UserAssetKind::Sequence, "", error)
               && error == "The user library folder does not exist yet"
               && ! upic::library_repository::revealFolder (upic::UserAssetKind::Sequence),
               "R4.4 a reveal of a folder that is not there is refused", error);
        upic::library_repository::setRootForTest (root);
    }

    {
        juce::File target;
        juce::String error;
        check (! upic::library_repository::revealTarget (upic::UserAssetKind::Wave, "", target, error)
               && error == "No library file is selected",
               "R4.4 an empty reveal selection is refused", error);

        error.clear();
        const bool sub = upic::library_repository::revealTarget (upic::UserAssetKind::Wave,
                                                          "sub/inside.json", target, error);
        const auto subError = error;
        error.clear();
        const bool outside = upic::library_repository::revealTarget (upic::UserAssetKind::Wave,
                                                              "..\\outside.json", target, error);
        const auto outsideError = error;
        error.clear();
        const bool absolute = upic::library_repository::revealTarget (upic::UserAssetKind::Wave,
                                                               "C:\\outside.json", target, error);
        check (! sub && ! outside && ! absolute
               && subError == "The library file name must not contain a path"
               && outsideError == "The library file name must not contain a path"
               && error == "The library file name must not contain a path",
               "R4.4 a reveal name with a path is refused", error);

        error.clear();
        check (! upic::library_repository::revealTarget (upic::UserAssetKind::Wave,
                                                  "gone.json", target, error)
               && error == "The selected library file no longer exists",
               "R4.4 a reveal of a file that is gone is refused", error);

        error.clear();
        const bool found = upic::library_repository::revealTarget (upic::UserAssetKind::Wave,
                                                            "my_wave.json", target, error);
        check (found && error.isEmpty()
               && target == upic::library_repository::folderFor (upic::UserAssetKind::Wave)
                              .getChildFile ("my_wave.json")
               && target.getParentDirectory()
                   == upic::library_repository::folderFor (upic::UserAssetKind::Wave),
               "R4.4 a plain name resolves inside the kind's own folder", error);
    }
}
