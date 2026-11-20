#if GUERRERO_INTEGRATION
#include "GuerreroUpicAdapter.h"

namespace upic
{
    namespace guerrero_bridge
    {
        upic::GuerreroImportArc toImportArc (const guerrero::upicadapter::PlannedArc& plan)
        {
            upic::GuerreroImportArc arc;
            arc.intensity = juce::jlimit (0.0f, 1.0f, plan.intensity);

            arc.points.reserve (plan.points.size());
            for (const auto& p : plan.points)
            {
                upic::GuerreroImportPoint pt;
                pt.t = p.t;
                pt.freq = p.freq;
                pt.i = juce::jlimit (0.0f, 1.0f, p.i);
                arc.points.push_back (pt);
            }

            return arc;
        }

        bool regenerateFromRecipe (const upic::GuerreroRecipe& recipe,
                                   guerrero::upicadapter::PlannedArc& out,
                                   bool* lossyOut,
                                   uint32_t* omittedOut)
        {
            if (recipe.generatorVersion != 1 || recipe.generator != 0)
                return false;   // unknown generator: refuse rather than guess

            guerrero::Composition comp;
            comp.tempoMap.push_back ({ guerrero::RationalBeatTime (0, 1), recipe.bpm });

            auto makeEnvelope = [] (const upic::GuerreroEnvelopeRecipe& e,
                                    uint64_t seed)
            {
                guerrero::BrownianSettings s;
                s.roughness = e.roughness;
                s.depth = (uint32_t) juce::jmax (0, e.depth);
                s.frequencyRange[0] = e.freqLo;
                s.frequencyRange[1] = e.freqHi;
                s.timeRange[0] = e.timeStart;
                s.timeRange[1] = e.timeEnd;
                s.allowDegenerateRangeExpansion = e.allowDegenerateRangeExpansion;
                return guerrero::generateBrownianEnvelope (s, seed);
            };

            guerrero::Curve lower = makeEnvelope (recipe.lower, recipe.lowerSeed);
            guerrero::Curve upper = makeEnvelope (recipe.upper, recipe.upperSeed);
            if (lower.knots.empty() || upper.knots.empty())
                return false;

            guerrero::BrownianSettings seedSettings;
            seedSettings.roughness = recipe.seed.roughness;
            seedSettings.depth = (uint32_t) juce::jmax (0, recipe.seed.depth);

            guerrero::SeedCurve seed = guerrero::generateConfinementSeed (
                lower, &upper, seedSettings, recipe.seedSeed);
            if (seed.knots.empty())
                return false;

            comp.envelopes.push_back (lower);
            comp.envelopes.push_back (upper);
            comp.seeds.push_back (seed);

            guerrero::upicadapter::ConversionOptions opts;
            opts.maxPoints = (uint32_t) juce::jmax (2, recipe.maxPoints);
            opts.pointsPolicy = (recipe.pointsPolicy == upic::GuerreroPointPolicy::Downsample)
                                    ? guerrero::upicadapter::PointsPolicy::Downsample
                                    : guerrero::upicadapter::PointsPolicy::Reject;
            opts.frequencyPolicy = (recipe.frequencyPolicy == upic::GuerreroOutOfRange::Compress)
                                    ? guerrero::upicadapter::RangePolicy::Clamp
                                    : guerrero::upicadapter::RangePolicy::Reject;
            opts.fallbackBpm = recipe.bpm;

            const auto planned = guerrero::upicadapter::planArcFromCurve (seed, comp, opts);
            if (! planned.success)
                return false;

            if (lossyOut != nullptr)   *lossyOut = planned.lossy;
            if (omittedOut != nullptr) *omittedOut = planned.omittedPoints;

            out = planned.arc;
            return true;
        }
    }
}
#endif // GUERRERO_INTEGRATION
