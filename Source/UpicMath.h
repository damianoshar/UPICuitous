#pragma once

// R2.1: no JUCE header. The three numeric helpers this file used
// (juce::jmax, juce::jmin, juce::jlimit) are the standard library's, with
// the same expressions and therefore the same results - including the NaN
// behaviour, which all three leave unchanged. The domain's own clampFinite
// is the one that refuses a NaN.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "UpicConstants.h"

namespace upic
{
    inline double midiToFreq(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
    inline double freqToMidi(double f)
    {
        f = std::clamp (f, kMinFreq, kMaxFreq);
        return 69.0 + 12.0 * std::log2(f / 440.0);
    }

    inline float freqToNormY(double f) // 0=bottom(low) 1=top(high), log scale
    {
        double lo = std::log2(kMinFreq), hi = std::log2(kMaxFreq);
        double v = (std::log2(std::clamp (f, kMinFreq, kMaxFreq)) - lo) / (hi - lo);
        return (float) std::clamp (v, 0.0, 1.0);
    }
    inline double normYToFreq(float n)
    {
        n = std::clamp (n, 0.0f, 1.0f);
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
        const double l0 = std::log2 (std::max (1.0, lo));
        const double l1 = std::log2 (std::max (1.0, hi));
        return (float) ((std::log2 (std::max (1.0, f)) - l0)
                        / std::max (1.0e-6, l1 - l0));
    }
    inline double normYToFreqIn (float n, double lo, double hi)
    {
        n = std::clamp (n, 0.0f, 1.0f);
        const double l0 = std::log2 (std::max (1.0, lo));
        const double l1 = std::log2 (std::max (1.0, hi));
        return std::exp2 (l0 + (double) n * (l1 - l0));
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

    //---------------- the cursor lookups (v1.33.4, U6 option C) ------
    // The render walks the samples of a block in order, so the time moves
    // monotonically - forwards, or backwards when the transport is
    // reversed. The segment can therefore be FOLLOWED instead of searched:
    // the cursor is placed once (one binary search) and then only moves as
    // the time moves, which makes the per-sample cost O(1) instead of
    // O(log n).
    //
    // The cursor is a plain index into the point list it was made for: the
    // list must not change while the cursor is used, and the cursor must
    // not be shared between two lists.
    struct SegmentCursor
    {
        size_t index = 1;    // the segment's upper point (the lower one is index-1)
        bool placed = false; // the binary search has not run yet
    };

    // Moves the cursor to the segment that holds the key. The key may move
    // in either direction (the transport can be reversed); the cursor
    // follows it. The result is the same index findSegment() would return:
    // the first point whose key is >= the searched one.
    template <typename PointType, typename KeyType, typename KeyGetter>
    inline size_t followSegment (const std::vector<PointType>& points,
                                  KeyType key, KeyGetter keyOf,
                                  SegmentCursor& cursor)
    {
        const size_t n = points.size();
        if (n == 0) return 0;
        if (! cursor.placed)
        {
            cursor.index = findSegment (points, key, keyOf);
            cursor.placed = true;
            return cursor.index;
        }
        // the time moved forwards: skip the segments that are behind it
        while (cursor.index < n && keyOf (points[cursor.index]) < key)
            ++cursor.index;
        // the time moved backwards: step back over the segments that are
        // still ahead of it
        while (cursor.index > 0 && keyOf (points[cursor.index - 1]) >= key)
            --cursor.index;
        return cursor.index;
    }
    //---------------- Magnetic snap: stickiness + time grid (UPISketch-style) ----
    // Blend `value` toward `snapped` by stickiness in 0..1
    // (0 = free drawing, 1 = fully magnetic — the "Stickiness" slider).
    inline double stickify(double value, double snapped, double stickiness)
    {
        stickiness = std::clamp (stickiness, 0.0, 1.0);
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
        return std::clamp (tToEnd / fadeSec, 0.0, 1.0);
    }
}
