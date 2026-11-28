#pragma once

//==============================================================================
// R4.4: the user library's storage - the edge.
//
// The library is a per-user folder of assets that outlive the piece: a
// wavetable, an envelope, an amplitude table, a frequency table, a
// performance sequence. An asset is one JSON file - the kind, the library's
// own id, the name, the colour and the asset's own payload - and the folder
// is where those files live.
//
// The review gives the storage a name of its own - the "library repository"
// (section 34, Persistence/Libraries) - and the reusable assets a format of
// their own - the "library asset codec" (section 33). This is the storage:
// the folder, the atomic write, the validated read, the listing, the first
// free id and the reveal target. The asset's *format* stays in
// Source/UserLibrary.h (UserLibrary::assetToVar / assetFromVar and the
// payload validation), and the shell keeps the service: which asset is
// selected, the bank's caps, the undo transaction and the words the user is
// told.
//
// The bodies are the old UserLibrary methods, body for body. The property
// names, the 4 MB asset cap, the atomic temp-and-rename write, the collision
// counter, the order of the checks and the wording of every refusal do not
// change by a character; the suite (Source/SelfTestLibraryRepository.cpp)
// pins them.
//
// The plan defers the type migration (section 2), so the edge carries the
// JUCE types it needs.
#include <JuceHeader.h>

#include <set>
#include <vector>

#include "PieceLimits.h"
#include "UserLibrary.h"

namespace upic
{
    namespace library_repository
    {
        //================ the folder =========================================
        // The selftest redirects the root to a temp folder, so a test run
        // never touches the user's real library.
        inline juce::File& rootOverride()
        {
            static juce::File root;
            return root;
        }

        inline juce::File rootFolder()
        {
            if (rootOverride() != juce::File())
                return rootOverride();

            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("UPICuitous")
                     .getChildFile ("Library");
        }

        inline void setRootForTest (const juce::File& root) { rootOverride() = root; }
        inline void clearRootForTest() { rootOverride() = juce::File(); }
        inline bool rootIsOverriddenForTest() { return rootOverride() != juce::File(); }

        // The folders are coarser than the kinds: the two table kinds share
        // "tables", so the file carries the kind and a file in the wrong
        // folder is refused.
        inline juce::File folderFor (UserAssetKind kind)
        {
            return rootFolder().getChildFile (userAssetFolderName (kind));
        }

        // A file name from an asset name: the letters and digits survive, the
        // rest becomes an underscore. The name inside the file keeps the
        // original spelling.
        inline juce::String slugFor (const juce::String& name)
        {
            juce::String s;
            for (auto c : name)
            {
                if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_')
                    s += juce::CharacterFunctions::toLowerCase (c);
                else if (s.isNotEmpty() && ! s.endsWith ("_"))
                    s += '_';
            }
            while (s.endsWith ("_")) s = s.dropLastCharacters (1);
            if (s.isEmpty()) s = "asset";
            return s.substring (0, 40);
        }

        //================ writing ============================================
        // Publishes an asset. The name is kept as it is; the file name is
        // derived from it and never overwrites an existing asset (a counter
        // is added instead), so publishing the same name twice keeps both.
        inline bool publish (UserAssetKind kind, int id, const juce::String& name,
                             juce::Colour colour, const juce::var& payload,
                             juce::String& error)
        {
            error.clear();

            const auto trimmed = name.trim();
            if (trimmed.isEmpty())
            {
                error = "The asset needs a name";
                return false;
            }
            if (trimmed.length() > 128)
            {
                error = "The asset's name is too long";
                return false;
            }
            // Piece banks already use zero for their first built-in asset,
            // so the library accepts the same id range. The id is metadata
            // inside the library; an import always mints a new local id.
            if (id < 0 || id > limits::kMaxItemId)
            {
                error = "The asset's id is outside the valid range";
                return false;
            }
            if (! UserLibrary::payloadIsValid (kind, payload, error))
                return false;

            const auto folder = folderFor (kind);
            if (! folder.createDirectory())
            {
                error = "The library folder could not be created: " + folder.getFullPathName();
                return false;
            }

            const auto base = slugFor (trimmed);
            juce::File target = folder.getChildFile (base + ".json");
            for (int n = 2; target.exists() && n < 1000; ++n)
                target = folder.getChildFile (base + "-" + juce::String (n) + ".json");
            if (target.exists())
            {
                error = "The library already holds too many files called \"" + trimmed + "\"";
                return false;
            }

            const auto json = juce::JSON::toString (UserLibrary::assetToVar (kind, id, trimmed,
                                                                             colour, payload), true);

            // The write is atomic: the text goes to a temp file first, and the
            // temp file is renamed over the target. A crash in the middle
            // leaves the old asset (or no asset), never half of one.
            const auto tmp = folder.getChildFile (target.getFileName() + ".tmp");
            tmp.deleteFile();
            if (! tmp.replaceWithText (json))
            {
                error = "The asset could not be written to " + tmp.getFullPathName();
                tmp.deleteFile();
                return false;
            }
            if (! tmp.moveFileTo (target))
            {
                error = "The asset could not be renamed into the library";
                tmp.deleteFile();
                return false;
            }
            return true;
        }

        //================ reading ============================================
        // Reads one file. Everything is validated: the kind has to match the
        // folder it was found in, the payload has to be within the piece's
        // limits, and the name and every number have to have the right type.
        inline bool read (UserAssetKind kind, const juce::File& file,
                          UserAsset& out, juce::String& error)
        {
            error.clear();
            out = UserAsset();

            if (! file.existsAsFile())
            {
                error = "The asset file is gone";
                return false;
            }
            if (file.getSize() > (juce::int64) limits::kMaxUserAssetBytes)
            {
                error = "The asset file is too large (the limit is "
                      + juce::String ((int) (limits::kMaxUserAssetBytes / 1024)) + " kB)";
                return false;
            }

            const auto text = file.loadFileAsString();
            if (text.isEmpty())
            {
                error = "The asset file is empty";
                return false;
            }
            // The file may have grown between the size check and the read.
            // Check the bytes that were actually loaded as well, so the cap
            // is never only advisory.
            if (text.getNumBytesAsUTF8() > (juce::int64) limits::kMaxUserAssetBytes)
            {
                error = "The asset file is too large (the limit is "
                      + juce::String ((int) (limits::kMaxUserAssetBytes / 1024)) + " kB)";
                return false;
            }

            const auto parsed = juce::JSON::parse (text);
            if (! parsed.isObject())
            {
                error = "The asset file is not a valid library asset";
                return false;
            }

            return UserLibrary::assetFromVar (kind, file.getFileName(), parsed, out, error);
        }

        // The selected asset's file name (the manager's list) becomes a file
        // in the kind's own folder. The name is the list's, not a path: the
        // shell never hands the edge a path. Like the old read, this overload
        // does not guard the name - the listing produced it; the reveal path
        // is the one that guards, because a typed name reaches it.
        inline bool read (UserAssetKind kind, const juce::String& fileName,
                          UserAsset& out, juce::String& error)
        {
            return read (kind, folderFor (kind).getChildFile (fileName), out, error);
        }

        // The folder's assets. A malformed or wrongly placed file is skipped
        // (the others still load), counted and reported with its reason. The
        // two table kinds deliberately share one folder, so a valid table of
        // the other kind is not an error.
        inline std::vector<UserAsset> list (UserAssetKind kind, int* skipped = nullptr,
                                            juce::StringArray* failures = nullptr)
        {
            std::vector<UserAsset> out;
            if (skipped != nullptr) *skipped = 0;
            if (failures != nullptr) failures->clear();

            const auto folder = folderFor (kind);
            if (! folder.isDirectory()) return out;

            juce::Array<juce::File> files;
            folder.findChildFiles (files, juce::File::findFiles, false, "*.json");
            files.sort();

            for (const auto& f : files)
            {
                UserAsset a;
                juce::String error;
                if (read (kind, f, a, error))
                {
                    out.push_back (a);
                    continue;
                }

                if (kind == UserAssetKind::AmpTable || kind == UserAssetKind::FreqTable)
                {
                    const auto otherKind = kind == UserAssetKind::AmpTable
                                             ? UserAssetKind::FreqTable
                                             : UserAssetKind::AmpTable;
                    UserAsset other;
                    juce::String otherError;
                    if (read (otherKind, f, other, otherError))
                        continue; // a valid table of the other kind
                }

                if (skipped != nullptr) ++*skipped;
                if (failures != nullptr)
                    failures->add (f.getFileName() + ": " + error);
            }
            return out;
        }

        //================ the first free id ==================================
        // The first free id in the library's own bank for this kind. The
        // library is a flat folder of files, so the id is the asset's
        // identity inside the library (it is never a model id: an import
        // always mints a new local one). A copied file can carry a
        // duplicate id, so the id is repaired to the first free one - the
        // same rule the piece's banks use. -1 means that the library bank
        // has no free id left.
        inline int nextLibraryId (UserAssetKind kind)
        {
            std::set<int> used;
            int* skipped = nullptr;
            for (const auto& asset : list (kind, skipped))
                if (asset.id >= 0 && asset.id <= limits::kMaxItemId)
                    used.insert (asset.id);

            for (int id = 0; id <= limits::kMaxItemId; ++id)
                if (used.count (id) == 0)
                    return id;

            return -1;
        }

        //================ the folder in the file browser =====================
        // Resolves one selected asset without opening Explorer. The file name
        // must be a plain name in the asset's own folder: a path separator or
        // an absolute path is refused before any shell action can happen.
        inline bool revealTarget (UserAssetKind kind, const juce::String& fileName,
                                  juce::File& target, juce::String& error)
        {
            target = juce::File();
            error.clear();
            if (fileName.isEmpty())
            {
                error = "No library file is selected";
                return false;
            }
            if (fileName.contains ("/") || fileName.contains ("\\"))
            {
                error = "The library file name must not contain a path";
                return false;
            }

            const auto folder = folderFor (kind);
            const auto candidate = folder.getChildFile (fileName);
            if (candidate.getParentDirectory() != folder)
            {
                error = "The library file is outside its own folder";
                return false;
            }
            if (! candidate.existsAsFile())
            {
                error = "The selected library file no longer exists";
                return false;
            }

            target = candidate;
            return true;
        }

        inline bool reveal (UserAssetKind kind, const juce::String& fileName,
                            juce::String& error)
        {
            if (fileName.isEmpty())
            {
                const auto folder = folderFor (kind);
                if (! folder.isDirectory())
                {
                    error = "The user library folder does not exist yet";
                    return false;
                }
                folder.revealToUser();
                return true;
            }

            juce::File target;
            if (! revealTarget (kind, fileName, target, error))
                return false;

            target.revealToUser();
            return true;
        }

        inline bool reveal (UserAssetKind kind, const juce::String& fileName)
        {
            juce::String error;
            return reveal (kind, fileName, error);
        }

        inline bool revealFolder (UserAssetKind kind)
        {
            const auto folder = folderFor (kind);
            if (! folder.isDirectory())
                return false;
            folder.revealToUser();
            return true;
        }
    }
}
