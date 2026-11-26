#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Arc.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

//==============================================================================
// R2.3: the arc's geometry - the frame, the bounds and the transforms.
//
// The UPIC's page was a surface: a drawing could be turned, mirrored,
// aligned, stretched and repeated on it. Those operations live in the
// page's own NORMALISED space, and they are geometry, not data: they do
// not know what an arc is made of, they move a curve.
//
// R2.3 separates them - the review's R012, "Separate Arc data from
// geometry operations". This suite is the contract that separation has to
// keep: it pins the frame's conversions, the bounds of a scope, the
// pitch centre, the rule that writes a moved point back, and each
// transform with its edges.
//
// The suite reaches the geometry through the domain's own headers. It
// builds no window, no component tree and no engine: the geometry's rules
// are the model's rules.

namespace
{
    const double kPi = 3.14159265358979323846;

    // The page the checks are drawn on: 30 s, the default ambitus.
    upic::ArcFrame pageFrame() { return upic::ArcFrame { 30.0, upic::kDefaultPitchLo, upic::kDefaultPitchHi }; }

    // The length of a curve in the normalised space - the rotation has to
    // keep it.
    double lengthIn (const upic::Arc& a, const upic::ArcFrame& frame)
    {
        double len = 0.0;
        for (size_t i = 1; i < a.points.size(); ++i)
        {
            const double x0 = frame.normX (a.points[i-1].t), y0 = frame.normY (a.points[i-1].freq);
            const double x1 = frame.normX (a.points[i].t),   y1 = frame.normY (a.points[i].freq);
            len += std::sqrt ((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
        }
        return len;
    }

    // Are the points in time order, and are their numbers in range?
    bool pointsAreSound (const upic::Arc& a, const upic::ArcFrame& frame)
    {
        for (size_t i = 0; i < a.points.size(); ++i)
        {
            const auto& p = a.points[i];
            if (! std::isfinite (p.t) || ! std::isfinite (p.freq)) return false;
            if (p.t < 0.0 || p.t > frame.duration) return false;
            if (p.freq < upic::kMinFreq || p.freq > upic::kMaxFreq) return false;
            if (i > 0 && a.points[i-1].t > p.t) return false;
        }
        return true;
    }
}

void upic::runArcGeometryChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    const upic::ArcFrame frame = pageFrame();

    // --- the frame: the page's own normalised space ------------------------
    {
        check (std::abs (frame.normX (0.0) - 0.0) < 1e-12
               && std::abs (frame.normX (15.0) - 0.5) < 1e-12
               && std::abs (frame.normX (30.0) - 1.0) < 1e-12,
               "the frame maps the page's times to 0..1");
        check (std::abs (frame.normY (upic::kDefaultPitchLo) - 0.0) < 1e-9
               && std::abs (frame.normY (upic::kDefaultPitchHi) - 1.0) < 1e-9,
               "the frame maps the page's ambitus to 0..1");
        check (frame.normY (440.0) > frame.normY (220.0),
               "the frame's pitch axis grows with the pitch");
        check (std::abs (frame.denormX (frame.normX (12.5)) - 12.5) < 1e-9,
               "the frame's time conversion is its own inverse");
        check (std::abs (frame.denormY (frame.normY (440.0)) - 440.0) < 1e-6,
               "the frame's pitch conversion is its own inverse",
               juce::String (frame.denormY (frame.normY (440.0)), 4) + " Hz");
        check (std::abs (frame.denormX (0.5) - 15.0) < 1e-12
               && std::abs (frame.denormY (0.0) - upic::kDefaultPitchLo) < 1e-9
               && std::abs (frame.denormY (1.0) - upic::kDefaultPitchHi) < 1e-6,
               "the frame's way back is the page's own numbers");
    }

    // --- the frame: its edges ---------------------------------------------
    {
        const upic::ArcFrame noTime { 0.0, upic::kDefaultPitchLo, upic::kDefaultPitchHi };
        check (noTime.normX (5.0) == 0.0,
               "a frame without time puts everything at the start");

        const upic::ArcFrame noPitch { 30.0, 100.0, 100.0 };
        check (std::isfinite (noPitch.normY (200.0)),
               "a frame without an ambitus does not divide by zero");
        check (std::abs (noPitch.normY (100.0)) < 1e-9,
               "a frame without an ambitus puts its own pitch at the bottom");

        check (std::abs (frame.normY (0.5) - frame.normY (1.0)) < 1e-12,
               "a pitch below 1 Hz is read as 1 Hz");
    }

    // --- the bounds of a scope --------------------------------------------
    {
        upic::Arc a;
        a.id = 1;
        a.points = { { 5.0, 220.0, 1.0f }, { 15.0, 880.0, 1.0f } };
        upic::ArcRefs arcs { &a };

        const auto b = upic::arcBounds (arcs, frame);
        check (b.valid, "a scope with points has bounds");
        check (std::abs (b.x0 - frame.normX (5.0)) < 1e-12
               && std::abs (b.x1 - frame.normX (15.0)) < 1e-12,
               "the bounds reach the first and the last point in time");
        check (std::abs (b.y0 - frame.normY (220.0)) < 1e-12
               && std::abs (b.y1 - frame.normY (880.0)) < 1e-12,
               "the bounds reach the lowest and the highest pitch");
        check (std::abs (b.centreX() - (b.x0 + b.x1) * 0.5) < 1e-12
               && std::abs (b.centreY() - (b.y0 + b.y1) * 0.5) < 1e-12,
               "the centre of the bounds is their middle");

        upic::Arc empty;
        upic::ArcRefs emptyArcs { &empty };
        check (! upic::arcBounds (emptyArcs, frame).valid,
               "a scope without points has no bounds");

        const upic::ArcRefs nothing;
        check (! upic::arcBounds (nothing, frame).valid,
               "an empty scope has no bounds");

        upic::Arc c;
        c.id = 2;
        c.points = { { 20.0, 110.0, 1.0f } };
        upic::ArcRefs both { &a, &c };
        const auto b2 = upic::arcBounds (both, frame);
        check (std::abs (b2.x1 - frame.normX (20.0)) < 1e-12
               && std::abs (b2.y0 - frame.normY (110.0)) < 1e-12,
               "the bounds of a scope are the union of its arcs' bounds");

        upic::ArcRefs withNull { nullptr, &a };
        const auto b3 = upic::arcBounds (withNull, frame);
        check (b3.valid && std::abs (b3.x1 - b.x1) < 1e-12,
               "a scope with an arc that is not there still has bounds");
    }

    // --- the pitch centre --------------------------------------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 1.0f }, { 1.0, 880.0, 1.0f } };
        check (std::abs (upic::arcPitchCentre (a) - std::sqrt (440.0 * 880.0)) < 1e-6,
               "the pitch centre is the geometric mean of the pitches",
               juce::String (upic::arcPitchCentre (a), 4) + " Hz");
        const upic::Arc empty;
        check (upic::arcPitchCentre (empty) == 0.0,
               "an arc without points has no pitch centre");
    }

    // --- writing a moved point back ---------------------------------------
    {
        upic::ArcPoint p { 5.0, 440.0, 1.0f };
        upic::writeTransformedPoint (p, 7.0, 880.0, frame);
        check (p.t == 7.0 && p.freq == 880.0,
               "a moved point is written back");

        upic::writeTransformedPoint (p, -5.0, 1e300, frame);
        check (p.t == 0.0 && p.freq == upic::kMaxFreq,
               "a moved point stays inside the page and inside the model's range");

        upic::ArcPoint keep { 5.0, 440.0, 1.0f };
        upic::writeTransformedPoint (keep, std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::quiet_NaN(), frame);
        check (keep.t == 5.0 && keep.freq == 440.0,
               "a value that is not a number leaves the point as it was");

        upic::ArcPoint over { 5.0, 440.0, 1.0f };
        upic::writeTransformedPoint (over, 1000.0, 440.0, frame);
        check (over.t == frame.duration,
               "a point that leaves the page is pulled back to its end");
    }

    // --- the rotation ------------------------------------------------------
    {
        upic::Arc a;
        a.id = 1;
        a.points = { { 5.0, 220.0, 1.0f }, { 10.0, 440.0, 1.0f }, { 15.0, 880.0, 1.0f } };
        upic::ArcRefs arcs { &a };
        const auto bounds = upic::arcBounds (arcs, frame);
        const double before = lengthIn (a, frame);
        upic::transformRotate (arcs, frame, bounds.centreX(), bounds.centreY(),
                               15.0 * kPi / 180.0);
        check (std::abs (before - lengthIn (a, frame)) < 1e-9,
               "the rotation keeps the length of the curve",
               juce::String (before, 6) + " -> " + juce::String (lengthIn (a, frame), 6));
        check (pointsAreSound (a, frame),
               "the rotation leaves the points sorted and in range");

        upic::Arc still;
        still.points = { { 5.0, 220.0, 1.0f } };
        upic::ArcRefs stillArcs { &still };
        upic::transformRotate (stillArcs, frame, 0.5, 0.5, 0.0);
        check (still.points[0].t == 5.0 && still.points[0].freq == 220.0,
               "a rotation of no angle changes nothing");

        upic::transformRotate (stillArcs, frame, 0.5, 0.5,
                               std::numeric_limits<double>::quiet_NaN());
        check (still.points[0].t == 5.0 && still.points[0].freq == 220.0,
               "a rotation with a broken angle changes nothing");
    }

    // --- the two mirrors ---------------------------------------------------
    {
        upic::Arc a;
        a.id = 1;
        a.points = { { 5.0, 220.0, 1.0f }, { 10.0, 440.0, 1.0f }, { 15.0, 880.0, 1.0f } };
        upic::ArcRefs arcs { &a };
        const auto bounds = upic::arcBounds (arcs, frame);
        const double t0 = a.points[0].t, t2 = a.points[2].t;

        upic::transformMirrorTime (arcs, frame, bounds.centreX());
        check (std::abs (a.points[0].t - (2.0 * bounds.centreX() * frame.duration - t2)) < 1e-6,
               "the mirror in time swaps the ends of the curve",
               juce::String (a.points[0].t, 4) + " / " + juce::String (a.points[2].t, 4));
        check (pointsAreSound (a, frame),
               "the mirror in time leaves the points sorted and in range");
        upic::transformMirrorTime (arcs, frame, bounds.centreX());
        check (std::abs (a.points[0].t - t0) < 1e-6 && std::abs (a.points[2].t - t2) < 1e-6,
               "the mirror in time is its own inverse");

        upic::transformMirrorTime (arcs, frame, std::numeric_limits<double>::quiet_NaN());
        check (std::abs (a.points[0].t - t0) < 1e-6,
               "a mirror with a broken axis changes nothing");
    }
    {
        upic::Arc a;
        a.id = 1;
        a.points = { { 5.0, 220.0, 1.0f }, { 10.0, 440.0, 1.0f }, { 15.0, 880.0, 1.0f } };
        upic::ArcRefs arcs { &a };
        const auto bounds = upic::arcBounds (arcs, frame);
        const double f0 = a.points[0].freq, f2 = a.points[2].freq;
        const double t0 = a.points[0].t;

        upic::transformMirrorPitch (arcs, frame, bounds.centreY());
        check (std::abs (a.points[0].freq - f2) < 1e-6
               && std::abs (a.points[2].freq - f0) < 1e-6,
               "the mirror in pitch turns the curve over",
               juce::String (a.points[0].freq, 3) + " / " + juce::String (a.points[2].freq, 3) + " Hz");
        check (std::abs (a.points[0].t - t0) < 1e-12,
               "the mirror in pitch leaves the times alone");
        upic::transformMirrorPitch (arcs, frame, bounds.centreY());
        check (std::abs (a.points[0].freq - f0) < 1e-6 && std::abs (a.points[2].freq - f2) < 1e-6,
               "the mirror in pitch is its own inverse");

        upic::transformMirrorPitch (arcs, frame, std::numeric_limits<double>::quiet_NaN());
        check (std::abs (a.points[0].freq - f0) < 1e-6,
               "a mirror with a broken axis changes nothing");
    }

    // --- the scale ---------------------------------------------------------
    {
        upic::Arc a;
        a.id = 1;
        a.points = { { 10.0, 220.0, 1.0f }, { 12.0, 880.0, 1.0f } };
        upic::ArcRefs arcs { &a };
        const auto bounds = upic::arcBounds (arcs, frame);

        upic::transformScale (arcs, frame, bounds.centreX(), bounds.centreY(), 1.0, 2.0);
        check (std::abs (a.points[1].freq / a.points[0].freq - 16.0) < 1e-3,
               "the pitch scale doubles the distance in octaves",
               juce::String (a.points[1].freq / a.points[0].freq, 4) + " (expect 16)");
        upic::transformScale (arcs, frame, bounds.centreX(), bounds.centreY(), 2.0, 1.0);
        check (std::abs ((a.points[1].t - a.points[0].t) - 4.0) < 1e-6,
               "the time scale doubles the time span",
               juce::String (a.points[1].t - a.points[0].t, 4) + " s");
        check (pointsAreSound (a, frame),
               "the scale leaves the points sorted and in range");

        const double t = a.points[0].t;
        upic::transformScale (arcs, frame, bounds.centreX(), bounds.centreY(), 0.0, 1.0);
        check (a.points[0].t == t, "a scale of zero changes nothing");
        upic::transformScale (arcs, frame, bounds.centreX(), bounds.centreY(),
                              std::numeric_limits<double>::quiet_NaN(), 1.0);
        check (a.points[0].t == t, "a scale that is not a number changes nothing");
    }

    // --- the alignment -----------------------------------------------------
    {
        upic::Arc a, b;
        a.id = 1; a.points = { { 2.0, 220.0, 1.0f }, { 4.0, 440.0, 1.0f } };
        b.id = 2; b.points = { { 6.0, 330.0, 1.0f }, { 9.0, 660.0, 1.0f } };
        upic::ArcRefs arcs { &a, &b };

        upic::transformAlign (arcs, frame, 0);
        check (std::abs (a.startTime() - b.startTime()) < 1e-9
               && std::abs (a.startTime() - 2.0) < 1e-9,
               "the alignment of the starts takes the earliest start",
               juce::String (a.startTime(), 4) + " / " + juce::String (b.startTime(), 4));
    }
    {
        upic::Arc a, b;
        a.id = 1; a.points = { { 2.0, 220.0, 1.0f }, { 4.0, 440.0, 1.0f } };
        b.id = 2; b.points = { { 6.0, 330.0, 1.0f }, { 9.0, 660.0, 1.0f } };
        upic::ArcRefs arcs { &a, &b };

        upic::transformAlign (arcs, frame, 1);
        check (std::abs (a.endTime() - b.endTime()) < 1e-9
               && std::abs (a.endTime() - 9.0) < 1e-9,
               "the alignment of the ends takes the latest end",
               juce::String (a.endTime(), 4) + " / " + juce::String (b.endTime(), 4));
    }
    {
        upic::Arc a, b;
        a.id = 1; a.points = { { 2.0, 220.0, 1.0f }, { 4.0, 440.0, 1.0f } };
        b.id = 2; b.points = { { 6.0, 330.0, 1.0f }, { 9.0, 660.0, 1.0f } };
        upic::ArcRefs arcs { &a, &b };

        upic::transformAlign (arcs, frame, 2);
        check (std::abs (upic::arcPitchCentre (a) - upic::arcPitchCentre (b)) < 1e-6,
               "the alignment of the pitch gives the arcs the same pitch centre",
               juce::String (upic::arcPitchCentre (a), 3) + " / "
               + juce::String (upic::arcPitchCentre (b), 3));
        check (std::abs (a.startTime() - 2.0) < 1e-12 && std::abs (b.startTime() - 6.0) < 1e-12,
               "the alignment of the pitch leaves the times alone");
    }
    {
        upic::Arc a, b;
        a.id = 1; a.points = { { 2.0, 220.0, 1.0f }, { 4.0, 440.0, 1.0f } };
        b.id = 2; b.points = { { 6.0, 330.0, 1.0f }, { 9.0, 660.0, 1.0f } };
        upic::ArcRefs arcs { &a, &b, nullptr };
        upic::transformAlign (arcs, frame, 0);
        check (std::abs (a.startTime() - b.startTime()) < 1e-9,
               "the alignment skips an arc that is not there");
    }
    {
        const upic::ArcRefs nothing;
        upic::transformAlign (nothing, frame, 0);
        check (true, "the alignment of an empty scope does nothing");
    }

    // --- the repeat --------------------------------------------------------
    {
        upic::Arc a;
        a.id = 7;
        a.waveformId = 3;
        a.envelopeId = 4;
        a.points = { { 2.0, 220.0, 1.0f }, { 4.0, 440.0, 1.0f } };
        upic::ArcRefs arcs { &a };

        const auto copies = upic::transformRepeat (arcs, frame, 4.0, 12.0, 2, 100);
        check (copies.size() == 2,
               "the repeat makes the copies it was asked for",
               juce::String (copies.size()));
        if (copies.size() == 2)
        {
            check (copies[0].id == 100 && copies[1].id == 101,
                   "the copies get their own ids",
                   juce::String (copies[0].id) + " / " + juce::String (copies[1].id));
            check (copies[0].id != a.id,
                   "a copy does not reuse the original's id");
            check (std::abs (copies[0].points[0].t - 6.0) < 1e-6
                   && std::abs (copies[1].points[0].t - 10.0) < 1e-6,
                   "each copy is one step further along",
                   juce::String (copies[0].points[0].t, 4) + " / "
                   + juce::String (copies[1].points[0].t, 4) + " s");
            check (std::abs (copies[0].points[0].freq - 440.0) < 1e-6
                   && std::abs (copies[1].points[0].freq - 880.0) < 1e-6,
                   "each copy is one step higher in pitch",
                   juce::String (copies[0].points[0].freq, 3) + " / "
                   + juce::String (copies[1].points[0].freq, 3) + " Hz");
            check (copies[0].waveformId == a.waveformId
                   && copies[0].envelopeId == a.envelopeId,
                   "a copy keeps the material of the original");
            check (pointsAreSound (copies[0], frame) && pointsAreSound (copies[1], frame),
                   "the copies are sorted and in range");
        }

        const auto again = upic::transformRepeat (arcs, frame, 4.0, 12.0, 2, 100);
        check (again.size() == 2 && again[0].id == 100 && again[1].id == 101,
               "the repeat is stable - the same input gives the same ids");

        check (upic::transformRepeat (arcs, frame, 4.0, 12.0, 0, 100).empty(),
               "a repeat of no copies makes no copies");
        check (upic::transformRepeat (arcs, frame,
                                      std::numeric_limits<double>::quiet_NaN(), 12.0, 2, 100).empty(),
               "a repeat with a broken offset makes no copies");

        const upic::ArcRefs nothing;
        check (upic::transformRepeat (nothing, frame, 4.0, 12.0, 2, 100).empty(),
               "a repeat of an empty scope makes no copies");
    }
}
