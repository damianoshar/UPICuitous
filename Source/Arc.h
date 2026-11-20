#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "PieceLimits.h"
#include "UpicConstants.h"
#include "UpicMath.h"
#include "GuerreroImport.h"
#include "GuerreroProvenanceIO.h"

namespace upic
{
    //---------------- Arc (pitch curve on a page) ----------------
    // i = pen-pressure intensity multiplier for this point (0..1, 1 = no
    // pressure data). Playback interpolates it along the arc.
    struct ArcPoint { double t = 0.0; double freq = 440.0; float i = 1.0f; };

    struct Arc
    {
        int id = 0;
        int waveformId = 0;
        int envelopeId = 0;
        float intensity = 0.8f; // 0..1
        bool muted = false;
        int mode = 0;            // 0 = osc (wavetable), 1 = sample varispeed, 2 = live input, 3 = PM (2-op phase mod)
        int sampleId = 1;        // which sample slot to shift (mode 1)
        double refFreq = 261.63; // base pitch of the material / unison (mode 1/2)
        std::vector<ArcPoint> points; // sorted by t
        // v1.21 (wave 19): the per-arc overrides of the arc list panel.
        // pitchMode: -1 = follow the tab's setting, 0..2 = this arc's
        // algorithm (varispeed / granular / vocoder for a sample, granular /
        // tape / vocoder for the live input).
        // reversed: this LIVE arc plays the microphone backwards (the mic is
        // shared, so the direction belongs to the arc; a sample's direction
        // belongs to its slot).
        // v1.33.4 (H7): both change the sound, so they travel with the piece
        // and with the host state (they used to be dropped on save, so a
        // reopened piece or session sounded different).
        int pitchMode = -1;
        bool reversed = false;

        //---------------- the polyagogy (U1) ----------------
        // The real UPIC's central idea: "once a line or curve has been
        // drawn, nothing is really decided about its interpretation as a
        // signal". The curve could be the carrier's timbre AND a control
        // signal at the same time - an amplitude LFO (envelope shaping) or a
        // frequency LFO (a fixed pitch or a glissando), read at its own rate.
        //
        // Here the modulating curve is the arc's OWN wave: the same drawn
        // curve that makes the sound also shapes it. waveUse says what it
        // does besides being the wavetable.
        enum WaveUse
        {
            waveAsOscillator = 0,   // only the timbre (the classic case)
            waveAlsoAmplitude = 1,  // and an amplitude LFO
            waveAlsoFrequency = 2   // and a frequency LFO
        };
        int    waveUse = waveAsOscillator;
        double modRate  = 1.0;    // the LFO's rate, in cycles per second
        double modDepth = 0.0;    // 0..1 - how deep the modulation goes
        double pmRatio = 2.0;        // PM: modulator / carrier
        double pmIndex = 1.5;        // PM: peak modulation index
        int    pmIndexEnv = 0;       // PM: envelope shaping the index (0 = flat)
        double pmFeedback = 0.0;     // PM: feedback coefficient (0..1)
        int    pmOps = 2;          // PM: 2 or 4 operators
        double pmR2 = 2.0, pmR3 = 3.0, pmR4 = 5.0;  // PM: modulator ratios (4-op)
        double pmI2 = 1.0, pmI3 = 1.0, pmI4 = 1.0;  // PM: stage indices (4-op)

        //---------------- the discrete frequency space (v1.32) ----------------
        // The UPIC's frequency space could be "non-discrete" - the
        // continuum, where a diagonal line is a glissando - or "discrete",
        // with equal temperaments of 1..99 divisions per octave: "the same
        // diagonal arc would then sound like discrete steps in a defined
        // temperament" (Brigitte Robindoré, in "From Xenakis's UPIC to
        // Graphic Notation Today", Hatje Cantz 2024).
        //
        // The sieve (above) snaps the DRAWN points; this steps the SOUND.
        // The line on the canvas stays a smooth curve, but the pitch that
        // plays moves through a temperament, so a glissando becomes a
        // staircase of discrete steps - the "tempered discontinuum".
        //
        // 0 = the continuum (continuous, the default); 1..99 = the number
        // of equal divisions per octave the pitch is moved through.
        int stepDivisions = 0;

        // WP2: the amplitude conversion table this arc plays through. -1 is
        // "the envelope as it is"; otherwise the envelope's output (0..1) is
        // read as the input of the table, and the table's output is the
        // level. The envelope keeps its shape: the table only maps it.
        int amplitudeTableId = -1;

        // WP5: an independent modulating arc on the same page. The modulator
        // is a drawn arc like any other; its contour (pitch target) or its
        // envelope (amplitude target) drives this arc. modAlign: 0 = the
        // page's absolute time, 1 = the modulator starts with the carrier.
        int    modulatorArcId = -1;
        int    modTarget = 0;     // 0 = pitch, 1 = amplitude
        double modArcDepth = 0.5; // 0..1
        int    modAlign = 0;      // 0 = absolute, 1 = carrier start

        // WP6: the mixer channel (0 = the implicit main channel).
        int channelId = 0;



        //---------------- the frequency space (v1.33) ----------------
        // Which of the page's four spaces this arc is played in (0..3).
        // The UPIC assigned a table to an arc "the same way as one would
        // assign the waveform or envelope to it"; the space itself is the
        // page's, the name is the arc's.
        int space = 0;

        //---------------- Guerrero provenance (optional) ----------------
        // Present only when the arc came from the Guerrero engine; hand-drawn
        // arcs leave it absent, so their serialized form is unchanged.
        bool hasGuerrero = false;
        GuerreroProvenance guerrero;


        // FNV-1a over the geometry, computed on QUANTISED values. Used both when
        // recording what was generated and when verifying, at load or before
        // regeneration, that the points still match that record.
        //
        // The exact bit pattern cannot be used: the piece is stored as JSON
        // numbers at 15 significant digits, so an untouched arc can come back
        // with a different low bit and would be falsely flagged as edited.
        // (At 127 BPM, 55 of 128 points change under a 15-digit round-trip.)
        //
        // 1e-6 is used as the quantum: one microsecond in time and one microHz in
        // frequency, far below any musical meaning, and it keeps every value
        // inside 15 significant digits for the ranges the host allows.
        static uint64_t geometryFingerprint (const std::vector<ArcPoint>& pts)
        {
            uint64_t h = 1469598103934665603ULL;
            auto mix = [&h] (uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                {
                    h ^= (v & 0xFFu);
                    h *= 1099511628211ULL;
                    v >>= 8;
                }
            };

            auto quantise = [] (double v) -> uint64_t
            {
                const double scaled = v * 1000000.0;
                const long long q = (long long) std::llround (scaled);
                return (uint64_t) q;
            };

            for (const auto& p : pts)
            {
                mix (quantise (p.t));
                mix (quantise ((double) p.freq));
                mix (quantise ((double) p.i));
            }
            return h;
        }
        // Algorithm identifiers for the stored fingerprint.
        //   1 = exact bits (records written before quantisation)
        //   2 = quantised to 1e-6 (survives a 15-digit JSON round-trip)
        static constexpr int kFingerprintExactBits = 1;
        static constexpr int kFingerprintQuantised = 2;

        // Legacy (v1) fingerprint: exact bits. Kept only so records written
        // before quantisation can be verified rather than assumed edited.
        static uint64_t geometryFingerprintLegacy (const std::vector<ArcPoint>& pts)
        {
            uint64_t h = 1469598103934665603ULL;
            auto mix = [&h] (uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                {
                    h ^= (v & 0xFFu);
                    h *= 1099511628211ULL;
                    v >>= 8;
                }
            };
            for (const auto& p : pts)
            {
                uint64_t tBits = 0, fBits = 0;
                uint32_t iBits = 0;
                std::memcpy (&tBits, &p.t,    sizeof (tBits));
                std::memcpy (&fBits, &p.freq, sizeof (fBits));
                std::memcpy (&iBits, &p.i,    sizeof (iBits));
                mix (tBits);
                mix (fBits);
                mix ((uint64_t) iBits);
            }
            return h;
        }

        double startTime() const { return points.empty() ? 0 : points.front().t; }
        double endTime()   const { return points.empty() ? 0 : points.back().t; }

        //---------------- the UPIC's drawn-curve transformations -------------
        // The real UPIC could transpose, reverse and invert a composition
        // (v1.31 / U3). The reverse already exists - the Rev box, the
        // global Rev and the slot Rev are the time mirror. These two are
        // the pitch ones, and they work on the drawn curve, which is where
        // the music lives.
        //
        // Transposing moves every drawn pitch by a number of semitones.
        // The material's unison (refFreq) stays where it is, so the arc
        // moves relative to the grid - which is what transposing a score
        // means. In all three tabs the stored frequency is the absolute
        // pitch the arc plays (the sample and live ratios are derived from
        // it), so one operation covers them.
        void transpose (double semitones)
        {
            if (semitones == 0.0 || points.empty()) return;
            const double factor = std::pow (2.0, semitones / 12.0);
            for (auto& p : points)
                p.freq = limits::clampFinite (p.freq * factor, kMinFreq, kMaxFreq);
        }

        // Inverting mirrors the pitch contour about the arc's own centre
        // (the geometric mean of its pitches). The canvas is log-pitch, so
        // this is exactly what flipping the drawing upside down does, and
        // it is its own inverse: inverting twice gives the arc back.
        void invertPitch()
        {
            if (points.size() < 2) return;
            double sumLog = 0.0;
            for (auto& p : points) sumLog += std::log2 (juce::jmax (1.0, p.freq));
            const double centre = std::pow (2.0, sumLog / (double) points.size());
            for (auto& p : points)
                p.freq = limits::clampFinite (centre * centre / p.freq, kMinFreq, kMaxFreq);
        }

        // Retrograde: the time mirror, the other half of the classical
        // transformation family. The contour is mirrored about the arc's
        // own time centre and the points are reversed, so the curve reads
        // backwards inside its own span (a rising line becomes a falling
        // one). It is its own inverse.
        void retrograde()
        {
            if (points.size() < 2) return;
            const double centre = (points.front().t + points.back().t) * 0.5;
            for (auto& p : points) p.t = centre - (p.t - centre);
            std::reverse (points.begin(), points.end());
            for (auto& p : points) p.t = limits::clampFinite (p.t, 0.0, kMaxPageSeconds);
        }

        // Augmentation / diminution: the time scale. The contour is
        // stretched (factor > 1) or compressed (factor < 1) from the arc's
        // own start - the same shape, played slower or faster. Scaling
        // from the start (not from the centre) keeps the arc in the page
        // and is the classic form: the phrase begins when it began and
        // takes longer.
        void scaleTime (double factor)
        {
            if (points.size() < 2 || ! (factor > 0.0) || ! std::isfinite (factor)) return;
            const double t0 = points.front().t;
            for (auto& p : points)
                p.t = limits::clampFinite (t0 + (p.t - t0) * factor,
                                           0.0, kMaxPageSeconds);
        }

        // v1.33.4 (U6-C): the cursor for the arc's own lookups. It follows
        // the segment as the time moves and keeps the segment's endpoints -
        // and their log2, which the pitch interpolation needs - so the
        // per-sample work is a lerp instead of a binary search plus two
        // log2 calls.
        struct PitchCursor
        {
            SegmentCursor seg;
            size_t cachedIndex = (size_t) -1;  // the segment the cache is for
            double t0 = 0.0, t1 = 0.0;
            double l0 = 0.0, l1 = 0.0;         // the log2 of the endpoints
            float  i0 = 1.0f, i1 = 1.0f;        // the endpoints' pressure
        };

        // Moves the cursor to the segment that holds t and refreshes the
        // cached endpoints (they only change when the cursor moves).
        void seekCursor (double t, PitchCursor& c) const
        {
            const size_t i = followSegment (points, t,
                                           [] (const ArcPoint& p) { return p.t; },
                                           c.seg);
            if (i == c.cachedIndex) return;
            c.cachedIndex = i;
            if (i == 0 || i >= points.size()) return;   // guarded by the callers
            const size_t i0 = i - 1, i1 = i;
            c.t0 = points[i0].t; c.t1 = points[i1].t;
            c.l0 = std::log2 (juce::jmax (1.0, points[i0].freq));
            c.l1 = std::log2 (juce::jmax (1.0, points[i1].freq));
            c.i0 = points[i0].i;   c.i1 = points[i1].i;
        }

        // log-domain interpolation so glissandi sound linear in pitch
        double freqAt(double t) const
        {
            PitchCursor c;
            return freqAt (t, c);
        }
        // v1.33.4 (U6-C): the cursor-based pitch. The caller walks the
        // samples in order, so the cursor can follow the segment instead of
        // searching for it again for every sample.
        double freqAt(double t, PitchCursor& c) const
        {
            if (points.empty()) return 440.0;
            if (t <= points.front().t) return points.front().freq;
            if (t >= points.back().t)  return points.back().freq;
            seekCursor (t, c);
            if (c.t1 <= c.t0) return points[c.cachedIndex].freq;
            double f = (t - c.t0) / (c.t1 - c.t0);
            return std::pow (2.0, c.l0 + (c.l1 - c.l0) * f);
        }
        // 0..1 over arc lifetime
        float lifeAt(double t) const
        {
            double s = startTime(), e = endTime();
            if (e <= s) return 0.0f;
            return (float) juce::jlimit(0.0, 1.0, (t - s) / (e - s));
        }
        // per-point pressure intensity curve (linear, 1.0 where no pressure)
        float intensityAt(double t) const
        {
            PitchCursor c;
            return intensityAt (t, c);
        }
        // v1.33.4 (U6-C): the cursor-based intensity - the same segment as
        // the pitch, so it shares the cursor.
        float intensityAt(double t, PitchCursor& c) const
        {
            if (points.empty()) return 1.0f;
            if (t <= points.front().t) return points.front().i;
            if (t >= points.back().t)  return points.back().i;
            seekCursor (t, c);
            if (c.t1 <= c.t0) return c.i1;
            float f = (float) ((t - c.t0) / (c.t1 - c.t0));
            return c.i0 + (c.i1 - c.i0) * f;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id); o->setProperty("waveformId", waveformId);
            o->setProperty("envelopeId", envelopeId); o->setProperty("intensity", intensity);
            o->setProperty("muted", muted);
            o->setProperty("mode", mode);
            o->setProperty("pmRatio", pmRatio);
            o->setProperty("pmIndex", pmIndex);
            o->setProperty("pmIndexEnv", pmIndexEnv);
            o->setProperty("pmFeedback", pmFeedback);
            o->setProperty ("pmOps", pmOps); o->setProperty ("pmR2", pmR2); o->setProperty ("pmR3", pmR3); o->setProperty ("pmR4", pmR4);
            o->setProperty ("pmI2", pmI2); o->setProperty ("pmI3", pmI3); o->setProperty ("pmI4", pmI4);
            o->setProperty("sampleId", sampleId);
            o->setProperty("refFreq", refFreq);
            // v1.31 (U1): the polyagogy settings travel with the piece
            o->setProperty("waveUse", waveUse);
            o->setProperty("modRate", modRate);
            o->setProperty("modDepth", modDepth);
            // v1.32: the discrete frequency space travels with the piece
            if (stepDivisions > 0) o->setProperty("sd", stepDivisions);
            if (amplitudeTableId >= 0) o->setProperty ("ampTable", amplitudeTableId);
            if (modulatorArcId >= 0) o->setProperty ("mod", modulatorArcId);
            if (modTarget != 0) o->setProperty ("modTarget", modTarget);
            if (std::abs (modArcDepth - 0.5) > 1e-9) o->setProperty ("modArcDepth", modArcDepth);
            if (modAlign != 0) o->setProperty ("modAlign", modAlign);
            if (channelId > 0) o->setProperty ("ch", channelId);
            // v1.33: the frequency space the arc is played in
            if (space != 0) o->setProperty("sp", space);
            // v1.33.4 (H7): the per-arc overrides change the sound, so they
            // travel with the piece (and with the host state)
            if (pitchMode >= 0) o->setProperty("pitchMode", pitchMode);
            if (reversed) o->setProperty("reversed", true);
            if (reversed) o->setProperty("reversed", true);
            // Guerrero provenance: written only when present, so hand-drawn
            // arcs keep their existing on-disk shape.
            if (hasGuerrero) o->setProperty("guerrero", guerreroio::toVar (guerrero));
            juce::Array<juce::var> arr;
            for (auto& p : points)
            {
                auto* po = new juce::DynamicObject();
                po->setProperty("t", p.t); po->setProperty("f", p.freq);
                if (p.i != 1.0f) po->setProperty("i", p.i); // omit default (keeps files compact)
                arr.add(juce::var(po));
            }
            o->setProperty("points", arr);
            return juce::var(o);
        }
        static Arc fromVar(const juce::var& v)
        {
            Arc a;
            a.id = (int) v.getProperty("id", 0);
            a.waveformId = (int) v.getProperty("waveformId", 0);
            a.envelopeId = (int) v.getProperty("envelopeId", 0);
            a.intensity = limits::clampIntensity((float)(double) v.getProperty("intensity", 0.8));
            a.muted = (bool) v.getProperty("muted", false);
            // absent in old / Python-written files -> oscillator mode, as before
            a.mode = juce::jlimit(0, 3, (int) v.getProperty("mode", 0));
            a.pmRatio    = limits::clampFinite((double) v.getProperty("pmRatio", 2.0), 0.25, 16.0);
            a.pmIndex    = limits::clampFinite((double) v.getProperty("pmIndex", 1.5), 0.0, 100.0);
            a.pmIndexEnv = (int)    v.getProperty("pmIndexEnv", 0);
            a.pmFeedback  = limits::clampFinite((double) v.getProperty("pmFeedback", 0.0), -1.0, 1.0);
            a.pmOps = juce::jlimit (2, 4, (int) v.getProperty ("pmOps", 2));
            a.pmR2  = limits::clampFinite((double) v.getProperty ("pmR2", 2.0), 0.25, 16.0);  a.pmR3 = limits::clampFinite((double) v.getProperty ("pmR3", 3.0), 0.25, 16.0);  a.pmR4 = limits::clampFinite((double) v.getProperty ("pmR4", 5.0), 0.25, 16.0);
            a.pmI2  = limits::clampFinite((double) v.getProperty ("pmI2", 1.0), 0.0, 100.0);  a.pmI3 = limits::clampFinite((double) v.getProperty ("pmI3", 1.0), 0.0, 100.0);  a.pmI4 = limits::clampFinite((double) v.getProperty ("pmI4", 1.0), 0.0, 100.0);
            a.sampleId = juce::jlimit(1, kMaxSampleSlots, (int) v.getProperty("sampleId", 1));
            a.refFreq = limits::clampFinite((double) v.getProperty("refFreq", 261.63), 20.0, 4000.0);
            // v1.31 (U1): the polyagogy settings (absent in older pieces ->
            // the classic oscillator)
            a.waveUse = juce::jlimit (0, 2, (int) v.getProperty ("waveUse", 0));
            a.modRate  = limits::clampFinite ((double) v.getProperty ("modRate", 1.0), 0.01, 100.0);
            a.modDepth = limits::clampFinite ((double) v.getProperty ("modDepth", 0.0), 0.0, 1.0);
            // v1.32: the discrete frequency space (absent in older pieces ->
            // the continuum)
            a.stepDivisions = juce::jlimit (0, 99, (int) v.getProperty ("sd", 0));
            a.amplitudeTableId = juce::jlimit (-1, limits::kMaxItemId,
                                            (int) v.getProperty ("ampTable", -1));
            a.modulatorArcId = juce::jlimit (-1, limits::kMaxItemId,
                                           (int) v.getProperty ("mod", -1));
            a.modTarget = juce::jlimit (0, 1, (int) v.getProperty ("modTarget", 0));
            a.modArcDepth = limits::clampFinite ((double) v.getProperty ("modArcDepth", 0.5), 0.0, 1.0);
            a.modAlign = juce::jlimit (0, 1, (int) v.getProperty ("modAlign", 0));
            a.channelId = juce::jlimit (0, limits::kMaxItemId, (int) v.getProperty ("ch", 0));
            // v1.33: the frequency space (absent in older pieces -> the
            // page's own range, which is the identity)
            a.space = juce::jlimit (0, 3, (int) v.getProperty ("sp", 0));
            // v1.33.4 (H7): the per-arc overrides (absent in older pieces ->
            // follow the tab's setting, play forward)
            a.pitchMode = juce::jlimit (-1, 2, (int) v.getProperty ("pitchMode", -1));
            a.reversed = (bool) v.getProperty ("reversed", false);
            a.reversed = (bool) v.getProperty ("reversed", false);
            // Guerrero provenance (absent in every piece written before this
            // feature, and in hand-drawn arcs).
            if (v.hasProperty ("guerrero"))
            {
                a.hasGuerrero = true;
                a.guerrero = guerreroio::fromVar (v.getProperty ("guerrero", juce::var()));
            }
            if (auto* arr = v.getProperty("points", juce::var()).getArray())
                for (auto& pv : *arr)
                {
                    if ((int) a.points.size() >= limits::kMaxPointsPerArc) break;
                    upic::ArcPoint pt;
                    pt.t = limits::clampFinite((double) pv.getProperty("t", 0), 0.0, kMaxPageSeconds);
                    pt.freq = limits::clampFinite((double) pv.getProperty("f", 440), kMinFreq, kMaxFreq);
                    pt.i = limits::clampIntensity((float)(double) pv.getProperty("i", 1.0));
                    a.points.push_back(pt);
                }
            // stable: a staircase's corners share a time, and their order is
            // the direction of the jump
            std::stable_sort(a.points.begin(), a.points.end(), [](auto& x, auto& y){ return x.t < y.t; });
            // Load-time integrity check: if the file says "this geometry is
            // what the recipe produced", verify it. A hand-edited JSON that
            // changes the points but leaves the provenance alone is caught
            // here, so regeneration cannot silently overwrite it.
            //
            // Records written before quantisation carry an exact-bits hash. They
            // are verified with THAT algorithm and then upgraded, so an older
            // untouched arc is not mistaken for an edited one.
            if (a.hasGuerrero && a.guerrero.hasFingerprint)
            {
                const int algo = a.guerrero.fingerprintAlgorithm;
                const bool exactBits = (algo == kFingerprintExactBits);

                const uint64_t current = exactBits ? geometryFingerprintLegacy (a.points)
                                                   : geometryFingerprint (a.points);
                const bool sizeOk = ((int) a.points.size() == a.guerrero.pointCount);

                if (current == a.guerrero.geometryHash && sizeOk)
                {
                    if (exactBits)
                    {
                        // Verified against the old algorithm: migrate forward.
                        a.guerrero.geometryHash = geometryFingerprint (a.points);
                        a.guerrero.fingerprintAlgorithm = kFingerprintQuantised;
                    }
                }
                else
                {
                    a.guerrero.geometryEdited = true;
                }
            }

            return a;
        }
    };
}
