#pragma once

#include <JuceHeader.h>
#include "GuerreroImport.h"

#include <cstdint>
#include <limits>

//==============================================================================
// Serialization for upic::GuerreroProvenance.
//
// Rules that matter for data integrity:
//
//  1. 64-bit fields are written as DECIMAL STRINGS. The engine's ids routinely
//     have the high bit set, and a JSON number would come back negative or lose
//     precision in any reader using doubles.
//  2. Parsing is STRICT: a present-but-malformed field is an ERROR, never a
//     silent zero. Substituting a different random seed would change the
//     regenerated music without any indication.
//  3. A record written before the recipe existed has NO recipe. It is marked
//     legacy and is not regenerable; a recipe is never fabricated from defaults.
//==============================================================================

namespace upic
{
    namespace guerreroio
    {
        inline juce::String u64ToString (uint64_t v)
        {
            if (v == 0) return "0";
            char buf[21];
            int i = 20;
            buf[i] = 0;
            while (v > 0 && i > 0)
            {
                buf[--i] = (char) ('0' + (int) (v % 10));
                v /= 10;
            }
            return juce::String (buf + i);
        }

        // Strict decimal parse: digits only, overflow detected before it happens.
        inline bool tryParseU64 (const juce::String& s, uint64_t& out)
        {
            if (s.isEmpty() || s.length() > 20) return false;

            uint64_t v = 0;
            const uint64_t max = std::numeric_limits<uint64_t>::max();
            for (auto c : s)
            {
                if (c < '0' || c > '9') return false;
                const uint64_t d = (uint64_t) (c - '0');
                if (v > (max - d) / 10u) return false;   // would overflow
                v = v * 10u + d;
            }
            out = v;
            return true;
        }

        // The provenance record schema this build understands. A record with a
        // different version is not interpreted: its fields may mean something
        // else entirely.
        constexpr int kSupportedProvenanceVersion = 1;

        enum class ReadStatus { Absent, Ok, Invalid };

        // Reads a 64-bit field that may be a decimal string (current form) or a
        // number (older form). A present but unusable value reports Invalid so
        // the caller can refuse to use the record.
        inline ReadStatus readU64 (const juce::var& parent, const char* key, uint64_t& out)
        {
            if (! parent.hasProperty (key)) return ReadStatus::Absent;

            const auto v = parent.getProperty (key, juce::var());

            if (v.isString())
                return tryParseU64 (v.toString(), out) ? ReadStatus::Ok : ReadStatus::Invalid;

            if (v.isInt() || v.isInt64())
            {
                const auto i = (juce::int64) v;
                if (i < 0) { out = (uint64_t) i; return ReadStatus::Ok; }  // legacy signed id
                out = (uint64_t) i;
                return ReadStatus::Ok;
            }
            if (v.isDouble())
            {
                const double d = (double) v;
                if (! std::isfinite (d))
                    return ReadStatus::Invalid;

                // 2^64 is exactly representable as a double, so compare
                // against it rather than against 2^64-1: the latter rounds
                // UP to 2^64 and lets an out-of-range value through.
                const double kTwo64 = 18446744073709551616.0;
                if (d >= kTwo64 || d < -9223372036854775808.0)
                    return ReadStatus::Invalid;

                // A double cannot represent every integer above 2^53, so a large
                // legacy number may already be a rounded version of a DIFFERENT
                // seed. Only migrate values that are exact as doubles; full 64-bit
                // ids travel as decimal strings.
                // 2^53 itself is excluded: 2^53+1 rounds DOWN to 2^53, so a
                // stored 2^53 may already stand for a different value.
                const double kTwo53 = 9007199254740992.0;
                if (d >= kTwo53 || d <= -kTwo53)
                    return ReadStatus::Invalid;
                if (d != std::floor (d))
                    return ReadStatus::Invalid;

                if (d < 0.0)
                {
                    // Older records stored the unsigned id in a signed field,
                    // so a high-bit id came back negative. Recover the original
                    // bit pattern rather than inventing a different seed.
                    out = (uint64_t) (juce::int64) d;
                    return ReadStatus::Ok;
                }

                out = (uint64_t) d;
                return ReadStatus::Ok;
            }


            return ReadStatus::Invalid;
        }

        // Requires an integral value: a fractional or over-range number is
        // rejected rather than truncated. 1000.8 is not a point cap of 1000.
        inline bool readIntStrict (const juce::var& parent, const char* key, int& out)
        {
            if (! parent.hasProperty (key)) return false;

            const auto v = parent.getProperty (key, juce::var());

            if (v.isInt() || v.isInt64())
            {
                // Narrowing a 64-bit variant to int without a range check lets a
                // huge value wrap into a plausible one: 4294967297 becomes 1, so
                // an unknown generatorVersion would pass as version 1. Reject
                // anything that does not fit.
                const auto n = (juce::int64) v;
                if (n < std::numeric_limits<int>::min()
                    || n > std::numeric_limits<int>::max())
                    return false;

                out = (int) n;
                return true;
            }
            if (v.isDouble())
            {
                const double d = (double) v;
                if (! std::isfinite (d) || d != std::floor (d))
                    return false;
                if (d < -2147483648.0 || d > 2147483647.0)
                    return false;
                out = (int) d;
                return true;
            }
            return false;
        }

        // Requires a NUMERIC variant: a string like "120" is rejected rather
        // than reinterpreted, so a malformed type cannot masquerade as a valid
        // parameter.
        inline bool readFiniteDoubleStrict (const juce::var& parent, const char* key, double& out)
        {
            if (! parent.hasProperty (key)) return false;

            const auto v = parent.getProperty (key, juce::var());
            if (! (v.isDouble() || v.isInt() || v.isInt64())) return false;

            out = (double) v;
            return std::isfinite (out);
        }

        inline juce::var envelopeToVar (const GuerreroEnvelopeRecipe& e)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("roughness", e.roughness);
            o->setProperty ("depth", e.depth);
            o->setProperty ("freqLo", e.freqLo);
            o->setProperty ("freqHi", e.freqHi);
            o->setProperty ("timeStart", e.timeStart);
            o->setProperty ("timeEnd", e.timeEnd);
            if (e.allowDegenerateRangeExpansion)
                o->setProperty ("expandDegenerate", true);
            return juce::var (o);
        }

        inline GuerreroEnvelopeRecipe envelopeFromVar (const juce::var& v)
        {
            GuerreroEnvelopeRecipe e;
            if (! v.isObject()) return e;
            e.roughness  = (double) v.getProperty ("roughness", 0.0);
            e.depth      = (int)    v.getProperty ("depth", 0);
            e.freqLo     = (double) v.getProperty ("freqLo", 0.0);
            e.freqHi     = (double) v.getProperty ("freqHi", 0.0);
            e.timeStart  = (double) v.getProperty ("timeStart", 0.0);
            e.timeEnd    = (double) v.getProperty ("timeEnd", 0.0);
            e.allowDegenerateRangeExpansion = (bool) v.getProperty ("expandDegenerate", false);
            return e;
        }

        inline juce::var recipeToVar (const GuerreroRecipe& r)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("generatorVersion", r.generatorVersion);
            o->setProperty ("generator", r.generator);
            o->setProperty ("lower", envelopeToVar (r.lower));
            o->setProperty ("upper", envelopeToVar (r.upper));

            auto* s = new juce::DynamicObject();
            s->setProperty ("roughness", r.seed.roughness);
            s->setProperty ("depth", r.seed.depth);
            o->setProperty ("seed", juce::var (s));

            o->setProperty ("lowerSeed", u64ToString (r.lowerSeed));
            o->setProperty ("upperSeed", u64ToString (r.upperSeed));
            o->setProperty ("seedSeed", u64ToString (r.seedSeed));

            o->setProperty ("bpm", r.bpm);
            o->setProperty ("frequencyPolicy", (int) r.frequencyPolicy);
            o->setProperty ("pointsPolicy", (int) r.pointsPolicy);
            o->setProperty ("maxPoints", r.maxPoints);
            return juce::var (o);
        }

        // Returns false when the recipe object is missing or a required field is
        // malformed. A missing recipe is a LEGACY record, not one to be filled
        // in with defaults.
        // Strict envelope reader. Every field consumed by regeneration must be
        // present and sane: a recipe with missing bounds is not a recipe.
        inline bool envelopeFromVarStrict (const juce::var& v, GuerreroEnvelopeRecipe& e)
        {
            if (! v.isObject()) return false;

            const char* required[] = { "roughness", "depth", "freqLo", "freqHi",
                                       "timeStart", "timeEnd" };
            for (auto k : required)
                if (! v.hasProperty (k)) return false;

            if (! readFiniteDoubleStrict (v, "roughness", e.roughness)) return false;
            if (! readIntStrict (v, "depth", e.depth)) return false;
            if (! readFiniteDoubleStrict (v, "freqLo", e.freqLo)) return false;
            if (! readFiniteDoubleStrict (v, "freqHi", e.freqHi)) return false;
            if (! readFiniteDoubleStrict (v, "timeStart", e.timeStart)) return false;
            if (! readFiniteDoubleStrict (v, "timeEnd", e.timeEnd)) return false;
            e.allowDegenerateRangeExpansion = (bool) v.getProperty ("expandDegenerate", false);

            if (e.roughness < 0.0 || e.roughness > 8.0) return false;
            if (e.depth < 0 || e.depth > 30) return false;
            if (e.freqLo <= 0.0) return false;
            // A zero-width range is only legal when the caller explicitly asked
            // for it to be expanded. Otherwise it must not pass validation,
            // because no stochastic variation is possible inside it.
            if (e.freqHi < e.freqLo) return false;
            if (e.freqHi == e.freqLo && ! e.allowDegenerateRangeExpansion) return false;
            if (e.timeEnd <= e.timeStart) return false;
            return true;
        }

        // Absent  = no recipe at all (a genuine pre-recipe legacy record).
        // Valid   = every field regeneration needs is present and sane.
        // Invalid = a recipe object exists but is incomplete or out of range.
        //           That is damage, not legacy, and is reported as such.
        enum class RecipeStatus { Absent, Valid, Invalid };

        inline RecipeStatus recipeFromVar (const juce::var& v, GuerreroRecipe& r)
        {
            // A void property means the recipe was absent (legacy). Anything
            // present that is not an object - null, a number, a string - is a
            // damaged recipe, not a historical one.
            if (! v.isObject())
                return v.isVoid() ? RecipeStatus::Absent : RecipeStatus::Invalid;

            // Every parameter regeneration consumes must be present. A default
            // would silently change the rebuilt music.
            const char* required[] = { "generatorVersion", "generator", "lower", "upper",
                                       "seed", "lowerSeed", "upperSeed", "seedSeed",
                                       "bpm", "frequencyPolicy", "pointsPolicy", "maxPoints" };
            for (auto k : required)
                if (! v.hasProperty (k)) return RecipeStatus::Invalid;

            if (! readIntStrict (v, "generatorVersion", r.generatorVersion))
                return RecipeStatus::Invalid;
            if (! readIntStrict (v, "generator", r.generator))
                return RecipeStatus::Invalid;
            if (r.generatorVersion < 1) return RecipeStatus::Invalid;
            if (r.generator < 0)        return RecipeStatus::Invalid;

            if (! envelopeFromVarStrict (v.getProperty ("lower", juce::var()), r.lower))
                return RecipeStatus::Invalid;
            if (! envelopeFromVarStrict (v.getProperty ("upper", juce::var()), r.upper))
                return RecipeStatus::Invalid;

            const auto s = v.getProperty ("seed", juce::var());
            if (! s.isObject()) return RecipeStatus::Invalid;
            if (! s.hasProperty ("roughness") || ! s.hasProperty ("depth"))
                return RecipeStatus::Invalid;
            if (! readFiniteDoubleStrict (s, "roughness", r.seed.roughness))
                return RecipeStatus::Invalid;
            if (! readIntStrict (s, "depth", r.seed.depth)) return RecipeStatus::Invalid;
            if (r.seed.roughness < 0.0 || r.seed.roughness > 8.0)
                return RecipeStatus::Invalid;
            if (r.seed.depth < 0 || r.seed.depth > 30) return RecipeStatus::Invalid;

            if (readU64 (v, "lowerSeed", r.lowerSeed) != ReadStatus::Ok) return RecipeStatus::Invalid;
            if (readU64 (v, "upperSeed", r.upperSeed) != ReadStatus::Ok) return RecipeStatus::Invalid;
            if (readU64 (v, "seedSeed",  r.seedSeed)  != ReadStatus::Ok) return RecipeStatus::Invalid;

            // Values are rejected, not clamped: a clamped parameter silently
            // produces a different result from the one that was recorded.
            if (! readFiniteDoubleStrict (v, "bpm", r.bpm))
                return RecipeStatus::Invalid;
            if (r.bpm <= 0.0 || r.bpm > 1000.0)
                return RecipeStatus::Invalid;

            int fp = -1;
            if (! readIntStrict (v, "frequencyPolicy", fp) || fp < 0 || fp > 1)
                return RecipeStatus::Invalid;
            r.frequencyPolicy = (GuerreroOutOfRange) fp;

            int pp = -1;
            if (! readIntStrict (v, "pointsPolicy", pp) || pp < 0 || pp > 1)
                return RecipeStatus::Invalid;
            r.pointsPolicy = (GuerreroPointPolicy) pp;

            int mp = -1;
            if (! readIntStrict (v, "maxPoints", mp) || mp < 2 || mp > 100000)
                return RecipeStatus::Invalid;
            r.maxPoints = mp;

            return RecipeStatus::Valid;
        }

        inline juce::var toVar (const GuerreroProvenance& p)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", p.version);

            o->setProperty ("compositionSeed", u64ToString (p.compositionSeed));
            o->setProperty ("sourceSeedId", u64ToString (p.sourceSeedId));
            o->setProperty ("sourceCurveId", u64ToString (p.sourceCurveId));
            o->setProperty ("randomSeed", u64ToString (p.randomSeed));

            // The recipe is written only when it is actually known, so a legacy
            // record is not silently upgraded to a fabricated one.
            if (p.recipeValid)
                o->setProperty ("recipe", recipeToVar (p.recipe));

            o->setProperty ("lossy", p.lossy);
            o->setProperty ("omittedPoints", (int) p.omittedPoints);

            o->setProperty ("revision", p.revision);
            o->setProperty ("geometryEdited", p.geometryEdited);
            // The damaged flag must survive a save, otherwise a damaged record
            // would be reclassified as a legitimate legacy one on reload.
            if (p.malformed) o->setProperty ("malformed", true);
            o->setProperty ("geometryHash", u64ToString (p.geometryHash));
            o->setProperty ("geometryHashAlgorithm", p.fingerprintAlgorithm);
            o->setProperty ("pointCount", p.pointCount);
            return juce::var (o);
        }

        inline GuerreroProvenance fromVar (const juce::var& v)
        {
            GuerreroProvenance p;
            if (! v.isObject()) { p.malformed = true; return p; }

            // Only an ABSENT version is a legacy record. A present but invalid
            // value must not be repaired to 1: "version": "99" and
            // 4294967297 are damage, not history.
            if (v.hasProperty ("version"))
            {
                if (! readIntStrict (v, "version", p.version))
                    p.version = 0;   // forces the range check below to reject
            }
            else
            {
                p.version = 1;
            }

            if (p.version < 1 || p.version > kSupportedProvenanceVersion)
            {
                p.malformed = true;
                p.recipeValid = false;
            }

            uint64_t tmp = 0;
            auto readOrMark = [&p, &v, &tmp](const char* key, uint64_t& dst)
            {
                const auto st = readU64 (v, key, tmp);
                if (st == ReadStatus::Ok)      { dst = tmp; return; }
                if (st == ReadStatus::Invalid) { p.malformed = true; }
                // Absent: leave the default and let the caller decide.
            };

            readOrMark ("compositionSeed", p.compositionSeed);
            readOrMark ("sourceSeedId", p.sourceSeedId);
            readOrMark ("sourceCurveId", p.sourceCurveId);
            readOrMark ("randomSeed", p.randomSeed);
            readOrMark ("geometryHash", p.geometryHash);
            p.hasFingerprint = (p.geometryHash != 0);

            // Records written before quantisation carry no algorithm field and
            // were hashed over exact bits; they are verified with that
            // algorithm and then migrated.
            if (v.hasProperty ("geometryHashAlgorithm"))
            {
                int algo = 2;
                if (! readIntStrict (v, "geometryHashAlgorithm", algo)
                    || algo < 1 || algo > 2)
                    p.malformed = true;
                else
                    p.fingerprintAlgorithm = algo;
            }
            else
            {
                p.fingerprintAlgorithm = 1;
            }
            // incomplete = damaged, which is reported separately.
            // Presence must come from hasProperty(): JUCE parses a JSON null as a
            // void value, so isVoid() alone cannot distinguish "null" from "absent".
            const bool recipePresent = v.hasProperty ("recipe");

            switch (recipeFromVar (v.getProperty ("recipe", juce::var()), p.recipe))
            {
                case RecipeStatus::Valid:
                    p.recipeValid = true;
                    break;
                case RecipeStatus::Invalid:
                    p.recipeValid = false;
                    p.malformed = true;
                    break;
                case RecipeStatus::Absent:
                    p.recipeValid = false;
                    // Present but not a recipe object (null, number, string) is
                    // damage; genuinely absent is a legacy record.
                    if (recipePresent) p.malformed = true;
                    break;
            }
            p.lossy         = (bool) v.getProperty ("lossy", false);
            p.omittedPoints = (uint32_t) juce::jmax (0, (int) v.getProperty ("omittedPoints", 0));

            // A revision read from JSON is user-supplied: narrow it strictly.
            if (v.hasProperty ("revision"))
            {
                int rev = 1;
                if (! readIntStrict (v, "revision", rev) || rev < 1)
                    p.malformed = true;
                else
                    p.revision = rev;
            }
            else
            {
                p.revision = 1;
            }
            // New key first, then the older name.
            p.geometryEdited = (bool) v.getProperty ("geometryEdited",
                                 (bool) v.getProperty ("baked", false));
            // A damaged record stays damaged across a save and reload.
            if ((bool) v.getProperty ("malformed", false)) p.malformed = true;

            // pointCount is user-supplied too, and gates the fingerprint.
            if (v.hasProperty ("pointCount"))
            {
                int pc = 0;
                if (! readIntStrict (v, "pointCount", pc) || pc < 0)
                    p.malformed = true;
                else
                    p.pointCount = pc;
            }
            if (p.pointCount <= 0) p.hasFingerprint = false;

            return p;
        }
    }
}
