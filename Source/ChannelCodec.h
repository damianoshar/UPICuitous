#pragma once

//==============================================================================
// R2.8: the channel's file format - the edge.
//
// The channel is the model's data (Source/Channel.h). How a channel reaches
// a piece file, and how it comes back, is a different thing: this is the
// edge, and the JUCE value types live here.
//
// The bodies are the members the channel used to have, body for body. The
// property names, their order, the keys that are left out when they are at
// their default, the clamps a crafted file gets and the cap a curve over
// the limit gets are the file format itself, and they did not change by a
// character.
#include <JuceHeader.h>

#include "Channel.h"
#include "PieceLimits.h"

namespace upic
{
    // The channel as a piece file carries it. Only the keys that are not at
    // their default are written: id, name and colour are always there, and
    // gain, mute, solo and the curve come only when they carry something.
    inline juce::var channelToVar (const Channel& c)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", c.id);
        o->setProperty ("name", c.name);
        o->setProperty ("colour", (int) c.colour.getARGB());
        if (c.gain != 1.0f) o->setProperty ("gain", c.gain);
        if (c.muted) o->setProperty ("mute", true);
        if (c.solo) o->setProperty ("solo", true);
        if (! c.envelope.empty())
        {
            juce::Array<juce::var> arr;
            for (auto& p : c.envelope)
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("x", p.x);
                po->setProperty ("y", p.y);
                arr.add (juce::var (po));
            }
            o->setProperty ("env", arr);
        }
        return juce::var (o);
    }

    // The channel a piece file carries, read back. The id is pulled into
    // 1..the last id, the name is cut to 64 characters (and an empty one
    // falls back to "Channel"), the gain is pulled into 0..2, and the curve
    // is capped and sorted in time order.
    inline Channel channelFromVar (const juce::var& v, int fallbackId)
    {
        Channel c;
        c.id = juce::jlimit (1, limits::kMaxItemId, (int) v.getProperty ("id", fallbackId));
        if (c.id <= 0) c.id = juce::jmax (1, fallbackId);
        const auto n = v.getProperty ("name", "Channel").toString();
        c.name = n.isNotEmpty() ? n.substring (0, 64) : juce::String ("Channel");
        c.colour = juce::Colour ((juce::uint32) (int) v.getProperty ("colour", (int) 0xff5ac8fa));
        c.gain = (float) limits::clampFinite ((double) v.getProperty ("gain", 1.0), 0.0, 2.0);
        c.muted = (bool) v.getProperty ("mute", false);
        c.solo = (bool) v.getProperty ("solo", false);
        if (auto* arr = v.getProperty ("env", juce::var()).getArray())
            for (auto& pv : *arr)
            {
                if ((int) c.envelope.size() >= limits::kMaxPointsPerEnvelope) break;
                EnvPoint p;
                p.x = (float) limits::clampFinite ((double) pv.getProperty ("x", 0.0), 0.0, 1.0);
                p.y = (float) limits::clampFinite ((double) pv.getProperty ("y", 1.0), 0.0, 1.0);
                c.envelope.push_back (p);
            }
        std::sort (c.envelope.begin(), c.envelope.end(),
                   [] (const EnvPoint& a, const EnvPoint& b) { return a.x < b.x; });
        return c;
    }
}
