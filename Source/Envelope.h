#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <vector>

#include "PieceLimits.h"
#include "UpicMath.h"

namespace upic
{
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
            SegmentCursor c;
            return valueAt (t, c);
        }
        // v1.33.4 (U6-C): the cursor-based envelope lookup. The arc's life
        // position moves monotonically while the render walks the samples,
        // so the segment can be followed instead of searched.
        float valueAt(float t, SegmentCursor& cursor) const
        {
            if (points.empty()) return 1.0f;
            t = juce::jlimit(0.0f, 1.0f, t);
            if (t <= points.front().x) return points.front().y;
            if (t >= points.back().x)  return points.back().y;
            // The points are sorted by x, so the segment is found with a
            // binary search instead of a linear scan (this runs per sample).
            const size_t i = followSegment (points, t,
                                           [] (const EnvPoint& p) { return p.x; },
                                           cursor);
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

        // R2.6: the file format of an envelope is a different thing from what
        // an envelope is. It lives at the edge (Source/EnvelopeCodec.h),
        // where the JUCE value types are kept.
    };
}
