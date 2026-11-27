#pragma once

//==============================================================================
// R4.1: the piece's file format - the edge.
//
// The piece is the model's data (Source/Piece.h). How a piece reaches a
// file, and how it comes back, is a different thing: this is the edge,
// and the JUCE value types live here - the same place every other format
// in the app lives (Source/PageCodec.h and its neighbours).
//
// The read side is the pipeline the review describes (section 32): decode,
// migrate, validate, resolve the references and build the candidate. The
// candidate is handed over; the caller puts it in force, and a document
// that is not a piece leaves the caller's piece alone.
//
// The plan defers the type migration (section 2), so the diagnostics
// carry the JUCE types the edge already uses. The wording of the two
// messages does not change by a character: they are the strings the
// loader showed before, and the suite (Source/SelfTestPieceCodec.cpp)
// pins them.
#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Piece.h"

#include "ChannelCodec.h"
#include "EnvelopeCodec.h"
#include "Ids.h"
#include "PageCodec.h"
#include "PerformanceCodec.h"
#include "PieceLimits.h"
#include "SampleCodec.h"
#include "TuningCodec.h"
#include "UpicConstants.h"
#include "WaveformCodec.h"

namespace upic
{
    //================ what the loader found out ==============================
    //
    // The review asks for structured load errors rather than a single
    // success flag (section 32). The plan defers the type migration
    // (section 2), so the diagnostics carry the JUCE types the edge
    // already uses: the two messages are the strings the app shows, and
    // the counts are what is behind them.
    struct PieceDiagnostics
    {
        // the document was a piece
        bool ok = false;
        // "This piece is bigger than the app supports - not loaded: ..."
        // (empty when there was nothing to say)
        juce::String notLoaded;
        // "This piece was adjusted: ..." (empty when there was nothing to say)
        juce::String adjusted;
        // the ids that had to be renumbered
        int renamedIds = 0;
        // the references that had to be put back
        int fixedRefs = 0;
        // the items that did not make it in (the samples, the pages, the arcs...)
        int droppedItems = 0;
        // the sample frames that did not make it in
        int droppedFrames = 0;
    };

    //================ the document as a var -> the candidate =================
    //
    // Nothing of the running app is touched: the caller commits the
    // candidate, and a document that is not a piece leaves the caller's
    // piece alone.
    inline bool pieceFromVar (const juce::var& v, PieceModel& out, PieceDiagnostics& diag)
    {
        diag = PieceDiagnostics();
        // the candidate is built in a local one and handed over only when
        // the document really is a piece: a document that is refused leaves
        // the caller's piece alone (M25).

        // v1.33.4 (M25): the piece is parsed into a candidate first, and it
        // replaces the current one only when it really is a piece. The old
        // code cleared the model before it parsed, so a file that was not a
        // piece (or a host state that was rubbish) left an empty default
        // page behind and the unsaved work was gone.
        const auto pagesVar = v.getProperty ("pages", juce::var());
        auto* pagesArray = pagesVar.getArray();
        if (v.isVoid() || pagesArray == nullptr || pagesArray->isEmpty())
            return false;

        // --- the document is read into temporaries (the caller's piece is
        //     not touched yet, so a document that fails leaves it alone) ---
        std::vector<Waveform> newWaves;
        std::vector<Envelope> newEnvelopes;
        std::vector<AmplitudeTable> newAmpTables;
        std::vector<PerformanceSequence> newSequences;
        std::vector<Channel> newChannels;
        std::vector<Page> newPages;
        std::vector<SampleSlot> newSlots;
        for (int i = 0; i < upic::kMaxSampleSlots; ++i)
        {
            SampleSlot s;
            s.id = i + 1;
            newSlots.push_back (std::move (s));
        }
        // v1.33.4 (H13): the piece format caps the page, arc, zone and point
        // counts. A piece over a cap used to lose the extra music silently on
        // the next open; the user is told what did not make it in.
        juce::String droppedMsg;
        // M5: the piece was adjusted, not truncated (the arcs' own Refs gave
        // way to the material's Ref). It gets its own message.
        juce::String adjustedMsg;
        // v1.33.4 (L13): how many ids had to be changed (a duplicate or an
        // impossible one) and how many material references pointed at nothing.
        int renamed = 0;
        int fixedRefs = 0;
        int droppedItems = 0;
        auto noteDrop = [&droppedMsg, &droppedItems] (const juce::String& one, const juce::String& many, int n)
        {
            if (n > 0)
            {
                droppedItems += n;
                droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(n) + " "
                                + (n == 1 ? one : many);
            }
        };
        if (auto* arr = v.getProperty("samples", juce::var()).getArray())
        {
            // SECURITY (S-03): the decoded sample data of all the slots together
            // is capped, so a piece with many huge slots cannot exhaust memory.
            size_t totalSampleBytes = 0;
            int stored = 0;
            int droppedFrames = 0;
            for (auto& sv : *arr)
            {
                auto slot = upic::sampleSlotFromVar (sv, 1);
                // v1.33.4 (M19): the frames the slot lost to the caps (the
                // duration at the material's own rate, the budget) are counted
                // too, so the user hears about the cut material.
                const int rawFrames = (int) sv.getProperty("frames", 0);
                const int keptFrames = slot.data != nullptr ? slot.data->getNumSamples() : 0;
                droppedFrames += juce::jmax (0, rawFrames - keptFrames);
                if (slot.data != nullptr)
                    totalSampleBytes += (size_t) slot.data->getNumSamples()
                                      * (size_t) slot.data->getNumChannels() * sizeof(float);
                if (totalSampleBytes > upic::limits::kMaxTotalSampleBytes) break;
                for (auto& s : newSlots)
                    if (s.id == slot.id)
                    {
                        // the first entry for a slot id wins (a file that
                        // carries the same slot twice must not overwrite the
                        // first one)
                        if (s.data == nullptr) { s = std::move (slot); ++stored; }
                        break;
                    }
            }
            noteDrop ("sample", "samples", (int) arr->size() - stored);
            noteDrop ("sample frame", "sample frames", droppedFrames);
            diag.droppedFrames = droppedFrames;
        }
        if (auto* arr = v.getProperty("waveforms", juce::var()).getArray())
        {
            int i = 0;
            for (auto& wv : *arr)
            {
                if ((int) newWaves.size() >= upic::limits::kMaxWaveforms) break;
                newWaves.push_back (upic::waveformFromVar (wv, i++));
            }
            noteDrop ("wavetable", "wavetables", (int) arr->size() - (int) newWaves.size());
        }
        if (auto* arr = v.getProperty("envelopes", juce::var()).getArray())
        {
            int i = 0;
            for (auto& ev : *arr)
            {
                if ((int) newEnvelopes.size() >= upic::limits::kMaxEnvelopes) break;
                newEnvelopes.push_back (upic::envelopeFromVar (ev, i++));
            }
            noteDrop ("envelope", "envelopes", (int) arr->size() - (int) newEnvelopes.size());
            // the envelope's own points are capped too
            for (int ei = 0; ei < (int) newEnvelopes.size() && ei < (int) arr->size(); ++ei)
                if (auto* ptsArr = arr->getReference(ei).getProperty("points", juce::var()).getArray())
                    noteDrop ("envelope point", "envelope points",
                              (int) ptsArr->size() - (int) newEnvelopes[(size_t) ei].points.size());
        }
        // WP2: the amplitude conversion tables. Absent in older pieces, and an
        // arc that names one must get its reference checked like any other.
        if (auto* arr = v.getProperty("ampTables", juce::var()).getArray())
        {
            int i = 0;
            for (auto& tv : *arr)
            {
                if ((int) newAmpTables.size() >= upic::limits::kMaxAmplitudeTables) break;
                newAmpTables.push_back (upic::envelopeFromVar (tv, i++));
            }
            noteDrop ("amplitude table", "amplitude tables",
                      (int) arr->size() - (int) newAmpTables.size());
        }
        // WP4: the performance sequences (absent in older pieces).
        if (auto* sarr = v.getProperty("sequences", juce::var()).getArray())
        {
            int si = 0;
            for (auto& sv : *sarr)
            {
                if ((int) newSequences.size() >= upic::limits::kMaxSequences) break;
                newSequences.push_back (upic::performanceSequenceFromVar (sv, si++));
            }
            noteDrop ("performance sequence", "performance sequences",
                      (int) sarr->size() - (int) newSequences.size());
        }
        // WP6: the mixer channels (absent in older pieces -> the legacy path).
        const int fileNextChannelId = (int) v.getProperty ("nextChannelId", 1);
        if (auto* carr = v.getProperty ("channels", juce::var()).getArray())
        {
            int ci = 0;
            for (auto& cv : *carr)
            {
                if ((int) newChannels.size() >= upic::limits::kMaxChannels) break;
                newChannels.push_back (upic::channelFromVar (cv, ci++));
            }
            noteDrop ("channel", "channels", (int) carr->size() - (int) newChannels.size());
        }
        renamed += upic::uniquifyIds (newChannels);
        if (auto* arr = v.getProperty("pages", juce::var()).getArray())
        {
            for (auto& pv : *arr)
            {
                if ((int) newPages.size() >= upic::limits::kMaxPages) break;
                // v1.33.4 (L13): the page's parser checks the arc and zone ids
                // (a duplicate id made the selection and the delete hit the
                // wrong arc) and derives the next id from the arcs that are
                // there. `renamed` counts what it had to change.
                newPages.push_back (upic::pageFromVar(pv, &renamed));
            }
            noteDrop ("page", "pages", (int) arr->size() - (int) newPages.size());
            // the arcs, the sieve zones and the arc points of the pages that
            // were loaded (the page's own parser caps them)
            for (int pi = 0; pi < (int) newPages.size(); ++pi)
            {
                const juce::var pageVar = arr->getReference(pi);
                if (auto* arcsArr = pageVar.getProperty("arcs", juce::var()).getArray())
                {
                    noteDrop ("arc", "arcs", (int) arcsArr->size() - (int) newPages[(size_t) pi].arcs.size());
                    for (int ai = 0; ai < (int) newPages[(size_t) pi].arcs.size(); ++ai)
                        if (auto* ptsArr = arcsArr->getReference(ai).getProperty("points", juce::var()).getArray())
                            noteDrop ("arc point", "arc points",
                                      (int) ptsArr->size() - (int) newPages[(size_t) pi].arcs[(size_t) ai].points.size());
                }
                if (auto* zonesArr = pageVar.getProperty("zones", juce::var()).getArray())
                    noteDrop ("sieve zone", "sieve zones",
                              (int) zonesArr->size() - (int) newPages[(size_t) pi].zones.size());
            }
        }
        if (newWaves.empty()) newWaves = upic::defaultWaveforms();
        if (newEnvelopes.empty()) newEnvelopes = upic::defaultEnvelopes();
        if (newPages.empty()) newPages.push_back (upic::Page());

        // M5: the material's Ref is the SLOT's (the UPIC's per-slot REF). A
        // piece written before M5 carries no Ref on the slot - the Ref lived
        // on the arc - so the slot inherits the Ref of the first arc that
        // uses it. The piece then sounds exactly as it did, unless an arc
        // carried a Ref of its own (a different one) - the material's Ref
        // decides now, and the user is told.
        int ownRefArcs = 0;
        for (auto& s : newSlots)
        {
            if (s.refFreq > 0.0) continue;
            double ref = 0.0;
            for (const auto& pg : newPages)
            {
                for (const auto& a : pg.arcs)
                {
                    if (a.mode != 1 || a.sampleId != s.id) continue;
                    ref = a.refFreq;
                    break;
                }
                if (ref > 0.0) break;
            }
            s.refFreq = (ref > 0.0) ? upic::limits::clampFinite (ref, 20.0, 20000.0)
                                    : upic::kDefaultRefFreq;
        }
        // an arc whose Ref differs from its material's is now played with the
        // material's Ref - say so, so the change is never silent.
        for (const auto& pg : newPages)
            for (const auto& a : pg.arcs)
            {
                if (a.mode != 1) continue;
                for (const auto& s : newSlots)
                    if (s.id == a.sampleId && s.refFreq > 0.0
                        && std::abs (1200.0 * std::log2 (a.refFreq / s.refFreq)) > 1.0)
                    { ++ownRefArcs; break; }
            }
        if (ownRefArcs > 0)
            adjustedMsg = juce::String (ownRefArcs) + (ownRefArcs == 1 ? " arc had a Ref of its own"
                                                                       : " arcs had Refs of their own")
                    + " - the material's Ref decides now";

        // v1.33.4 (L13): the wavetables and the envelopes are keyed by their id
        // too - two wavetables with the same id made the arc list show the
        // wrong name, and the editor's controls edited the wrong one. The first
        // item of an id keeps it (so the arcs that point at the material keep
        // pointing at the same thing), a duplicate gets a free id.
        renamed += upic::uniquifyIds (newWaves);
        renamed += upic::uniquifyIds (newEnvelopes);
        renamed += upic::uniquifyIds (newAmpTables);
        renamed += upic::uniquifyIds (newSequences);

        // and the arcs' references to the material are checked: a piece can
        // point at a wavetable or an envelope that is not there (the arc then
        // showed an empty material and played the wrong thing), so the
        // reference is put back onto material that exists.
        for (auto& pg : newPages)
            for (auto& a : pg.arcs)
            {
                if (! newWaves.empty() && ! upic::hasId (newWaves, a.waveformId))
                { a.waveformId = newWaves.front().id; ++fixedRefs; }
                if (! newEnvelopes.empty() && ! upic::hasId (newEnvelopes, a.envelopeId))
                { a.envelopeId = newEnvelopes.front().id; ++fixedRefs; }
                // a table is optional: a missing one falls back to none
                if (a.amplitudeTableId >= 0 && ! upic::hasId (newAmpTables, a.amplitudeTableId))
                { a.amplitudeTableId = -1; ++fixedRefs; }
                // 0 is the "no envelope of its own" marker for the PM index
                if (a.pmIndexEnv != 0 && ! upic::hasId (newEnvelopes, a.pmIndexEnv))
                { a.pmIndexEnv = 0; ++fixedRefs; }
            }
        // WP4: the sequences' pages and positions are checked against the
        // pages that actually loaded.
        for (auto& sq : newSequences)
        {
            std::vector<upic::PerformancePoint> kept;
            double lastT = -1.0;
            for (auto p : sq.points)
            {
                if (newPages.empty()) break;
                p.page = juce::jlimit (0, (int) newPages.size() - 1, p.page);
                p.pos = upic::limits::clampFinite (p.pos, 0.0,
                                                   newPages[(size_t) p.page].durationSec);
                if (p.t <= lastT) continue;
                lastT = p.t;
                kept.push_back (p);
            }
            sq.points = std::move (kept);
        }

        // WP3: the groups' overrides point into the same banks; a missing one
        // falls back to "the arc's own" rather than switching the timbre to
        // unrelated material.
        for (auto& pg : newPages)
            for (auto& g : pg.groups)
            {
                if (g.waveformId >= 0 && ! upic::hasId (newWaves, g.waveformId))
                { g.waveformId = -1; ++fixedRefs; }
                if (g.envelopeId >= 0 && ! upic::hasId (newEnvelopes, g.envelopeId))
                { g.envelopeId = -1; ++fixedRefs; }
                if (g.amplitudeTableId >= 0 && ! upic::hasId (newAmpTables, g.amplitudeTableId))
                { g.amplitudeTableId = -1; ++fixedRefs; }
            }
        // WP5: a modulator must exist on its own page and the chain must not
        // loop back to the carrier.
        for (auto& pg : newPages)
        {
            auto findArc = [&pg] (int id) -> const upic::Arc*
            {
                for (auto& a : pg.arcs) if (a.id == id) return &a;
                return nullptr;
            };
            for (auto& a : pg.arcs)
            {
                if (a.modulatorArcId < 0) continue;
                if (a.modulatorArcId == a.id || findArc (a.modulatorArcId) == nullptr)
                { a.modulatorArcId = -1; ++fixedRefs; continue; }
                bool cycle = false;
                std::vector<int> chain;
                int cur = a.modulatorArcId;
                for (int guard = 0; guard < 64 && cur >= 0; ++guard)
                {
                    if (cur == a.id) { cycle = true; break; }
                    chain.push_back (cur);
                    const upic::Arc* m = findArc (cur);
                    cur = (m != nullptr) ? m->modulatorArcId : -1;
                }
                if (cycle)
                {
                    // break every edge of the loop, not just this arc's
                    a.modulatorArcId = -1;
                    for (int id : chain)
                        for (auto& other : pg.arcs)
                            if (other.id == id) other.modulatorArcId = -1;
                    ++fixedRefs;
                }
            }
        }
        // WP6: a channel reference must exist in the bank (0 = main).
        for (auto& pg : newPages)
        {
            for (auto& a : pg.arcs)
                if (a.channelId > 0 && ! upic::hasId (newChannels, a.channelId))
                { a.channelId = 0; ++fixedRefs; }
            for (auto& g : pg.groups)
                if (g.channelId > 0 && ! upic::hasId (newChannels, g.channelId))
                { g.channelId = -1; ++fixedRefs; }
        }
        if (renamed > 0)
            droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(renamed)
                    + (renamed == 1 ? " item had a duplicate id and has been renumbered"
                                    : " items had duplicate ids and have been renumbered");
        if (fixedRefs > 0)
            droppedMsg += (droppedMsg.isEmpty() ? "" : ", ") + juce::String(fixedRefs)
                    + (fixedRefs == 1 ? " arc pointed at material that was not there"
                                      : " arcs pointed at material that was not there");

        // WP9: the workspace layout comes back with the piece. The pages are
        // checked against the pages that actually loaded (a piece can name a
        // page that is not there).
        std::vector<int> workspacePageList;
        double workspaceSplit = 0.5;
        const auto w = v.getProperty ("workspace", juce::var());
        if (w.isObject())
        {
            if (auto* arr = w.getProperty ("pages", juce::var()).getArray())
                for (const auto& p : *arr)
                {
                    const int index = (int) p;
                    if (index >= 0 && index < (int) newPages.size())
                        workspacePageList.push_back (index);
                }
            workspaceSplit = juce::jlimit (0.2, 0.8,
                                           (double) w.getProperty ("split", 0.5));
        }

        // --- the settings the document carries -----------------------------
        // The document's own numbers are read here, with the defaults and
        // the clamps the loader always applied, so the candidate is a
        // complete piece.
        PieceModel m;
        m.waveforms = std::move (newWaves);
        m.envelopes = std::move (newEnvelopes);
        m.amplitudeTables = std::move (newAmpTables);
        m.sequences = std::move (newSequences);
        m.channels = std::move (newChannels);
        m.pages = std::move (newPages);
        m.sampleSlots = std::move (newSlots);
        m.nextChannelId = 1;
        for (auto& c : m.channels) m.nextChannelId = juce::jmax (m.nextChannelId, c.id + 1);
        if (fileNextChannelId > m.nextChannelId && fileNextChannelId <= upic::limits::kMaxItemId)
            m.nextChannelId = fileNextChannelId;
        m.currentPage = juce::jlimit (0, (int) m.pages.size() - 1, (int) v.getProperty("currentPage", 0));
        // SECURITY (S-03): the master gain is clamped to the slider's range, so a
        // piece cannot push the mix into clipping or silence.
        m.masterGain = upic::limits::clampGain ((float) (double) v.getProperty ("master", 0.8));
        // v1.1 state (defaults keep old / Python-written files working unchanged)
        m.seqPages = (bool) v.getProperty ("seqPages", false);
        m.recordPressure = (bool) v.getProperty ("recordPressure", false);
        m.scrubAudition = (bool) v.getProperty ("scrubAudition", true);
        m.muteWhileDragging = (bool) v.getProperty ("muteWhileDragging", true);
        m.dragSmoothMs = juce::jlimit (0, 1000, (int) v.getProperty ("dragSmoothMs", 20));
        m.scrubGrainMs = juce::jlimit (5, 500, (int) v.getProperty ("scrubGrainMs", 40));
        m.sampleGrainMs = juce::jlimit (5, 500, (int) v.getProperty ("sampleGrainMs", 40));
        m.liveGrainMs = juce::jlimit (5, 500, (int) v.getProperty ("liveGrainMs", 40));
        m.sieveOn = (bool) v.getProperty ("sieveOn", false);
        m.sieveM = juce::jlimit (2, 120, (int) v.getProperty ("sieveM", 12));
        m.sieveR.clear();
        if (auto* arr = v.getProperty ("sieveR", juce::var()).getArray())
        {
            for (auto& rv : *arr)
            {
                if ((int) m.sieveR.size() >= upic::limits::kMaxSieveRemainders) break;
                m.sieveR.push_back((((int) rv % m.sieveM) + m.sieveM) % m.sieveM);
            }
            noteDrop ("sieve remainder", "sieve remainders", (int) arr->size() - (int) m.sieveR.size());
        }
        // v1.33.2: an old settings file can carry an empty sieveR (the field was
        // allowed to be empty, and the placeholder made it look like 0,7). That
        // restored a sieve with no remainders, which is inert - the Sieve
        // checkbox then did nothing. Fall back to the octave sieve.
        if (m.sieveR.empty()) m.sieveR = { 0 };
        // v1.33.4 (H7): the global pitch modes come back with the piece. Absent
        // in an older piece -> the session keeps what it had (backward
        // compatible), present -> the piece's mode wins. A negative mode says
        // "the document carried none".
        const int samplePm = (int) v.getProperty ("samplePitchMode", -1);
        const int livePm = (int) v.getProperty ("livePitchMode", -1);
        m.samplePitchMode = samplePm >= 0 ? juce::jlimit (0, 3, samplePm) : -1;
        m.livePitchMode = livePm >= 0 ? juce::jlimit (0, 2, livePm) : -1;
        // M4: the tuning the piece was drawn in. An old piece carries none ->
        // 12-tone equal temperament, which is what it was drawn in.
        m.tuning = upic::tuningFromVar (v.getProperty ("tuning", juce::var()));
        // v1.2 state (defaults: full stickiness, no time grid = v1.1 behaviour)
        m.stickiness = upic::limits::clampFinite ((double) v.getProperty ("stickiness", 1.0), 0.0, 1.0);
        m.timeGrid = upic::limits::clampFinite ((double) v.getProperty ("timeGrid", 0.0), 0.0, 64.0);
        m.workspacePages = std::move (workspacePageList);
        m.workspaceSplit = workspaceSplit;

        // --- the diagnostics ------------------------------------------------
        if (! droppedMsg.isEmpty())
            diag.notLoaded = "This piece is bigger than the app supports - not loaded: " + droppedMsg;
        if (! adjustedMsg.isEmpty())
            diag.adjusted = "This piece was adjusted: " + adjustedMsg;
        diag.renamedIds = renamed;
        diag.fixedRefs = fixedRefs;
        diag.droppedItems = droppedItems;
        diag.ok = true;

        out = std::move (m);
        return true;
    }

    //================ the piece as a var =====================================
    //
    // The piece as a var. The samples become base64 here, and that is the
    // heavy part of a save - so the caller can run this on a worker thread
    // (v1.33.4 / M28).
    //
    // v1.33.4 (L15, part 3): the piece without the sample audio. The
    // recovery snapshot keeps the audio in a sidecar file, so the piece JSON
    // stays small and the unchanged audio is not embedded again and again.
    inline juce::var pieceToVar (const PieceModel& m, bool includeSampleAudio)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("app", "UPICuitous");
        o->setProperty("version", 1);
        o->setProperty("master", m.masterGain);
        o->setProperty("currentPage", m.currentPage);
        // v1.1 (absent in older / Python-written files -> defaults applied)
        o->setProperty("seqPages", m.seqPages);
        o->setProperty("recordPressure", m.recordPressure);
        o->setProperty("scrubAudition", m.scrubAudition);
        o->setProperty("muteWhileDragging", m.muteWhileDragging);
        o->setProperty("dragSmoothMs", m.dragSmoothMs);
        o->setProperty("scrubGrainMs", m.scrubGrainMs);
        o->setProperty("sampleGrainMs", m.sampleGrainMs);
        o->setProperty("liveGrainMs", m.liveGrainMs);
        o->setProperty("sieveOn", m.sieveOn);
        o->setProperty("sieveM", m.sieveM);
        // v1.33.4 (H7): the global pitch modes change the sound, so they
        // travel with the piece (and with the host state). A candidate that
        // came from a document without a mode has none to write (-1), and
        // the session's mode stands.
        if (m.samplePitchMode >= 0) o->setProperty("samplePitchMode", m.samplePitchMode);
        if (m.livePitchMode >= 0) o->setProperty("livePitchMode", m.livePitchMode);
        juce::Array<juce::var> srArr;
        for (int r : m.sieveR) srArr.add (r);
        o->setProperty("sieveR", srArr);
        // v1.2 (same rule: defaults keep old files working unchanged)
        o->setProperty("stickiness", m.stickiness);
        o->setProperty("timeGrid", m.timeGrid);
        // M4: the tuning. An old piece has none -> 12-tone equal
        // temperament, which is what it was drawn in.
        if (m.tuning.custom)
            o->setProperty("tuning", upic::tuningToVar (m.tuning));
        juce::Array<juce::var> wa, ea, pa, sa;
        for (auto& w : m.waveforms) wa.add (upic::waveformToVar (w));
        for (auto& e : m.envelopes) ea.add (upic::envelopeToVar (e));
        for (auto& s : m.sampleSlots) if (s.loaded()) sa.add (upic::sampleSlotToVar (s, includeSampleAudio));
        for (auto& p : m.pages) pa.add (pageToVar (p));
        o->setProperty("waveforms", wa);
        o->setProperty("envelopes", ea);
        // WP2: omitted for a piece that never made a table, so old files and
        // old hash comparisons are unchanged.
        if (! m.sequences.empty())
        {
            juce::Array<juce::var> sa;
            for (auto& s : m.sequences) sa.add (upic::performanceSequenceToVar (s));
            o->setProperty ("sequences", sa);
        }
        if (! m.amplitudeTables.empty())
        {
            juce::Array<juce::var> ta;
            for (auto& t : m.amplitudeTables) ta.add (upic::envelopeToVar (t));
            o->setProperty ("ampTables", ta);
        }
        // WP6: the channels (omitted when the piece has none).
        if (! m.channels.empty())
        {
            juce::Array<juce::var> ca;
            for (auto& c : m.channels) ca.add (upic::channelToVar (c));
            o->setProperty ("channels", ca);
            o->setProperty ("nextChannelId", m.nextChannelId);
        }
        o->setProperty("samples", sa);
        o->setProperty("pages", pa);
        // WP9: the workspace layout (omitted when there is none, so an old
        // piece is written exactly as it was).
        if (m.workspacePages.size() >= 2)
        {
            auto* w = new juce::DynamicObject();
            juce::Array<juce::var> wpa;
            for (int p : m.workspacePages) wpa.add (p);
            w->setProperty ("pages", wpa);
            w->setProperty ("split", m.workspaceSplit);
            o->setProperty ("workspace", juce::var (w));
        }
        return juce::var (o);
    }

    // The piece with the audio in it (the normal one).
    inline juce::var pieceToVar (const PieceModel& m)
    {
        return pieceToVar (m, true);
    }
}
