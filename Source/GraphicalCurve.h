#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "Arc.h"
#include "ArcGeometry.h"
#include "Envelope.h"
#include "FreqSpace.h"
#include "PieceLimits.h"
#include "Waveform.h"

//==============================================================================
// WP9: the generalized curve.
//
// The UPIC's page was a surface of shapes, and a shape could be more than
// one kind of information: the same drawn line could serve as a pitch
// trajectory, a wavetable, an amplitude envelope or a frequency conversion
// table. The clone kept each of those as its own type, so a shape could
// not be reused - the drawing had to be drawn again, and the second
// drawing was a different object with a different meaning.
//
// A GraphicalCurve is the SHAPE, separated from what it means:
//
//   x          the position along the shape's own axis
//   domain     what that position means:
//                Normalised   a position, 0..1
//                Seconds      a time, in seconds
//                LogHz        a pitch, as log2 Hz
//   y          the HEIGHT at that position, always 0..1
//
// What a height means depends on what the curve is converted into: as a
// wavetable it is the amplitude of one cycle, as an envelope it is the
// gain over the object's life, as a frequency table it is the pitch the
// position sounds at. The conversion says which it assumed - the curve
// itself does not decide.
//
// The conversions produce NEW objects. An object is never reinterpreted in
// place: the wavetable that was drawn is still the wavetable, and the
// curve that came out of it is a new thing in a new bank.
//==============================================================================
namespace upic
{
    struct CurvePoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct GraphicalCurve
    {
        enum class Domain { Normalised, Seconds, LogHz };

        Domain domain = Domain::Normalised;
        std::vector<CurvePoint> points;   // sorted by x

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        juce::String domainName() const
        {
            switch (domain)
            {
                case Domain::Seconds:   return "seconds";
                case Domain::LogHz:     return "log Hz";
                case Domain::Normalised:
                default:                return "normalised";
            }
        }

        double xMin() const
        {
            if (points.empty()) return 0.0;
            double v = points.front().x;
            for (const auto& p : points) v = juce::jmin (v, p.x);
            return v;
        }

        double xMax() const
        {
            if (points.empty()) return 0.0;
            double v = points.front().x;
            for (const auto& p : points) v = juce::jmax (v, p.x);
            return v;
        }

        double yMin() const
        {
            if (points.empty()) return 0.0;
            double v = points.front().y;
            for (const auto& p : points) v = juce::jmin (v, p.y);
            return v;
        }

        double yMax() const
        {
            if (points.empty()) return 0.0;
            double v = points.front().y;
            for (const auto& p : points) v = juce::jmax (v, p.y);
            return v;
        }

        // The height at a position, linearly between the points. Outside the
        // curve the edge values are held - a curve does not fall to zero
        // outside its own extent, that would be an envelope's rule and not a
        // shape's.
        double valueAt (double x) const
        {
            if (points.empty()) return 0.0;
            if (points.size() == 1) return points.front().y;
            if (x <= points.front().x) return points.front().y;
            if (x >= points.back().x)  return points.back().y;
            size_t lo = 0, hi = points.size() - 1;
            while (hi - lo > 1)
            {
                const size_t mid = (lo + hi) / 2;
                if (points[mid].x <= x) lo = mid; else hi = mid;
            }
            const double x0 = points[lo].x, y0 = points[lo].y;
            const double x1 = points[hi].x, y1 = points[hi].y;
            const double span = x1 - x0;
            if (span <= 1.0e-12) return y1;
            const double f = (x - x0) / span;
            return y0 + (y1 - y0) * f;
        }

        // The shape with its x axis rescaled to 0..1 and the domain set to
        // Normalised. This is the form the asset conversions work on: the
        // shape's own extent, whatever it meant, becomes the unit axis.
        GraphicalCurve normalised() const
        {
            GraphicalCurve c;
            c.domain = Domain::Normalised;
            if (points.empty()) return c;
            const double lo = xMin(), hi = xMax();
            const double span = hi - lo;
            c.points.reserve (points.size());
            for (const auto& p : points)
            {
                CurvePoint q;
                q.x = span > 1.0e-12 ? (p.x - lo) / span : 0.0;
                q.y = limits::clampFinite (p.y, 0.0, 1.0);
                c.points.push_back (q);
            }
            return c;
        }
    };

    //--------------------------------------------------------------------------
    // The context a domain conversion needs: how many seconds a normalised
    // position stands for, and which pitches the log-Hz axis covers.
    //--------------------------------------------------------------------------
    struct CurveSpace
    {
        double seconds = 30.0;
        double loHz = kDefaultPitchLo;
        double hiHz = kDefaultPitchHi;

        double logLo() const { return std::log2 (juce::jmax (1.0, loHz)); }
        double logHi() const { return std::log2 (juce::jmax (1.0, hiHz)); }
        double logSpan() const { return juce::jmax (1.0e-6, logHi() - logLo()); }
    };

    // Builds a curve: the points are sorted by x, and a point that is not
    // finite is dropped - a NaN must not travel into an editor or into the
    // engine.
    inline GraphicalCurve makeCurve (GraphicalCurve::Domain domain, std::vector<CurvePoint> points)
    {
        GraphicalCurve c;
        c.domain = domain;
        c.points.reserve (points.size());
        for (const auto& p : points)
            if (std::isfinite (p.x) && std::isfinite (p.y))
                c.points.push_back (p);
        std::stable_sort (c.points.begin(), c.points.end(),
                          [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        return c;
    }

    //--------------------------------------------------------------------------
    // The arc: a curve drawn on the page, in the page's own normalised space
    // (the same space the WP8 transforms work in).
    //--------------------------------------------------------------------------
    inline GraphicalCurve arcToCurve (const Arc& a, const ArcFrame& frame)
    {
        std::vector<CurvePoint> pts;
        pts.reserve (a.points.size());
        for (const auto& p : a.points)
            pts.push_back ({ frame.normX (p.t), frame.normY (p.freq) });
        return makeCurve (GraphicalCurve::Domain::Normalised, std::move (pts));
    }

    // The curve back to an arc, on the page it was drawn on. The times and
    // the pitches are clamped the way a transform clamps them: the time
    // stays on the page, the pitch stays in the model's range.
    inline Arc curveToArc (const GraphicalCurve& c, const ArcFrame& frame, int id = 1)
    {
        Arc a;
        a.id = id;
        for (const auto& p : c.points)
        {
            ArcPoint ap;
            ap.t = frame.denormX (p.x);
            ap.freq = frame.denormY (p.y);
            writeTransformedPoint (ap, ap.t, ap.freq, frame);
            a.points.push_back (ap);
        }
        sortTransformedPoints (a);
        return a;
    }

    //--------------------------------------------------------------------------
    // The wavetable: a cycle of samples. The wave is bipolar, so a height of
    // 0.5 is the middle of the wave and a height of 0 or 1 is its edge.
    //--------------------------------------------------------------------------
    inline GraphicalCurve waveformToCurve (const std::vector<float>& samples)
    {
        std::vector<CurvePoint> pts;
        if (! samples.empty())
        {
            pts.reserve (samples.size());
            const double last = (double) juce::jmax (1, (int) samples.size() - 1);
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const float s = samples[i];
                pts.push_back ({ (double) i / last,
                                 limits::clampFinite ((double) s * 0.5 + 0.5, 0.0, 1.0) });
            }
        }
        return makeCurve (GraphicalCurve::Domain::Normalised, std::move (pts));
    }

    // The samples of one cycle, resampled from the curve. The curve's own
    // extent is the cycle: the shape is read from end to end.
    inline std::vector<float> curveToWaveform (const GraphicalCurve& c, int waveSize)
    {
        std::vector<float> out;
        const int n = juce::jmax (2, waveSize);
        out.assign ((size_t) n, 0.0f);
        if (c.points.empty()) return out;
        const auto norm = c.normalised();
        for (int i = 0; i < n; ++i)
        {
            const double x = (double) i / (double) (n - 1);
            const double y = limits::clampFinite (norm.valueAt (x), 0.0, 1.0);
            out[(size_t) i] = (float) (y * 2.0 - 1.0);
        }
        return out;
    }

    //--------------------------------------------------------------------------
    // The envelope: a gain over the object's life. The envelope's x axis is
    // the unit axis already, so the curve is the envelope's points.
    //--------------------------------------------------------------------------
    inline GraphicalCurve envelopeToCurve (const Envelope& e)
    {
        std::vector<CurvePoint> pts;
        pts.reserve (e.points.size());
        for (const auto& p : e.points)
            pts.push_back ({ (double) p.x, (double) p.y });
        return makeCurve (GraphicalCurve::Domain::Normalised, std::move (pts));
    }

    inline Envelope curveToEnvelope (const GraphicalCurve& c)
    {
        Envelope e;
        e.points.clear();
        if (c.points.empty()) { e.presetFlat(); return e; }
        const auto norm = c.normalised();
        for (const auto& p : norm.points)
            e.points.push_back ({ (float) limits::clampFinite (p.x, 0.0, 1.0),
                                  (float) limits::clampFinite (p.y, 0.0, 1.0) });
        if (e.points.empty()) e.presetFlat();
        return e;
    }

    //--------------------------------------------------------------------------
    // The frequency table: a curve drawn on the page's PITCH axis. The x is
    // the normalised position on that axis and the height is the pitch the
    // position sounds at, over the ambitus the table is drawn in.
    //
    // A curve that came from an arc carries the PAGE's positions: its x is
    // the position in time and its height is the position on the pitch axis.
    // Turning such a curve into a table reads the drawing on its side: the
    // drawing's horizontal axis becomes the page's vertical axis, and the
    // height it has there is the pitch that position sounds at. The table
    // follows the drawing, which is what the drawn table always did.
    //--------------------------------------------------------------------------
    inline GraphicalCurve tableToCurve (const std::vector<TablePoint>& table, const CurveSpace& space)
    {
        std::vector<CurvePoint> pts;
        pts.reserve (table.size());
        for (const auto& p : table)
        {
            const double y = ((double) p.log2Hz - space.logLo()) / space.logSpan();
            pts.push_back ({ (double) p.x, limits::clampFinite (y, 0.0, 1.0) });
        }
        return makeCurve (GraphicalCurve::Domain::Normalised, std::move (pts));
    }

    // The table's x is the position on the page's pitch axis, so it is the
    // curve's own x: a curve that came from an arc already carries the
    // page's positions (see arcToPitchCurve). The height is the pitch the
    // position sounds at, over the ambitus the table is drawn in.
    inline std::vector<TablePoint> curveToTable (const GraphicalCurve& c, const CurveSpace& space)
    {
        std::vector<TablePoint> out;
        if (c.points.empty()) return out;
        out.reserve (c.points.size());
        for (const auto& p : c.points)
        {
            TablePoint tp;
            tp.x = (float) limits::clampFinite (p.x, 0.0, 1.0);
            const double log2Hz = space.logLo() + limits::clampFinite (p.y, 0.0, 1.0) * space.logSpan();
            tp.log2Hz = (float) log2Hz;
            out.push_back (tp);
        }
        return out;
    }

    //--------------------------------------------------------------------------
    // The curve at another resolution: the same shape, read evenly over its
    // own extent. This is how a shape meets a target that has a fixed
    // number of points - a wavetable's samples, an envelope's points.
    //
    // The x axis keeps its meaning: the resampled curve still lives over
    // the same span (the seconds, the log Hz), only the number of points
    // changes.
    //--------------------------------------------------------------------------
    inline GraphicalCurve resampleCurve (const GraphicalCurve& c, int count)
    {
        GraphicalCurve out;
        out.domain = c.domain;
        const int n = juce::jmax (2, count);
        if (c.points.size() <= 1)
        {
            out.points = c.points;
            return out;
        }
        const double lo = c.xMin(), hi = c.xMax();
        const double span = hi - lo;
        if (span <= 1.0e-12)
        {
            // a curve with no width cannot be read evenly: it keeps its
            // points, and the target sees a flat line
            out.points = c.points;
            return out;
        }
        out.points.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double x = lo + span * (double) i / (double) (n - 1);
            out.points.push_back ({ x, c.valueAt (x) });
        }
        return out;
    }

    //--------------------------------------------------------------------------
    // Between the coordinate systems: a curve whose x is a time becomes one
    // whose x is a position, and a curve whose x is a pitch becomes one
    // whose x is a position on the ambitus.
    //
    // A conversion that does not make sense (seconds to log Hz) is refused:
    // the curve comes back unchanged and the call is told so.
    //--------------------------------------------------------------------------
    inline bool rescaleDomain (const GraphicalCurve& in, GraphicalCurve::Domain target,
                                const CurveSpace& space, GraphicalCurve& out)
    {
        if (in.domain == target)
        {
            out = in;
            return true;
        }

        std::vector<CurvePoint> pts;
        pts.reserve (in.points.size());

        const bool ok =
            (in.domain == GraphicalCurve::Domain::Normalised && target == GraphicalCurve::Domain::Seconds)
            ? (space.seconds > 1.0e-9)
            : true;

        if (! ok)
        {
            out = in;
            return false;
        }

        for (const auto& p : in.points)
        {
            CurvePoint q = p;
            if (in.domain == GraphicalCurve::Domain::Normalised && target == GraphicalCurve::Domain::Seconds)
                q.x = p.x * space.seconds;
            else if (in.domain == GraphicalCurve::Domain::Seconds && target == GraphicalCurve::Domain::Normalised)
                q.x = p.x / space.seconds;
            else if (in.domain == GraphicalCurve::Domain::Normalised && target == GraphicalCurve::Domain::LogHz)
                q.x = space.logLo() + p.x * space.logSpan();
            else if (in.domain == GraphicalCurve::Domain::LogHz && target == GraphicalCurve::Domain::Normalised)
                q.x = (p.x - space.logLo()) / space.logSpan();
            else if (in.domain == GraphicalCurve::Domain::Seconds && target == GraphicalCurve::Domain::LogHz)
                q.x = space.logLo() + (p.x / space.seconds) * space.logSpan();
            else if (in.domain == GraphicalCurve::Domain::LogHz && target == GraphicalCurve::Domain::Seconds)
                q.x = ((p.x - space.logLo()) / space.logSpan()) * space.seconds;
            else
            {
                out = in;
                return false;
            }
            pts.push_back (q);
        }

        out = makeCurve (target, std::move (pts));
        return true;
    }
}
