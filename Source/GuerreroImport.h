#pragma once

#include <cstdint>
#include <vector>

//==============================================================================
// The single import contract between the Guerrero engine and this host.
//
// Why a separate type and not upic::Arc:
//   - MainComponent.h must not depend on the Guerrero headers, and the Guerrero
//     headers must not depend on JUCE.
//   - upic::Arc carries host-only fields (id, waveformId, mode, pm*, space, ...)
//     that the engine has no opinion about.
//
// The flow is exactly one hop:
//
//   guerrero::Curve
//     -> guerrero::upicadapter::planArcFromCurve()   (engine side, JUCE-free)
//     -> upic::GuerreroImportArc                     (this type, plain data)
//     -> MainComponent::insertGuerreroArcs()         (assigns id, fills host fields)
//     -> upic::Arc
//
// Provenance lives on each import arc, not on the batch: a generator run that
// produces several threads must be able to give each arc its own record.
//==============================================================================

namespace upic
{
    struct GuerreroImportPoint
    {
        double t = 0.0;      // score seconds
        double freq = 440.0; // Hz
        float  i = 1.0f;     // 0..1 pressure
    };

    //---- generation recipe -------------------------------------------------
    // Enough to regenerate the arc deterministically. This is a versioned
    // record: `generatorVersion` gates how the fields are interpreted, so a
    // future change of the generator cannot silently reinterpret old data.
    struct GuerreroEnvelopeRecipe
    {
        double roughness = 0.0;
        int    depth = 0;
        double freqLo = 0.0;
        double freqHi = 0.0;
        double timeStart = 0.0;
        double timeEnd = 0.0;
        bool   allowDegenerateRangeExpansion = false;
    };

    struct GuerreroSeedRecipe
    {
        double roughness = 0.0;
        int    depth = 0;
    };

    enum class GuerreroOutOfRange { Reject = 0, Compress = 1 };
    enum class GuerreroPointPolicy { Reject = 0, Downsample = 1 };

    struct GuerreroRecipe
    {
        int generatorVersion = 1;   // recipe schema version
        int generator = 0;          // 0 = envelope + confined seed

        GuerreroEnvelopeRecipe lower;
        GuerreroEnvelopeRecipe upper;
        GuerreroSeedRecipe     seed;

        uint64_t lowerSeed = 0;
        uint64_t upperSeed = 0;
        uint64_t seedSeed  = 0;

        double   bpm = 120.0;       // single tempo marker at beat zero
        GuerreroOutOfRange frequencyPolicy = GuerreroOutOfRange::Reject;
        GuerreroPointPolicy pointsPolicy = GuerreroPointPolicy::Reject;
        int      maxPoints = 1000;
    };

    struct GuerreroProvenance
    {
        int version = 1;

        // --- identity ------------------------------------------------------
        uint64_t compositionSeed = 0;
        uint64_t sourceSeedId = 0;   // Guerrero SeedCurve id (unsigned 64-bit)
        uint64_t sourceCurveId = 0;  // id of the converted curve
        uint64_t randomSeed = 0;     // RNG seed for this arc

        // --- the complete recipe -------------------------------------------
        GuerreroRecipe recipe;

        // --- conversion outcome --------------------------------------------
        bool     lossy = false;
        uint32_t omittedPoints = 0;

        // --- editing state -------------------------------------------------
        // Whether the GENERATED GEOMETRY has been modified. Deliberately scoped
        // to the curve: changing an arc's waveform, envelope, intensity or
        // synthesis mode is a host material override, not a change to the
        // generated material, and regeneration must not be blocked by it.
        int  revision = 1;
        bool geometryEdited = false;

        // Fingerprint of the geometry as generated. endEdit() compares the
        // current arc against it, so an edit is detected on EVERY path (drag,
        // transpose, invert, reverse, point insert/remove) without having to
        // instrument each one.
        uint64_t geometryHash = 0;
        int      pointCount = 0;

        // True only when the fingerprint fields above were present and usable.
        // A recipe-bearing record WITHOUT a fingerprint cannot be integrity
        // checked, so it must not be regenerable: "no fingerprint" must never
        // mean "skip the check and proceed".
        bool hasFingerprint = false;

        // Which algorithm produced geometryHash. 1 = exact bits (records from
        // before quantisation), 2 = quantised to 1e-6. Old records carry no such
        // field and are treated as 1, then upgraded once verified.
        int fingerprintAlgorithm = 2;

        // --- record integrity ----------------------------------------------
        // False for a record written before the recipe existed. Such an arc is
        // NOT regenerable: its parameters are unknown, and fabricating defaults
        // would produce different music while looking authoritative.
        bool recipeValid = false;
        // True when a present field could not be parsed. The record must not be
        // trusted for regeneration.
        bool malformed = false;
    };

    struct GuerreroImportArc
    {
        std::vector<GuerreroImportPoint> points;
        float intensity = 0.8f;

        bool hasProvenance = false;
        GuerreroProvenance provenance;

        double startTime() const { return points.empty() ? 0.0 : points.front().t; }
        double endTime()   const { return points.empty() ? 0.0 : points.back().t; }
    };
}
