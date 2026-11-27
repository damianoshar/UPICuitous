#pragma once

//==============================================================================
// R2.10: the tuning's file format - the edge.
//
// The tuning is the model's data (Source/Tuning.h). How a tuning reaches a
// piece file, and how it comes back, is a different thing: this is the
// edge, and the JUCE value types live here.
//
// The bodies are the members the tuning used to have, body for body. The
// property names, their order, the clamps a crafted file gets, the rules a
// scale has to follow and the rule that a broken tuning falls back to the
// default WHOLE are the file format itself, and they did not change by a
// character.
//
// The Scala (.scl) importer is NOT here: it reads a file, and reading a file
// is an edge of its own (the same way the sample's file reading is).
#include <JuceHeader.h>

#include "PieceLimits.h"
#include "Tuning.h"

namespace upic
{
    // The tuning in a piece: the name, the base note and the degrees.
    inline juce::var tuningToVar (const Tuning& t)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", t.name);
        o->setProperty ("base", t.baseNote);
        juce::Array<juce::var> c;
        for (double x : t.cents) c.add (x);
        o->setProperty ("cents", c);
        return juce::var (o);
    }

    // A tuning read from a piece. A broken one falls back to the
    // default: a piece must always load.
    inline Tuning tuningFromVar (const juce::var& v)
    {
        Tuning t;
        if (v.getDynamicObject() == nullptr) return t;

        t.name = v.getProperty ("name", juce::var()).toString();
        t.baseNote = juce::jlimit (0, 127, (int) v.getProperty ("base", 60));

        if (auto* arr = v.getProperty ("cents", juce::var()).getArray())
            for (auto& cv : *arr)
            {
                const double c = (double) cv;
                if (! limits::isFinite (c) || c < 0.0 || c >= 1200.0) return Tuning{};
                if (t.cents.empty())
                {
                    if (std::abs (c) > 0.01) return Tuning{};   // the base is 0 cents
                }
                else if (c <= t.cents.back()) return Tuning{};   // the degrees must rise
                if ((int) t.cents.size() >= limits::kMaxTuningDegrees) return Tuning{};
                t.cents.push_back (c);
            }

        if (t.cents.empty()) return Tuning{};   // no scale -> the default
        t.custom = true;
        return t;
    }
}
