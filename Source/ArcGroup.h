#pragma once

#include <JuceHeader.h>
#include <vector>

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
//
// R2.5: the file format of a group is a different thing from what a group
// is. It lives at the edge (Source/ArcGroupCodec.h), where the JUCE value
// types are kept. The name and the colour stay JUCE's for now: the plan
// defers replacing the domain's value types (section 2), so the group
// still asks for the JUCE header.
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
