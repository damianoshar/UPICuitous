#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Arc.h"
#include "Envelope.h"
#include "FreqSpace.h"
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

        //---------------- the page's acceleration (v1.33) ----------------
        // The UPIC's page could be expanded and contracted in time "with the
        // additional option of acceleration and deceleration" (Robindoré):
        // the page's own time ran at a varying rate. The warp is a tempo
        // sweep across the page: 0 is a straight line (the rate is 1 all
        // the way), a positive value starts slow and speeds up, a negative
        // one starts fast and slows down.
        double warp = 0.0;

        // The rate at which the page's time runs at this point of the page.
        // The integral over the page is the duration, so the page still
        // lasts as long - the time is redistributed inside it.
        double rateAt (double playhead) const
        {
            if (warp == 0.0 || durationSec <= 0.0) return 1.0;
            const double x = juce::jlimit (0.0, 1.0, playhead / durationSec);
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
            const double w = juce::jlimit (-0.99, 0.99, warp);
            return durationSec / (2.0 * w) * std::log ((1.0 + w) / (1.0 - w));
        }

        // The score position after that much audio.
        double scoreAtAudioTime (double audioSec) const
        {
            if (durationSec <= 0.0) return 0.0;
            if (std::abs (warp) < 1.0e-6) return audioSec;
            const double w = juce::jlimit (-0.99, 0.99, warp);
            const double e = std::exp (2.0 * w * audioSec / durationSec);
            return durationSec / (2.0 * w) * (1.0 - w) * (e - 1.0);
        }

        // The audio time it takes to reach a score position.
        double audioTimeAtScore (double scoreSec) const
        {
            if (durationSec <= 0.0) return 0.0;
            if (std::abs (warp) < 1.0e-6) return scoreSec;
            const double w = juce::jlimit (-0.99, 0.99, warp);
            const double x = juce::jlimit (0.0, 1.0, scoreSec / durationSec);
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
            double apply (double hz) const
            {
                if (identity) return hz;
                const double n = juce::jlimit (0.0, 1.0,
                                (std::log2 (juce::jmax (1.0, hz)) - a0)
                                  / juce::jmax (1.0e-6, a1 - a0));
                return std::exp2 (t0 + n * (t1 - t0));
            }
        };

        SpaceMap spaceMap (int index) const
        {
            const auto& presets = freqSpacePresets();
            const FreqSpacePreset p = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1,
                                                spaces[(size_t) juce::jlimit (0, 3, index)])];
            SpaceMap m;
            m.a0 = std::log2 (juce::jmax (1.0, pitchLo));
            m.a1 = std::log2 (juce::jmax (1.0, pitchHi));
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

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("duration", durationSec);
            if (tempoBpm != 120.0) o->setProperty("tempo", tempoBpm);
            o->setProperty("nextArcId", nextArcId);
            juce::Array<juce::var> arr;
            for (auto& a : arcs) arr.add(a.toVar());
            o->setProperty("arcs", arr);
            // omitted when empty (old/Python readers never see the key)
            if (! zones.empty())
            {
                juce::Array<juce::var> zarr;
                for (auto& z : zones) zarr.add(z.toVar());
                o->setProperty("zones", zarr);
                o->setProperty("nextZoneId", nextZoneId);
            }
            // v1.33: the ambitus and the four spaces. Omitted when they are
            // the defaults, so an old piece's file is byte-for-byte what it
            // was.
            if (pitchLo != kDefaultPitchLo || pitchHi != kDefaultPitchHi)
            {
                o->setProperty("ambLo", pitchLo);
                o->setProperty("ambHi", pitchHi);
            }
            bool anySpace = false;
            for (auto s : spaces) if (s != 0) { anySpace = true; break; }
            if (anySpace)
            {
                juce::Array<juce::var> sarr;
                for (auto s : spaces) sarr.add((int) s);
                o->setProperty("spaces", sarr);
            }
            // v1.33: the page's acceleration (omitted when it is zero)
            if (warp != 0.0) o->setProperty("warp", warp);
            return juce::var(o);
        }
        static Page fromVar(const juce::var& v, int* renamedIds = nullptr)
        {
            Page p;
            p.durationSec = limits::clampFinite((double) v.getProperty("duration", 30.0),
                                              kMinPageSeconds, kMaxPageSeconds);
            p.tempoBpm    = limits::clampFinite((double) v.getProperty("tempo", 120.0), 20.0, 400.0);
            // v1.33.4 (L13): the file's own "next id" is only a suggestion -
            // it is checked against the arcs that are there (and against the
            // range an id can have) further down.
            const int fileNextArcId = (int) v.getProperty("nextArcId", 1);
            if (auto* arr = v.getProperty("arcs", juce::var()).getArray())
                for (auto& av : *arr)
                {
                    if ((int) p.arcs.size() >= limits::kMaxArcsPerPage) break;
                    p.arcs.push_back(Arc::fromVar(av));
                }
            // v1.33.4 (L13): the arc ids are the key of the per-arc maps (the
            // selection, the drag map, the arc list). A file with two arcs
            // carrying the same id made the selection and the delete hit the
            // wrong arc, so the ids are made unique here - the first arc of
            // an id keeps it, a duplicate or an impossible id gets a free
            // one.
            int renamed = uniquifyIds (p.arcs, 1);
            // The next id is derived from the arcs that are there: a file
            // could hand out an id that is already in use, or one so large
            // that the counter overflowed. The file's value is still used
            // when it is sane and higher (the ids it handed out earlier may
            // still be in the user's notes).
            p.nextArcId = 1;
            for (auto& a : p.arcs)
                p.nextArcId = juce::jmax(p.nextArcId, a.id + 1);
            if (fileNextArcId > p.nextArcId && fileNextArcId <= limits::kMaxItemId)
                p.nextArcId = fileNextArcId;
            // zones absent in old / Python-written files -> no zones, as before
            const int fileNextZoneId = (int) v.getProperty("nextZoneId", 1);
            if (auto* zarr = v.getProperty("zones", juce::var()).getArray())
                for (auto& zv : *zarr)
                {
                    if ((int) p.zones.size() >= limits::kMaxSieveZones) break;
                    p.zones.push_back(SieveZone::fromVar(zv));
                }
            renamed += uniquifyIds (p.zones, 1);
            p.nextZoneId = 1;
            for (auto& z : p.zones)
                p.nextZoneId = juce::jmax(p.nextZoneId, z.id + 1);
            if (fileNextZoneId > p.nextZoneId && fileNextZoneId <= limits::kMaxItemId)
                p.nextZoneId = fileNextZoneId;
            if (renamedIds != nullptr) *renamedIds += renamed;
            // v1.33: the ambitus and the four spaces (absent in old pieces ->
            // the whole canvas range, and the page's own space)
            p.pitchLo = limits::clampFinite((double) v.getProperty("ambLo", kDefaultPitchLo),
                                            kMinFreq, kMaxFreq);
            p.pitchHi = limits::clampFinite((double) v.getProperty("ambHi", kDefaultPitchHi),
                                            kMinFreq, kMaxFreq);
            if (p.pitchHi <= p.pitchLo * 1.0001)
            {
                p.pitchLo = kDefaultPitchLo;
                p.pitchHi = kDefaultPitchHi;
            }
            if (auto* sarr = v.getProperty("spaces", juce::var()).getArray())
                for (size_t i = 0; i < p.spaces.size() && i < (size_t) sarr->size(); ++i)
                    p.spaces[i] = juce::jlimit (0, (int) freqSpacePresets().size() - 1,
                                               (int) (*sarr)[(int) i]);
            // v1.33: the page's acceleration (absent in old pieces -> a
            // straight line)
            p.warp = limits::clampFinite ((double) v.getProperty ("warp", 0.0), -0.9, 0.9);
            return p;
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
