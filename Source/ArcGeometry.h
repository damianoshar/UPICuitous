#pragma once

//==============================================================================
// R2.3: the arc's geometry.
//
// The UPIC's page was a surface: a drawing could be turned, mirrored,
// aligned, stretched and repeated on it. Those operations are geometry,
// not data - they move a curve in the page's own normalised space, and
// they do not know what an arc is made of.
//
// The review's R012 asks for the arc's data and its geometry to be
// separate. The data stays in Source/Arc.h (the points, the material, the
// settings, the four classical transformations, the interpolation the
// engine reads per sample); the geometry moves here.
//
// The module is pure: no JUCE header, no GUI, no app header beyond the arc
// it moves. The page's own numbers - the units, the validation and the
// lookups - are the domain's, and the gate checks the boundary.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "Arc.h"
#include "PieceLimits.h"
#include "UpicConstants.h"
#include "UpicMath.h"

namespace upic
{
    //================ WP8: the geometric transforms =================
    //
    // The UPIC's page was a surface: a drawing could be turned, mirrored,
    // aligned, stretched and repeated on it. These helpers do that to the
    // DRAWN curve, in the page's own NORMALISED space:
    //
    //   x = t / duration                                  (the time)
    //   y = (log2 f - log2 lo) / (log2 hi - log2 lo)       (the pitch, in
    //                                                     octaves of the page's ambitus)
    //
    // In that space a rotation or a scale does not depend on the zoom, and a
    // rotation keeps the length of the curve.
    struct ArcFrame
    {
        double duration = 30.0;
        double pitchLo  = kDefaultPitchLo;
        double pitchHi  = kDefaultPitchHi;

        double logLo()   const { return std::log2 (std::max (1.0, pitchLo)); }
        double logHi()   const { return std::log2 (std::max (1.0, pitchHi)); }
        double logSpan() const { return std::max (1.0e-6, logHi() - logLo()); }

        double normX (double t) const { return duration > 1.0e-9 ? t / duration : 0.0; }
        double normY (double f) const { return (std::log2 (std::max (1.0, f)) - logLo()) / logSpan(); }
        double denormX (double x) const { return x * duration; }
        double denormY (double y) const { return std::exp2 (logLo() + y * logSpan()); }
    };

    // The arcs a transform touches. The app builds the scope (the selection,
    // a group or the whole page); the helpers only need the arcs.
    using ArcRefs = std::vector<Arc*>;

    // The bounding box of a scope, in normalised space. `valid` is false when
    // the scope has no points at all.
    struct ArcBounds
    {
        bool valid = false;
        double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
        double centreX() const { return (x0 + x1) * 0.5; }
        double centreY() const { return (y0 + y1) * 0.5; }
    };

    inline ArcBounds arcBounds (const ArcRefs& arcs, const ArcFrame& frame)
    {
        ArcBounds b;
        for (const auto* a : arcs)
        {
            if (a == nullptr) continue;
            for (const auto& p : a->points)
            {
                const double x = frame.normX (p.t), y = frame.normY (p.freq);
                if (! b.valid)
                {
                    b.x0 = b.x1 = x; b.y0 = b.y1 = y; b.valid = true;
                    continue;
                }
                b.x0 = std::min (b.x0, x); b.x1 = std::max (b.x1, x);
                b.y0 = std::min (b.y0, y); b.y1 = std::max (b.y1, y);
            }
        }
        return b;
    }

    // The pitch centre of an arc: the geometric mean of its pitches (the
    // canvas is log-pitch, so this is the middle of the arc's own range).
    inline double arcPitchCentre (const Arc& a)
    {
        if (a.points.empty()) return 0.0;
        double sumLog = 0.0;
        for (const auto& p : a.points) sumLog += std::log2 (std::max (1.0, p.freq));
        return std::pow (2.0, sumLog / (double) a.points.size());
    }

    // Writes a transformed point back: the time stays on the page, the pitch
    // stays in the model's range, and a value that is not finite leaves the
    // point as it was (a NaN must not travel into the engine).
    inline void writeTransformedPoint (ArcPoint& p, double t, double f, const ArcFrame& frame)
    {
        if (std::isfinite (t))
            p.t = limits::clampFinite (t, 0.0, std::max (0.0, frame.duration));
        if (std::isfinite (f))
            p.freq = limits::clampFinite (f, kMinFreq, kMaxFreq);
    }

    // The engine's segment search reads the points in time order, so a
    // transform that reorders them (a rotation or a mirror can) has to sort
    // them again. The sort is stable: a staircase's two corners share a time
    // and their order is the direction of the jump.
    inline void sortTransformedPoints (Arc& a)
    {
        std::stable_sort (a.points.begin(), a.points.end(),
                          [] (const ArcPoint& x, const ArcPoint& y) { return x.t < y.t; });
    }

    // The rotation: the points turn about the anchor (normalised space). The
    // angle is in radians, anticlockwise (the page's y axis points up, so the
    // mathematical convention is the musical one).
    inline void transformRotate (const ArcRefs& arcs, const ArcFrame& frame,
                                 double anchorX, double anchorY, double angle)
    {
        if (! std::isfinite (angle) || angle == 0.0) return;
        const double c = std::cos (angle), s = std::sin (angle);
        for (auto* a : arcs)
        {
            if (a == nullptr) continue;
            for (auto& p : a->points)
            {
                const double x = frame.normX (p.t) - anchorX;
                const double y = frame.normY (p.freq) - anchorY;
                const double nx = anchorX + x * c - y * s;
                const double ny = anchorY + x * s + y * c;
                writeTransformedPoint (p, frame.denormX (nx), frame.denormY (ny), frame);
            }
            sortTransformedPoints (*a);
        }
    }

    // The two mirrors. They are their own inverse: mirroring twice gives the
    // curve back, as long as nothing was clamped on the way.
    inline void transformMirrorTime (const ArcRefs& arcs, const ArcFrame& frame, double axisX)
    {
        if (! std::isfinite (axisX)) return;
        for (auto* a : arcs)
        {
            if (a == nullptr) continue;
            for (auto& p : a->points)
            {
                const double x = frame.normX (p.t);
                writeTransformedPoint (p, frame.denormX (2.0 * axisX - x), p.freq, frame);
            }
            sortTransformedPoints (*a);
        }
    }

    inline void transformMirrorPitch (const ArcRefs& arcs, const ArcFrame& frame, double axisY)
    {
        if (! std::isfinite (axisY)) return;
        for (auto* a : arcs)
        {
            if (a == nullptr) continue;
            for (auto& p : a->points)
            {
                const double y = frame.normY (p.freq);
                writeTransformedPoint (p, p.t, frame.denormY (2.0 * axisY - y), frame);
            }
        }
    }

    // The scale. sx is the time scale, sy the pitch scale - and the pitch
    // scale works in the log domain, so sy = 2 doubles the distance between
    // the pitches (in octaves of the page's ambitus).
    inline void transformScale (const ArcRefs& arcs, const ArcFrame& frame,
                                double anchorX, double anchorY, double sx, double sy)
    {
        if (! (sx > 0.0) || ! (sy > 0.0)) return;
        if (! std::isfinite (sx) || ! std::isfinite (sy)) return;
        for (auto* a : arcs)
        {
            if (a == nullptr) continue;
            for (auto& p : a->points)
            {
                const double x = frame.normX (p.t), y = frame.normY (p.freq);
                const double nx = anchorX + (x - anchorX) * sx;
                const double ny = anchorY + (y - anchorY) * sy;
                writeTransformedPoint (p, frame.denormX (nx), frame.denormY (ny), frame);
            }
            sortTransformedPoints (*a);
        }
    }

    // The alignment: the arcs of the scope share an edge.
    //   0 = the starts (they all begin at the same time)
    //   1 = the ends   (they all end at the same time)
    //   2 = the pitch  (their pitch centres are the same)
    inline void transformAlign (const ArcRefs& arcs, const ArcFrame& frame, int edge)
    {
        if (arcs.empty()) return;

        if (edge == 0 || edge == 1)
        {
            double target = edge == 0 ? std::numeric_limits<double>::max()
                                      : -std::numeric_limits<double>::max();
            bool any = false;
            for (const auto* a : arcs)
            {
                if (a == nullptr || a->points.empty()) continue;
                const double t = edge == 0 ? a->startTime() : a->endTime();
                target = edge == 0 ? std::min (target, t) : std::max (target, t);
                any = true;
            }
            if (! any) return;

            for (auto* a : arcs)
            {
                if (a == nullptr || a->points.empty()) continue;
                const double t = edge == 0 ? a->startTime() : a->endTime();
                const double dt = target - t;
                for (auto& p : a->points) writeTransformedPoint (p, p.t + dt, p.freq, frame);
                sortTransformedPoints (*a);
            }
            return;
        }

        if (edge == 2)
        {
            double sumLog = 0.0;
            size_t count = 0;
            for (const auto* a : arcs)
            {
                if (a == nullptr) continue;
                for (const auto& p : a->points)
                {
                    sumLog += std::log2 (std::max (1.0, p.freq));
                    ++count;
                }
            }
            if (count == 0) return;
            const double target = std::pow (2.0, sumLog / (double) count);
            for (auto* a : arcs)
            {
                if (a == nullptr || a->points.empty()) continue;
                const double centre = arcPitchCentre (*a);
                if (! (centre > 0.0)) continue;
                const double factor = target / centre;
                for (auto& p : a->points)
                    writeTransformedPoint (p, p.t, p.freq * factor, frame);
            }
        }
    }

    // The repeat: copies of the scope, each one further along in time and in
    // pitch. A copy keeps everything the original had (its material, its
    // envelope, its own settings) and gets an id of its own; the caller adds
    // the copies to the page and moves the id counter past them.
    inline std::vector<Arc> transformRepeat (const ArcRefs& arcs, const ArcFrame& frame,
                                             double dt, double dPitch, int copies, int firstId)
    {
        std::vector<Arc> out;
        if (copies <= 0 || arcs.empty()) return out;
        if (! std::isfinite (dt) || ! std::isfinite (dPitch)) return out;
        const double pitchFactor = std::pow (2.0, dPitch / 12.0);
        int nextId = firstId;
        for (int k = 1; k <= copies; ++k)
        {
            const double timeOffset = dt * (double) k;
            const double pf = std::pow (pitchFactor, (double) k);
            for (const auto* a : arcs)
            {
                if (a == nullptr) continue;
                Arc copy = *a;
                copy.id = nextId++;
                for (auto& p : copy.points)
                    writeTransformedPoint (p, p.t + timeOffset, p.freq * pf, frame);
                sortTransformedPoints (copy);
                out.push_back (std::move (copy));
            }
        }
        return out;
    }
}
