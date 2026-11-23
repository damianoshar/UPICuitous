#pragma once
#include <JuceHeader.h>
#include <vector>
#include "PieceLimits.h"

//==============================================================================
// WP3: persistent arc groups.
//
// A group is a named set of arcs on the page with shared, NON-DESTRUCTIVE
// attributes: mute, solo, gain, transpose and timbre/channel overrides. The
// arcs' own data is never changed - the group is resolved into a render-time
// override at snapshot publication, which is also where solo is decided.
//
// The model follows the historical UPIC: groups were a performance structure
// (the four groups a page could address), not an edit. Deleting a group
// leaves every arc exactly as it was.
namespace upic
{
    struct ArcGroup
    {
        int id = 0;
        juce::String name = "Group";
        juce::Colour colour { 0xff5ac8fa };
        std::vector<int> arcIds;      // membership, one group per arc
        bool muted = false;
        bool solo = false;
        float gain = 1.0f;            // 0..2
        double transposeSemitones = 0.0;  // -48..48, applied after the space map
        // -1 means "keep the arc's own". The ids are checked against the
        // banks when the piece is loaded, so a missing one falls back to -1.
        int waveformId = -1;
        int envelopeId = -1;
        int amplitudeTableId = -1;
        int channelId = -1;           // used by WP6

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("name", name);
            o->setProperty ("colour", (int) colour.getARGB());
            juce::Array<juce::var> a;
            for (int aid : arcIds) a.add (aid);
            o->setProperty ("arcs", a);
            if (muted) o->setProperty ("mute", true);
            if (solo) o->setProperty ("solo", true);
            if (gain != 1.0f) o->setProperty ("gain", gain);
            if (transposeSemitones != 0.0) o->setProperty ("transpose", transposeSemitones);
            if (waveformId >= 0) o->setProperty ("wave", waveformId);
            if (envelopeId >= 0) o->setProperty ("env", envelopeId);
            if (amplitudeTableId >= 0) o->setProperty ("ampTable", amplitudeTableId);
            if (channelId >= 0) o->setProperty ("channel", channelId);
            return juce::var (o);
        }

        static ArcGroup fromVar (const juce::var& v, int fallbackId)
        {
            ArcGroup g;
            g.id = juce::jlimit (1, limits::kMaxItemId, (int) v.getProperty ("id", fallbackId));
            if (g.id <= 0) g.id = juce::jmax (1, fallbackId);
            const auto n = v.getProperty ("name", "Group").toString();
            g.name = n.isNotEmpty() ? n.substring (0, 64) : juce::String ("Group");
            g.colour = juce::Colour ((juce::uint32) (int) v.getProperty ("colour", (int) 0xff5ac8fa));
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
    };

    // The resolved view of one arc for one render, built on the message
    // thread. The audio thread only reads these; no group lookup happens per
    // sample, and no map is allocated in the audio thread.
    struct ArcRenderOverride
    {
        bool muted = false;
        float gain = 1.0f;
        double transposeSemitones = 0.0;
        int waveformId = -1;
        int envelopeId = -1;
        int amplitudeTableId = -1;
        int channelId = -1;

        // WP5: the resolved modulating arc (index into the page's arcs).
        // modLoLog2/modHiLog2 are the modulator's drawn log2-frequency
        // bounds, so the audio thread only normalises the value it computes.
        int modIndex = -1;
        int modTarget = 0;       // 0 = pitch, 1 = amplitude
        double modDepth = 0.0;   // 0..1
        int modAlign = 0;        // 0 = absolute time, 1 = carrier start
        double modLoLog2 = 0.0, modHiLog2 = 0.0;
    };
}
