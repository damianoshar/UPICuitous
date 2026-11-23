#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "UpicConstants.h"
#include "PieceLimits.h"
#include "Tuning.h"

namespace upic
{
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
    //---------------- Sieve quantizer (Xenakis) ----------------
    // Allowed pitches on the step axis: n mod m ∈ remainders.
    // Returns the nearest allowed frequency; identity when inert.
    //
    // M4: the axis is the tuning's own: semitones in 12-tone equal
    // temperament (the numbers do not change), the scale's degrees in a
    // tuning. The old behaviour is therefore untouched when no tuning is
    // loaded.
    inline double sieveQuantizeHz(double hz, int m, const std::vector<int>& remainders,
                                  const Tuning& tuning = defaultTuning())
    {
        if (m < 2 || remainders.empty() || hz <= 0.0) return hz;
        const double n = tuning.freqToStep(hz);
        double best = n, bestDist = 1.0e18;
        for (int r : remainders)
        {
            const int rr = ((r % m) + m) % m;
            const int q = (int) std::floor((n - rr) / (double) m + 0.5); // nearest integer ≡ rr (mod m)
            const double cand = (double)(q * m + rr);
            const double d = std::abs(cand - n);
            if (d < bestDist) { bestDist = d; best = cand; }
        }
        return juce::jlimit(kMinFreq, kMaxFreq, tuning.stepToFreq(best));
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

    // v1.33.4 (M21): the presets are a compile-time constant array, not a
    // lazily-initialised std::vector. Page::spaceMap() runs on the audio
    // thread, and the first call to a function-local static would build the
    // vector there - a malloc on the audio thread.
    inline const std::array<FreqSpacePreset, 7>& freqSpacePresets()
    {
        static constexpr std::array<FreqSpacePreset, 7> p = {{
            { "The page's range",      1.0,  0.0 },
            { "Half the range",        0.5,  0.0 },
            { "Double the range",      2.0,  0.0 },
            { "An octave up",          1.0,  1.0 },
            { "An octave down",        1.0, -1.0 },
            { "Half, an octave up",    0.5,  1.0 },
            { "Double, an octave down", 2.0, -1.0 },
        }};
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

    //---------------- the drawn frequency tables (WP1) ----------------
    // The New UPIC's frequency table was not a preset: it was a conversion
    // curve the composer could draw (or define by range, tuning and divisions
    // per octave, and invert or step). This is that curve.
    //
    // Conventions:
    //   x       the normalised position on the page's vertical axis
    //           (0 = the bottom, 1 = the top), so the table follows the
    //           drawing, not the ambitus;
    //   log2Hz  the absolute log2 frequency the position sounds at. Storing
    //           the absolute pitch (not a relative offset) keeps the table
    //           independent of the page's ambitus, exactly as the historical
    //           grids were independent of the drawing's scale.
    struct TablePoint { float x = 0.0f; float log2Hz = 0.0f; };

    struct FreqSpaceDef
    {
        int  preset = 0;                  // the classic affine preset (0..6)
        bool editable = false;            // true -> `points` is authoritative
        std::vector<TablePoint> points;   // sorted by x, 2..kMaxTablePoints
        bool stepped = false;             // quantise after the curve
        int  stepDivisions = 0;           // 1..99 equal divisions per octave
        bool inverted = false;            // mirror the drawn curve

        bool isDefault() const
        {
            return ! editable && ! stepped && ! inverted && points.empty();
        }

        // The log2 frequency at a normalised position. An empty table falls
        // back to the page's ambitus (optionally inverted), so a space that
        // is only stepped or inverted still behaves predictably.
        double log2At (float n, double a0, double a1) const
        {
            const float x = inverted ? 1.0f - juce::jlimit (0.0f, 1.0f, n)
                                     : juce::jlimit (0.0f, 1.0f, n);
            if (points.empty())
                return a0 + (double) x * (a1 - a0);
            if (x <= points.front().x) return points.front().log2Hz;
            if (x >= points.back().x)  return points.back().log2Hz;
            // binary search for the segment (the table can hold 16384 points)
            size_t lo = 0, hi = points.size() - 1;
            while (hi - lo > 1)
            {
                const size_t mid = (lo + hi) / 2;
                if (points[mid].x <= x) lo = mid; else hi = mid;
            }
            const TablePoint& p0 = points[lo];
            const TablePoint& p1 = points[hi];
            const float span = p1.x - p0.x;
            const float f = span > 1.0e-9f ? (x - p0.x) / span : 0.0f;
            return p0.log2Hz + (p1.log2Hz - p0.log2Hz) * (double) f;
        }

        // The curve as a var. Only the non-default fields are written, so a
        // piece that never edited a table is byte-for-byte what it was.
        static juce::var pointsToVar (const std::vector<TablePoint>& pts)
        {
            juce::Array<juce::var> arr;
            for (const auto& p : pts)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("x", p.x);
                o->setProperty ("hz", std::exp2 ((double) p.log2Hz));
                arr.add (juce::var (o));
            }
            return juce::var (arr);
        }

        juce::var toVar (int index) const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("space", index);
            if (preset != 0) o->setProperty ("preset", preset);
            o->setProperty ("editable", editable);
            if (stepped) o->setProperty ("stepped", true);
            if (stepDivisions > 0) o->setProperty ("divisions", stepDivisions);
            if (inverted) o->setProperty ("inverted", true);
            if (! points.empty()) o->setProperty ("points", pointsToVar (points));
            return juce::var (o);
        }

        static FreqSpaceDef fromVar (const juce::var& v)
        {
            FreqSpaceDef d;
            d.preset = juce::jlimit (0, (int) freqSpacePresets().size() - 1,
                                     (int) v.getProperty ("preset", 0));
            d.editable = (bool) v.getProperty ("editable", false);
            d.stepped = (bool) v.getProperty ("stepped", false);
            d.stepDivisions = juce::jlimit (0, limits::kMaxStepDivisions,
                                            (int) v.getProperty ("divisions", 0));
            d.inverted = (bool) v.getProperty ("inverted", false);
            if (auto* arr = v.getProperty ("points", juce::var()).getArray())
            {
                const double loLog = std::log2 (kMinFreq);
                const double hiLog = std::log2 (kMaxFreq);
                for (auto& pv : *arr)
                {
                    if ((int) d.points.size() >= limits::kMaxTablePoints) break;
                    TablePoint p;
                    p.x = (float) limits::clampFinite ((double) pv.getProperty ("x", 0.0), 0.0, 1.0);
                    const double hz = limits::clampFinite ((double) pv.getProperty ("hz", 440.0),
                                                          kMinFreq, kMaxFreq);
                    p.log2Hz = (float) limits::clampFinite (std::log2 (juce::jmax (1.0e-6, hz)),
                                                           loLog, hiLog);
                    d.points.push_back (p);
                }
                std::sort (d.points.begin(), d.points.end(),
                           [] (const TablePoint& a, const TablePoint& b) { return a.x < b.x; });
            }
            // a table needs two points to be a curve; anything less falls
            // back to the preset rather than producing a constant pitch
            if (d.points.size() < 2) d.editable = false;
            if (! d.editable) d.points.clear();
            return d;
        }
    };

    //---------------- the render-time lookup table (WP1) ----------------
    // The realtime path cannot scan a 16384-point table per sample. The
    // message thread builds this 1024-entry table from the drawn curve
    // (snapshot publication), and the audio thread interpolates two entries.
    struct FreqLut
    {
        static constexpr int kSize = 1024;
        std::array<float, kSize + 1> log2Hz {};
        bool valid = false;
        bool identity = false;

        // Interpolated log2 frequency at a normalised position.
        float at (float n) const
        {
            const float x = juce::jlimit (0.0f, 1.0f, n) * (float) kSize;
            const int i = juce::jmin (kSize - 1, (int) x);
            const float f = x - (float) i;
            return log2Hz[(size_t) i] * (1.0f - f) + log2Hz[(size_t) i + 1] * f;
        }
    };

    // The four tables of one page, as pointers into the snapshot's owned
    // LUTs (null when the space is a plain preset).
    struct PageLutSet
    {
        const FreqLut* lut[4] { nullptr, nullptr, nullptr, nullptr };
    };

    // Build a LUT for a space. Only a drawn (or inverted) table needs one:
    // the presets keep their closed-form mapping (and the identity fast
    // path that the phase vocoder depends on).
    inline FreqLut buildFreqLut (const FreqSpaceDef& def, double ambLo, double ambHi)
    {
        FreqLut lut;
        const bool needsCurve = def.editable || def.inverted;
        if (! needsCurve) return lut; // valid = false
        const double a0 = std::log2 (juce::jmax (1.0e-6, ambLo));
        const double a1 = std::log2 (juce::jmax (1.0e-6, ambHi));
        const double loLog = std::log2 (kMinFreq);
        const double hiLog = std::log2 (kMaxFreq);
        for (int i = 0; i <= FreqLut::kSize; ++i)
        {
            const float n = (float) i / (float) FreqLut::kSize;
            const double l = limits::clampFinite (def.log2At (n, a0, a1), loLog, hiLog);
            lut.log2Hz[(size_t) i] = (float) l;
        }
        lut.valid = true;
        lut.identity = false;
        return lut;
    }
}
