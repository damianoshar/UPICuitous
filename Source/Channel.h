#pragma once
#include <JuceHeader.h>
#include <array>
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

    //==============================================================================
    // WP6.4: the resolved mixer.
    //
    // The plan's design: the mix is resolved on the MESSAGE thread - in the
    // snapshot - so the audio thread only reads it: the solo flags, the
    // gains and the gain curve, the curve as a 64-point LUT. The audio
    // thread samples the LUT once per block (the plan's "block-quantised,
    // <= 1 block" rule), so the per-sample path costs a multiply instead of
    // a curve lookup.
    //
    // The mix is built from the channel bank, so the piece keeps naming a
    // channel by its id while the engine works with buckets.
    struct ChannelMix
    {
        // The plan's LUT resolution: 64 points over the page.
        static constexpr int kLutPoints = 64;

        int id = 0;        // the channel's id (the piece names a channel by its id)
        int bucket = 0;    // the engine's mix bucket (0 = Main, 1..N = the bank's channels)
        float gain = 1.0f; // 0..2
        bool muted = false;
        bool solo = false;
        // The curve, sampled over the page (x = 0..1). A flat curve is all
        // ones, so the lookup is a multiply by one.
        std::array<float, kLutPoints> envLut {};

        ChannelMix() { envLut.fill (1.0f); }

        // The LUT lookup: linear between the points. The curve is sampled
        // at block rate, so this runs once per block per channel.
        float curveAt (float x) const
        {
            const float t = juce::jlimit (0.0f, 1.0f, x) * (float) (kLutPoints - 1);
            const int i = (int) t;
            if (i >= kLutPoints - 1) return envLut[(size_t) (kLutPoints - 1)];
            const float f = t - (float) i;
            return envLut[(size_t) i]
                 + (envLut[(size_t) (i + 1)] - envLut[(size_t) i]) * f;
        }
    };

    // The mix is resolved from the channel bank on the message thread (the
    // snapshot publisher) or on the export's worker thread. It never runs
    // on the audio thread: the LUT is built once, not per block.
    inline std::vector<ChannelMix> buildChannelMix (const std::vector<Channel>& channels)
    {
        std::vector<ChannelMix> mix;
        mix.reserve (channels.size());
        for (size_t i = 0; i < channels.size(); ++i)
        {
            const Channel& c = channels[i];
            ChannelMix m;
            m.id = c.id;
            m.bucket = (int) i + 1;
            m.gain = c.gain;
            m.muted = c.muted;
            m.solo = c.solo;
            // The curve: 64 points over the page. The cursor walks the
            // channel's own point list (a cursor belongs to one list).
            SegmentCursor cursor;
            for (int k = 0; k < ChannelMix::kLutPoints; ++k)
            {
                const float x = (float) k / (float) (ChannelMix::kLutPoints - 1);
                m.envLut[(size_t) k] = c.envelope.empty() ? 1.0f
                                                           : c.envelopeAt (x, cursor);
            }
            mix.push_back (m);
        }
        return mix;
    }
}
