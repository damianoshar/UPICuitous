#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Arc.h"
#include "FreqSpace.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

namespace upic
{
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
}
