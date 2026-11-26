#pragma once

// R2.4: no JUCE header. The page is the model's surface - the arcs, the
// zones, the four frequency spaces, the groups, the ambitus and the page's
// own time - and it is data and rules. The file format is not part of it:
// the codec lives at the edge (Source/PageCodec.h), where the JUCE value
// types are kept.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Arc.h"
#include "ArcGeometry.h"
#include "ArcGroup.h"
#include "Envelope.h"
#include "FreqSpace.h"
#include "Ids.h"
#include "PieceLimits.h"
#include "Sieve.h"
#include "UpicConstants.h"
#include "UpicTheme.h"
#include "Waveform.h"

namespace upic
{
    //---------------- Page ----------------
    struct Page
    {
        double durationSec = 30.0;
        double tempoBpm    = 120.0; // musical time: beats per minute
        std::vector<Arc> arcs;
        int nextArcId = 1;
        std::vector<SieveZone> zones; // magnetic sieve regions (empty = none)
        int nextZoneId = 1;

        //---------------- the page's frequency spaces (v1.33) ----------------
        // The ambitus is the interval the page's vertical axis covers: "the
        // scale of the millimetric surface could be selected, so that the
        // vertical axis could correspond to any interval" (Mache). The
        // default is the interval the clone has always drawn in, so an old
        // piece is unchanged.
        double pitchLo = kDefaultPitchLo;
        double pitchHi = kDefaultPitchHi;

        // The four spaces (the UPIC's four frequency tables). Each is a
        // preset relative to the ambitus; space 0 is the page's own range,
        // which is the identity, so an arc in space 0 sounds as drawn.
        std::array<int, 4> spaces { 0, 0, 0, 0 };

        // WP1: the drawn table of each space. A space with
        // `spaceDefs[i].editable` (or inverted) is mapped by its drawn curve
        // instead of its preset; the preset stays as the starting point the
        // editor opens with. Default entries serialize to nothing, so an old
        // piece is unchanged.
        std::array<FreqSpaceDef, 4> spaceDefs {};

        // WP3: the page's groups. Resolved on the message thread into the
        // render overrides; an empty list (the default) serializes to
        // nothing, so an old piece is unchanged.
        std::vector<ArcGroup> groups;
        int nextGroupId = 1;

        //---------------- the page's acceleration (v1.33) ----------------
        // The UPIC's page could be expanded and contracted in time "with the
        // additional option of acceleration and deceleration" (Robindoré):
        // the page's own time ran at a varying rate. The warp is a tempo
        // sweep across the page: 0 is a straight line (the rate is 1 all
        // the way), a positive value starts slow and speeds up, a negative
        // one starts fast and slows down.
        double warp = 0.0;

        // WP8: the page's own frame for the geometric transforms - the
        // normalised space the drawn curve is turned, mirrored and scaled in.
        ArcFrame frame() const { return ArcFrame { durationSec, pitchLo, pitchHi }; }

        // The rate at which the page's time runs at this point of the page.
        // The integral over the page is the duration, so the page still
        // lasts as long - the time is redistributed inside it.
        double rateAt (double playhead) const
        {
            if (warp == 0.0 || durationSec <= 0.0) return 1.0;
            const double x = std::clamp (playhead / durationSec, 0.0, 1.0);
            return 1.0 + warp * (2.0 * x - 1.0);
        }

        //---------------- the page's time map (v1.33.4 / H8) ----------------
        // The page's own time and the audio time are two views of the same
        // page. The rate is linear in the score, so the map between them
        // has a closed form:
        //
        //   rate(s) = (1 - w) + 2w s/D
        //   a(s)    = D/(2w) ln((1 - w + 2w s/D)/(1 - w))
        //   s(a)    = D/(2w) (1 - w) (e^(2w a/D) - 1)
        //
        // The playback and the export use the same map, so an export is as
        // long as the page really needs and the sound lines up with what
        // the user heard. Without it the export was given durationSec *
        // sampleRate frames and cut the end of a warped page off: a 30 s
        // page at warp 0.9 needs 49.07 s of audio, and an arc at 25-29 s
        // exported as silence.
        double audioDurationSec() const
        {
            if (durationSec <= 0.0) return 0.0;
            if (std::abs (warp) < 1.0e-6) return durationSec;
            const double w = std::clamp (warp, -0.99, 0.99);
            return durationSec / (2.0 * w) * std::log ((1.0 + w) / (1.0 - w));
        }

        // The score position after that much audio.
        double scoreAtAudioTime (double audioSec) const
        {
            if (durationSec <= 0.0) return 0.0;
            if (std::abs (warp) < 1.0e-6) return audioSec;
            const double w = std::clamp (warp, -0.99, 0.99);
            const double e = std::exp (2.0 * w * audioSec / durationSec);
            return durationSec / (2.0 * w) * (1.0 - w) * (e - 1.0);
        }

        // The audio time it takes to reach a score position.
        double audioTimeAtScore (double scoreSec) const
        {
            if (durationSec <= 0.0) return 0.0;
            if (std::abs (warp) < 1.0e-6) return scoreSec;
            const double w = std::clamp (warp, -0.99, 0.99);
            const double x = std::clamp (scoreSec / durationSec, 0.0, 1.0);
            return durationSec / (2.0 * w) * std::log ((1.0 - w + 2.0 * w * x) / (1.0 - w));
        }

        // The page's average rate over a block of audio that starts at the
        // score position `scorePos` and runs for `audioSec` (negative when
        // the transport runs backwards). Without a warp it is exactly 1, so
        // an unwarped page behaves exactly as before; with a warp it is the
        // average of the time map over the block, so the block's content
        // and the playhead agree exactly. The playback and the export use
        // this one function, so they share the same mapping.
        double averageRate (double scorePos, double audioSec) const
        {
            if (durationSec <= 0.0 || audioSec == 0.0) return rateAt (scorePos);
            if (std::abs (warp) < 1.0e-6) return 1.0;
            const double a0 = audioTimeAtScore (scorePos);
            const double s1 = scoreAtAudioTime (a0 + audioSec);
            return (s1 - scorePos) / audioSec;
        }

        // The range of one of the page's spaces.
        void spaceRange (int index, double& lo, double& hi) const
        {
            const auto m = spaceMap (index);
            lo = std::exp2 (m.t0);
            hi = std::exp2 (m.t1);
        }

        // The pitch a drawn pitch sounds at in one of the page's spaces:
        // the position on the page's vertical axis is mapped into the
        // space. The intervals are scaled with it (a space half as wide
        // halves them) and the register moves with the space's centre.
        double placeInSpace (double hz, int index) const
        {
            return spaceMap (index).apply (hz);
        }

        // The mapping of the page's vertical axis into one of its spaces.
        // It is precomputed ONCE per arc on the audio thread: the per-
        // sample cost is a log2 and an exp2, not a table lookup and a
        // range calculation.
        struct SpaceMap
        {
            double a0 = 0.0, a1 = 1.0;   // the ambitus, in log2
            double t0 = 0.0, t1 = 1.0;   // the space, in log2
            // The page's own space IS the identity: the pitch passes through
            // UNCHANGED, bit for bit. That matters: the phase vocoder picks
            // its output bin with a truncated (k * ratio), so a mapping that
            // returns the pitch with one ULP different can move a partial
            // into the next bin and wreck the voice. An identity mapping
            // must therefore not round-trip through log2/exp2 at all.
            bool identity = true;
            // WP1: a drawn (or inverted) table. The realtime path receives
            // the precomputed LUT; without one (UI, offline, tests) the
            // curve is evaluated directly.
            const FreqSpaceDef* def = nullptr;
            const FreqLut* lut = nullptr;
            int stepDivisions = 0;   // the space's own staircase, if stepped
            // WP3: a group's transpose, in semitones, applied after the
            // space map and its temperament (the plan's documented order).
            double transposeSemis = 0.0;

            double apply (double hz) const
            {
                if (identity) return hz;
                double out = hz;
                if (! identity)
                {
                    const double n = std::clamp (
                                    (std::log2 (std::max (1.0, hz)) - a0)
                                      / std::max (1.0e-6, a1 - a0), 0.0, 1.0);
                    if (lut != nullptr && lut->valid)
                        out = std::exp2 ((double) lut->at ((float) n));
                    else if (def != nullptr)
                        out = std::exp2 (def->log2At ((float) n, a0, a1));
                    else
                        out = std::exp2 (t0 + n * (t1 - t0));
                }
                return out;
            }
        };

        // `lut` is the snapshot's precomputed table for this space, when the
        // space has a drawn curve (null for the presets, which stay analytic).
        SpaceMap spaceMap (int index, const FreqLut* lut = nullptr) const
        {
            const int i = std::clamp (index, 0, 3);
            const FreqSpaceDef& def = spaceDefs[(size_t) i];
            SpaceMap m;
            m.a0 = std::log2 (std::max (1.0, pitchLo));
            m.a1 = std::log2 (std::max (1.0, pitchHi));
            m.stepDivisions = (def.stepped && def.stepDivisions > 0) ? def.stepDivisions : 0;

            if (def.editable || def.inverted)
            {
                m.def = &def;
                m.lut = (lut != nullptr && lut->valid) ? lut : nullptr;
                m.identity = false;
                m.t0 = def.log2At (0.0f, m.a0, m.a1);
                m.t1 = def.log2At (1.0f, m.a0, m.a1);
                if (m.t1 <= m.t0) m.t1 = m.t0 + 1.0e-6; // an inverted/empty table
                return m;
            }

            const auto& presets = freqSpacePresets();
            const FreqSpacePreset p = presets[(size_t) std::clamp (spaces[(size_t) i], 0,
                                                (int) presets.size() - 1)];
            const double centre = (m.a0 + m.a1) * 0.5 + p.shift;
            const double half   = (m.a1 - m.a0) * 0.5 * p.span;
            m.t0 = centre - half;
            m.t1 = centre + half;
            m.identity = (p.shift == 0.0 && p.span == 1.0);
            return m;
        }

        double beatsToSeconds (double beat) const {
            const double bpm = (tempoBpm > 1.0) ? tempoBpm : 120.0;
            return beat * (60.0 / bpm);
        }
        double secondsToBeats (double sec) const {
            const double bpm = (tempoBpm > 1.0) ? tempoBpm : 120.0;
            return sec * (bpm / 60.0);
        }

        // WP3: the render-time view of the groups. One override per arc,
        // parallel to `arcs`. Solo is resolved here (any solo group silences
        // the groups that are not solo); an arc-level mute still wins in the
        // engine. Membership is one group per arc - the first group that
        // lists an arc owns it.
        std::vector<ArcRenderOverride> resolveArcOverrides() const
        {
            std::vector<ArcRenderOverride> out (arcs.size());
            bool anySolo = false;
            for (auto& g : groups)
                if (g.solo && ! g.muted) { anySolo = true; break; }

            std::vector<int> groupOf (arcs.size(), -1);
            for (size_t gi = 0; gi < groups.size(); ++gi)
                for (int aid : groups[gi].arcIds)
                    for (size_t ai = 0; ai < arcs.size(); ++ai)
                        if (arcs[ai].id == aid && groupOf[ai] < 0)
                            groupOf[ai] = (int) gi;

            // WP5: the arc index by id, once per page (the modulation chain
            // walk and the resolved modulator index both need it).
            int maxId = 0;
            for (auto& a : arcs) maxId = std::max (maxId, a.id);
            std::vector<int> indexOf ((size_t) maxId + 1, -1);
            for (size_t ai = 0; ai < arcs.size(); ++ai)
                if (arcs[ai].id >= 0 && arcs[ai].id <= maxId)
                    indexOf[(size_t) arcs[ai].id] = (int) ai;

            for (size_t ai = 0; ai < arcs.size(); ++ai)
            {
                ArcRenderOverride& o = out[ai];
                const int gi = groupOf[ai];
                if (gi < 0)
                {
                    // conventional solo: everything outside the soloed
                    // groups is silenced, ungrouped arcs included
                    o.muted = anySolo;
                }
                else
                {
                    const ArcGroup& g = groups[(size_t) gi];
                    o.gain = g.gain;
                    o.transposeSemitones = g.transposeSemitones;
                    o.waveformId = g.waveformId;
                    o.envelopeId = g.envelopeId;
                    o.amplitudeTableId = g.amplitudeTableId;
                    o.channelId = g.channelId;
                    o.muted = g.muted || (anySolo && ! g.solo);
                }

                // WP5: resolve the modulating arc. The chain is walked to
                // refuse a cycle here (the audio thread never has to).
                const Arc& carrier = arcs[ai];
                if (carrier.modulatorArcId >= 0 && carrier.modulatorArcId <= maxId
                    && carrier.modulatorArcId != carrier.id && carrier.modArcDepth > 0.0)
                {
                    const int mi = indexOf[(size_t) carrier.modulatorArcId];
                    if (mi >= 0 && mi < (int) arcs.size())
                    {
                        bool cycle = false;
                        int walk = mi;
                        for (int guard = 0; guard < 64 && walk >= 0; ++guard)
                        {
                            if (arcs[(size_t) walk].id == carrier.id) { cycle = true; break; }
                            const int nextId = arcs[(size_t) walk].modulatorArcId;
                            walk = (nextId >= 0 && nextId <= maxId)
                                       ? indexOf[(size_t) nextId] : -1;
                        }
                        if (! cycle)
                        {
                            double lo = 0.0, hi = 0.0;
                            bool first = true;
                            for (auto& p : arcs[(size_t) mi].points)
                            {
                                const double l = std::log2 (std::max (1.0e-6, p.freq));
                                if (first) { lo = hi = l; first = false; }
                                else { lo = std::min (lo, l); hi = std::max (hi, l); }
                            }
                            o.modIndex = mi;
                            o.modTarget = carrier.modTarget;
                            o.modDepth = carrier.modArcDepth;
                            o.modAlign = carrier.modAlign;
                            o.modLoLog2 = lo;
                            o.modHiLog2 = hi;
                        }
                    }
                }
            }
            return out;
        }
    };

    inline std::vector<Waveform> defaultWaveforms()
    {
        std::vector<Waveform> w;
        Waveform s0(0, "Sine", paletteColour(0)); s0.makeSine(); w.push_back(s0);
        Waveform s1(1, "Saw", paletteColour(1)); s1.makeSaw(); s1.smooth(1); w.push_back(s1);
        Waveform s2(2, "Square", paletteColour(2)); s2.makeSquare(); s2.smooth(2); w.push_back(s2);
        Waveform s3(3, "Triangle", paletteColour(3)); s3.makeTriangle(); w.push_back(s3);
        Waveform s4(4, "Reed", paletteColour(4)); s4.makeRandomSmooth(7); s4.normalize(); w.push_back(s4);
        Waveform s5(5, "Buzz", paletteColour(5)); s5.makeSaw(); for (auto& x : s5.samples) x = std::tanh(3.0f*x); w.push_back(s5);
        return w;
    }

    inline std::vector<Envelope> defaultEnvelopes()
    {
        std::vector<Envelope> e;
        Envelope e0(0, "Flat", paletteColour(1)); e0.presetFlat(); e.push_back(e0);
        Envelope e1(1, "Percussive", paletteColour(2)); e1.presetPercussive(); e.push_back(e1);
        Envelope e2(2, "Swell", paletteColour(3)); e2.presetSwell(); e.push_back(e2);
        Envelope e3(3, "Gate", paletteColour(4)); e3.presetGate(); e.push_back(e3);
        Envelope e4(4, "Strings", paletteColour(0)); e4.presetStrings(); e.push_back(e4);
        return e;
    }

    inline Page demoPage()
    {
        Page p; p.durationSec = 30.0;
        auto mk = [&](int wid, int eid, float inten, std::vector<std::pair<double,double>> pts){
            Arc a; a.id = p.nextArcId++; a.waveformId = wid; a.envelopeId = eid; a.intensity = inten;
            for (auto& pr : pts) a.points.push_back({pr.first, pr.second});
            p.arcs.push_back(a);
        };
        mk(0, 0, 0.8f, {{0, 110}, {6, 220}, {12, 440}, {18, 330}, {24, 440}});
        mk(1, 1, 0.7f, {{2, 880}, {8, 660}, {14, 990}, {22, 1320}});
        mk(3, 2, 0.6f, {{4, 55}, {16, 82.5}, {28, 110}});
        mk(2, 1, 0.5f, {{10, 523.25}, {11, 587.33}, {12, 659.25}, {13, 783.99}, {16, 1046.5}});
        mk(4, 4, 0.65f, {{0, 220}, {15, 233.08}, {29, 246.94}});
        return p;
    }
}
