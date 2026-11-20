#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

#if GUERRERO_INTEGRATION
#include "GuerreroUpicAdapter.h"
#include "GuerreroDialog.h"
#include "guerrero/core/Curves.h"
#include "guerrero/upic/GuerreroUpicPlan.h"
#endif

//==============================================================================
// Guerrero engine -> UPICuitous arc, end to end inside the host.
//
// This is the check that matters for the integration: it does not merely call
// the conversion in isolation, it inserts the result into a real MainComponent,
// inspects the serialized piece, and exercises undo, redo and reload - in
// memory and through a real file on disk.
void upic::runGuerreroChecks (SelfTest& t)
{
#if GUERRERO_INTEGRATION
    // --- 1. the engine produces a plan -----------------------------------
    guerrero::Composition comp;
    comp.name = "selftest";
    comp.tempoMap.push_back ({ guerrero::RationalBeatTime (0, 1), 120.0 });

    guerrero::BrownianSettings lower;
    lower.roughness = 0.35;
    lower.depth = 7;
    lower.frequencyRange[0] = 110.0;
    lower.frequencyRange[1] = 880.0;
    lower.timeRange[0] = 0.0;
    lower.timeRange[1] = 8.0;

    guerrero::BrownianSettings upper = lower;
    upper.frequencyRange[0] = 880.0;
    upper.frequencyRange[1] = 1760.0;

    guerrero::Curve lowerEnv = guerrero::generateBrownianEnvelope (lower, 42u);
    guerrero::Curve upperEnv = guerrero::generateBrownianEnvelope (upper, 43u);

    guerrero::BrownianSettings seedSettings;
    seedSettings.roughness = 0.55;
    seedSettings.depth = 7;

    guerrero::SeedCurve seed = guerrero::generateConfinementSeed (
        lowerEnv, &upperEnv, seedSettings, 44u);

    comp.envelopes.push_back (lowerEnv);
    comp.envelopes.push_back (upperEnv);
    comp.seeds.push_back (seed);

    t.check (comp.validate(), "Guerrero: the generated piece validates");

    guerrero::upicadapter::ConversionOptions opts;
    opts.maxPoints = (uint32_t) upic::limits::kMaxPointsPerArc;
    opts.pointsPolicy = guerrero::upicadapter::PointsPolicy::Downsample;
    opts.frequencyPolicy = guerrero::upicadapter::RangePolicy::Clamp;
    opts.fallbackBpm = 120.0;

    const auto planned = guerrero::upicadapter::planArcFromCurve (seed, comp, opts);
    t.check (planned.success, "Guerrero: seed converts to an arc plan",
             planned.success ? juce::String ((int) planned.arc.points.size()) + " points"
                             : juce::String (planned.message));

    if (! planned.success)
        return;

    // Four beats at 120 bpm is 2 s; the seed spans 8 beats, so 4 s.
    t.check (std::abs (planned.arc.points.front().t - 0.0) < 1e-9
             && std::abs (planned.arc.points.back().t - 4.0) < 1e-6,
             "Guerrero: beats convert to score seconds (8 beats at 120 bpm = 4 s)",
             juce::String (planned.arc.points.back().t, 3) + " s");

    bool freqOk = true;
    for (const auto& p : planned.arc.points)
        if (p.freq < upic::kMinFreq || p.freq > upic::kMaxFreq) freqOk = false;
    t.check (freqOk, "Guerrero: every planned frequency is inside the host range");

    // --- 2. convert, attach provenance, insert ---------------------------
    upic::GuerreroImportArc importArc = upic::guerrero_bridge::toImportArc (planned.arc);

    // The complete recipe, exactly as runGuerreroGenerator() builds it.
    upic::GuerreroProvenance prov;
    prov.version         = 1;
    prov.compositionSeed = comp.seed;
    prov.sourceSeedId    = seed.id;
    prov.sourceCurveId   = seed.id;
    prov.randomSeed      = 44u;

    prov.recipe.generatorVersion = 1;
    prov.recipe.generator        = 0;
    prov.recipe.lower.roughness  = lower.roughness;
    prov.recipe.lower.depth      = (int) lower.depth;
    prov.recipe.lower.freqLo     = lower.frequencyRange[0];
    prov.recipe.lower.freqHi     = lower.frequencyRange[1];
    prov.recipe.lower.timeStart  = lower.timeRange[0];
    prov.recipe.lower.timeEnd    = lower.timeRange[1];
    prov.recipe.upper.roughness  = upper.roughness;
    prov.recipe.upper.depth      = (int) upper.depth;
    prov.recipe.upper.freqLo     = upper.frequencyRange[0];
    prov.recipe.upper.freqHi     = upper.frequencyRange[1];
    prov.recipe.upper.timeStart  = upper.timeRange[0];
    prov.recipe.upper.timeEnd    = upper.timeRange[1];
    prov.recipe.seed.roughness   = seedSettings.roughness;
    prov.recipe.seed.depth       = (int) seedSettings.depth;
    prov.recipe.lowerSeed        = 42u;
    prov.recipe.upperSeed        = 43u;
    prov.recipe.seedSeed         = 44u;
    prov.recipe.bpm              = 120.0;
    prov.recipe.frequencyPolicy  = upic::GuerreroOutOfRange::Compress;
    prov.recipe.pointsPolicy     = upic::GuerreroPointPolicy::Downsample;
    prov.recipe.maxPoints        = (int) opts.maxPoints;
    prov.lossy                   = planned.lossy;
    prov.omittedPoints           = planned.omittedPoints;
    prov.revision                = 1;
    prov.recipeValid             = true;
    prov.geometryEdited          = false;

    importArc.hasProvenance = true;
    importArc.provenance    = prov;

    MainComponent mc;
    mc.setSize (1280, 800);

    const int before = mc.arcCountForTest();
    std::vector<int> ids;
    const int added = mc.insertGuerreroArcs ({ importArc }, "Guerrero selftest", &ids);

    t.check (added == 1 && mc.arcCountForTest() == before + 1,
             "Guerrero: insertion adds exactly one arc",
             juce::String (added) + " added, " + juce::String (mc.arcCountForTest()) + " arcs");
    t.check (ids.size() == 1, "Guerrero: insertion reports the host-assigned id",
             ids.empty() ? juce::String ("none") : juce::String (ids.front()));

    const int insertedId = ids.empty() ? -1 : ids.front();

    // --- helper: find the inserted arc's provenance in a serialized piece --
    auto readProvenance = [insertedId](const juce::var& piece,
                                       int& pointCountOut,
                                       double& lastTOut,
                                       upic::GuerreroProvenance& provOut,
                                       bool& hasMetaOut)
    {
        hasMetaOut = false;
        if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    for (int i = 0; i < arcs->size(); ++i)
                    {
                        const auto arc = arcs->getReference (i);
                        if ((int) arc.getProperty ("id", -1) != insertedId) continue;

                        if (auto* pts = arc.getProperty ("points", juce::var()).getArray())
                        {
                            pointCountOut = pts->size();
                            if (pointCountOut > 0)
                                lastTOut = (double) pts->getReference (pointCountOut - 1)
                                               .getProperty ("t", 0.0);
                        }
                        const auto meta = arc.getProperty ("guerrero", juce::var());
                        if (meta.isObject())
                        {
                            hasMetaOut = true;
                            provOut = upic::guerreroio::fromVar (meta);
                        }
                        break;
                    }
    };

    // --- 3. the points survive serialization ------------------------------
    {
        const auto piece = mc.pieceToVar();
        int arcCount = -1;
        int pointCount = -1;
        double lastT = -1.0;
        bool hasMeta = false;
        upic::GuerreroProvenance readBack;

        readProvenance (piece, pointCount, lastT, readBack, hasMeta);

        if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    arcCount = arcs->size();

        t.check (arcCount == before + 1,
                 "Guerrero: the serialized piece holds the inserted arc",
                 juce::String (arcCount) + " arcs");
        t.check (pointCount == (int) planned.arc.points.size(),
                 "Guerrero: all converted points are serialized",
                 juce::String (pointCount) + " of "
                   + juce::String ((int) planned.arc.points.size()));
        t.check (std::abs (lastT - planned.arc.points.back().t) < 1e-6,
                 "Guerrero: the final point's time survives serialization",
                 juce::String (lastT, 3) + " s");

        t.check (hasMeta, "Guerrero: the inserted arc carries provenance");

        // Compare EVERY field, including high-bit ids. The demo seed id has the
        // high bit set (10186951107398961394), so a signed round-trip would
        // show up here.
        const bool idsMatch = readBack.sourceSeedId == seed.id
                              && readBack.sourceCurveId == seed.id
                              && readBack.compositionSeed == comp.seed
                              && readBack.randomSeed == prov.randomSeed;
        t.check (idsMatch, "Guerrero: provenance records all 64-bit ids exactly",
                 upic::guerreroio::u64ToString (readBack.sourceSeedId)
                   + " / expected "
                   + upic::guerreroio::u64ToString (seed.id));

        const auto& r = readBack.recipe;
        const bool recipeMatches =
               r.generatorVersion == prov.recipe.generatorVersion
            && r.generator == prov.recipe.generator
            && std::abs (r.lower.roughness - prov.recipe.lower.roughness) < 1e-12
            && r.lower.depth == prov.recipe.lower.depth
            && std::abs (r.lower.freqLo - prov.recipe.lower.freqLo) < 1e-9
            && std::abs (r.lower.freqHi - prov.recipe.lower.freqHi) < 1e-9
            && std::abs (r.lower.timeEnd - prov.recipe.lower.timeEnd) < 1e-9
            && std::abs (r.upper.roughness - prov.recipe.upper.roughness) < 1e-12
            && r.upper.depth == prov.recipe.upper.depth
            && std::abs (r.upper.freqLo - prov.recipe.upper.freqLo) < 1e-9
            && std::abs (r.upper.freqHi - prov.recipe.upper.freqHi) < 1e-9
            && std::abs (r.seed.roughness - prov.recipe.seed.roughness) < 1e-12
            && r.seed.depth == prov.recipe.seed.depth
            && r.lowerSeed == prov.recipe.lowerSeed
            && r.upperSeed == prov.recipe.upperSeed
            && r.seedSeed == prov.recipe.seedSeed
            && std::abs (r.bpm - prov.recipe.bpm) < 1e-9
            && r.frequencyPolicy == prov.recipe.frequencyPolicy
            && r.pointsPolicy == prov.recipe.pointsPolicy
            && r.maxPoints == prov.recipe.maxPoints;
        t.check (recipeMatches, "Guerrero: the whole generation recipe round-trips");

        t.check (readBack.revision == 1 && ! readBack.geometryEdited,
                 "Guerrero: a freshly generated arc has unedited geometry");

        // A hand-drawn arc must NOT gain a provenance object.
        bool handDrawnClean = true;
        if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    for (int i = 0; i < arcs->size(); ++i)
                    {
                        const auto arc = arcs->getReference (i);
                        if ((int) arc.getProperty ("id", -1) == insertedId) continue;
                        if (arc.hasProperty ("guerrero")) handDrawnClean = false;
                    }
        t.check (handDrawnClean, "Guerrero: hand-drawn arcs gain no provenance field");
    }

    // --- 4. undo / redo ---------------------------------------------------
    mc.undo();
    t.check (mc.arcCountForTest() == before,
             "Guerrero: undo removes the inserted arc",
             juce::String (mc.arcCountForTest()) + " arcs");

    mc.redo();
    t.check (mc.arcCountForTest() == before + 1,
             "Guerrero: redo restores the inserted arc",
             juce::String (mc.arcCountForTest()) + " arcs");

    // --- 4b. editing the geometry marks it; undo restores it -------------
    // This is the regeneration-safety contract: a generated arc the user has
    // touched must be distinguishable from one still as generated.
    //
    // A dedicated component is used so the transform cannot depend on whatever
    // selection the earlier undo/redo sequence happened to leave behind: the
    // insertion selects the new arc, so the transform targets exactly it.
    {
        MainComponent mce;
        mce.setSize (1280, 800);

        std::vector<int> eids;
        mce.insertGuerreroArcs ({ importArc }, "edit test", &eids);
        const int eid = eids.empty() ? -1 : eids.front();

        t.check (! mce.guerreroArcGeometryEditedForTest (eid),
                 "Guerrero: the generated geometry is unedited after insertion");

        // 105 = invert pitch. The transform codes are 101-108, not 0-3, and an
        // unknown code returns without touching anything.
        mce.transformPageForTest (105);

        t.check (mce.guerreroArcGeometryEditedForTest (eid),
                 "Guerrero: editing the geometry marks it edited");

        mce.undo();
        t.check (! mce.guerreroArcGeometryEditedForTest (eid),
                 "Guerrero: undoing the edit restores the unedited state");

        mce.redo();
        t.check (mce.guerreroArcGeometryEditedForTest (eid),
                 "Guerrero: redoing the edit marks it edited again");
    }
    // --- 5. save and reload (in memory) ----------------------------------
    {
        const auto saved = mc.pieceToVar();

        MainComponent reloaded;
        reloaded.setSize (1280, 800);
        reloaded.loadFromVar (saved);

        t.check (reloaded.arcCountForTest() == before + 1,
                 "Guerrero: the arc survives save and reload",
                 juce::String (reloaded.arcCountForTest()) + " arcs");

        int pc = -1; double lt = -1.0; bool hm = false;
        upic::GuerreroProvenance rb;
        readProvenance (reloaded.pieceToVar(), pc, lt, rb, hm);
        t.check (hm && rb.sourceSeedId == seed.id,
                 "Guerrero: provenance survives save and reload");
    }

    // --- 6. a physical file round-trip ------------------------------------
    // A unique name: parallel test runs must not collide on a fixed path.
    {
        const auto savedVar = mc.pieceToVar();
        const auto jsonText = juce::JSON::toString (savedVar, false);

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("upic_guerrero_roundtrip_"
                                       + juce::String (juce::Random::getSystemRandom().nextInt (1000000))
                                       + ".json");
        const bool wrote = file.replaceWithText (jsonText);
        t.check (wrote && file.existsAsFile() && file.getSize() > 0,
                 "Guerrero: the piece is written to a real file",
                 juce::String (file.getSize()) + " bytes");

        if (wrote)
        {
            const auto text = file.loadFileAsString();
            juce::var parsed;
            const auto parseResult = juce::JSON::parse (text, parsed);
            t.check (parseResult.wasOk(), "Guerrero: the written piece parses back",
                     parseResult.getErrorMessage());

            MainComponent fromDisk;
            fromDisk.setSize (1280, 800);
            fromDisk.loadFromVar (parsed);
            t.check (fromDisk.arcCountForTest() == before + 1,
                     "Guerrero: the arc loads from a real file",
                     juce::String (fromDisk.arcCountForTest()) + " arcs");

            // The disk round-trip must preserve the provenance too, including
            // the high-bit ids and the whole recipe.
            int pc = -1; double lt = -1.0; bool hm = false;
            upic::GuerreroProvenance rb;
            readProvenance (fromDisk.pieceToVar(), pc, lt, rb, hm);

            const bool diskMatch =
                   hm
                && rb.sourceSeedId == seed.id
                && rb.sourceCurveId == seed.id
                && rb.compositionSeed == comp.seed
                && rb.randomSeed == prov.randomSeed
                && rb.recipe.lowerSeed == prov.recipe.lowerSeed
                && rb.recipe.upperSeed == prov.recipe.upperSeed
                && rb.recipe.seedSeed == prov.recipe.seedSeed
                && rb.recipe.generatorVersion == prov.recipe.generatorVersion
                && rb.recipe.maxPoints == prov.recipe.maxPoints
                && std::abs (rb.recipe.seed.roughness - prov.recipe.seed.roughness) < 1e-12
                && rb.revision == prov.revision
                // The arc was edited in section 4b, so the on-disk copy must
                // agree with the CURRENT state, not with the unedited record.
                && rb.geometryEdited == mc.guerreroArcGeometryEditedForTest (insertedId);

            t.check (diskMatch, "Guerrero: provenance survives a real file round-trip",
                     hm ? upic::guerreroio::u64ToString (rb.sourceSeedId)
                        : juce::String ("no metadata"));
        }

        file.deleteFile();
    }
    // --- 7. record integrity ---------------------------------------------
    {
        uint64_t v = 0;

        t.check (! upic::guerreroio::tryParseU64 ("123abc45", v),
                 "Guerrero: a malformed id string is rejected, not silently truncated");
        t.check (! upic::guerreroio::tryParseU64 ("-1", v),
                 "Guerrero: a negative id is rejected");
        t.check (! upic::guerreroio::tryParseU64 ("18446744073709551616", v),
                 "Guerrero: an overflowing id is rejected before it can wrap");
        t.check (! upic::guerreroio::tryParseU64 ("", v),
                 "Guerrero: an empty id is rejected");
        const bool maxOk = upic::guerreroio::tryParseU64 ("18446744073709551615", v)
                           && v == std::numeric_limits<uint64_t>::max();
        t.check (maxOk, "Guerrero: the largest 64-bit id parses exactly",
                 juce::String (maxOk ? "18446744073709551615" : "FAILED"));

        // A pre-recipe record: no recipe object at all. It must load as legacy,
        // NOT as a recipe filled in from defaults.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            o->setProperty ("sourceSeedId", upic::guerreroio::u64ToString (12345));
            const auto legacy = upic::guerreroio::fromVar (juce::var (o));

            t.check (! legacy.recipeValid && ! legacy.malformed
                     && legacy.sourceSeedId == 12345,
                     "Guerrero: a pre-recipe record loads as legacy, not as a fabricated recipe");
        }

        // A present but unusable field must be flagged, not defaulted to zero.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            o->setProperty ("sourceSeedId", "not-an-id");
            const auto bad = upic::guerreroio::fromVar (juce::var (o));

            t.check (bad.malformed,
                     "Guerrero: a present but invalid id marks the record malformed");
        }
    }

    // --- 8. two arcs in one batch keep independent provenance ------------
    {
        MainComponent mc2;
        mc2.setSize (1280, 800);

        upic::GuerreroImportArc a1 = importArc;
        upic::GuerreroImportArc a2 = importArc;
        a2.provenance.sourceSeedId = seed.id + 1u;
        a2.provenance.recipe.seedSeed = 99u;

        std::vector<int> ids2;
        const int added2 = mc2.insertGuerreroArcs ({ a1, a2 }, "two arcs", &ids2);

        bool distinct = false;
        if (added2 == 2 && ids2.size() == 2)
        {
            const auto piece = mc2.pieceToVar();
            uint64_t seedA = 0, seedB = 0;
            if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            const auto arc = arcs->getReference (i);
                            const int id = (int) arc.getProperty ("id", -1);
                            const auto meta = upic::guerreroio::fromVar (
                                                arc.getProperty ("guerrero", juce::var()));
                            if (id == ids2[0]) seedA = meta.sourceSeedId;
                            if (id == ids2[1]) seedB = meta.sourceSeedId;
                        }
            distinct = (seedA != seedB);
        }

        t.check (distinct,
                 "Guerrero: two arcs in one batch carry independent provenance");
    }
    // --- 9. regeneration from the saved recipe ---------------------------
    // This is what makes the recipe meaningful: it must rebuild the same arc,
    // not merely describe it.
    {
        guerrero::upicadapter::PlannedArc regenerated;
        const bool ok = upic::guerrero_bridge::regenerateFromRecipe (prov.recipe, regenerated);

        t.check (ok, "Guerrero: the saved recipe regenerates an arc");

        if (ok)
        {
            bool same = regenerated.points.size() == planned.arc.points.size();

            if (same)
                for (size_t i = 0; i < planned.arc.points.size(); ++i)
                {
                    const auto& a = regenerated.points[i];
                    const auto& b = planned.arc.points[i];
                    if (std::abs (a.t - b.t) > 1e-12
                        || std::abs (a.freq - b.freq) > 1e-9
                        || std::abs ((double) a.i - (double) b.i) > 1e-9)
                    {
                        same = false;
                        break;
                    }
                }

            t.check (same, "Guerrero: regeneration reproduces every point of the original arc",
                     juce::String ((int) regenerated.points.size()) + " points");
        }

        // An unknown recipe version must be refused rather than guessed.
        guerrero::upicadapter::PlannedArc ignored;
        upic::GuerreroRecipe unknown;
        unknown.generatorVersion = 99;
        t.check (! upic::guerrero_bridge::regenerateFromRecipe (unknown, ignored),
                 "Guerrero: an unknown recipe version is refused, not guessed");
    }
    // --- 10. the host Regenerate command --------------------------------
    {
        MainComponent mcr;
        mcr.setSize (1280, 800);

        std::vector<int> rid;
        mcr.insertGuerreroArcs ({ importArc }, "regen", &rid);
        const int id0 = rid.empty() ? -1 : rid.front();

        t.check (mcr.regenerateGuerreroArc (id0),
                 "Guerrero: an unedited arc regenerates in place");

        // Save to a real file, reload into a fresh component, and regenerate
        // from the recipe that came back off disk. That is the path a user
        // actually takes: close the piece, open it, regenerate.
        {
            const auto jsonText = juce::JSON::toString (mcr.pieceToVar(), false);
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("upic_guerrero_regen_"
                                           + juce::String (juce::Random::getSystemRandom().nextInt (1000000))
                                           + ".json");
            const bool wrote = file.replaceWithText (jsonText);

            if (wrote)
            {
                juce::var parsed;
                juce::JSON::parse (file.loadFileAsString(), parsed);

                MainComponent fromDisk;
                fromDisk.setSize (1280, 800);
                fromDisk.loadFromVar (parsed);

                // Find the Guerrero arc id in the loaded piece.
                int loadedId = -1;
                if (auto* pages = parsed.getProperty ("pages", juce::var()).getArray())
                    if (! pages->isEmpty())
                        if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                            for (int i = 0; i < arcs->size(); ++i)
                            {
                                const auto a = arcs->getReference (i);
                                if (a.hasProperty ("guerrero"))
                                { loadedId = (int) a.getProperty ("id", -1); break; }
                            }

                t.check (loadedId > 0,
                         "Guerrero: the reloaded piece still identifies its Guerrero arc");

                if (loadedId > 0)
                    t.check (fromDisk.regenerateGuerreroArc (loadedId),
                             "Guerrero: an arc reloaded from disk regenerates from its stored recipe");
            }

            file.deleteFile();
        }

        // An edited arc must refuse: regenerating over an edit destroys work.
        mcr.transformPageForTest (105);
        t.check (! mcr.regenerateGuerreroArc (id0),
                 "Guerrero: an edited arc refuses regeneration");

        // And the refusal must be undone cleanly: undo the edit, then the arc
        // is unedited again and regenerates.
        mcr.undo();
        t.check (mcr.regenerateGuerreroArc (id0),
                 "Guerrero: undoing the edit makes the arc regenerable again");

        // A hand-drawn arc has no provenance and must refuse.
        t.check (! mcr.regenerateGuerreroArc (1),
                 "Guerrero: a hand-drawn arc refuses regeneration");
    }
    // --- 11. recipe strictness and legacy numeric safety -----------------
    {
        // A recipe missing a parameter regeneration needs must be REJECTED,
        // not filled in with a default.
        {
            auto recipe = upic::guerreroio::recipeToVar (prov.recipe);
            if (auto* o = recipe.getDynamicObject())
                o->removeProperty ("bpm");

            auto* wrapper = new juce::DynamicObject();
            wrapper->setProperty ("version", 1);
            wrapper->setProperty ("recipe", recipe);

            const auto damaged = upic::guerreroio::fromVar (juce::var (wrapper));
            t.check (! damaged.recipeValid && damaged.malformed,
                     "Guerrero: a recipe missing bpm is rejected as damaged, not defaulted");
        }

        // An out-of-range parameter is rejected rather than clamped.
        {
            auto recipe = upic::guerreroio::recipeToVar (prov.recipe);
            if (auto* o = recipe.getDynamicObject())
                o->setProperty ("maxPoints", 1);   // below the minimum of 2

            auto* wrapper = new juce::DynamicObject();
            wrapper->setProperty ("version", 1);
            wrapper->setProperty ("recipe", recipe);

            const auto bad = upic::guerreroio::fromVar (juce::var (wrapper));
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: an out-of-range maxPoints is rejected, not clamped");
        }

        // A genuine pre-recipe record is legacy, NOT malformed.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            o->setProperty ("sourceSeedId", upic::guerreroio::u64ToString (7));
            const auto legacy = upic::guerreroio::fromVar (juce::var (o));
            t.check (! legacy.recipeValid && ! legacy.malformed,
                     "Guerrero: an absent recipe is legacy, not damage");
        }

        // Legacy numeric ids above 2^53 cannot be trusted: a double may already
        // hold a rounded version of a different seed.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            // 2^53 + 4 is above the safe-integer range, so a double cannot
            // represent a specific seed there: reject rather than migrate a
            // rounded value.
            o->setProperty ("sourceSeedId", 9007199254740996.0);
            const auto rounded = upic::guerreroio::fromVar (juce::var (o));
            t.check (rounded.malformed,
                     "Guerrero: a legacy double id above 2^53 is rejected");
        }

        // ...but a small exact numeric id still migrates.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            o->setProperty ("sourceSeedId", 12345.0);
            const auto small = upic::guerreroio::fromVar (juce::var (o));
            t.check (! small.malformed && small.sourceSeedId == 12345,
                     "Guerrero: a small exact legacy numeric id still migrates");
        }
    }

    // --- 12. hand-edited geometry is caught at load ------------------------
    // A file whose points were changed but whose provenance was left alone must
    // not be treated as unedited: regeneration would otherwise overwrite it.
    {
        const auto saved = mc.pieceToVar();

        // Find the Guerrero arc and nudge one point's frequency.
        juce::var tampered = juce::JSON::parse (juce::JSON::toString (saved, false));
        int tamperedId = -1;
        if (auto* pages = tampered.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    for (int i = 0; i < arcs->size(); ++i)
                    {
                        auto arc = arcs->getReference (i);
                        if (! arc.hasProperty ("guerrero")) continue;
                        tamperedId = (int) arc.getProperty ("id", -1);

                        // Remove the last point: an unambiguous geometry change
                        // that also alters the point count, so the check cannot
                        // be defeated by rounding.
                        if (auto* pts = arc.getProperty ("points", juce::var()).getArray())
                            if (pts->size() > 2)
                                pts->removeLast();
                        break;
                    }

        t.check (tamperedId > 0, "Guerrero: the tamper test found the Guerrero arc");

        // Confirm the tamper reached the JSON: the stored point count and the
        // array length must now disagree.
        int storedCount = -1;
        int actualCount = -1;
        if (auto* pages = tampered.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    for (int i = 0; i < arcs->size(); ++i)
                    {
                        const auto a = arcs->getReference (i);
                        if ((int) a.getProperty ("id", -1) != tamperedId) continue;
                        if (auto* g = a.getProperty ("guerrero", juce::var()).getDynamicObject())
                            storedCount = (int) g->getProperty ("pointCount");
                        if (auto* pts = a.getProperty ("points", juce::var()).getArray())
                            actualCount = pts->size();
                        break;
                    }

        t.check (storedCount > 0 && actualCount == storedCount - 1,
                 "Guerrero: the tamper removed exactly one point",
                 juce::String (actualCount) + " of " + juce::String (storedCount));

        MainComponent fromTampered;
        fromTampered.setSize (1280, 800);
        fromTampered.loadFromVar (tampered);

        t.check (fromTampered.guerreroArcGeometryEditedForTest (tamperedId),
                 "Guerrero: geometry changed outside an edit is flagged at load");

        t.check (! fromTampered.regenerateGuerreroArc (tamperedId),
                 "Guerrero: hand-edited geometry cannot be regenerated over");
    }

    // --- 13. the converted arc actually produces audio --------------------
    // Everything above proves the data reaches the model. This proves the
    // model makes sound: the Guerrero arc is rendered offline through the
    // normal UPIC synthesis path and the output is inspected.
    {
        const int sr = 48000;
        const double seconds = 2.0;

        upic::Page pg;
        pg.durationSec = 4.0;

        upic::Arc a;
        a.id = 1;
        a.waveformId = 1;
        a.envelopeId = 1;
        a.intensity = 1.0f;
        a.mode = 0;   // oscillator
        a.points.reserve (importArc.points.size());
        for (const auto& p : importArc.points)
        {
            upic::ArcPoint pt;
            pt.t = p.t; pt.freq = p.freq; pt.i = p.i;
            a.points.push_back (pt);
        }
        pg.arcs.push_back (std::move (a));

        juce::AudioBuffer<float> out (2, (int) (seconds * sr));
        out.clear();

        std::vector<upic::SampleSlot> noSlots;
        UpicEngine e;
        e.prepare ((double) sr, 8192);
        e.renderPageOffline (pg, upic::defaultWaveforms(), upic::defaultEnvelopes(),
                             noSlots, out, (double) sr, 0.8f);

        float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (out.getSample (0, i)));

        t.check (peak > 1.0e-4f,
                 "Guerrero: the converted arc produces audio",
                 "peak=" + juce::String (peak, 5));

        // A truly silent page must stay silent, so the check above means
        // something rather than the renderer always emitting something.
        upic::Page empty;
        empty.durationSec = 4.0;
        juce::AudioBuffer<float> silent (2, (int) (seconds * sr));
        silent.clear();
        UpicEngine e2;
        e2.prepare ((double) sr, 8192);
        e2.renderPageOffline (empty, upic::defaultWaveforms(), upic::defaultEnvelopes(),
                              noSlots, silent, (double) sr, 0.8f);

        float silentPeak = 0.0f;
        for (int i = 0; i < silent.getNumSamples(); ++i)
            silentPeak = juce::jmax (silentPeak, std::abs (silent.getSample (0, i)));

        t.check (silentPeak < 1.0e-6f,
                 "Guerrero: an empty page stays silent (the audio check is meaningful)",
                 "peak=" + juce::String (silentPeak, 8));
    }
    // --- 14. bypasses and numeric coercion are refused --------------------
    {
        // A record whose fingerprint was DELETED must not become regenerable:
        // "no fingerprint" means unverifiable, not "skip the check".
        {
            auto pv = upic::guerreroio::toVar (prov);
            if (auto* o = pv.getDynamicObject()) o->removeProperty ("geometryHash");
            const auto stripped = upic::guerreroio::fromVar (pv);
            t.check (! stripped.hasFingerprint,
                     "Guerrero: a record with the fingerprint removed is not verifiable");
        }
        {
            auto pv = upic::guerreroio::toVar (prov);
            if (auto* o = pv.getDynamicObject()) o->removeProperty ("pointCount");
            const auto stripped = upic::guerreroio::fromVar (pv);
            t.check (! stripped.hasFingerprint,
                     "Guerrero: a record with the point count removed is not verifiable");
        }

        // "recipe": null is DAMAGE, not a legacy record. Note that this must be
        // var::null, not a void var: setting a void property is the same as
        // removing it, which is a genuine legacy record.
        {
            // Parse real JSON so the property is a genuine null, exactly as a
            // hand-edited file would produce. A void property would instead be
            // indistinguishable from an absent one (a legacy record).
            juce::var wrapper;
            juce::JSON::parse ("{\"version\":1,\"recipe\":null}", wrapper);
            const auto nul = upic::guerreroio::fromVar (wrapper);
            t.check (! nul.recipeValid && nul.malformed,
                     "Guerrero: a null recipe is damaged, not legacy");
        }

        // A damaged recipe must still be damaged after being saved and reloaded.
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            if (auto* ro = recipe.getDynamicObject()) ro->removeProperty ("bpm");
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);

            const auto damaged = upic::guerreroio::fromVar (pv);
            const auto afterSave = upic::guerreroio::fromVar (upic::guerreroio::toVar (damaged));
            t.check (damaged.malformed && afterSave.malformed,
                     "Guerrero: a damaged record stays damaged across save and reload");
        }

        // Fractional parameters are rejected rather than truncated.
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("maxPoints", 1000.8);
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);

            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: a fractional maxPoints is rejected, not truncated");
        }
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            auto lower = recipe.getProperty ("lower", juce::var());
            if (auto* lo = lower.getDynamicObject()) lo->setProperty ("depth", 4.5);
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("lower", lower);
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);

            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: a fractional envelope depth is rejected");
        }

        // 64-bit integers that would wrap into a plausible 32-bit value must be
        // rejected. Each of these has been chosen so the low 32 bits look valid.
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            if (auto* ro = recipe.getDynamicObject())
                ro->setProperty ("maxPoints", (juce::int64) 4294968296);   // wraps to 1000
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);
            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: an int64 maxPoints that wraps to 1000 is rejected");
        }
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            auto lower = recipe.getProperty ("lower", juce::var());
            if (auto* lo = lower.getDynamicObject())
                lo->setProperty ("depth", (juce::int64) 4294967300);      // wraps to 4
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("lower", lower);
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);
            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: an int64 envelope depth that wraps to 4 is rejected");
        }
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            if (auto* ro = recipe.getDynamicObject())
                ro->setProperty ("generatorVersion", (juce::int64) 4294967297);  // wraps to 1
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);
            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: an int64 generatorVersion that wraps to 1 is rejected");
        }

        // A numeric field given as a STRING is rejected rather than reinterpreted.
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("bpm", "120");
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);
            const auto bad = upic::guerreroio::fromVar (pv);
            t.check (! bad.recipeValid && bad.malformed,
                     "Guerrero: a string bpm is rejected, not coerced");
        }

        // A zero-width range is legal ONLY with the explicit expansion flag.
        {
            auto pv = upic::guerreroio::toVar (prov);
            auto recipe = pv.getProperty ("recipe", juce::var());
            auto lower = recipe.getProperty ("lower", juce::var());
            if (auto* lo = lower.getDynamicObject())
            {
                lo->setProperty ("freqLo", 440.0);
                lo->setProperty ("freqHi", 440.0);
                lo->setProperty ("expandDegenerate", false);
            }
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("lower", lower);
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);

            const auto rejected = upic::guerreroio::fromVar (pv);
            t.check (! rejected.recipeValid,
                     "Guerrero: a zero-width range without expansion is rejected");

            if (auto* lo = lower.getDynamicObject())
                lo->setProperty ("expandDegenerate", true);
            if (auto* ro = recipe.getDynamicObject()) ro->setProperty ("lower", lower);
            if (auto* o = pv.getDynamicObject()) o->setProperty ("recipe", recipe);
            const auto accepted = upic::guerreroio::fromVar (pv);
            t.check (accepted.recipeValid,
                     "Guerrero: a zero-width range with expansion is accepted");
        }

        // 2^53 is excluded: 2^53+1 rounds DOWN to it, so it may stand for a
        // different seed.
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("version", 1);
            o->setProperty ("sourceSeedId", 9007199254740992.0);   // exactly 2^53
            const auto atLimit = upic::guerreroio::fromVar (juce::var (o));
            t.check (atLimit.malformed,
                     "Guerrero: a legacy double at exactly 2^53 is rejected as ambiguous");
        }
    }

    // --- 15. regenerate -> undo -> redo restores geometry and revision ----
    {
        MainComponent mc3;
        mc3.setSize (1280, 800);

        std::vector<int> ids3;
        mc3.insertGuerreroArcs ({ importArc }, "regen undo", &ids3);
        const int id3 = ids3.empty() ? -1 : ids3.front();

        auto revisionOf = [id3](const juce::var& piece)
        {
            if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            const auto a = arcs->getReference (i);
                            if ((int) a.getProperty ("id", -1) != id3) continue;
                            const auto g = a.getProperty ("guerrero", juce::var());
                            return g.isObject() ? (int) g.getProperty ("revision", 0) : -1;
                        }
            return -1;
        };

        const int revBefore = revisionOf (mc3.pieceToVar());

        t.check (mc3.regenerateGuerreroArc (id3), "Guerrero: regenerate succeeds for the undo test");
        const int revAfter = revisionOf (mc3.pieceToVar());
        t.check (revAfter == revBefore + 1,
                 "Guerrero: regeneration bumps the revision",
                 juce::String (revBefore) + " -> " + juce::String (revAfter));

        mc3.undo();
        t.check (revisionOf (mc3.pieceToVar()) == revBefore,
                 "Guerrero: undoing regeneration restores the previous revision");

        mc3.redo();
        t.check (revisionOf (mc3.pieceToVar()) == revAfter,
                 "Guerrero: redoing regeneration restores the new revision");
    }
    // --- 15b. the dialog's apply path: new arc, edit, undo, refusal --------
    // applyGuerreroRecipe() is what the parameter dialog calls. The unit under
    // test is the transaction, not the widgets: a recipe must create an arc,
    // an edited recipe must replace geometry and recipe in ONE undo step, and
    // an arc with user-edited geometry must be refused.
    {
        MainComponent mcR;
        mcR.setSize (1280, 800);

        auto makeRecipe = [] (uint64_t rootSeed, int maxPoints)
        {
            upic::GuerreroRecipe r;
            r.lower.roughness = 0.35;
            r.lower.depth     = 7;
            r.lower.freqLo    = 110.0;
            r.lower.freqHi    = 880.0;
            r.lower.timeStart = 0.0;
            r.lower.timeEnd   = 16.0;
            r.upper = r.lower;
            r.upper.freqLo = 880.0;
            r.upper.freqHi = 1760.0;
            r.seed.roughness = 0.55;
            r.seed.depth     = 7;
            r.lowerSeed = rootSeed;
            r.upperSeed = rootSeed + 1u;
            r.seedSeed  = rootSeed + 2u;
            r.bpm = 120.0;
            r.frequencyPolicy = upic::GuerreroOutOfRange::Compress;
            r.pointsPolicy    = upic::GuerreroPointPolicy::Downsample;
            r.maxPoints = maxPoints;
            return r;
        };

        auto firstGuerreroArc = [] (const juce::var& piece, upic::GuerreroProvenance& out) -> int
        {
            if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            const auto a = arcs->getReference (i);
                            const auto g = a.getProperty ("guerrero", juce::var());
                            if (! g.isObject()) continue;
                            out = upic::guerreroio::fromVar (g);
                            return (int) a.getProperty ("id", -1);
                        }
            return -1;
        };

        const upic::GuerreroRecipe recA = makeRecipe (424242u, 1000);
        juce::String err;

        t.check (mcR.applyGuerreroRecipe (0, recA, err),
                 "Guerrero: an accepted recipe creates an arc", err);

        upic::GuerreroProvenance provA;
        const int rid = firstGuerreroArc (mcR.pieceToVar(), provA);
        t.check (rid > 0, "Guerrero: the new arc is present");

        t.check (provA.recipeValid && ! provA.malformed,
                 "Guerrero: the new arc carries a usable recipe");
        t.check (provA.recipe.lowerSeed == recA.lowerSeed
                     && provA.recipe.maxPoints == recA.maxPoints
                     && std::abs (provA.recipe.seed.roughness - recA.seed.roughness) < 1e-12,
                 "Guerrero: the stored recipe matches what was applied");
        t.check (provA.hasFingerprint,
                 "Guerrero: the new arc has a geometry fingerprint");

        const uint64_t hashA = provA.geometryHash;
        const int revA = provA.revision;

        // --- apply a CHANGED recipe to the same arc ------------------------
        upic::GuerreroRecipe recB = recA;
        recB.lowerSeed += 1000u;
        recB.upperSeed  = recB.lowerSeed + 1u;
        recB.seedSeed   = recB.lowerSeed + 2u;

        err.clear();
        t.check (mcR.applyGuerreroRecipe (rid, recB, err),
                 "Guerrero: a changed recipe is applied to an existing arc", err);

        upic::GuerreroProvenance provB;
        firstGuerreroArc (mcR.pieceToVar(), provB);
        t.check (provB.recipe.lowerSeed == recB.lowerSeed,
                 "Guerrero: the arc's stored recipe was replaced");
        t.check (provB.geometryHash != hashA,
                 "Guerrero: the applied recipe produced different geometry");
        t.check (provB.revision == revA + 1,
                 "Guerrero: applying a recipe bumps the revision");

        // --- one undo restores BOTH geometry and recipe --------------------
        mcR.undo();
        upic::GuerreroProvenance provUndo;
        firstGuerreroArc (mcR.pieceToVar(), provUndo);
        t.check (provUndo.recipe.lowerSeed == recA.lowerSeed,
                 "Guerrero: undo restores the previous recipe");
        t.check (provUndo.geometryHash == hashA,
                 "Guerrero: undo restores the previous geometry");

        mcR.redo();
        upic::GuerreroProvenance provRedo;
        firstGuerreroArc (mcR.pieceToVar(), provRedo);
        t.check (provRedo.recipe.lowerSeed == recB.lowerSeed,
                 "Guerrero: redo restores the applied recipe");

        // --- a user-edited arc refuses a recipe change ---------------------
        // Transform the page so endEdit() marks the geometry as edited, then
        // confirm the recipe path refuses rather than overwriting that work.
        mcR.transformPageForTest (105);   // invert pitch
        t.check (mcR.guerreroArcGeometryEditedForTest (rid),
                 "Guerrero: the transform marked the arc as edited");

        err.clear();
        t.check (! mcR.applyGuerreroRecipe (rid, recA, err),
                 "Guerrero: applying a recipe over edited geometry is refused");
        t.check (err.containsIgnoreCase ("edited"),
                 "Guerrero: the refusal explains that the geometry was edited", err);

        // The same guard must apply to the plain regenerate path, so the two
        // commands cannot disagree about when an arc is protected.
        t.check (! mcR.regenerateGuerreroArc (rid),
                 "Guerrero: regenerate also refuses edited geometry");
    }
    // --- 15c. the dialog's own validation contract -------------------------
    // The dialog is the only place a user can type an impossible recipe, so its
    // collect() is tested directly: it must accept a good recipe unchanged and
    // refuse the values the engine would reject later with a vague message.
    {
        auto good = []
        {
            upic::GuerreroRecipe r;
            r.lower.roughness = 0.35;
            r.lower.depth     = 7;
            r.lower.freqLo    = 110.0;
            r.lower.freqHi    = 880.0;
            r.lower.timeStart = 0.0;
            r.lower.timeEnd   = 16.0;
            r.upper = r.lower;
            r.upper.freqLo = 880.0;
            r.upper.freqHi = 1760.0;
            r.seed.roughness = 0.55;
            r.seed.depth     = 7;
            r.lowerSeed = 424242u;
            r.upperSeed = 424243u;
            r.seedSeed  = 424244u;
            r.bpm = 120.0;
            r.maxPoints = 1000;
            return r;
        };

        // A valid recipe survives the round trip through the widgets.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (dlg.collect (out, err),
                     "Guerrero dialog: a valid recipe is accepted", err);
            t.check (out.maxPoints == 1000 && out.lowerSeed == 424242u
                         && std::abs (out.lower.freqHi - 880.0) < 1e-9
                         && std::abs (out.seed.roughness - 0.55) < 1e-9,
                     "Guerrero dialog: the collected recipe matches the input");
        }

        // Roughness above the recipe maximum.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.recipe.lower.roughness = 9.0;
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (! dlg.collect (out, err),
                     "Guerrero dialog: roughness above 8 is refused");
            t.check (err.containsIgnoreCase ("roughness"),
                     "Guerrero dialog: the roughness refusal names the field", err);
        }

        // Reversed frequency range.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.recipe.upper.freqLo = 2000.0;
            dlg.recipe.upper.freqHi = 1000.0;
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (! dlg.collect (out, err),
                     "Guerrero dialog: a reversed frequency range is refused");
            t.check (err.containsIgnoreCase ("greater"),
                     "Guerrero dialog: the range refusal explains the ordering", err);
        }

        // A frequency outside the storable domain.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.recipe.lower.freqHi = 200000.0;
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (! dlg.collect (out, err),
                     "Guerrero dialog: a frequency above 100 kHz is refused");
        }

        // Too few points.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.recipe.maxPoints = 1;
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (! dlg.collect (out, err),
                     "Guerrero dialog: maxPoints of 1 is refused");
        }

        // A zero-width envelope range: the dialog offers no expansion opt-in,
        // so it must refuse rather than silently widen behind the user.
        {
            GuerreroParamOverlay dlg;
            dlg.recipe = good();
            dlg.recipe.lower.freqHi = dlg.recipe.lower.freqLo;
            dlg.loadFromRecipe();

            upic::GuerreroRecipe out;
            juce::String err;
            t.check (! dlg.collect (out, err),
                     "Guerrero dialog: a zero-width range is refused, not widened");
        }
    }

    // --- 15d. dialog round-trip fidelity ----------------------------------
    // Opening a saved recipe and pressing Apply must not change anything. The
    // dialog only parses a field whose text the user actually edited, so these
    // cases pin that contract: unrelated seeds, seeds above INT64_MAX, an
    // out-of-range seed, and numbers that a fixed decimal format would truncate.
    {
        auto recipeWith = [] (uint64_t lower, uint64_t upper, uint64_t seed,
                              double bpm, double rough)
        {
            upic::GuerreroRecipe r;
            r.lower.roughness = rough;
            r.lower.depth     = 7;
            r.lower.freqLo    = 110.0;
            r.lower.freqHi    = 880.0;
            r.lower.timeStart = 0.0;
            r.lower.timeEnd   = 16.0;
            r.upper = r.lower;
            r.upper.freqLo = 880.0;
            r.upper.freqHi = 1760.0;
            r.seed.roughness = 0.55;
            r.seed.depth     = 7;
            r.lowerSeed = lower;
            r.upperSeed = upper;
            r.seedSeed  = seed;
            r.bpm = bpm;
            r.maxPoints = 1000;
            return r;
        };

        const upic::GuerreroRecipe saved =
            recipeWith (123u, 999u, 777u, 127.123456, 0.123456789);

        GuerreroParamOverlay dlg;
        dlg.recipe = saved;
        dlg.loadFromRecipe();

        upic::GuerreroRecipe out;
        juce::String err;
        t.check (dlg.collect (out, err),
                 "Guerrero dialog: a non-round recipe is accepted unchanged", err);
        t.check (out.lowerSeed == 123u && out.upperSeed == 999u && out.seedSeed == 777u,
                 "Guerrero dialog: unrelated stored seeds are preserved, not re-derived");
        t.check (std::abs (out.bpm - 127.123456) < 1e-12,
                 "Guerrero dialog: a high-precision tempo is preserved exactly");
        t.check (std::abs (out.lower.roughness - 0.123456789) < 1e-15,
                 "Guerrero dialog: a high-precision roughness is preserved exactly");

        // The renderer itself must show the full unsigned value: a seed above
        // INT64_MAX would display as negative if it went through int64.
        const upic::GuerreroRecipe big = recipeWith (9223372036854775808ULL, 1u, 2u,
                                                     120.0, 0.35);
        GuerreroParamOverlay bigDlg;
        bigDlg.recipe = big;
        bigDlg.loadFromRecipe();
        t.check (bigDlg.textForTest ("Lower envelope seed") == "9223372036854775808",
                 "Guerrero dialog: a seed above INT64_MAX is rendered in full");

        upic::GuerreroRecipe bigOut;
        t.check (bigDlg.collect (bigOut, err),
                 "Guerrero dialog: a seed above INT64_MAX round-trips", err);
        t.check (bigOut.lowerSeed == 9223372036854775808ULL,
                 "Guerrero dialog: the large seed survives exactly");

        // Above UINT64_MAX must be refused, never wrapped to something else.
        GuerreroParamOverlay overDlg;
        overDlg.recipe = saved;
        overDlg.loadFromRecipe();
        overDlg.setTextForTest ("Seed curve seed", "18446744073709551616");
        upic::GuerreroRecipe overOut;
        t.check (! overDlg.collect (overOut, err),
                 "Guerrero dialog: a seed above UINT64_MAX is rejected");
        t.check (overOut.seedSeed == saved.seedSeed,
                 "Guerrero dialog: a rejected seed leaves the stored value intact");

        // Full-consumption parsing: "120.5.6" is not a number, even though it
        // contains a valid prefix.
        GuerreroParamOverlay junkDlg;
        junkDlg.recipe = saved;
        junkDlg.loadFromRecipe();
        junkDlg.setTextForTest ("Tempo (BPM)", "120.5.6");
        upic::GuerreroRecipe junkOut;
        t.check (! junkDlg.collect (junkOut, err),
                 "Guerrero dialog: a malformed number is rejected, not read as a prefix");

        // A saved recipe using degenerate-range expansion stays editable.
        upic::GuerreroRecipe deg = recipeWith (123u, 999u, 777u, 120.0, 0.35);
        deg.lower.freqHi = deg.lower.freqLo;
        deg.lower.allowDegenerateRangeExpansion = true;

        GuerreroParamOverlay degDlg;
        degDlg.recipe = deg;
        degDlg.loadFromRecipe();
        upic::GuerreroRecipe degOut;
        err.clear();
        t.check (degDlg.collect (degOut, err),
                 "Guerrero dialog: a saved zero-width recipe with expansion stays valid", err);
        t.check (degOut.lower.allowDegenerateRangeExpansion
                     && degOut.lower.freqHi == degOut.lower.freqLo,
                 "Guerrero dialog: the expansion flag survives the round trip");
    }

    // --- 15e. provenance identity follows the recipe ----------------------
    {
        MainComponent mcP;
        mcP.setSize (1280, 800);

        auto recipeFor = [] (uint64_t root)
        {
            upic::GuerreroRecipe r;
            r.lower.roughness = 0.35;
            r.lower.depth     = 7;
            r.lower.freqLo    = 110.0;
            r.lower.freqHi    = 880.0;
            r.lower.timeStart = 0.0;
            r.lower.timeEnd   = 16.0;
            r.upper = r.lower;
            r.upper.freqLo = 880.0;
            r.upper.freqHi = 1760.0;
            r.seed.roughness = 0.55;
            r.seed.depth     = 7;
            r.lowerSeed = root;
            r.upperSeed = root + 1u;
            r.seedSeed  = root + 2u;
            r.bpm = 120.0;
            r.maxPoints = 1000;
            return r;
        };

        auto provAt = [] (const juce::var& piece, int& idOut, upic::GuerreroProvenance& out) -> bool
        {
            if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            const auto a = arcs->getReference (i);
                            const auto g = a.getProperty ("guerrero", juce::var());
                            if (! g.isObject()) continue;
                            out = upic::guerreroio::fromVar (g);
                            idOut = (int) a.getProperty ("id", -1);
                            return true;
                        }
            return false;
        };

        const upic::GuerreroRecipe r1 = recipeFor (1000u);
        juce::String err;
        t.check (mcP.applyGuerreroRecipe (0, r1, err),
                 "Guerrero: a recipe with distinct seeds creates an arc", err);

        upic::GuerreroProvenance p1;
        int pid = -1;
        t.check (provAt (mcP.pieceToVar(), pid, p1), "Guerrero: the arc is found");
        t.check (p1.compositionSeed == r1.lowerSeed && p1.randomSeed == r1.seedSeed,
                 "Guerrero: the new arc records the recipe's seed identities");

        // Change the seeds and apply; the identity fields must follow, not stay
        // at the values from the original generation.
        const upic::GuerreroRecipe r2 = recipeFor (5000u);
        err.clear();
        t.check (mcP.applyGuerreroRecipe (pid, r2, err),
                 "Guerrero: a changed recipe is applied", err);

        upic::GuerreroProvenance p2;
        provAt (mcP.pieceToVar(), pid, p2);
        t.check (p2.compositionSeed == r2.lowerSeed,
                 "Guerrero: compositionSeed follows the applied recipe");
        t.check (p2.randomSeed == r2.seedSeed,
                 "Guerrero: randomSeed follows the applied recipe");

        // Undo and redo must move the identities too.
        mcP.undo();
        upic::GuerreroProvenance pUndo;
        provAt (mcP.pieceToVar(), pid, pUndo);
        t.check (pUndo.compositionSeed == r1.lowerSeed && pUndo.randomSeed == r1.seedSeed,
                 "Guerrero: undo restores the previous seed identities");

        mcP.redo();
        upic::GuerreroProvenance pRedo;
        provAt (mcP.pieceToVar(), pid, pRedo);
        t.check (pRedo.compositionSeed == r2.lowerSeed && pRedo.randomSeed == r2.seedSeed,
                 "Guerrero: redo restores the applied seed identities");
    }

    // --- 15f. the overlay follows the window ------------------------------
    // The dialog is laid out once when it opens, so a resize while it is open
    // must move it again - otherwise a narrow or landscape window clips the
    // card at the old bounds.
    {
        MainComponent mcW;
        mcW.setSize (1280, 800);

        mcW.openGuerreroDialog (0);
        t.check (mcW.guerreroOverlay != nullptr,
                 "Guerrero: the parameter dialog opens");

        if (mcW.guerreroOverlay != nullptr)
        {
            mcW.setSize (760, 420);
            t.check (mcW.guerreroOverlay->getBounds() == mcW.getLocalBounds(),
                     "Guerrero: the dialog follows a window resize");
        }

        mcW.closeGuerreroDialog();
        t.check (mcW.guerreroOverlay == nullptr,
                 "Guerrero: the dialog closes and is destroyed");
    }
    // --- 16. persistence precision: an untouched arc stays unedited ------
    // At 127 BPM the score times are not exact decimals, so a fingerprint over
    // raw bits changes after a JSON round-trip and the arc would look edited.
    {
        guerrero::Composition c127;
        c127.tempoMap.push_back ({ guerrero::RationalBeatTime (0, 1), 127.0 });

        guerrero::BrownianSettings lo127;
        lo127.roughness = 0.35;
        lo127.depth = 7;
        lo127.frequencyRange[0] = 110.0;
        lo127.frequencyRange[1] = 880.0;
        lo127.timeRange[0] = 0.0;
        lo127.timeRange[1] = 8.0;

        guerrero::BrownianSettings up127 = lo127;
        up127.frequencyRange[0] = 880.0;
        up127.frequencyRange[1] = 1760.0;

        auto loEnv = guerrero::generateBrownianEnvelope (lo127, 101u);
        auto upEnv = guerrero::generateBrownianEnvelope (up127, 102u);

        guerrero::BrownianSettings sd127;
        sd127.roughness = 0.55;
        sd127.depth = 7;
        auto sd = guerrero::generateConfinementSeed (loEnv, &upEnv, sd127, 103u);

        c127.envelopes.push_back (loEnv);
        c127.envelopes.push_back (upEnv);
        c127.seeds.push_back (sd);

        guerrero::upicadapter::ConversionOptions o127;
        o127.maxPoints = 1000;
        o127.pointsPolicy = guerrero::upicadapter::PointsPolicy::Downsample;
        o127.frequencyPolicy = guerrero::upicadapter::RangePolicy::Clamp;
        o127.fallbackBpm = 127.0;

        auto pl127 = guerrero::upicadapter::planArcFromCurve (sd, c127, o127);
        t.check (pl127.success, "Guerrero: 127 BPM material converts");

        if (pl127.success)
        {
            upic::GuerreroImportArc ia127 = upic::guerrero_bridge::toImportArc (pl127.arc);

            upic::GuerreroProvenance p127;
            p127.recipeValid = true;
            p127.recipe.generatorVersion = 1;
            p127.recipe.lower.roughness = lo127.roughness;
            p127.recipe.lower.depth = (int) lo127.depth;
            p127.recipe.lower.freqLo = lo127.frequencyRange[0];
            p127.recipe.lower.freqHi = lo127.frequencyRange[1];
            p127.recipe.lower.timeStart = lo127.timeRange[0];
            p127.recipe.lower.timeEnd = lo127.timeRange[1];
            p127.recipe.upper.roughness = up127.roughness;
            p127.recipe.upper.depth = (int) up127.depth;
            p127.recipe.upper.freqLo = up127.frequencyRange[0];
            p127.recipe.upper.freqHi = up127.frequencyRange[1];
            p127.recipe.upper.timeStart = up127.timeRange[0];
            p127.recipe.upper.timeEnd = up127.timeRange[1];
            p127.recipe.seed.roughness = sd127.roughness;
            p127.recipe.seed.depth = (int) sd127.depth;
            p127.recipe.lowerSeed = 101u;
            p127.recipe.upperSeed = 102u;
            p127.recipe.seedSeed = 103u;
            p127.recipe.bpm = 127.0;
            p127.recipe.frequencyPolicy = upic::GuerreroOutOfRange::Compress;
            p127.recipe.pointsPolicy = upic::GuerreroPointPolicy::Downsample;
            p127.recipe.maxPoints = 1000;

            ia127.hasProvenance = true;
            ia127.provenance = p127;

            MainComponent m127;
            m127.setSize (1280, 800);
            std::vector<int> i127;
            m127.insertGuerreroArcs ({ ia127 }, "127 bpm", &i127);
            const int id127 = i127.empty() ? -1 : i127.front();

            // Round-trip through a real file, exactly as a user would.
            const auto text = juce::JSON::toString (m127.pieceToVar(), false);
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("upic_g127_"
                                           + juce::String (juce::Random::getSystemRandom().nextInt (1000000))
                                           + ".json");
            file.replaceWithText (text);

            juce::var parsed;
            juce::JSON::parse (file.loadFileAsString(), parsed);
            MainComponent r127;
            r127.setSize (1280, 800);
            r127.loadFromVar (parsed);
            file.deleteFile();

            int rid = -1;
            if (auto* pages = parsed.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                            if (arcs->getReference (i).hasProperty ("guerrero"))
                            { rid = (int) arcs->getReference (i).getProperty ("id", -1); break; }

            t.check (rid > 0, "Guerrero: the 127 BPM arc is found after reload");
            t.check (! r127.guerreroArcGeometryEditedForTest (rid),
                     "Guerrero: an untouched arc is NOT flagged edited after a 127 BPM round-trip");
            t.check (r127.regenerateGuerreroArc (rid),
                     "Guerrero: a 127 BPM arc reloaded from disk still regenerates");
        }
    }

    // --- 17. schema version and revision overflow ------------------------
    {
        // An unknown outer schema version is not interpreted.
        {
            juce::var wrapper;
            juce::JSON::parse ("{\"version\":99}", wrapper);
            const auto v99 = upic::guerreroio::fromVar (wrapper);
            t.check (v99.malformed && ! v99.recipeValid,
                     "Guerrero: an unsupported provenance schema version is rejected");
        }

        // A revision at INT_MAX cannot be incremented: regeneration refuses.
        {
            MainComponent mr;
            mr.setSize (1280, 800);
            std::vector<int> ir;
            mr.insertGuerreroArcs ({ importArc }, "rev", &ir);
            const int rid = ir.empty() ? -1 : ir.front();

            auto piece = mr.pieceToVar();
            if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            auto arc = arcs->getReference (i);
                            if ((int) arc.getProperty ("id", -1) != rid) continue;
                            if (auto* g = arc.getProperty ("guerrero", juce::var()).getDynamicObject())
                                g->setProperty ("revision", std::numeric_limits<int>::max());
                            break;
                        }

            MainComponent mrv;
            mrv.setSize (1280, 800);
            mrv.loadFromVar (piece);
            t.check (! mrv.regenerateGuerreroArc (rid),
                     "Guerrero: an exhausted revision counter refuses regeneration");
        }
    }
    // --- 18. fingerprint migration from the pre-quantisation algorithm ----
    // Older records carry an exact-bits hash and no algorithm field. They must
    // be verified with THAT algorithm, not assumed edited.
    {
        MainComponent mm;
        mm.setSize (1280, 800);
        std::vector<int> im;
        mm.insertGuerreroArcs ({ importArc }, "migrate", &im);
        const int mid = im.empty() ? -1 : im.front();

        // Rewrite the arc's provenance as an OLD record: exact-bits hash,
        // algorithm field removed.
        auto piece = mm.pieceToVar();
        bool rewrote = false;
        if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
            if (! pages->isEmpty())
                if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                    for (int i = 0; i < arcs->size(); ++i)
                    {
                        auto arc = arcs->getReference (i);
                        if ((int) arc.getProperty ("id", -1) != mid) continue;

                        std::vector<upic::ArcPoint> pts;
                        if (auto* parr = arc.getProperty ("points", juce::var()).getArray())
                            for (int k = 0; k < parr->size(); ++k)
                            {
                                const auto pv = parr->getReference (k);
                                upic::ArcPoint p;
                                p.t = (double) pv.getProperty ("t", 0.0);
                                p.freq = (double) pv.getProperty ("f", 440.0);
                                p.i = (float) (double) pv.getProperty ("i", 1.0);
                                pts.push_back (p);
                            }

                        if (auto* g = arc.getProperty ("guerrero", juce::var()).getDynamicObject())
                        {
                            g->setProperty ("geometryHash",
                                            upic::guerreroio::u64ToString (
                                                upic::Arc::geometryFingerprintLegacy (pts)));
                            g->removeProperty ("geometryHashAlgorithm");
                            rewrote = true;
                        }
                        break;
                    }

        t.check (rewrote, "Guerrero: an old-style record was constructed for the migration test");

        if (rewrote)
        {
            MainComponent mig;
            mig.setSize (1280, 800);
            mig.loadFromVar (piece);

            t.check (! mig.guerreroArcGeometryEditedForTest (mid),
                     "Guerrero: an old exact-bits fingerprint is verified, not assumed edited");

            // After verification the record is upgraded, so a later save uses
            // the quantised algorithm.
            const auto after = mig.pieceToVar();
            int algo = 0;
            if (auto* pages = after.getProperty ("pages", juce::var()).getArray())
                if (! pages->isEmpty())
                    if (auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray())
                        for (int i = 0; i < arcs->size(); ++i)
                        {
                            const auto a = arcs->getReference (i);
                            if ((int) a.getProperty ("id", -1) != mid) continue;
                            const auto g = a.getProperty ("guerrero", juce::var());
                            algo = g.isObject() ? (int) g.getProperty ("geometryHashAlgorithm", 0) : 0;
                            break;
                        }

            t.check (algo == upic::Arc::kFingerprintQuantised,
                     "Guerrero: a verified old fingerprint is migrated to the quantised algorithm",
                     juce::String (algo));

            t.check (mig.regenerateGuerreroArc (mid),
                     "Guerrero: a migrated old arc can be regenerated");
        }
    }

    // --- 19. invalid loader fields are not repaired -----------------------
    {
        // "version" as a string is damage, not an absent version.
        {
            juce::var w;
            juce::JSON::parse ("{\"version\":\"99\"}", w);
            const auto bad = upic::guerreroio::fromVar (w);
            t.check (bad.malformed, "Guerrero: a string version is rejected, not defaulted to 1");
        }
        // A revision that would overflow is rejected at load.
        {
            juce::var w;
            juce::JSON::parse ("{\"version\":1,\"revision\":4294967297}", w);
            const auto bad = upic::guerreroio::fromVar (w);
            t.check (bad.malformed, "Guerrero: an oversized revision is rejected at load");
        }
        // An absent version is still a legitimate legacy record.
        {
            juce::var w;
            juce::JSON::parse ("{\"sourceSeedId\":\"7\"}", w);
            const auto legacy = upic::guerreroio::fromVar (w);
            t.check (! legacy.malformed && legacy.version == 1,
                     "Guerrero: an absent version is accepted as legacy version 1");
        }
    }
#else
    juce::ignoreUnused (t);
#endif
}

//==============================================================================
// The Guerrero parameter dialog: recipe round trip and its transactions.
//
// Deliberately a SEPARATE function from runGuerreroChecks(). That function is
// already near the stack limit after constructing a dozen MainComponents, and
// adding one more inside it overflowed the stack (0xC00000FD). Keeping these
// checks at the same level as the caller gives them their own frame.
//
// The provenance lookup below keeps every juce::var in a named local. That is
// for clarity of ownership rather than a proven lifetime defect: JUCE's variant
// arrays are reference counted, and the crash that prompted this function was
// established to be stack exhaustion (it disappeared when the checks moved out
// of runGuerreroChecks()) - not a use-after-free. No sanitizer run has
// confirmed a lifetime fault here.
#if GUERRERO_INTEGRATION
namespace {

bool findGuerreroProvenance (const juce::var& piece, int& idOut,
                             upic::GuerreroProvenance& out)
{
    const juce::var pagesVar = piece.getProperty ("pages", juce::var());
    auto* pages = pagesVar.getArray();
    if (pages == nullptr || pages->isEmpty()) return false;

    const juce::var arcsVar = pages->getReference (0).getProperty ("arcs", juce::var());
    auto* arcs = arcsVar.getArray();
    if (arcs == nullptr) return false;

    for (int i = 0; i < arcs->size(); ++i)
    {
        const juce::var a = arcs->getReference (i);
        const juce::var g = a.getProperty ("guerrero", juce::var());
        if (! g.isObject()) continue;
        out = upic::guerreroio::fromVar (g);
        idOut = (int) a.getProperty ("id", -1);
        return true;
    }
    return false;
}

} // anonymous namespace
#endif

void upic::runGuerreroDialogChecks (SelfTest& t)
{
#if GUERRERO_INTEGRATION
    auto baseRecipe = []
    {
        upic::GuerreroRecipe r;
        r.lower.roughness = 0.35;
        r.lower.depth     = 7;
        r.lower.freqLo    = 110.0;
        r.lower.freqHi    = 880.0;
        r.lower.timeStart = 0.0;
        r.lower.timeEnd   = 16.0;
        r.upper = r.lower;
        r.upper.freqLo = 880.0;
        r.upper.freqHi = 1760.0;
        r.seed.roughness = 0.55;
        r.seed.depth     = 7;
        r.lowerSeed = 1000u;
        r.upperSeed = 1001u;
        r.seedSeed  = 1002u;
        r.bpm = 120.0;
        r.maxPoints = 1000;
        return r;
    };

    // --- a saved recipe outside the dialog's preferred defaults ------------
    // 0.5 BPM, depth 20 and maxPoints 2000 are all valid in the persisted
    // format, so opening and pressing Apply must not reject them.
    {
        upic::GuerreroRecipe slow = baseRecipe();
        slow.bpm = 0.5;
        slow.maxPoints = 2000;
        slow.lower.depth = 20;
        slow.upper.depth = 20;
        slow.seed.depth = 20;

        GuerreroParamOverlay dlg;
        dlg.recipe = slow;
        dlg.loadFromRecipe();

        upic::GuerreroRecipe out;
        juce::String err;
        t.check (dlg.collect (out, err),
                 "Guerrero dialog: a saved 0.5 BPM / 2000 point recipe stays editable", err);
        t.check (out.bpm == 0.5 && out.maxPoints == 2000 && out.lower.depth == 20,
                 "Guerrero dialog: those saved values survive the round trip");
    }

    // --- underflow is rejected, not turned into zero ----------------------
    {
        GuerreroParamOverlay dlg;
        dlg.recipe = baseRecipe();
        dlg.loadFromRecipe();
        dlg.setTextForTest ("Lower roughness (0-8)", "1e-999");

        upic::GuerreroRecipe out;
        juce::String err;
        t.check (! dlg.collect (out, err),
                 "Guerrero dialog: an underflowing number is rejected");
        t.check (out.lower.roughness == 0.35,
                 "Guerrero dialog: the rejected underflow leaves the value intact");
    }

    // --- an identical recipe is a no-op, not a new revision ---------------
    {
        upic::GuerreroRecipe r = baseRecipe();
        r.lowerSeed = 2000u;
        r.upperSeed = 2001u;
        r.seedSeed  = 2002u;

        MainComponent mcN;
        mcN.setSize (1280, 800);

        juce::String err;
        t.check (mcN.applyGuerreroRecipe (0, r, err),
                 "Guerrero: the base recipe creates an arc", err);

        int id = -1;
        upic::GuerreroProvenance p1;
        {
            const juce::var piece = mcN.pieceToVar();
            t.check (findGuerreroProvenance (piece, id, p1),
                     "Guerrero: the created arc carries provenance");
        }

        err.clear();
        t.check (mcN.applyGuerreroRecipe (id, r, err),
                 "Guerrero: applying an identical recipe reports success", err);

        upic::GuerreroProvenance p2;
        {
            const juce::var piece = mcN.pieceToVar();
            findGuerreroProvenance (piece, id, p2);
        }

        t.check (p2.revision == p1.revision,
                 "Guerrero: an identical recipe does not bump the revision",
                 juce::String (p1.revision) + " -> " + juce::String (p2.revision));
        t.check (p2.geometryHash == p1.geometryHash,
                 "Guerrero: an identical recipe leaves the geometry untouched");
    }

    // --- a stale deferred close must not destroy a newer dialog -----------
    {
        MainComponent mcT;
        mcT.setSize (1280, 800);

        mcT.openGuerreroDialog (0);
        const int firstGeneration = mcT.guerreroOverlayGenerationForTest();
        t.check (firstGeneration > 0, "Guerrero: the first dialog has an identity");

        mcT.openGuerreroDialog (0);
        const int secondGeneration = mcT.guerreroOverlayGenerationForTest();
        t.check (secondGeneration != firstGeneration,
                 "Guerrero: a second dialog gets a different identity");

        mcT.closeGuerreroDialog (firstGeneration);   // the stale request
        t.check (mcT.guerreroOverlay != nullptr,
                 "Guerrero: a stale close leaves the newer dialog open");

        mcT.closeGuerreroDialog (secondGeneration);
        t.check (mcT.guerreroOverlay == nullptr,
                 "Guerrero: the matching close removes the dialog");
    }

    // --- an EDITED value travels from the editor into the model -----------
    {
        upic::GuerreroRecipe r = baseRecipe();
        r.lowerSeed = 3000u;
        r.upperSeed = 3001u;
        r.seedSeed  = 3002u;

        GuerreroParamOverlay dlg;
        dlg.recipe = r;
        dlg.loadFromRecipe();
        dlg.setTextForTest ("Lower roughness (0-8)", "0.42");
        dlg.setTextForTest ("Upper envelope seed", "12345");

        upic::GuerreroRecipe edited;
        juce::String err;
        t.check (dlg.collect (edited, err),
                 "Guerrero dialog: edited values are accepted", err);
        t.check (edited.lower.roughness == 0.42,
                 "Guerrero dialog: the edited roughness reaches the recipe");
        t.check (edited.upperSeed == 12345u,
                 "Guerrero dialog: the edited seed reaches the recipe");
        t.check (edited.lowerSeed == 3000u && edited.seedSeed == 3002u,
                 "Guerrero dialog: the other seeds are untouched by the edit");

        MainComponent mcE;
        mcE.setSize (1280, 800);
        t.check (mcE.applyGuerreroRecipe (0, edited, err),
                 "Guerrero: the edited recipe applies to the model", err);

        upic::GuerreroProvenance pE;
        int idE = -1;
        {
            const juce::var piece = mcE.pieceToVar();
            findGuerreroProvenance (piece, idE, pE);
        }

        t.check (std::abs (pE.recipe.lower.roughness - 0.42) < 1e-12,
                 "Guerrero: the model stores the edited roughness");
        t.check (pE.recipe.upperSeed == 12345u,
                 "Guerrero: the model stores the edited seed");
        t.check (pE.hasFingerprint && pE.pointCount > 1,
                 "Guerrero: the edited arc has geometry and a fingerprint");
    }

    // --- a six-digit point limit is enterable ------------------------------
    {
        GuerreroParamOverlay dlg;
        dlg.recipe = baseRecipe();
        dlg.loadFromRecipe();
        dlg.setTextForTest ("Max points per arc", "100000");

        upic::GuerreroRecipe out;
        juce::String err;
        t.check (dlg.collect (out, err),
                 "Guerrero dialog: a 100000 point limit is accepted", err);
        t.check (out.maxPoints == 100000,
                 "Guerrero dialog: the six-digit point limit survives");
    }

    // --- a non-default saved recipe applies end to end ---------------------
    // 0.5 BPM at 0.2 beats is 24 s, inside the 30 s page, so this exercises
    // generation, the transaction and undo rather than only collect().
    {
        MainComponent mcS;
        mcS.setSize (1280, 800);

        const upic::GuerreroRecipe ordinary = baseRecipe();
        juce::String err;
        t.check (mcS.applyGuerreroRecipe (0, ordinary, err),
                 "Guerrero: an ordinary recipe creates the arc", err);

        int id = -1;
        upic::GuerreroProvenance before;
        {
            const juce::var piece = mcS.pieceToVar();
            findGuerreroProvenance (piece, id, before);
        }
        const uint64_t hashBefore = before.geometryHash;
        const int revBefore = before.revision;

        upic::GuerreroRecipe slow = ordinary;
        slow.bpm = 0.5;
        slow.maxPoints = 2000;
        slow.lower.depth = 20;
        slow.upper.depth = 20;
        slow.seed.depth = 20;
        slow.lower.timeStart = 0.0;
        slow.lower.timeEnd   = 0.2;
        slow.upper.timeStart = 0.0;
        slow.upper.timeEnd   = 0.2;
        // 10000 seed knots against 2000 points needs the downsample policy;
        // with Reject the planner correctly refuses rather than truncating.
        slow.pointsPolicy = upic::GuerreroPointPolicy::Downsample;
        slow.lowerSeed = 5000u;
        slow.upperSeed = 5001u;
        slow.seedSeed  = 5002u;

        err.clear();
        t.check (mcS.applyGuerreroRecipe (id, slow, err),
                 "Guerrero: a 0.5 BPM / depth 20 recipe applies to an arc", err);

        upic::GuerreroProvenance after;
        {
            const juce::var piece = mcS.pieceToVar();
            findGuerreroProvenance (piece, id, after);
        }
        t.check (after.recipe.bpm == 0.5 && after.recipe.maxPoints == 2000
                     && after.recipe.lower.depth == 20,
                 "Guerrero: the non-default recipe is stored");
        t.check (after.geometryHash != hashBefore,
                 "Guerrero: the non-default recipe produced new geometry");
        t.check (after.revision == revBefore + 1,
                 "Guerrero: the non-default apply bumped the revision");

        mcS.undo();
        upic::GuerreroProvenance undone;
        {
            const juce::var piece = mcS.pieceToVar();
            findGuerreroProvenance (piece, id, undone);
        }
        t.check (undone.recipe.bpm == ordinary.bpm && undone.geometryHash == hashBefore,
                 "Guerrero: undo restores the ordinary recipe and geometry");

        mcS.redo();
        upic::GuerreroProvenance redone;
        {
            const juce::var piece = mcS.pieceToVar();
            findGuerreroProvenance (piece, id, redone);
        }
        t.check (redone.recipe.bpm == 0.5,
                 "Guerrero: redo restores the non-default recipe");
    }

    // --- an unchanged apply must return BEFORE any generation -------------
    // The stored recipe is replaced (geometry and fingerprint untouched) by one
    // whose 16 beats at 0.5 BPM span 1920 s and cannot fit the 30 s page.
    // Applying that recipe unchanged has to succeed, because equality is
    // decided before generation; changing it must fail, which proves that
    // generation runs only for a real change.
    {
        MainComponent mcO;
        mcO.setSize (1280, 800);

        const upic::GuerreroRecipe ordinary = baseRecipe();
        juce::String err;
        t.check (mcO.applyGuerreroRecipe (0, ordinary, err),
                 "Guerrero: the ordering test arc is created", err);

        int id = -1;
        upic::GuerreroProvenance p;
        {
            const juce::var piece = mcO.pieceToVar();
            findGuerreroProvenance (piece, id, p);
        }
        const int revBefore = p.revision;

        upic::GuerreroRecipe tooLong = ordinary;
        tooLong.bpm = 0.5;
        tooLong.lower.timeEnd = 16.0;
        tooLong.upper.timeEnd = 16.0;
        tooLong.pointsPolicy = upic::GuerreroPointPolicy::Downsample;

        t.check (mcO.setGuerreroRecipeForTest (id, tooLong),
                 "Guerrero: the stored recipe was overridden for the ordering test");

        err.clear();
        t.check (mcO.applyGuerreroRecipe (id, tooLong, err),
                 "Guerrero: an unchanged recipe returns before generation", err);

        upic::GuerreroProvenance p2;
        {
            const juce::var piece = mcO.pieceToVar();
            findGuerreroProvenance (piece, id, p2);
        }
        t.check (p2.revision == revBefore,
                 "Guerrero: the unchanged apply did not bump the revision");

        // The same recipe with one seed changed is a REAL change, so generation
        // runs and the page-fit check refuses it.
        upic::GuerreroRecipe changed = tooLong;
        changed.lowerSeed += 77u;
        changed.upperSeed += 77u;
        changed.seedSeed  += 77u;

        err.clear();
        t.check (! mcO.applyGuerreroRecipe (id, changed, err),
                 "Guerrero: a changed recipe that cannot fit the page is refused");
        t.check (err.containsIgnoreCase ("fit"),
                 "Guerrero: the refusal says it does not fit the page", err);
    }

    // --- an Apply must not follow a page change ---------------------------
    // Arc ids are page-local, and playback can advance the active page while the
    // dialog is open. A copy of the page carries the SAME ids, so without the
    // page identity guard an edit would land on the wrong page's arc.
    {
        MainComponent mcG;
        mcG.setSize (1280, 800);

        const upic::GuerreroRecipe r = baseRecipe();
        juce::String err;
        t.check (mcG.applyGuerreroRecipe (0, r, err),
                 "Guerrero: the page-identity arc is created", err);

        int id = -1;
        upic::GuerreroProvenance p;
        {
            const juce::var piece = mcG.pieceToVar();
            findGuerreroProvenance (piece, id, p);
        }
        const uint64_t hashOnFirstPage = p.geometryHash;

        mcG.openGuerreroDialog (id);
        mcG.addPageCopyForTest();   // same arc ids, different page

        auto* overlay = dynamic_cast<GuerreroParamOverlay*> (mcG.guerreroOverlay.get());
        t.check (overlay != nullptr,
                 "Guerrero: the dialog is open for the page-identity test");

        if (overlay != nullptr && overlay->onApply != nullptr)
        {
            upic::GuerreroRecipe changed = r;
            changed.lowerSeed = 9999u;

            juce::String applyError;
            const bool accepted = overlay->onApply (changed, applyError);

            t.check (! accepted,
                     "Guerrero: Apply is refused after the active page changed");
            t.check (applyError.containsIgnoreCase ("page"),
                     "Guerrero: the refusal names the page", applyError);
        }

        t.check (mcG.guerreroArcHashForTest (id) == hashOnFirstPage,
                 "Guerrero: the other page's arc was not modified");

        mcG.closeGuerreroDialog();
    }

    // --- a piece replacement at the same index must also be refused --------
    // The page index, arc count and duration can all match a different piece,
    // so the edit context also records the page epoch, which loadFromVar()
    // bumps when it swaps the pages.
    {
        MainComponent mcR;
        mcR.setSize (1280, 800);

        const upic::GuerreroRecipe r = baseRecipe();
        juce::String err;
        t.check (mcR.applyGuerreroRecipe (0, r, err),
                 "Guerrero: the replacement test arc is created", err);

        int id = -1;
        upic::GuerreroProvenance p;
        {
            const juce::var piece = mcR.pieceToVar();
            findGuerreroProvenance (piece, id, p);
        }

        mcR.openGuerreroDialog (id);

        // Same index, same arc count, same duration, same arc id - only the
        // geometry differs, so nothing but the epoch can tell them apart.
        // Same index, same arc count, same duration, same arc id - only the
        // geometry differs, so nothing but the epoch can tell them apart.
        // var copies share their array and object storage, so mutating the
        // point through the pointer below is visible in `other`.
        juce::var other = mcR.pieceToVar();
        {
            const juce::var pagesVar = other.getProperty ("pages", juce::var());
            auto* pages = pagesVar.getArray();
            if (pages != nullptr && ! pages->isEmpty())
            {
                const juce::var arcsVar = pages->getReference (0).getProperty ("arcs", juce::var());
                auto* arcs = arcsVar.getArray();
                if (arcs != nullptr && ! arcs->isEmpty())
                {
                    const juce::var ptsVar = arcs->getReference (0).getProperty ("points", juce::var());
                    auto* pts = ptsVar.getArray();
                    if (pts != nullptr && ! pts->isEmpty())
                        if (auto* obj = pts->getReference (0).getDynamicObject())
                            obj->setProperty ("f", 654.321);
                }
            }
        }

        t.check (mcR.loadFromVar (other),
                 "Guerrero: a different piece with the same page shape loads");

        const uint64_t hashAfterLoad = mcR.guerreroArcHashForTest (id);

        auto* overlay = dynamic_cast<GuerreroParamOverlay*> (mcR.guerreroOverlay.get());
        t.check (overlay != nullptr,
                 "Guerrero: the dialog survives the piece replacement");

        if (overlay != nullptr && overlay->onApply != nullptr)
        {
            upic::GuerreroRecipe changed = r;
            changed.lowerSeed = 777u;

            juce::String applyError;
            const bool accepted = overlay->onApply (changed, applyError);

            t.check (! accepted,
                     "Guerrero: Apply is refused after the piece was replaced");
            t.check (applyError.containsIgnoreCase ("page"),
                     "Guerrero: the replacement refusal names the page", applyError);
        }

        t.check (mcR.guerreroArcHashForTest (id) == hashAfterLoad,
                 "Guerrero: the replacement piece's arc was not modified");
    }
#else
    juce::ignoreUnused (t);
#endif
}
