#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstddef>
#include <vector>

#include "UpicConstants.h"

namespace upic
{
    //---------------- Waveform ----------------
    struct Waveform
    {
        int id = 0;
        juce::String name = "Sine";
        std::vector<float> samples; // kWaveSize samples by default, up to kMaxWaveSize, -1..1
        juce::Colour colour = juce::Colours::cyan;

        Waveform() : samples((size_t) kWaveSize, 0.0f) {}
        Waveform(int i, const juce::String& n, juce::Colour c) : id(i), name(n), samples((size_t) kWaveSize, 0.0f), colour(c) {}

        // WP10: a generator makes a wave of the default size. A wave that
        // was imported with 4096 samples goes back to 1024 when a preset is
        // applied - the preset IS 1024 samples, so leaving the rest of the
        // old table behind would be a half-replaced wave.
        void makeSine()      { samples.assign((size_t) kWaveSize, 0.0f); for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * i / kWaveSize); }
        void makeSaw()       { samples.assign((size_t) kWaveSize, 0.0f); for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = 2.0f * i / kWaveSize - 1.0f; }
        void makeSquare()    { samples.assign((size_t) kWaveSize, 0.0f); for (int i = 0; i < kWaveSize; ++i) samples[(size_t)i] = (i < kWaveSize/2) ? 1.0f : -1.0f; }
        void makeTriangle()  { samples.assign((size_t) kWaveSize, 0.0f); for (int i = 0; i < kWaveSize; ++i) { float p = (float)i / kWaveSize; samples[(size_t)i] = p < 0.25f ? p*4.0f : (p < 0.75f ? 2.0f - p*4.0f : p*4.0f - 4.0f); } }
        void makeNoise(unsigned seed = 22222) { samples.assign((size_t) kWaveSize, 0.0f); juce::Random r((int)seed + id*101); for (auto& s : samples) s = r.nextFloat() * 2.0f - 1.0f; }
        void makeRandomSmooth(unsigned seed = 12345)
        {
            samples.assign((size_t) kWaveSize, 0.0f);
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
            // WP10: the pass wraps over the wave's own length, so a
            // 4096-sample table is smoothed as one table.
            const int n = (int) samples.size();
            if (n <= 1) return;
            for (int p = 0; p < passes; ++p)
            {
                std::vector<float> c = samples;
                for (int i = 0; i < n; ++i)
                    samples[(size_t)i] = (c[(size_t)((i-1+n)%n)] + c[(size_t)i]*2.0f + c[(size_t)((i+1)%n)]) * 0.25f;
            }
        }
        float valueAt(double phase01) const // phase 0..1, linear interp, loop
        {
            // WP10: the table is the wave's own samples, not the default
            // size. A wave that holds 4096 samples is read over its 4096
            // samples - the phase still runs 0..1 over the whole table.
            const int n = (int) samples.size();
            if (n <= 0) return 0.0f;
            if (n == 1) return samples[0];
            double p = phase01 - std::floor(phase01);
            double idx = p * n;
            int i0 = (int) idx % n;
            int i1 = (i0 + 1) % n;
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
            arr.ensureStorageAllocated((int) samples.size());
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
            if (auto* arr = v.getProperty("samples", juce::var()).getArray())
            {
                // WP10: the length is the wave's own. The piece carries the
                // samples, so the table keeps the length it was written
                // with - up to the cap. A piece with no samples keeps the
                // default size.
                const int n = juce::jmin (kMaxWaveSize, (int) arr->size());
                if (n > 0)
                {
                    w.samples.assign ((size_t) n, 0.0f);
                    for (int i = 0; i < n; ++i)
                        w.samples[(size_t)i] = (float)(double)(*arr)[i];
                }
            }
            return w;
        }
    };
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
}
