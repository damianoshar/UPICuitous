#pragma once
#include <JuceHeader.h>
#include <vector>
#include "Envelope.h"
#include "PieceLimits.h"
#include "UpicMath.h"

//==============================================================================
// WP6.1: the mixer channel MODEL only.
//
// A channel is piece-level: id, name, colour, gain, mute, solo and a
// page-long gain curve (x = page position 0..1, y = gain 0..1). Channel id 0
// is the implicit main channel - unity, no curve - so a piece that never
// creates a channel is byte- and sample-identical to 0.33.
//
// This wave deliberately changes NO audio path: the channels are carried in
// the piece and the snapshot so the later engine/UI milestones have a stable,
// tested model to build on.
namespace upic
{
    struct Channel
    {
        int id = 0;
        juce::String name = "Channel";
        juce::Colour colour { 0xff5ac8fa };
        float gain = 1.0f;          // 0..2
        bool muted = false;
        bool solo = false;
        std::vector<EnvPoint> envelope;   // empty = flat 1

        // Allocation-free lookup, shared by the future mixer. It is the same
        // segment walk the envelopes use (cursor-based).
        float envelopeAt (float x, SegmentCursor& c) const
        {
            if (envelope.empty()) return 1.0f;
            const float t = juce::jlimit (0.0f, 1.0f, x);
            if (t <= envelope.front().x) return envelope.front().y;
            if (t >= envelope.back().x) return envelope.back().y;
            const size_t i = followSegment (envelope, t,
                                            [] (const EnvPoint& p) { return p.x; }, c);
            const size_t i0 = i - 1, i1 = i;
            const float span = envelope[i1].x - envelope[i0].x;
            const float f = span > 1.0e-9f ? (t - envelope[i0].x) / span : 0.0f;
            return envelope[i0].y + (envelope[i1].y - envelope[i0].y) * f;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("name", name);
            o->setProperty ("colour", (int) colour.getARGB());
            if (gain != 1.0f) o->setProperty ("gain", gain);
            if (muted) o->setProperty ("mute", true);
            if (solo) o->setProperty ("solo", true);
            if (! envelope.empty())
            {
                juce::Array<juce::var> arr;
                for (auto& p : envelope)
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

        static Channel fromVar (const juce::var& v, int fallbackId)
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
    };
}
