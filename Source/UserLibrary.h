#pragma once
#include <JuceHeader.h>

#include <cmath>
#include <limits>
#include <set>

#include "Envelope.h"
#include "FreqSpace.h"
#include "Performance.h"
#include "PieceLimits.h"
#include "UpicConstants.h"
#include "Waveform.h"

//==============================================================================
// WP7 (Wave F, 2026-11-21): the user-level asset library.
//
// The piece owns the material it uses: a wavetable, an envelope, an amplitude
// table, a frequency table, a performance sequence. The library is the OTHER
// half - a per-user folder that outlives the piece, so material can be moved
// between pieces without inventing a file format for libraries.
//
// The rules (they are the reason this file is small and boring):
//   - the library never becomes a dependency. A piece always carries what it
//     uses, so deleting the library folder cannot change a piece;
//   - an imported asset gets a NEW local id. The ids stay local to the piece,
//     so two pieces can hold the same asset without agreeing about ids;
//   - a file is written atomically (a temp file + a rename), so a crash or a
//     full disk cannot leave a half-written asset behind;
//   - everything read back is validated against the same limits the piece
//     uses, and a file that fails is REFUSED, not silently repaired.
//
// The folder: <user application data>/UPICuitous/Library/{waves,envelopes,
// tables,sequences}. Every asset is one JSON file:
//   { "kind":"wave", "id":1, "version":1,
//     "name":"...", "colour":-1234, "payload":{ ...the asset's own var... } }
//==============================================================================
namespace upic
{
    // The kinds of asset the library holds. The folders are coarser than the
    // kinds (the two table kinds share "tables"), so the file carries the
    // kind and a file in the wrong folder is refused.
    enum class UserAssetKind
    {
        Wave = 0,
        Envelope,
        AmpTable,
        FreqTable,
        Sequence
    };

    inline const char* userAssetKindName (UserAssetKind kind)
    {
        switch (kind)
        {
            case UserAssetKind::Wave:     return "wave";
            case UserAssetKind::Envelope: return "envelope";
            case UserAssetKind::AmpTable: return "ampTable";
            case UserAssetKind::FreqTable: return "freqTable";
            case UserAssetKind::Sequence: return "sequence";
            default:                      return "wave";
        }
    }

    inline const char* userAssetFolderName (UserAssetKind kind)
    {
        switch (kind)
        {
            case UserAssetKind::Wave:     return "waves";
            case UserAssetKind::Envelope: return "envelopes";
            case UserAssetKind::AmpTable:
            case UserAssetKind::FreqTable: return "tables";
            case UserAssetKind::Sequence: return "sequences";
            default:                      return "waves";
        }
    }

    // One asset in the library. The payload is the asset's own var (the
    // Waveform's / the Envelope's / the table's / the sequence's), so the
    // library does not need to know how any of them is built.
    struct UserAsset
    {
        UserAssetKind kind = UserAssetKind::Wave;
        int id = 1;
        juce::String fileName;
        juce::String name;
        juce::Colour colour = juce::Colours::white;
        juce::var payload;
    };

    class UserLibrary
    {
    public:
        //=== the folder =======================================================
        // The selftest redirects the root to a temp folder, so a test run
        // never touches the user's real library.
        static juce::File rootFolder()
        {
            if (rootOverride != juce::File())
                return rootOverride;

            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("UPICuitous")
                     .getChildFile ("Library");
        }

        static void setRootForTest (const juce::File& root) { rootOverride = root; }
        static void clearRootForTest() { rootOverride = juce::File(); }
        static bool rootIsOverriddenForTest() { return rootOverride != juce::File(); }

        static juce::File folderFor (UserAssetKind kind)
        {
            return rootFolder().getChildFile (userAssetFolderName (kind));
        }

        // A file name from an asset name: the letters and digits survive, the
        // rest becomes an underscore. The name inside the file keeps the
        // original spelling.
        static juce::String slugFor (const juce::String& name)
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

        //=== writing =========================================================
        // Publishes an asset. The name is kept as it is; the file name is
        // derived from it and never overwrites an existing asset (a counter is
        // added instead), so publishing the same name twice keeps both.
        static bool publish (UserAssetKind kind, int id, const juce::String& name,
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
            if (! payloadIsValid (kind, payload, error))
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

            auto* o = new juce::DynamicObject();
            o->setProperty ("kind", userAssetKindName (kind));
            o->setProperty ("id", id);
            o->setProperty ("version", 1);
            o->setProperty ("name", trimmed);
            o->setProperty ("colour", (int) colour.getARGB());
            o->setProperty ("payload", payload);

            const auto json = juce::JSON::toString (juce::var (o), true);

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

        //=== the first free id =================================================
        // The first free id in the library's own bank for this kind. The
        // library is a flat folder of files, so the id is the asset's
        // identity inside the library (it is never a model id: an import
        // always mints a new local one). A copied file can carry a
        // duplicate id, so the id is repaired to the first free one - the
        // same rule the piece's banks use. -1 means that the library bank
        // has no free id left.
        static int nextLibraryId (UserAssetKind kind)
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

        //=== reading =========================================================
        // Reads one file. Everything is validated: the kind has to match the
        // folder it was found in, the payload has to be within the piece's
        // limits, and the name and every number have to have the right type.
        static bool read (UserAssetKind kind, const juce::File& file,
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
            if (! hasOnlySchemaProperties (parsed, error))
                return false;

            int version = 0;
            if (! readInteger (parsed, "version", version) || version != 1)
            {
                error = "The asset file has an unsupported version";
                return false;
            }

            juce::String kindName;
            if (! readString (parsed, "kind", kindName)
                || kindName != userAssetKindName (kind))
            {
                error = "The file does not hold a \""
                      + juce::String (userAssetKindName (kind)) + "\" asset";
                return false;
            }

            int id = 0;
            if (! readInteger (parsed, "id", id)
                || id < 0 || id > limits::kMaxItemId)
            {
                error = "The asset's id is outside the valid range";
                return false;
            }

            juce::String name;
            if (! readString (parsed, "name", name))
            {
                error = "The asset has no valid name";
                return false;
            }
            name = name.trim();
            if (name.isEmpty())
            {
                error = "The asset has no name";
                return false;
            }
            if (name.length() > 128)
            {
                error = "The asset's name is too long";
                return false;
            }

            int argb = 0;
            if (! readInteger (parsed, "colour", argb))
            {
                error = "The asset has no valid colour";
                return false;
            }

            if (! hasProperty (parsed, "payload"))
            {
                error = "The asset has no data";
                return false;
            }
            const auto payload = parsed.getProperty ("payload", juce::var());
            if (! payloadIsValid (kind, payload, error))
                return false;

            out.kind = kind;
            out.id = id;
            out.fileName = file.getFileName();
            out.name = name;
            out.colour = juce::Colour ((juce::uint32) argb);
            out.payload = payload;
            return true;
        }

        // The folder's assets. A malformed or wrongly placed file is skipped
        // (the others still load), counted and reported with its reason. The
        // two table kinds deliberately share one folder, so a valid table of
        // the other kind is not an error.
        static std::vector<UserAsset> list (UserAssetKind kind, int* skipped = nullptr,
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

        //=== the folder in the file browser ==================================
        // Resolves one selected asset without opening Explorer. The file name
        // must be a plain name in the asset's own folder: a path separator or
        // an absolute path is refused before any shell action can happen.
        static bool revealTarget (UserAssetKind kind, const juce::String& fileName,
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

        static bool reveal (UserAssetKind kind, const juce::String& fileName,
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

        static bool reveal (UserAssetKind kind, const juce::String& fileName)
        {
            juce::String error;
            return reveal (kind, fileName, error);
        }

        static bool revealFolder (UserAssetKind kind)
        {
            const auto folder = folderFor (kind);
            if (! folder.isDirectory())
                return false;
            folder.revealToUser();
            return true;
        }

        //=== the validation ==================================================
        // The payload has to be one of the asset's own vars, and it has to be
        // within the piece's limits. A malformed number or an out-of-range
        // value is REFUSED here: the import never has to guess what a broken
        // file was supposed to mean.
        static bool payloadIsValid (UserAssetKind kind, const juce::var& payload,
                                    juce::String& error)
        {
            error.clear();
            if (! payload.isObject())
            {
                error = "The asset has no data";
                return false;
            }

            switch (kind)
            {
                case UserAssetKind::Wave:
                    return wavePayloadIsValid (payload, error);
                case UserAssetKind::Envelope:
                case UserAssetKind::AmpTable:
                    return envelopePayloadIsValid (payload, error);
                case UserAssetKind::FreqTable:
                    return frequencyTablePayloadIsValid (payload, error);
                case UserAssetKind::Sequence:
                    return sequencePayloadIsValid (payload, error);
                default:
                    error = "Unknown asset kind";
                    return false;
            }
        }

    private:
        // The on-disk envelope is deliberately small and exact. Unknown
        // top-level properties are a sign that the file belongs to a newer
        // or different format, so they are refused instead of being copied
        // into a piece as if they were understood.
        static bool hasOnlySchemaProperties (const juce::var& object,
                                             juce::String& error)
        {
            auto* dynamic = object.getDynamicObject();
            if (dynamic == nullptr)
            {
                error = "The asset file is not a valid library asset";
                return false;
            }

            const auto& properties = dynamic->getProperties();
            for (int i = 0; i < properties.size(); ++i)
            {
                const auto name = properties.getName (i).toString();
                if (name != "kind" && name != "id" && name != "name"
                    && name != "colour" && name != "payload" && name != "version")
                {
                    error = "The asset file has an unknown property: " + name;
                    return false;
                }
            }
            return true;
        }

        // A JSON number, not a string that happens to convert to zero. The
        // library is untrusted input in exactly the same way as a piece, so
        // a malformed point is refused instead of being repaired silently.
        static bool readNumber (const juce::var& value, double& out)
        {
            if (! (value.isInt() || value.isInt64() || value.isDouble())) return false;
            out = (double) value;
            return std::isfinite (out);
        }

        static bool readInteger (const juce::var& value, int& out)
        {
            double number = 0.0;
            if (! readNumber (value, number)) return false;
            if (number < (double) std::numeric_limits<int>::min()
                || number > (double) std::numeric_limits<int>::max()
                || std::floor (number) != number)
                return false;
            out = (int) number;
            return true;
        }

        static bool readString (const juce::var& value, juce::String& out)
        {
            if (! value.isString()) return false;
            out = value.toString();
            return true;
        }

        static bool readBool (const juce::var& value, bool& out)
        {
            if (! value.isBool()) return false;
            out = (bool) value;
            return true;
        }

        static bool hasProperty (const juce::var& object, const juce::String& key)
        {
            auto* dynamic = object.getDynamicObject();
            return dynamic != nullptr && dynamic->hasProperty (key);
        }

        static bool readInteger (const juce::var& object, const juce::String& key, int& out)
        {
            if (! hasProperty (object, key)) return false;
            return readInteger (object.getProperty (key, juce::var()), out);
        }

        static bool readString (const juce::var& object, const juce::String& key, juce::String& out)
        {
            if (! hasProperty (object, key)) return false;
            return readString (object.getProperty (key, juce::var()), out);
        }

        static bool readBool (const juce::var& object, const juce::String& key, bool& out)
        {
            if (! hasProperty (object, key)) return false;
            return readBool (object.getProperty (key, juce::var()), out);
        }

        static bool optionalInteger (const juce::var& object, const juce::String& key,
                                     int& out, juce::String& error)
        {
            if (! hasProperty (object, key)) return true;
            if (! readInteger (object, key, out))
            {
                error = "The asset holds a malformed integer";
                return false;
            }
            return true;
        }

        static bool optionalBool (const juce::var& object, const juce::String& key,
                                  bool& out, juce::String& error)
        {
            if (! hasProperty (object, key)) return true;
            if (! readBool (object, key, out))
            {
                error = "The asset holds a malformed flag";
                return false;
            }
            return true;
        }

        static bool wavePayloadIsValid (const juce::var& payload, juce::String& error)
        {
            // WP10: a wavetable may hold up to 4096 samples, so the library
            // takes a table of any length the piece format allows - the
            // length is the wave's own.
            auto* samples = payload.getProperty ("samples", juce::var()).getArray();
            if (samples == nullptr || samples->size() < 2
                || samples->size() > kMaxWaveSize)
            {
                error = "The wavetable's sample count is outside the valid range (2 to "
                      + juce::String (kMaxWaveSize) + " samples)";
                return false;
            }
            for (const auto& v : *samples)
            {
                double x = 0.0;
                if (! readNumber (v, x) || x < -1.0 || x > 1.0)
                {
                    error = "The wavetable holds a value outside -1..1";
                    return false;
                }
            }
            return true;
        }

        static bool envelopePayloadIsValid (const juce::var& payload, juce::String& error)
        {
            auto* points = payload.getProperty ("points", juce::var()).getArray();
            if (points == nullptr || points->isEmpty()
                || points->size() < 2
                || points->size() > limits::kMaxPointsPerEnvelope)
            {
                error = "The envelope's point count is outside the valid range";
                return false;
            }

            double previousX = -1.0;
            for (const auto& v : *points)
            {
                if (! v.isObject())
                {
                    error = "The envelope holds a malformed point";
                    return false;
                }
                double x = 0.0, y = 0.0;
                if (! readNumber (v.getProperty ("x", juce::var()), x)
                    || ! readNumber (v.getProperty ("y", juce::var()), y)
                    || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
                {
                    error = "The envelope holds a point outside 0..1";
                    return false;
                }
                if (x < previousX)
                {
                    error = "The envelope's points are not sorted";
                    return false;
                }
                previousX = x;
            }
            return true;
        }

        static bool frequencyTablePayloadIsValid (const juce::var& payload, juce::String& error)
        {
            int sourceSpace = -1;
            if (! optionalInteger (payload, "space", sourceSpace, error)) return false;
            if (sourceSpace < -1 || sourceSpace > 3)
            {
                error = "The frequency table names an invalid page space";
                return false;
            }

            // A frequency table belongs to one page space in the piece. The
            // page is metadata for the library (the import still asks for
            // its current destination); it is validated so a corrupt file
            // cannot claim an impossible page.
            int sourcePage = -1;
            if (! optionalInteger (payload, "page", sourcePage, error)) return false;
            if (sourcePage < -1 || sourcePage >= limits::kMaxPages)
            {
                error = "The frequency table names an invalid page";
                return false;
            }

            int preset = 0;
            if (! optionalInteger (payload, "preset", preset, error)) return false;
            if (preset < 0 || preset > 6)
            {
                error = "The frequency table has an invalid preset";
                return false;
            }

            bool editable = false, stepped = false, inverted = false;
            if (! optionalBool (payload, "editable", editable, error)
                || ! optionalBool (payload, "stepped", stepped, error)
                || ! optionalBool (payload, "inverted", inverted, error))
                return false;

            int divisions = 0;
            if (! optionalInteger (payload, "divisions", divisions, error)) return false;
            if (divisions < 0 || divisions > limits::kMaxStepDivisions)
            {
                error = "The frequency table has an invalid staircase setting";
                return false;
            }

            auto* points = payload.getProperty ("points", juce::var()).getArray();
            if (editable)
            {
                if (points == nullptr || points->size() < 2
                    || points->size() > limits::kMaxTablePoints)
                {
                    error = "The frequency table's point count is outside the valid range";
                    return false;
                }

                double previousX = -1.0;
                for (const auto& v : *points)
                {
                    if (! v.isObject())
                    {
                        error = "The frequency table holds a malformed point";
                        return false;
                    }
                    double x = 0.0, hz = 0.0;
                    if (! readNumber (v.getProperty ("x", juce::var()), x)
                        || ! readNumber (v.getProperty ("hz", juce::var()), hz)
                        || x < 0.0 || x > 1.0
                        || hz < (double) kMinFreq || hz > (double) kMaxFreq)
                    {
                        error = "The frequency table holds a malformed point";
                        return false;
                    }
                    if (x < previousX)
                    {
                        error = "The frequency table's points are not sorted";
                        return false;
                    }
                    previousX = x;
                }
            }
            else if (points != nullptr && ! points->isEmpty())
            {
                error = "The frequency table has points but is not editable";
                return false;
            }

            (void) stepped;
            (void) inverted;
            return true;
        }

        static bool sequencePayloadIsValid (const juce::var& payload, juce::String& error)
        {
            bool loop = true;
            if (! optionalBool (payload, "loop", loop, error)) return false;
            (void) loop;

            auto* points = payload.getProperty ("points", juce::var()).getArray();
            if (points == nullptr || points->size() > limits::kMaxSequencePoints)
            {
                error = "The sequence's point count is outside the valid range";
                return false;
            }

            double previousT = -1.0;
            for (const auto& v : *points)
            {
                if (! v.isObject())
                {
                    error = "The sequence holds a malformed point";
                    return false;
                }
                double t = 0.0, pos = 0.0;
                int page = 0;
                if (! readNumber (v.getProperty ("t", juce::var()), t)
                    || ! readNumber (v.getProperty ("pos", juce::var()), pos)
                    || ! readInteger (v.getProperty ("page", juce::var()), page)
                    || t < 0.0 || t > (double) limits::kMaxSequenceSeconds
                    || pos < 0.0 || pos > kMaxPageSeconds
                    || page < 0 || page >= limits::kMaxPages)
                {
                    error = "The sequence holds a malformed point";
                    return false;
                }
                if (t < previousT)
                {
                    error = "The sequence's points are not sorted";
                    return false;
                }
                previousT = t;
            }
            return true;
        }

        // The selftest's override. Empty = the real user folder.
        static inline juce::File rootOverride;
    };
}
