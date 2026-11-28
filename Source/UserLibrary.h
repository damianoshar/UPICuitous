#pragma once
#include <JuceHeader.h>

#include <cmath>
#include <limits>

#include "Envelope.h"
#include "FreqSpace.h"
#include "Performance.h"
#include "PieceLimits.h"
#include "UpicConstants.h"
#include "Waveform.h"

//==============================================================================
// WP7 (Wave F, 2026-11-21): the user-level asset library - the asset format.
//
// The piece owns the material it uses: a wavetable, an envelope, an amplitude
// table, a frequency table, a performance sequence. The library is the OTHER
// half - a per-user folder that outlives the piece, so material can be moved
// between pieces without inventing a file format for libraries.
//
// This file is the asset's FORMAT: the kinds, the one-file envelope and the
// validation that refuses a malformed payload. The folder itself - where the
// files live, the atomic write, the listing and the reveal - is the library
// repository (Source/LibraryRepository.h).
//
// The rules (they are the reason this file is small and boring):
//   - the library never becomes a dependency. A piece always carries what it
//     uses, so deleting the library folder cannot change a piece;
//   - an imported asset gets a NEW local id. The ids stay local to the piece,
//     so two pieces can hold the same asset without agreeing about ids;
//   - everything read back is validated against the same limits the piece
//     uses, and a file that fails is REFUSED, not silently repaired.
//
// Every asset is one JSON file:
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

    // The asset's format - the codec. The repository (Source/LibraryRepository.h)
    // owns the folder and the files; the shell owns the service.
    class UserLibrary
    {
    public:
        //=== the envelope =====================================================
        // One asset as the file carries it. The name is the one the file will
        // hold (the caller has already trimmed it); the repository only adds
        // the folder and the file name.
        static juce::var assetToVar (UserAssetKind kind, int id, const juce::String& name,
                                     juce::Colour colour, const juce::var& payload)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("kind", userAssetKindName (kind));
            o->setProperty ("id", id);
            o->setProperty ("version", 1);
            o->setProperty ("name", name);
            o->setProperty ("colour", (int) colour.getARGB());
            o->setProperty ("payload", payload);
            return juce::var (o);
        }

        // Everything is validated: the kind has to match the folder it was
        // found in, the payload has to be within the piece's limits, and the
        // name and every number have to have the right type. The repository
        // has already checked the file's size and parsed the text; the file
        // name is the one the asset was read from.
        static bool assetFromVar (UserAssetKind kind, const juce::String& fileName,
                                  const juce::var& parsed, UserAsset& out, juce::String& error)
        {
            error.clear();

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
            out.fileName = fileName;
            out.name = name;
            out.colour = juce::Colour ((juce::uint32) argb);
            out.payload = payload;
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
    };
}
