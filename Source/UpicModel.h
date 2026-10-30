#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>

//==============================================================================
// UPIC data model — mirrors Xenakis' UPIC concepts:
//  Score Page (time=X, pitch=Y log) -> Arcs (pitch curves)
//  Wavetable (single-cycle waveform) per arc
//  Envelope (amplitude 0..1 over arc lifetime) per arc
//==============================================================================

namespace upic
{
    constexpr int kWaveSize = 1024;
    constexpr double kMinFreq = 20.0;
    constexpr double kMaxFreq = 12000.0;
    constexpr int kMaxWaveforms = 16;
    constexpr int kMaxEnvelopes = 16;
    constexpr int kMaxSampleSlots = 8;
    constexpr double kDefaultRefFreq = 261.63; // C4: base pitch for sample pitch-shift arcs

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
            for (size_t i = 1; i < points.size(); ++i)
            {
                if (t <= points[i].x)
                {
                    float x0 = points[i-1].x, y0 = points[i-1].y;
                    float x1 = points[i].x,   y1 = points[i].y;
                    if (x1 <= x0) return y1;
                    float f = (t - x0) / (x1 - x0);
                    return y0 + (y1 - y0) * f;
                }
            }
            return points.back().y;
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
                    e.points.push_back({ (float)(double)pv.getProperty("x", 0), (float)(double)pv.getProperty("y", 1) });
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
        double pmRatio = 2.0;        // PM: modulator / carrier
        double pmIndex = 1.5;        // PM: peak modulation index
        int    pmIndexEnv = 0;       // PM: envelope shaping the index (0 = flat)
        double pmFeedback = 0.0;     // PM: feedback coefficient (0..1)
        int    pmOps = 2;          // PM: 2 or 4 operators
        double pmR2 = 2.0, pmR3 = 3.0, pmR4 = 5.0;  // PM: modulator ratios (4-op)
        double pmI2 = 1.0, pmI3 = 1.0, pmI4 = 1.0;  // PM: stage indices (4-op)

        double startTime() const { return points.empty() ? 0 : points.front().t; }
        double endTime()   const { return points.empty() ? 0 : points.back().t; }

        // log-domain interpolation so glissandi sound linear in pitch
        double freqAt(double t) const
        {
            if (points.empty()) return 440.0;
            if (t <= points.front().t) return points.front().freq;
            if (t >= points.back().t)  return points.back().freq;
            for (size_t i = 1; i < points.size(); ++i)
                if (t <= points[i].t)
                {
                    double t0 = points[i-1].t, f0 = points[i-1].freq;
                    double t1 = points[i].t,   f1 = points[i].freq;
                    if (t1 <= t0) return f1;
                    double f = (t - t0) / (t1 - t0);
                    double l0 = std::log2(juce::jmax(1.0, f0)), l1 = std::log2(juce::jmax(1.0, f1));
                    return std::pow(2.0, l0 + (l1 - l0) * f);
                }
            return points.back().freq;
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
            for (size_t i = 1; i < points.size(); ++i)
                if (t <= points[i].t)
                {
                    double t0 = points[i-1].t, t1 = points[i].t;
                    if (t1 <= t0) return points[i].i;
                    float a = points[i-1].i, b = points[i].i;
                    float f = (float) ((t - t0) / (t1 - t0));
                    return a + (b - a) * f;
                }
            return points.back().i;
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
            a.intensity = (float)(double) v.getProperty("intensity", 0.8);
            a.muted = (bool) v.getProperty("muted", false);
            // absent in old / Python-written files -> oscillator mode, as before
            a.mode = juce::jlimit(0, 3, (int) v.getProperty("mode", 0));
            a.pmRatio    = (double) v.getProperty("pmRatio", 2.0);
            a.pmIndex    = (double) v.getProperty("pmIndex", 1.5);
            a.pmIndexEnv = (int)    v.getProperty("pmIndexEnv", 0);
            a.pmFeedback  = (double) v.getProperty("pmFeedback", 0.0);
            a.pmOps = juce::jlimit (2, 4, (int) v.getProperty ("pmOps", 2));
            a.pmR2  = (double) v.getProperty ("pmR2", 2.0);  a.pmR3 = (double) v.getProperty ("pmR3", 3.0);  a.pmR4 = (double) v.getProperty ("pmR4", 5.0);
            a.pmI2  = (double) v.getProperty ("pmI2", 1.0);  a.pmI3 = (double) v.getProperty ("pmI3", 1.0);  a.pmI4 = (double) v.getProperty ("pmI4", 1.0);
            a.sampleId = juce::jlimit(1, kMaxSampleSlots, (int) v.getProperty("sampleId", 1));
            a.refFreq = juce::jlimit(20.0, 4000.0, (double) v.getProperty("refFreq", 261.63));
            if (auto* arr = v.getProperty("points", juce::var()).getArray())
                for (auto& pv : *arr)
                {
                    upic::ArcPoint pt;
                    pt.t = (double) pv.getProperty("t", 0);
                    pt.freq = (double) pv.getProperty("f", 440);
                    pt.i = juce::jlimit(0.0f, 1.0f, (float)(double) pv.getProperty("i", 1.0));
                    a.points.push_back(pt);
                }
            std::sort(a.points.begin(), a.points.end(), [](auto& x, auto& y){ return x.t < y.t; });
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
    struct SievePreset { juce::String name; int m; std::vector<int> remainders; };
    inline std::vector<SievePreset> sievePresets()
    {
        return {
            { "Chromatic",                 12, {0} },
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
            return juce::var(o);
        }
        static SieveZone fromVar(const juce::var& v)
        {
            SieveZone z;
            z.id = (int) v.getProperty("id", 1);
            z.t0 = (double) v.getProperty("t0", 0.0);
            z.t1 = (double) v.getProperty("t1", 0.0);
            z.f0 = juce::jlimit(kMinFreq, kMaxFreq, (double) v.getProperty("f0", kMinFreq));
            z.f1 = juce::jlimit(kMinFreq, kMaxFreq, (double) v.getProperty("f1", kMaxFreq));
            z.m = juce::jlimit(2, 120, (int) v.getProperty("m", 12));
            z.timeGrid = juce::jlimit(0.0, 64.0, (double) v.getProperty("timeGrid", 0.0));
            std::vector<int> rs;
            if (auto* arr = v.getProperty("r", juce::var()).getArray())
                for (auto& rv : *arr) rs.push_back((((int) rv % z.m) + z.m) % z.m);
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
    // time grid overrides the global one. Stickiness (0..1) softens ALL snaps.
    inline void quantizePoint(bool chromaticSnap, bool sieveOn, int sieveM,
                              const std::vector<int>& sieveR, double timeGrid,
                              double stickiness, const std::vector<SieveZone>& zones,
                              double& t, double& f)
    {
        const SieveZone* z = zoneAt(zones, t, f);
        double target = f;
        if (z != nullptr)          target = sieveQuantizeHz(f, z->m, z->remainders);
        else if (sieveOn)          target = sieveQuantizeHz(f, sieveM, sieveR);
        else if (chromaticSnap)
        {
            double mn = std::round(freqToMidi(f));
            mn = juce::jlimit(12.0, 120.0, mn);
            target = midiToFreq(mn);
        }
        f = juce::jlimit(kMinFreq, kMaxFreq, stickify(f, target, stickiness));

        const double g = (z != nullptr) ? z->timeGrid : timeGrid;
        t = juce::jmax(0.0, stickify(t, timeGridSnap(t, g), stickiness));
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
            s.sampleRate = juce::jlimit(1000.0, 384000.0, (double) v.getProperty("sampleRate", 44100.0));
            const int ch = juce::jlimit(1, 2, (int) v.getProperty("channels", 1));
            const int n = juce::jmax(0, (int) v.getProperty("frames", 0));
            auto b64 = v.getProperty("data", "").toString();
            if (n > 0 && ch > 0 && b64.isNotEmpty())
            {
                juce::MemoryOutputStream mo;
                if (juce::Base64::convertFromBase64(mo, b64)
                    && mo.getDataSize() >= (size_t) n * (size_t) ch * sizeof(short))
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

    //---------------- Page ----------------
    struct Page
    {
        double durationSec = 30.0;
        double tempoBpm    = 120.0; // musical time: beats per minute
        std::vector<Arc> arcs;
        int nextArcId = 1;
        std::vector<SieveZone> zones; // magnetic sieve regions (empty = none)
        int nextZoneId = 1;

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
            return juce::var(o);
        }
        static Page fromVar(const juce::var& v)
        {
            Page p;
            p.durationSec = juce::jlimit(1.0, 600.0, (double) v.getProperty("duration", 30.0));
            p.tempoBpm    = juce::jlimit(20.0, 400.0, (double) v.getProperty("tempo", 120.0));
            p.nextArcId = (int) v.getProperty("nextArcId", 1);
            if (auto* arr = v.getProperty("arcs", juce::var()).getArray())
                for (auto& av : *arr) p.arcs.push_back(Arc::fromVar(av));
            // Files written by other tools may not carry nextArcId: make sure the
            // next generated id can never collide with an id already in use
            // (a collision would make select/delete hit the wrong arc).
            for (auto& a : p.arcs)
                p.nextArcId = juce::jmax(p.nextArcId, a.id + 1);
            // zones absent in old / Python-written files -> no zones, as before
            p.nextZoneId = (int) v.getProperty("nextZoneId", 1);
            if (auto* zarr = v.getProperty("zones", juce::var()).getArray())
                for (auto& zv : *zarr) p.zones.push_back(SieveZone::fromVar(zv));
            for (auto& z : p.zones)
                p.nextZoneId = juce::jmax(p.nextZoneId, z.id + 1);
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
