#pragma once

//==============================================================================
// R2.7: the wave's file format - the edge.
//
// The wave is the model's data (Source/Waveform.h). How a wave reaches a
// piece file, and how it comes back, is a different thing: this is the
// edge, and the JUCE value types live here.
//
// The bodies are the members the wave used to have, body for body. The
// property names, their order, the defaults a file without them falls back
// to and the cap a table over the limit gets are the file format itself,
// and they did not change by a character.
#include <JuceHeader.h>

#include "UpicConstants.h"
#include "Waveform.h"

namespace upic
{
    // The wave as a piece file carries it.
    inline juce::var waveformToVar (const Waveform& w)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", w.id);
        o->setProperty ("name", w.name);
        o->setProperty ("colour", (int) w.colour.getARGB());
        juce::Array<juce::var> arr;
        arr.ensureStorageAllocated ((int) w.samples.size());
        for (auto s : w.samples) arr.add ((double) s);
        o->setProperty ("samples", arr);
        return juce::var (o);
    }

    // The wave a piece file carries, read back. The table keeps the length
    // it was written with - the piece carries the samples, so a wave that
    // was imported with 4096 samples stays a 4096-sample wave - up to the
    // cap. A file with no samples keeps the default size.
    inline Waveform waveformFromVar (const juce::var& v, int fallbackId)
    {
        Waveform w;
        w.id = (int) v.getProperty ("id", fallbackId);
        w.name = v.getProperty ("name", "Wave").toString();
        w.colour = juce::Colour ((juce::uint32) (int) v.getProperty ("colour", (int) 0xff5ac8fa));
        if (auto* arr = v.getProperty ("samples", juce::var()).getArray())
        {
            // WP10: the length is the wave's own. The piece carries the
            // samples, so the table keeps the length it was written
            // with - up to the cap. A piece with no samples keeps the
            // default size.
            const int n = juce::jmin (kMaxWaveSize, (int) arr->size());
            if (n > 0)
            {
                w.samples.assign ((size_t) n, 0.0f);
                for (int i = 0; i < n; ++i)
                    w.samples[(size_t) i] = (float) (double) (*arr)[i];
            }
        }
        return w;
    }
}
