#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "PieceLimits.h"

//==============================================================================
// WP4: editable performance sequences.
//
// The New UPIC let the performer's movement through the score be recorded
// and replayed: a sequence is a list of (performance time, page, position)
// points. It is NOT part of the page data - the pages stay exactly as drawn -
// so a sequence can be re-recorded, trimmed or thrown away without touching
// the composition.
//
// Playback is driven by an audio-thread clock: `sequencePositionAt` is a pure
// function of the points and the elapsed performance time, so a replay is
// deterministic and does not consult the wall clock.
namespace upic
{
    struct PerformancePoint
    {
        double t = 0.0;     // performance time, seconds
        int page = 0;       // the page being performed
        double pos = 0.0;   // the page-local score position, seconds
    };

    struct PerformanceSequence
    {
        int id = 0;
        juce::String name = "Sequence";
        std::vector<PerformancePoint> points;  // sorted by t
        bool loop = true;

        double lengthSec() const { return points.empty() ? 0.0 : points.back().t; }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("name", name);
            if (! loop) o->setProperty ("loop", false);
            juce::Array<juce::var> arr;
            for (auto& p : points)
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("t", p.t);
                po->setProperty ("page", p.page);
                po->setProperty ("pos", p.pos);
                arr.add (juce::var (po));
            }
            o->setProperty ("points", arr);
            return juce::var (o);
        }

        static PerformanceSequence fromVar (const juce::var& v, int fallbackId)
        {
            PerformanceSequence s;
            s.id = juce::jlimit (1, limits::kMaxItemId, (int) v.getProperty ("id", fallbackId));
            if (s.id <= 0) s.id = juce::jmax (1, fallbackId);
            const auto n = v.getProperty ("name", "Sequence").toString();
            s.name = n.isNotEmpty() ? n.substring (0, 64) : juce::String ("Sequence");
            s.loop = (bool) v.getProperty ("loop", true);
            if (auto* arr = v.getProperty ("points", juce::var()).getArray())
                for (auto& pv : *arr)
                {
                    if ((int) s.points.size() >= limits::kMaxSequencePoints) break;
                    PerformancePoint p;
                    p.t = limits::clampFinite ((double) pv.getProperty ("t", 0.0),
                                               0.0, (double) limits::kMaxSequenceSeconds);
                    p.page = juce::jlimit (0, limits::kMaxPages - 1,
                                           (int) pv.getProperty ("page", 0));
                    p.pos = limits::clampFinite ((double) pv.getProperty ("pos", 0.0),
                                                  0.0, kMaxPageSeconds);
                    s.points.push_back (p);
                }
            std::sort (s.points.begin(), s.points.end(),
                       [] (const PerformancePoint& a, const PerformancePoint& b)
                       { return a.t < b.t; });
            return s;
        }
    };

    // The position the performance is at, at performance time t. Before the
    // first point and after the last one the position holds. A segment that
    // changes page reports the OLD page and a zero rate (a hold), so the
    // audio thread switches page exactly at the next point's time.
    struct SequencePosition
    {
        int page = 0;
        double pos = 0.0;
        double rate = 0.0;   // page-seconds per performance-second
    };

    inline bool sequencePositionAt (const std::vector<PerformancePoint>& pts, double t,
                                    SequencePosition& out)
    {
        if (pts.empty()) return false;
        if (t <= pts.front().t)
        {
            out = { pts.front().page, pts.front().pos, 0.0 };
            return true;
        }
        if (t >= pts.back().t)
        {
            out = { pts.back().page, pts.back().pos, 0.0 };
            return true;
        }
        const auto hiIt = std::lower_bound (pts.begin(), pts.end(), t,
                                            [] (const PerformancePoint& p, double v)
                                            { return p.t < v; });
        const size_t hi = (size_t) (hiIt - pts.begin());
        const PerformancePoint& a = pts[hi - 1];
        const PerformancePoint& b = pts[hi];
        if (a.page != b.page)
        {
            out = { a.page, a.pos, 0.0 };
            return true;
        }
        const double dt = b.t - a.t;
        const double f = dt > 1.0e-9 ? (t - a.t) / dt : 1.0;
        out.page = a.page;
        out.pos = a.pos + (b.pos - a.pos) * f;
        out.rate = dt > 1.0e-9 ? (b.pos - a.pos) / dt : 0.0;
        return true;
    }

    // WP5: the modulation factor of a modulating arc. `depth` of 1 means
    // +/- one octave at the contour's extremes (the historical +/-12 st);
    // the same normalised value gives the tremolo for the amplitude target.
    inline double modulationFactor (double modHz, double loLog2, double hiLog2,
                                    double depth, double* norm01 = nullptr)
    {
        const double span = juce::jmax (1.0e-6, hiLog2 - loLog2);
        const double norm = juce::jlimit (-1.0, 1.0,
                                          2.0 * (std::log2 (juce::jmax (1.0e-6, modHz)) - loLog2)
                                              / span - 1.0);
        if (norm01 != nullptr) *norm01 = 0.5 * (norm + 1.0);
        return std::exp2 (juce::jlimit (0.0, 1.0, depth) * norm);
    }

    // The recorded time wrapped for a looping sequence. `lengthSec` is the
    // sequence's last point.
    inline double sequenceLoopTime (double t, double lengthSec)
    {
        if (lengthSec <= 1.0e-6) return 0.0;
        t = std::fmod (t, lengthSec);
        if (t < 0.0) t += lengthSec;
        return t;
    }
}
