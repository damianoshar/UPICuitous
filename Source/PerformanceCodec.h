#pragma once

//==============================================================================
// R2.12: the performance sequence's file format - the edge.
//
// The performance sequence is the model's data (Source/Performance.h): the
// recorded movement through the score - a list of (performance time, page,
// position) points. How a sequence reaches a piece file, and how it comes
// back, is a different thing: this is the edge, and the JUCE value types
// live here.
//
// The bodies are the members the sequence used to have, body for body. The
// property names, their order, the optional key a looping sequence does not
// write, the clamps a crafted file gets, the cut of the name and the sort
// of the points are the file format itself, and they did not change by a
// character.
//
// The sequence's behaviour stays in Source/Performance.h: the length of the
// sequence, the position the performance is at, the loop time and the
// modulation factor.
#include <JuceHeader.h>

#include <algorithm>
#include <vector>

#include "Performance.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

namespace upic
{
    // The performance sequence as a piece file carries it: its id, its name,
    // its points and - when it does not loop - the loop flag.
    inline juce::var performanceSequenceToVar (const PerformanceSequence& s)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", s.id);
        o->setProperty ("name", s.name);
        if (! s.loop) o->setProperty ("loop", false);
        juce::Array<juce::var> arr;
        for (auto& p : s.points)
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

    // The performance sequence as a piece file carries it, read back. A file
    // is untrusted input: every number is clamped against the model's
    // limits, the name is cut, and the points come back in time order.
    inline PerformanceSequence performanceSequenceFromVar (const juce::var& v, int fallbackId)
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
}
