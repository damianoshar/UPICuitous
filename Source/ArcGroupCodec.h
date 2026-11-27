#pragma once

//==============================================================================
// R2.5: the arc group's file format - the edge.
//
// The group is the model's data (Source/ArcGroup.h). How a group reaches a
// piece file, and how it comes back, is a different thing: this is the
// edge, and the JUCE value types live here.
//
// The bodies are the members the group used to have, body for body. The
// property names, their order, the optional keys a default group does not
// write and the clamps a crafted file gets are the file format itself, and
// they did not change by a character.
#include <JuceHeader.h>

#include <algorithm>

#include "ArcGroup.h"
#include "PieceLimits.h"

namespace upic
{
    // The group as a piece file carries it.
    inline juce::var arcGroupToVar (const ArcGroup& g)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", g.id);
        o->setProperty ("name", juce::String (g.name));
        o->setProperty ("colour", (int) g.colour);
        juce::Array<juce::var> a;
        for (int aid : g.arcIds) a.add (aid);
        o->setProperty ("arcs", a);
        if (g.muted) o->setProperty ("mute", true);
        if (g.solo) o->setProperty ("solo", true);
        if (g.gain != 1.0f) o->setProperty ("gain", g.gain);
        if (g.transposeSemitones != 0.0) o->setProperty ("transpose", g.transposeSemitones);
        if (g.waveformId >= 0) o->setProperty ("wave", g.waveformId);
        if (g.envelopeId >= 0) o->setProperty ("env", g.envelopeId);
        if (g.amplitudeTableId >= 0) o->setProperty ("ampTable", g.amplitudeTableId);
        if (g.channelId >= 0) o->setProperty ("channel", g.channelId);
        return juce::var (o);
    }

    // The group a piece file carries, read back. A file is untrusted input:
    // every number is clamped against the model's limits, and the name and
    // the colour are the shapes the file holds.
    inline ArcGroup arcGroupFromVar (const juce::var& v, int fallbackId)
    {
        ArcGroup g;
        g.id = juce::jlimit (1, limits::kMaxItemId, (int) v.getProperty ("id", fallbackId));
        if (g.id <= 0) g.id = juce::jmax (1, fallbackId);
        const auto n = v.getProperty ("name", "Group").toString();
        g.name = n.isNotEmpty() ? n.substring (0, 64).toStdString() : std::string ("Group");
        g.colour = (std::uint32_t) (int) v.getProperty ("colour", (int) 0xff5ac8fa);
        if (auto* arr = v.getProperty ("arcs", juce::var()).getArray())
            for (auto& av : *arr)
            {
                if ((int) g.arcIds.size() >= limits::kMaxArcsPerPage) break;
                const int aid = (int) av;
                if (aid <= 0 || aid > limits::kMaxItemId) continue;
                if (std::find (g.arcIds.begin(), g.arcIds.end(), aid) == g.arcIds.end())
                    g.arcIds.push_back (aid);
            }
        g.muted = (bool) v.getProperty ("mute", false);
        g.solo = (bool) v.getProperty ("solo", false);
        g.gain = (float) limits::clampFinite ((double) v.getProperty ("gain", 1.0), 0.0, 2.0);
        g.transposeSemitones = limits::clampFinite ((double) v.getProperty ("transpose", 0.0),
                                                    -48.0, 48.0);
        auto bankId = [&] (const char* key)
        {
            return juce::jlimit (-1, limits::kMaxItemId, (int) v.getProperty (key, -1));
        };
        g.waveformId = bankId ("wave");
        g.envelopeId = bankId ("env");
        g.amplitudeTableId = bankId ("ampTable");
        g.channelId = bankId ("channel");
        return g;
    }
}
