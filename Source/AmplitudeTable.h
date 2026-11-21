#pragma once
#include <JuceHeader.h>
#include "Envelope.h"

//==============================================================================
// WP2: the amplitude conversion tables.
//
// The New UPIC separated the shape of a sound's amplitude (the envelope)
// from the mapping applied to it. The same envelope through a different
// table is louder, compressed, expanded or curved - a transfer function from
// the envelope's output (0..1) to the gain that is actually applied (0..1).
//
// The curve is the same type as an envelope on purpose: both banks reuse the
// envelope data, the envelope editor and all of their serialization. The
// BANKS are separate (a table can never be picked as an amplitude envelope),
// and an arc names one with `Arc::amplitudeTableId`.
namespace upic
{
    using AmplitudeTable = Envelope;

    // The presets a new table opens with. x is the envelope's output, y is
    // the gain it becomes: Linear is the identity, Square is v^2 (quieter
    // mid-values), Compress lifts the middle (louder), Expand pushes it down,
    // and S is the soft S-curve.
    enum AmpTablePreset
    {
        ampLinear = 0,
        ampSquare,
        ampCompress,
        ampExpand,
        ampS,
        ampPresetCount
    };

    inline const char* ampPresetName (int p)
    {
        switch (p)
        {
            case ampLinear:   return "Linear";
            case ampSquare:   return "Square";
            case ampCompress: return "Compress";
            case ampExpand:   return "Expand";
            case ampS:        return "S-curve";
            default:          return "Linear";
        }
    }

    inline void ampPresetPoints (int p, std::vector<EnvPoint>& pts)
    {
        switch (p)
        {
            case ampSquare:   pts = { { 0.0f, 0.0f }, { 0.25f, 0.0625f },
                                      { 0.5f, 0.25f }, { 0.75f, 0.5625f },
                                      { 1.0f, 1.0f } }; break;
            case ampCompress: pts = { { 0.0f, 0.0f }, { 0.25f, 0.4f },
                                      { 0.5f, 0.72f }, { 0.75f, 0.89f },
                                      { 1.0f, 1.0f } }; break;
            case ampExpand:   pts = { { 0.0f, 0.0f }, { 0.25f, 0.06f },
                                      { 0.5f, 0.26f }, { 0.75f, 0.58f },
                                      { 1.0f, 1.0f } }; break;
            case ampS:        pts = { { 0.0f, 0.0f }, { 0.25f, 0.12f },
                                      { 0.5f, 0.5f }, { 0.75f, 0.88f },
                                      { 1.0f, 1.0f } }; break;
            case ampLinear:
            default:          pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } }; break;
        }
    }

    inline AmplitudeTable makeAmplitudeTable (int id, const juce::String& name,
                                              juce::Colour colour, int preset)
    {
        AmplitudeTable t (id, name, colour);
        ampPresetPoints (preset, t.points);
        return t;
    }

    // The default bank of a new piece: one identity table, so the arc combo
    // has something to point at and the user can hear a swap immediately.
    inline std::vector<AmplitudeTable> defaultAmplitudeTables()
    {
        std::vector<AmplitudeTable> v;
        v.push_back (makeAmplitudeTable (1, "Linear", juce::Colour (0xff5aa0c8), ampLinear));
        return v;
    }
}
