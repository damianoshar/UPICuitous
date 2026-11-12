#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>
#include "PieceLimits.h"

//==============================================================================
// UPIC data model — mirrors Xenakis' UPIC concepts:
//  Score Page (time=X, pitch=Y log) -> Arcs (pitch curves)
//  Wavetable (single-cycle waveform) per arc
//  Envelope (amplitude 0..1 over arc lifetime) per arc
//==============================================================================

namespace upic
{
    constexpr int kWaveSize = 1024;
    // v1.33: the range the 1990s UPIC reached - "the frequency range of the
    // 1990s UPIC was extended to infrasonics, starting at 0.01 Hz" and the
    // frequency table could span "as large a range as 0.01-22,050 Hz"
    // (Robindoré). These are the LIMITS of what a pitch may be; the page's
    // ambitus (below) is the interval it is drawn in.
    constexpr double kMinFreq = 0.01;
    constexpr double kMaxFreq = 22050.0;
    // The interval the clone has always drawn in: the default page's
    // ambitus. Widening the limits must not change what a page looks like.
    constexpr double kDefaultPitchLo = 20.0;
    constexpr double kDefaultPitchHi = 12000.0;
    constexpr int kMaxWaveforms = 16;
    constexpr int kMaxEnvelopes = 16;
    constexpr int kMaxSampleSlots = 8;
    constexpr double kDefaultRefFreq = 261.63; // C4: base pitch for sample pitch-shift arcs

    //---------------- the page duration range (U2) ----------------
    // The real UPIC's page ran from a few milliseconds to several minutes
    // ("the range was 6 milliseconds to 12 minutes", Xenakis 1992) - the
    // drawings could sit in micro-time as well as in macro-time. The page
    // duration is the piece's clock: the same drawing can be played fast or
    // slow, and it is the one control that sets the time scale.
    constexpr double kMinPageSeconds = 0.005;  // 5 ms
    constexpr double kMaxPageSeconds = 720.0;  // 12 minutes

    inline double midiToFreq(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
    inline double freqToMidi(double f)
    {
        f = juce::jlimit(kMinFreq, kMaxFreq, f);
        return 69.0 + 12.0 * std::log2(f / 440.0);
    }

    inline float freqToNormY(double f) // 0=bottom(low) 1=top(high), log scale
    {
        double lo = std::log2(kMinFreq), hi = std::log2(kMaxFreq);
        double v = (std::log2(juce::jlimit(kMinFreq, kMaxFreq, f)) - lo) / (hi - lo);
        return (float) juce::jlimit(0.0, 1.0, v);
    }
    inline double normYToFreq(float n)
    {
        n = juce::jlimit(0.0f, 1.0f, n);
        double lo = std::log2(kMinFreq), hi = std::log2(kMaxFreq);
        return std::pow(2.0, lo + n * (hi - lo));
    }

    //---------------- the ambitus (v1.33) ----------------
    // "The scale of the millimetric surface could be selected, so that the
    // vertical axis could correspond to any interval" (Mache). The canvas
    // maps the pitch through the PAGE's interval, not through the whole
    // world: a page drawn for a semitone is a page for a semitone.
    //
    // Note: freqToNormYIn does NOT clamp - a pitch outside the interval is
    // off the page, and the canvas has to be able to draw it off the edge
    // (the plot area clips it). normYToFreqIn does clamp: the page only
    // holds what is inside it.
    inline float freqToNormYIn (double f, double lo, double hi)
    {
        const double l0 = std::log2 (juce::jmax (1.0, lo));
        const double l1 = std::log2 (juce::jmax (1.0, hi));
        return (float) ((std::log2 (juce::jmax (1.0, f)) - l0)
                        / juce::jmax (1.0e-6, l1 - l0));
    }
    inline double normYToFreqIn (float n, double lo, double hi)
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        const double l0 = std::log2 (juce::jmax (1.0, lo));
        const double l1 = std::log2 (juce::jmax (1.0, hi));
        return std::exp2 (l0 + (double) n * (l1 - l0));
    }

    // The page's intervals on offer. The UPIC's normal ambitus was six
    // octaves; it could go up to ten (with infrasounds and ultrasounds) or
    // down to a very small interval. The centring is on middle C.
    struct AmbitusPreset
    {
        const char* name = "";
        double lo = kMinFreq, hi = kMaxFreq;
    };
    inline const std::vector<AmbitusPreset>& ambitusPresets()
    {
        static const std::vector<AmbitusPreset> p = [] {
            std::vector<AmbitusPreset> v;
            v.push_back ({ "The full range (20 Hz - 12 kHz)", kDefaultPitchLo, kDefaultPitchHi });
            // the 1990s UPIC's whole range, infrasonic to ultrasonic
            v.push_back ({ "The whole world (0.01 Hz - 22 kHz)", kMinFreq, kMaxFreq });
            // six octaves around middle C: C1 - C7
            v.push_back ({ "Six octaves (C1 - C7)", 261.63 / 8.0, 261.63 * 8.0 });
            // three octaves: C2 - C5
            v.push_back ({ "Three octaves (C2 - C5)", 261.63 / 4.0, 261.63 * 4.0 });
            // one octave: C3 - C4
            v.push_back ({ "One octave (C3 - C4)", 261.63 / 2.0, 261.63 * 2.0 });
            // a semitone: C4 - C4 sharp
            v.push_back ({ "A semitone (C4 - C#4)", 261.63,
                           261.63 * std::pow (2.0, 1.0 / 12.0) });
            return v;
        }();
        return p;
    }

    inline juce::Colour paletteColour(int i)
    {
        static const juce::Colour cols[] = {
            juce::Colour(0xff5ac8fa), juce::Colour(0xffff9f0a), juce::Colour(0xffff375f),
            juce::Colour(0xff30d158), juce::Colour(0xffbf5af2), juce::Colour(0xffffd60a),
            juce::Colour(0xff64d2ff), juce::Colour(0xffff6482), juce::Colour(0xff0a84ff),
            juce::Colour(0xff30b0c7), juce::Colour(0xffac8e68), juce::Colour(0xff98989f),
            juce::Colour(0xff7d7aff), juce::Colour(0xff34c759), juce::Colour(0xffff453a),
            juce::Colour(0xff66d4cf),
        };
        return cols[i % 16];
    }

    //---------------- the point lookups ----------------
    // The index of the first point whose key is >= the searched one, in a
    // point list that is sorted by that key. A binary search, because these
    // lookups run per sample on the audio thread: a piece with a thousand
    // points per arc would otherwise cost a thousand comparisons for every
    // single sample (and a crafted piece is exactly how that happens).
    template <typename PointType, typename KeyType, typename KeyGetter>
    inline size_t findSegment (const std::vector<PointType>& points, KeyType key, KeyGetter keyOf)
    {
        size_t lo = 0, hi = points.size();
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            if (keyOf (points[mid]) < key) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    //---------------- Waveform ----------------
    struct Waveform
    {
        int id = 0;
        juce::String name = "Sine";
        std::vector<float> samples; // size kWaveSize, -1..1
        juce::Colour colour = juce::Colours::cyan;

        Waveform() : samples((size_t) kWaveSize, 0.0f) {}
        Waveform(int i, const juce::String& n, juce::Colour c) : id(i), name(n), samples((size_t) kWaveSize, 0.0f), colour(c) {}

        void makeSine()      { for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * i / kWaveSize); }
        void makeSaw()       { for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = 2.0f * i / kWaveSize - 1.0f; }
        void makeSquare()    { for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = (i < kWaveSize/2) ? 1.0f : -1.0f; }
        void makeTriangle()  { for (int i = 0; i < kWaveSize; ++i) { float p = (float)i / kWaveSize; samples[(size_t)i] = p < 0.25f ? p*4.0f : (p < 0.75f ? 2.0f - p*4.0f : p*4.0f - 4.0f); } }
        void makeNoise(unsigned seed = 22222) { juce::Random r((int)seed + id*101); for (auto& s : samples) s = r.nextFloat() * 2.0f - 1.0f; }
        void makeRandomSmooth(unsigned seed = 12345)
        {
            juce::Random r((int)seed + id*77);
            constexpr int kParts = 12;
            float v[kParts+1]; for (int i=0;i<=kParts;++i) v[i]=r.nextFloat()*2-1;
            for (int i=0;i<kWaveSize;++i){ float p=(float)i/kWaveSize*kParts; int k=(int)p; float f=p-k; float a=v[k],b=v[(k+1)%(kParts+1)]; samples[(size_t)i]=a+(b-a)*(f*f*(3-2*f)); }
        }
        void normalize()
        {
            float m = 0.0f; for (auto s : samples) m = juce::jmax(m, std::abs(s));
            if (m > 1e-6f) for (auto& s : samples) s /= m;
        }
        void smooth(int passes = 1)
        {
            for (int p = 0; p < passes; ++p)
            {
                std::vector<float> c = samples;
                for (int i = 0; i < kWaveSize; ++i)
                    samples[(size_t)i] = (c[(size_t)((i-1+kWaveSize)%kWaveSize)] + c[(size_t)i]*2.0f + c[(size_t)((i+1)%kWaveSize)]) * 0.25f;
            }
        }
        float valueAt(double phase01) const // phase 0..1, linear interp, loop
        {
            double p = phase01 - std::floor(phase01);
            double idx = p * kWaveSize;
            int i0 = (int) idx % kWaveSize;
            int i1 = (i0 + 1) % kWaveSize;
            float f = (float)(idx - std::floor(idx));
            return samples[(size_t)i0] * (1.0f - f) + samples[(size_t)i1] * f;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id);
            o->setProperty("name", name);
            o->setProperty("colour", (int) colour.getARGB());
            juce::Array<juce::var> arr;
            arr.ensureStorageAllocated(kWaveSize);
            for (auto s : samples) arr.add((double) s);
            o->setProperty("samples", arr);
            return juce::var(o);
        }
        static Waveform fromVar(const juce::var& v, int fallbackId)
        {
            Waveform w;
            w.id = (int) v.getProperty("id", fallbackId);
            w.name = v.getProperty("name", "Wave").toString();
            w.colour = juce::Colour((juce::uint32)(int) v.getProperty("colour", (int)0xff5ac8fa));
            w.samples.assign((size_t)kWaveSize, 0.0f);
            if (auto* arr = v.getProperty("samples", juce::var()).getArray())
                for (int i = 0; i < juce::jmin(kWaveSize, arr->size()); ++i)
                    w.samples[(size_t)i] = (float)(double)(*arr)[i];
            return w;
        }
    };

    //---------------- Envelope ----------------
    struct EnvPoint { float x = 0.0f; float y = 0.0f; }; // x: 0..1 over arc life, y: 0..1 gain

    struct Envelope
    {
        int id = 0;
        juce::String name = "Flat";
        std::vector<EnvPoint> points; // sorted by x
        juce::Colour colour = juce::Colours::orange;

        Envelope() { points = { {0,1}, {1,1} }; }
        Envelope(int i, const juce::String& n, juce::Colour c) : id(i), name(n), colour(c) { points = { {0,1}, {1,1} }; }

        float valueAt(float t) const
        {
            if (points.empty()) return 1.0f;
            t = juce::jlimit(0.0f, 1.0f, t);
            if (t <= points.front().x) return points.front().y;
            if (t >= points.back().x)  return points.back().y;
            // The points are sorted by x, so the segment is found with a
            // binary search instead of a linear scan (this runs per sample).
            const size_t i = findSegment (points, t, [] (const EnvPoint& p) { return p.x; });
            const size_t i0 = i - 1, i1 = i;
            float x0 = points[i0].x, y0 = points[i0].y;
            float x1 = points[i1].x, y1 = points[i1].y;
            if (x1 <= x0) return y1;
            float f = (t - x0) / (x1 - x0);
            return y0 + (y1 - y0) * f;
        }
        void presetFlat()      { points = { {0,1}, {1,1} }; }
        void presetPercussive(){ points = { {0,0}, {0.02f,1}, {0.25f,0.4f}, {0.7f,0.15f}, {1,0} }; }
        void presetSwell()     { points = { {0,0}, {0.4f,0.2f}, {0.7f,1}, {1,0} }; }
        void presetGate()      { points = { {0,1}, {0.45f,1}, {0.5f,0}, {1,0} }; }
        void presetStrings()   { points = { {0,0}, {0.15f,1}, {0.8f,0.9f}, {1,0} }; }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id);
            o->setProperty("name", name);
            o->setProperty("colour", (int) colour.getARGB());
            juce::Array<juce::var> arr;
            for (auto& p : points) { auto* po = new juce::DynamicObject(); po->setProperty("x", p.x); po->setProperty("y", p.y); arr.add(juce::var(po)); }
            o->setProperty("points", arr);
            return juce::var(o);
        }
        static Envelope fromVar(const juce::var& v, int fallbackId)
        {
            Envelope e; e.id = (int) v.getProperty("id", fallbackId);
            e.name = v.getProperty("name", "Env").toString();
            e.colour = juce::Colour((juce::uint32)(int) v.getProperty("colour", (int)0xffff9f0a));
            e.points.clear();
            if (auto* arr = v.getProperty("points", juce::var()).getArray())
                for (auto& pv : *arr)
                {
                    if ((int) e.points.size() >= limits::kMaxPointsPerEnvelope) break;
                    e.points.push_back({ limits::clampFinite((float)(double)pv.getProperty("x", 0), 0.0f, 1.0f),
                                         limits::clampFinite((float)(double)pv.getProperty("y", 1), 0.0f, 1.0f) });
                }
            if (e.points.empty()) e.presetFlat();
            std::sort(e.points.begin(), e.points.end(), [](auto& a, auto& b){ return a.x < b.x; });
            return e;
        }
    };

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
        // Both are session state, like the pitch modes and the grids: they
        // are NOT written to the piece (the format is untouched).
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

        //---------------- the frequency space (v1.33) ----------------
        // Which of the page's four spaces this arc is played in (0..3).
        // The UPIC assigned a table to an arc "the same way as one would
        // assign the waveform or envelope to it"; the space itself is the
        // page's, the name is the arc's.
        int space = 0;

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

        // log-domain interpolation so glissandi sound linear in pitch
        double freqAt(double t) const
        {
            if (points.empty()) return 440.0;
            if (t <= points.front().t) return points.front().freq;
            if (t >= points.back().t)  return points.back().freq;
            // The points are sorted by t, so the segment is found with a
            // binary search instead of a linear scan: this runs per sample
            // on the audio thread, and a crafted piece with a thousand
            // points per arc would otherwise cost a thousand comparisons
            // for every sample.
            const size_t i = findSegment (points, t, [] (const ArcPoint& p) { return p.t; });
            const size_t i0 = i - 1, i1 = i;
            double t0 = points[i0].t, f0 = points[i0].freq;
            double t1 = points[i1].t, f1 = points[i1].freq;
            if (t1 <= t0) return f1;
            double f = (t - t0) / (t1 - t0);
            double l0 = std::log2(juce::jmax(1.0, f0)), l1 = std::log2(juce::jmax(1.0, f1));
            return std::pow(2.0, l0 + (l1 - l0) * f);
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
            if (points.empty()) return 1.0f;
            if (t <= points.front().t) return points.front().i;
            if (t >= points.back().t)  return points.back().i;
            // binary search, like freqAt() above
            const size_t i = findSegment (points, t, [] (const ArcPoint& p) { return p.t; });
            const size_t i0 = i - 1, i1 = i;
            double t0 = points[i0].t, t1 = points[i1].t;
            if (t1 <= t0) return points[i1].i;
            float a = points[i0].i, b = points[i1].i;
            float f = (float) ((t - t0) / (t1 - t0));
            return a + (b - a) * f;
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
            // v1.33: the frequency space the arc is played in
            if (space != 0) o->setProperty("sp", space);
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
            // v1.33: the frequency space (absent in older pieces -> the
            // page's own range, which is the identity)
            a.space = juce::jlimit (0, 3, (int) v.getProperty ("sp", 0));
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
            return a;
        }
    };

    //---------------- Sieve quantizer (Xenakis) ----------------
    // Allowed pitches on the MIDI-note axis: n mod m ∈ remainders.
    // Returns the nearest allowed frequency; identity when inert.
    inline double sieveQuantizeHz(double hz, int m, const std::vector<int>& remainders)
    {
        if (m < 2 || remainders.empty() || hz <= 0.0) return hz;
        const double n = freqToMidi(hz);
        double best = n, bestDist = 1.0e18;
        for (int r : remainders)
        {
            const int rr = ((r % m) + m) % m;
            const int q = (int) std::floor((n - rr) / (double) m + 0.5); // nearest integer ≡ rr (mod m)
            const double cand = (double)(q * m + rr);
            const double d = std::abs(cand - n);
            if (d < bestDist) { bestDist = d; best = cand; }
        }
        return juce::jlimit(kMinFreq, kMaxFreq, midiToFreq(best));
    }

    //---------------- the discrete frequency space (v1.32) ----------------
    // Move a pitch through a temperament of `divisions` equal steps per
    // octave, anchored at `refFreq` (C4 by default): the continuum becomes
    // a staircase. 0 divisions leaves the pitch alone.
    //
    // This is the UPIC's "discrete/non-discrete" function. It is not the
    // sieve: the sieve snaps the drawn points, this snaps the pitch that
    // plays, so a glissando - a straight diagonal line - is heard as a
    // staircase of discrete steps.
    inline double applyTemperament (double hz, int divisions, double refFreq)
    {
        if (divisions <= 0 || ! (hz > 0.0) || ! std::isfinite (hz)) return hz;
        if (! (refFreq > 0.0) || ! std::isfinite (refFreq)) refFreq = kDefaultRefFreq;
        const double l = std::log2 (hz / refFreq);
        const double stepped = std::round (l * (double) divisions) / (double) divisions;
        // exp2, not pow(2, x): this runs per sample on the audio thread
        const double out = refFreq * std::exp2 (stepped);
        // a temperament must never move the pitch out of the world: a NaN or
        // a zero here would silence the arc.
        return (std::isfinite (out) && out > 0.0) ? out : hz;
    }

    //---------------- the frequency spaces (v1.33) ----------------
    // The UPIC's real-time versions had a "Frequency Table": "a composer
    // could choose to operate within up to four different simultaneous
    // tables or grids that could be superimposed on each page. These grids
    // were invisible to the eye, however, and accessed by assigning one of
    // four tables to whatever arc was being drawn, the same way as one
    // would assign the waveform or envelope to it. An arc could thus be
    // 'placed' within different frequency spaces determined by the
    // composer." (Robindoré, in "From Xenakis's UPIC to Graphic Notation
    // Today", Hatje Cantz 2024)
    //
    // So a space is a range of the frequency axis, and the page's vertical
    // axis (its ambitus) is mapped into it: the same drawing lands in a
    // different frequency space, with its intervals scaled or its register
    // moved. The four spaces live on the page; an arc names the one it is
    // played in, exactly as it names its wave or its envelope.
    //
    // A space is stored as a PRESET (a span factor and a centre shift in
    // octaves) relative to the page's ambitus, so that changing the
    // ambitus moves the spaces with it.
    struct FreqSpacePreset
    {
        const char* name = "";
        double span  = 1.0;  // the space's span, as a factor of the ambitus's
        double shift = 0.0;  // the space's centre, in octaves from the ambitus's
    };

    inline const std::vector<FreqSpacePreset>& freqSpacePresets()
    {
        static const std::vector<FreqSpacePreset> p = {
            { "The page's range",      1.0,  0.0 },
            { "Half the range",        0.5,  0.0 },
            { "Double the range",      2.0,  0.0 },
            { "An octave up",          1.0,  1.0 },
            { "An octave down",        1.0, -1.0 },
            { "Half, an octave up",    0.5,  1.0 },
            { "Double, an octave down", 2.0, -1.0 },
        };
        return p;
    }

    // The range of a space, given the page's ambitus.
    inline void freqSpaceRange (double ambLo, double ambHi, int preset,
                                double& lo, double& hi)
    {
        const auto& presets = freqSpacePresets();
        const FreqSpacePreset p = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, preset)];
        const double a0 = std::log2 (juce::jmax (1.0, ambLo));
        const double a1 = std::log2 (juce::jmax (1.0, ambHi));
        const double centre = (a0 + a1) * 0.5 + p.shift;
        const double half   = (a1 - a0) * 0.5 * p.span;
        lo = std::pow (2.0, centre - half);
        hi = std::pow (2.0, centre + half);
    }

    //---------------- Magnetic snap: stickiness + time grid (UPISketch-style) ----
    // Blend `value` toward `snapped` by stickiness in 0..1
    // (0 = free drawing, 1 = fully magnetic — the "Stickiness" slider).
    inline double stickify(double value, double snapped, double stickiness)
    {
        stickiness = juce::jlimit(0.0, 1.0, stickiness);
        return value + (snapped - value) * stickiness;
    }

    // Time grid: div = divisions per second (0 = off). Snaps to nearest step.
    inline double timeGridSnap(double t, double div)
    {
        if (div <= 0.0) return t;
        return std::floor(t * div + 0.5) / div;
    }

    // Click-free transport stop: gain ramp as a function of time-to-end.
    // 1 well before the fade window, linear down to 0 at the end.
    inline double stopFadeGain(double tToEnd, double fadeSec)
    {
        if (fadeSec <= 0.0) return tToEnd > 0.0 ? 1.0 : 0.0;
        return juce::jlimit(0.0, 1.0, tToEnd / fadeSec);
    }

    //---------------- Sieve preset library ----------------
    // Ready-made Xenakian/Messiaen/etc. sieves (UPISketch ships a library of
    // examples; custom m/R stay editable in the sieve fields).
    // The sieve is n mod m ∈ R on the semitone axis, so the chromatic set is
    // all twelve remainders - {0} alone would leave only the C's.
    struct SievePreset { juce::String name; int m; std::vector<int> remainders; };
    inline std::vector<SievePreset> sievePresets()
    {
        return {
            { "Chromatic",                 12, {0,1,2,3,4,5,6,7,8,9,10,11} },
            { "Whole-tone",                12, {0,2,4,6,8,10} },
            { "Major",                     12, {0,2,4,5,7,9,11} },
            { "Minor",                     12, {0,2,3,5,7,8,10} },
            { "Pentatonic",                12, {0,2,4,7,9} },
            { "Octatonic (Messiaen m2)",   12, {0,1,3,4,6,7,9,10} },
            { "Lydian",                    12, {0,2,4,6,7,9,11} },
            { "Hexatonic",                 12, {0,1,3,4,6,7} },
        };
    }

    //---------------- Sieve zones: translucent magnetic regions on the page ------
    // UPISketch 4.5 "Magnetic Sieves": place sieves as translucent regions on
    // the page; inside a region drawn gestures snap to the zone's pitch sieve
    // and time grid. Zones are page data (persisted in .upic.json, absent in
    // old files -> empty).
    struct SieveZone
    {
        int id = 1;
        double t0 = 0, t1 = 0;     // seconds (order-independent)
        double f0 = 20, f1 = 20;   // Hz     (order-independent)
        int m = 12;
        std::vector<int> remainders { 0 }; // empty = pitch inert inside the zone
        double timeGrid = 0;       // divisions/sec inside the zone (0 = none)
        // v1.33.2: a zone carries its own stickiness, so "global" really is
        // global only outside the zones. -1 = follow the global stick (the
        // behaviour of old pieces, which had no per-zone stick).
        double stickiness = -1.0;

        double effectiveStickiness(double globalStick) const
        {
            const double s = (stickiness >= 0.0) ? stickiness : globalStick;
            return juce::jlimit(0.0, 1.0, s);
        }

        bool contains(double t, double f) const
        {
            return t >= juce::jmin(t0, t1) && t <= juce::jmax(t0, t1)
                && f >= juce::jmin(f0, f1) && f <= juce::jmax(f0, f1);
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id);
            o->setProperty("t0", juce::jmin(t0, t1));
            o->setProperty("t1", juce::jmax(t0, t1));
            o->setProperty("f0", juce::jmin(f0, f1));
            o->setProperty("f1", juce::jmax(f0, f1));
            o->setProperty("m", m);
            juce::Array<juce::var> ra;
            for (int r : remainders) ra.add(r);
            o->setProperty("r", ra);
            if (timeGrid > 0) o->setProperty("timeGrid", timeGrid);
            if (stickiness >= 0.0) o->setProperty("stick", stickiness);
            return juce::var(o);
        }
        static SieveZone fromVar(const juce::var& v)
        {
            SieveZone z;
            z.id = (int) v.getProperty("id", 1);
            z.t0 = limits::clampFinite((double) v.getProperty("t0", 0.0), 0.0, kMaxPageSeconds);
            z.t1 = limits::clampFinite((double) v.getProperty("t1", 0.0), 0.0, kMaxPageSeconds);
            z.f0 = limits::clampFinite((double) v.getProperty("f0", kMinFreq), kMinFreq, kMaxFreq);
            z.f1 = limits::clampFinite((double) v.getProperty("f1", kMaxFreq), kMinFreq, kMaxFreq);
            z.m = juce::jlimit(2, 120, (int) v.getProperty("m", 12));
            z.timeGrid = limits::clampFinite((double) v.getProperty("timeGrid", 0.0), 0.0, 64.0);
            {
                const double st = (double) v.getProperty("stick", -1.0);
                z.stickiness = (st >= 0.0) ? limits::clampFinite(st, 0.0, 1.0) : -1.0;
            }
            std::vector<int> rs;
            if (auto* arr = v.getProperty("r", juce::var()).getArray())
                for (auto& rv : *arr)
                {
                    if ((int) rs.size() >= limits::kMaxSieveRemainders) break;
                    rs.push_back((((int) rv % z.m) + z.m) % z.m);
                }
            z.remainders = std::move(rs);
            return z;
        }
    };

    // Topmost zone wins (later zones paint on top), so magnetism matches what
    // the user sees when regions overlap.
    inline const SieveZone* zoneAt(const std::vector<SieveZone>& zones, double t, double f)
    {
        for (auto it = zones.rbegin(); it != zones.rend(); ++it)
            if (it->contains(t, f)) return &*it;
        return nullptr;
    }

    //---------------- Unified quantization: global params + zone override --------
    // Precedence: zone sieve > global sieve > plain chromatic snap; the zone's
    // time grid overrides the global one. A zone's own stick (when set) also
    // overrides the global stick, so "global" only means outside the zones.
    inline void quantizePoint(bool chromaticSnap, bool sieveOn, int sieveM,
                              const std::vector<int>& sieveR, double timeGrid,
                              double stickiness, const std::vector<SieveZone>& zones,
                              double& t, double& f)
    {
        const SieveZone* z = zoneAt(zones, t, f);
        const double stick = (z != nullptr) ? z->effectiveStickiness(stickiness) : stickiness;
        double target = f;
        if (z != nullptr)          target = sieveQuantizeHz(f, z->m, z->remainders);
        else if (sieveOn)          target = sieveQuantizeHz(f, sieveM, sieveR);
        else if (chromaticSnap)
        {
            double mn = std::round(freqToMidi(f));
            mn = juce::jlimit(12.0, 120.0, mn);
            target = midiToFreq(mn);
        }
        f = juce::jlimit(kMinFreq, kMaxFreq, stickify(f, target, stick));

        const double g = (z != nullptr) ? z->timeGrid : timeGrid;
        t = juce::jmax(0.0, stickify(t, timeGridSnap(t, g), stick));
    }

    //---------------- Freehand curve simplification (Ramer-Douglas-Peucker) ------
    // Raw strokes carry hundreds of points; simplify to a reasonable number of
    // control points so curves stay editable (UPISketch 4.0 feature). Distance
    // is measured in a normalized (time, log-freq) box, so eps is scale-free:
    // 0.006 ~ a few screen pixels for typical pages.
    inline void simplifyArcPoints(std::vector<ArcPoint>& pts, double eps)
    {
        const size_t n = pts.size();
        if (n < 3 || eps <= 0.0) return;

        const double t0 = pts.front().t, tSpan = juce::jmax(1e-9, pts.back().t - t0);
        double lmin = 1e18, lmax = -1e18;
        for (auto& p : pts)
        {
            const double l = std::log2(juce::jmax(1.0, p.freq));
            lmin = juce::jmin(lmin, l); lmax = juce::jmax(lmax, l);
        }
        const double lSpan = juce::jmax(1e-9, lmax - lmin);
        auto nrm = [&](const ArcPoint& p, double& x, double& y) {
            x = (p.t - t0) / tSpan;
            y = (std::log2(juce::jmax(1.0, p.freq)) - lmin) / lSpan;
        };

        std::vector<char> keep(n, 0);
        keep[0] = keep[n - 1] = 1;
        struct Seg { size_t a, b; };
        std::vector<Seg> stack { { 0, n - 1 } };
        while (! stack.empty())
        {
            const Seg s = stack.back(); stack.pop_back();
            if (s.b <= s.a + 1) continue;
            double ax, ay, bx, by;
            nrm(pts[s.a], ax, ay); nrm(pts[s.b], bx, by);
            const double dx = bx - ax, dy = by - ay;
            const double len = std::sqrt(dx * dx + dy * dy);
            double worst = 0.0; size_t wi = s.a + 1;
            for (size_t i = s.a + 1; i < s.b; ++i)
            {
                double px, py; nrm(pts[i], px, py);
                double d;
                if (len > 1e-12) d = std::abs(dx * (ay - py) - (ax - px) * dy) / len;
                else             d = std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
                if (d > worst) { worst = d; wi = i; }
            }
            if (worst > eps)
            {
                keep[wi] = 1;
                stack.push_back({ s.a, wi });
                stack.push_back({ wi, s.b });
            }
        }
        std::vector<ArcPoint> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) if (keep[i]) out.push_back(pts[i]);
        pts = std::move(out);
    }

    //---------------- Page sequencer ----------------
    // Next page in linear order, wrapping when asked; -1 = stop after this page.
    inline int seqNextPage(int current, int numPages, bool wrapAtEnd)
    {
        if (numPages <= 0) return -1;
        const int next = current + 1;
        if (next >= numPages) return wrapAtEnd ? 0 : -1;
        return next;
    }

    //---------------- Procedural arc generation (nUPIC-style) -------------------
    // Pure generators (Stochastic/Convergent Arborescence, Echelon Construction
    // after Bourbaki, Ordinal Spine with branching — nUPIC's procedural arc
    // family). They return bare arcs (id 0, voice fields untouched); the caller
    // stamps page ids and voice settings.
    struct GenParams { double t0 = 0.0, t1 = 10.0; double f0 = 90.0, f1 = 3000.0; };

    namespace gen
    {
        inline bool usable(const GenParams& p)
        { return p.t1 - p.t0 >= 0.2 && p.f1 > p.f0 && p.f0 >= kMinFreq && p.f1 <= kMaxFreq; }

        inline double clampLog(double l, const GenParams& p)
        { return juce::jlimit(std::log2(p.f0), std::log2(p.f1), l); }

        // Build an arc from parallel time/log2-freq arrays; drops duplicate
        // timestamps (keeps t strictly increasing) and clamps to the range.
        inline Arc makeArc(const std::vector<double>& ts, const std::vector<double>& ls, const GenParams& p)
        {
            Arc a;
            const size_t n = juce::jmin(ts.size(), ls.size());
            for (size_t i = 0; i < n; ++i)
            {
                const double t = juce::jlimit(p.t0, p.t1, ts[i]);
                if (! a.points.empty() && t <= a.points.back().t + 1e-9) continue;
                a.points.push_back({ t, std::pow(2.0, clampLog(ls[i], p)), 1.0f });
            }
            if (a.points.size() == 1)
                a.points.push_back({ juce::jmin(p.t1, a.points[0].t + 0.05), a.points[0].freq, 1.0f });
            return a;
        }

        // helper: log-freq line sampling
        inline void sampleLine(std::vector<double>& ts, std::vector<double>& ls,
                               double ta, double la, double tb, double lb,
                               double jitter, juce::Random& rng, int n)
        {
            for (int i = 0; i <= n; ++i)
            {
                const double u = (double) i / (double) n;
                double l = la + (lb - la) * u;
                if (i > 0 && i < n && jitter > 0.0)
                    l += (rng.nextFloat() * 2.0f - 1.0f) * jitter;
                ts.push_back(ta + (tb - ta) * u);
                ls.push_back(l);
            }
        }
    }

    // 1) Stochastic arborescence: a random-walk trunk that branches into child
    //    walks — Xenakis-style proliferating tree forms. The first spawn of
    //    each chain is forced, so the result is always a real tree (>=3 arcs).
    inline std::vector<Arc> genStochasticArborescence(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        struct Walk { double t0, t1, l0, slope, jitter; int depth; bool mustSpawn; };
        std::vector<Walk> queue;
        queue.push_back({ p.t0, p.t1, 0.5 * (lo + hi),
                          (rng.nextFloat() - 0.5f) * 0.5, 0.35, 0, true });
        const int steps = juce::jlimit(4, 24, (int) std::round(dur / 0.4));

        while (! queue.empty())
        {
            const Walk w = queue.front();
            queue.erase(queue.begin());
            if (w.t1 - w.t0 < 0.1) continue;

            const int n = juce::jmax(2, (int) std::round(steps * (w.t1 - w.t0) / dur));
            const double dt = (w.t1 - w.t0) / n;
            std::vector<double> ts, ls;
            double l = w.l0;
            for (int i = 0; i <= n; ++i)
            {
                ts.push_back(w.t0 + dt * i);
                ls.push_back(g::clampLog(l, p));
                l += w.slope * dt + (rng.nextFloat() * 2.0f - 1.0f) * w.jitter;
            }
            out.push_back(g::makeArc(ts, ls, p));

            // branch: up to two children (forced first spawn, deeper = rarer)
            if (w.depth < 2 && out.size() + queue.size() < 8)
            {
                bool spawned = false;
                for (int c = 0; c < 2; ++c)
                {
                    if (queue.size() + out.size() >= 8) break;
                    const bool doSpawn = (w.mustSpawn && ! spawned)
                                         || rng.nextFloat() <= 0.55f;
                    if (! doSpawn) continue;
                    spawned = true;
                    const int bi = 1 + rng.nextInt(juce::jmax(1, n - 1));
                    const double bt = w.t0 + dt * bi;
                    const double bend = bt + (w.t1 - bt) * (0.4f + rng.nextFloat() * 0.45f);
                    if (bend - bt < 0.15) continue;
                    queue.push_back({ bt, bend, ls[(size_t) juce::jmin(bi, n)],
                                      -w.slope * 0.4f + (rng.nextFloat() - 0.5f) * 0.8f,
                                      w.jitter * 0.8f, w.depth + 1, w.depth + 1 < 2 });
                }
            }
        }
        return out;
    }

    // 2) Convergent arborescence: staggered branches that all converge onto one
    //    target pitch at the end (mass narrowing to a line).
    inline std::vector<Arc> genConvergentArborescence(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);
        const double target = 0.5 * (lo + hi);

        const int nBranch = 4 + rng.nextInt(3); // 4..6
        for (int b = 0; b < nBranch; ++b)
        {
            const double u0 = (nBranch > 1) ? (double) b / (double) (nBranch - 1) : 0.5;
            const double start = juce::jlimit(lo, hi,
                    target + (u0 - 0.5) * 2.0 * 0.45 * (hi - lo) + (rng.nextFloat() - 0.5f) * 0.3);
            const double t0 = p.t0 + dur * 0.12 * u0;         // staggered starts
            const double jit = 0.18 * (1.0 - u0 * 0.3);
            std::vector<double> ts, ls;
            const int n = juce::jlimit(4, 24, (int) std::round(dur / 0.4));
            for (int i = 0; i <= n; ++i)
            {
                const double u = (double) i / n;
                const double t = juce::jmin(p.t1, t0 + (p.t1 - t0) * u);
                double l = start + (target - start) * u;        // exact convergence at u=1
                if (i < n) l += (rng.nextFloat() - 0.5f) * 2.0 * jit * (1.0 - u);
                ts.push_back(t);
                ls.push_back(i == n ? target : g::clampLog(l, p));
            }
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }

    // 3) Echelon construction (Bourbaki): parallel staircase ranks — discrete
    //    steps marching in lockstep, staggered in pitch like military ranks.
    inline std::vector<Arc> genEchelon(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        const int ranks  = 4 + rng.nextInt(3);        // 4..6 parallel arcs
        const int nSteps = 5 + rng.nextInt(6);        // 5..10 stair steps
        const double dir = rng.nextBool() ? 1.0 : -1.0;
        const double st  = (1.0 + rng.nextInt(2)) / 12.0;   // 1..2 semitones per step
        const double gap = (1.5 + rng.nextFloat()) / 12.0;  // 1.5..2.5 st between ranks
        const double base = 0.5 * (lo + hi) - dir * 0.5 * st * nSteps;

        const double hold = 0.65; // hold each step, then a quick rise to the next
        for (int r = 0; r < ranks; ++r)
        {
            std::vector<double> ts, ls;
            for (int k = 0; k <= nSteps; ++k)
            {
                const double tk  = p.t0 + dur * ((double) k / nSteps);
                const double tk1 = p.t0 + dur * ((double) (k + 1) / nSteps);
                const double l   = base + r * gap + dir * st * k;
                ts.push_back(tk);  ls.push_back(g::clampLog(l, p));
                if (k < nSteps)
                {
                    const double th = tk + (tk1 - tk) * hold;
                    ts.push_back(th); ls.push_back(g::clampLog(l, p));
                }
            }
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }

    // 4) Ordinal spine with branching: one central spine with branches leaving
    //    it at ordinal positions (1/4, 1/2, 3/4 …), offset by fixed intervals.
    inline std::vector<Arc> genOrdinalSpine(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        // spine: smooth drift through the range with light stochastic shading
        const int n = juce::jlimit(6, 32, (int) std::round(dur / 0.35));
        const double lA = juce::jlimit(lo, hi, 0.5 * (lo + hi) + (rng.nextFloat() - 0.5f) * 0.6 * (hi - lo));
        const double lB = juce::jlimit(lo, hi, 0.5 * (lo + hi) + (rng.nextFloat() - 0.5f) * 0.6 * (hi - lo));
        std::vector<double> spineT, spineL;
        for (int i = 0; i <= n; ++i)
        {
            const double u = (double) i / n;
            const double s = u * u * (3.0 - 2.0 * u); // smoothstep shape
            spineT.push_back(p.t0 + dur * u);
            spineL.push_back(g::clampLog(lA + (lB - lA) * s + (rng.nextFloat() - 0.5f) * 0.05, p));
        }
        out.push_back(g::makeArc(spineT, spineL, p));

        // branches at ordinal positions along the spine
        const int nBranch = 3 + rng.nextInt(2); // 3..4
        const double step = 0.33;               // fixed ordinal interval (octaves)
        for (int b = 0; b < nBranch; ++b)
        {
            const double ub = (b + 1) / (double) (nBranch + 1) + (rng.nextFloat() - 0.5f) * 0.06;
            const int si = juce::jlimit(1, n - 1, (int) std::round(ub * n));
            const double tb = spineT[(size_t) si], lb = spineL[(size_t) si];
            const double tEnd = juce::jmin(p.t1, tb + dur * (0.15 + rng.nextFloat() * 0.2));
            if (tEnd - tb < 0.1) continue;
            const double lEnd = lb + (b % 2 == 0 ? 1.0 : -1.0) * step * (1.0 + b * 0.5)
                                + (rng.nextFloat() - 0.5f) * 0.2;
            std::vector<double> ts, ls;
            g::sampleLine(ts, ls, tb, lb, tEnd, g::clampLog(lEnd, p), 0.0, rng,
                          juce::jmax(2, (int) std::round((tEnd - tb) / 0.3)));
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }

    //---------------- Sample slot (material for realtime pitch-shifted arcs) ----------------
    // Holds one loadable audio sample. Arcs in mode 1 pitch-shift this material
    // along their frequency curve (varispeed: position advances at the pitch
    // ratio). Embedded in .upic.json as base64 PCM16 so pieces stay
    // self-contained (max 60s per slot on load).
    struct SampleSlot
    {
        int id = 1;
        juce::String name;
        double sampleRate = 44100.0;
        // v1.18 (wave 15): the audio buffer is immutable and shared, so a
        // model snapshot can copy a slot by copying one pointer. Never write
        // through it once it is in a slot: replace the pointer instead (the
        // old buffer stays alive while any snapshot still reads it).
        std::shared_ptr<const juce::AudioBuffer<float>> data; // 1..2 ch, seamless loop
        // v1.19 (wave 17): the material plays backwards. Session state, like
        // the pitch modes and the grid - the piece format is untouched (the
        // idea in FUTURE_IDEAS.md says the file format is not part of this
        // slice). The engine mirrors the read, so every pitch mode works.
        bool reversed = false;

        bool loaded() const
        {
            return data != nullptr && data->getNumSamples() > 0 && data->getNumChannels() > 0;
        }
        double durationSec() const
        {
            return (data != nullptr && sampleRate > 0)
                       ? (double) data->getNumSamples() / sampleRate : 0.0;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id);
            o->setProperty("name", name);
            o->setProperty("sampleRate", sampleRate);
            // NB: "reversed" is session state and is deliberately not saved
            const int ch = juce::jmin(2, data != nullptr ? data->getNumChannels() : 0);
            const int n = data != nullptr ? data->getNumSamples() : 0;
            o->setProperty("channels", ch);
            o->setProperty("frames", n);
            if (n > 0 && ch > 0)
            {
                juce::MemoryBlock mb((size_t) n * (size_t) ch * sizeof(short));
                auto* dst = static_cast<short*>(mb.getData());
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < ch; ++c)
                        *dst++ = (short) juce::jlimit(-32767.0f, 32767.0f, data->getSample(c, i) * 32767.0f);
                o->setProperty("data", juce::Base64::toBase64(mb.getData(), mb.getSize()));
            }
            return juce::var(o);
        }
        static SampleSlot fromVar(const juce::var& v, int fallbackId)
        {
            SampleSlot s;
            s.id = juce::jlimit(1, kMaxSampleSlots, (int) v.getProperty("id", fallbackId));
            s.name = v.getProperty("name", "").toString();
            s.sampleRate = limits::clampFinite((double) v.getProperty("sampleRate", 44100.0),
                                               1000.0, limits::kMaxSampleRate);
            const int ch = juce::jlimit(1, 2, (int) v.getProperty("channels", 1));
            // The declared frame count is clamped to the 60-second cap before
            // anything is allocated: a crafted "frames" value must not be able
            // to ask for a gigabyte of buffer.
            const int n = juce::jlimit(0, limits::kMaxFramesPerSample, (int) v.getProperty("frames", 0));
            auto b64 = v.getProperty("data", "").toString();
            // ...and the base64 string is checked against the size it claims
            // to carry BEFORE it is decoded, so an oversized payload is
            // rejected without ever being expanded.
            const size_t expectedBytes = (size_t) n * (size_t) ch * sizeof(short);
            if (n > 0 && ch > 0 && b64.isNotEmpty()
                && b64.getNumBytesAsUTF8() <= expectedBytes * limits::kBase64Overhead)
            {
                juce::MemoryOutputStream mo;
                if (juce::Base64::convertFromBase64(mo, b64)
                    && mo.getDataSize() >= expectedBytes)
                {
                    juce::AudioBuffer<float> buf(ch, n);
                    const auto* src = static_cast<const short*>(mo.getData());
                    for (int i = 0; i < n; ++i)
                        for (int c = 0; c < ch; ++c)
                            buf.setSample(c, i, (float) (*src++) / 32768.0f);
                    s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                }
            }
            return s;
        }
    };

    // Fold the tail of a buffer into its head with a smooth cosine crossfade so
    // the material loops without a click — playback reads may cross the seam.
    // Result is (original length - crossfade) samples, seamless end-to-start.
    inline void makeSeamlessLoop(juce::AudioBuffer<float>& buf, double sr)
    {
        const int n = buf.getNumSamples();
        const int ch = buf.getNumChannels();
        if (n < 64 || ch < 1 || sr <= 0.0) return;
        const int x = juce::jlimit(1, n / 4, (int) (0.05 * sr)); // <= 50 ms fade
        juce::AudioBuffer<float> out(ch, n - x);
        for (int c = 0; c < ch; ++c)
        {
            const float* s = buf.getReadPointer(c);
            float* d = out.getWritePointer(c);
            for (int i = 0; i < n - x; ++i)
            {
                if (i >= x) { d[i] = s[i]; continue; }
                const float u = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * (float) i / (float) x);
                d[i] = s[i] * u + s[i + (n - x)] * (1.0f - u);
            }
        }
        buf = std::move(out);
    }

    // Live take -> sample material: copy (up to stereo) + seamless loop, the
    // same treatment as file-loaded slots so takes play back identically.
    inline SampleSlot slotFromCapture(int id, const juce::String& name,
                                      const juce::AudioBuffer<float>& take, double rate)
    {
        SampleSlot s;
        s.id = id;
        s.name = name;
        s.sampleRate = rate > 0.0 ? rate : 44100.0;
        const int n = take.getNumSamples();
        const int ch = juce::jmin(2, take.getNumChannels());
        if (n > 0 && ch > 0)
        {
            juce::AudioBuffer<float> buf(ch, n);
            for (int c = 0; c < ch; ++c)
                buf.copyFrom(c, 0, take, c, 0, n);
            makeSeamlessLoop(buf, s.sampleRate);
            s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        return s;
    }

    //---------------- the digitised wave page (U4) ----------------
    // The real UPIC could fill the wave page from a recording: the wavetable
    // was digitised through an A/D converter (Xenakis sampled the bellowing
    // of bulls for Taurhiphanie, 1988). The take becomes one loop of the
    // oscillator - the whole take is resampled into a single cycle of the
    // wavetable, which is what the oscillator then plays round and round.
    inline Waveform waveFromTake(const juce::AudioBuffer<float>& take, int id,
                                 const juce::String& name)
    {
        Waveform w;
        w.id = id;
        w.name = name;
        w.samples.assign ((size_t) kWaveSize, 0.0f);

        const int n = take.getNumSamples();
        if (n <= 0) return w;

        for (int i = 0; i < kWaveSize; ++i)
        {
            const double pos = (double) i * (double) n / (double) kWaveSize;
            const int i0 = juce::jlimit (0, n - 1, (int) pos);
            const int i1 = juce::jmin (i0 + 1, n - 1);
            const float f = (float) (pos - (double) i0);
            const float v0 = take.getSample (0, i0);
            const float v1 = take.getSample (0, i1);
            w.samples[(size_t) i] = v0 + (v1 - v0) * f;
        }
        w.normalize();
        return w;
    }

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
        static Page fromVar(const juce::var& v)
        {
            Page p;
            p.durationSec = limits::clampFinite((double) v.getProperty("duration", 30.0),
                                              kMinPageSeconds, kMaxPageSeconds);
            p.tempoBpm    = limits::clampFinite((double) v.getProperty("tempo", 120.0), 20.0, 400.0);
            p.nextArcId = (int) v.getProperty("nextArcId", 1);
            if (auto* arr = v.getProperty("arcs", juce::var()).getArray())
                for (auto& av : *arr)
                {
                    if ((int) p.arcs.size() >= limits::kMaxArcsPerPage) break;
                    p.arcs.push_back(Arc::fromVar(av));
                }
            // Files written by other tools may not carry nextArcId: make sure the
            // next generated id can never collide with an id already in use
            // (a collision would make select/delete hit the wrong arc).
            for (auto& a : p.arcs)
                p.nextArcId = juce::jmax(p.nextArcId, a.id + 1);
            // zones absent in old / Python-written files -> no zones, as before
            p.nextZoneId = (int) v.getProperty("nextZoneId", 1);
            if (auto* zarr = v.getProperty("zones", juce::var()).getArray())
                for (auto& zv : *zarr)
                {
                    if ((int) p.zones.size() >= limits::kMaxSieveZones) break;
                    p.zones.push_back(SieveZone::fromVar(zv));
                }
            for (auto& z : p.zones)
                p.nextZoneId = juce::jmax(p.nextZoneId, z.id + 1);
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
