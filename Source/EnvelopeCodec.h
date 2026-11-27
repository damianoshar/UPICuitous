#pragma once

//==============================================================================
// R2.6: the envelope's file format - the edge.
//
// The envelope is the model's data (Source/Envelope.h). How an envelope
// reaches a piece file, and how it comes back, is a different thing: this is
// the edge, and the JUCE value types live here.
//
// The bodies are the members the envelope used to have, body for body. The
// property names, their order, the defaults a file without them falls back
// to and the clamps a crafted file gets are the file format itself, and they
// did not change by a character.
#include <JuceHeader.h>

#include <algorithm>

#include "Envelope.h"
#include "PieceLimits.h"

namespace upic
{
    // The envelope as a piece file carries it.
    inline juce::var envelopeToVar (const Envelope& e)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("name", e.name);
        o->setProperty ("colour", (int) e.colour.getARGB());
        juce::Array<juce::var> arr;
        for (auto& p : e.points) { auto* po = new juce::DynamicObject(); po->setProperty ("x", p.x); po->setProperty ("y", p.y); arr.add (juce::var (po)); }
        o->setProperty ("points", arr);
        return juce::var (o);
    }

    // The envelope a piece file carries, read back. A file is untrusted input:
    // every number is clamped against the curve's own range, and the points
    // come back in time order.
    inline Envelope envelopeFromVar (const juce::var& v, int fallbackId)
    {
        Envelope e; e.id = (int) v.getProperty ("id", fallbackId);
        e.name = v.getProperty ("name", "Env").toString();
        e.colour = juce::Colour ((juce::uint32) (int) v.getProperty ("colour", (int) 0xffff9f0a));
        e.points.clear();
        if (auto* arr = v.getProperty ("points", juce::var()).getArray())
            for (auto& pv : *arr)
            {
                if ((int) e.points.size() >= limits::kMaxPointsPerEnvelope) break;
                e.points.push_back ({ limits::clampFinite ((float) (double) pv.getProperty ("x", 0), 0.0f, 1.0f),
                                     limits::clampFinite ((float) (double) pv.getProperty ("y", 1), 0.0f, 1.0f) });
            }
        if (e.points.empty()) e.presetFlat();
        std::sort (e.points.begin(), e.points.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        return e;
    }
}
