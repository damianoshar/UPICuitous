#pragma once

#include <JuceHeader.h>

namespace upic
{
    inline juce::Colour paletteColour(int i)
    {
        static const juce::Colour cols[] = {
            juce::Colour(0xff5ac8fa), juce::Colour(0xffff9f0a), juce::Colour(0xffff375f),
            juce::Colour(0xff30d158), juce::Colour(0xffbf5af2), juce::Colour(0xffffd60a),
            juce::Colour(0xff64d2ff), juce::Colour(0xffff6482), juce::Colour(0xff0a84ff),
            juce::Colour(0xff30b0c7), juce::Colour(0xffac8e68), juce::Colour(0xff98989f),
            juce::Colour(0xff7d7aff), juce::Colour(0xff34c759), juce::Colour(0xffff453a),
            juce::Colour(0xff66d4cf),
        };
        return cols[i % 16];
    }

    // v1.33.3: one dark surface for the whole window. Pure black made the
    // cards look like they left holes and shadows wherever the background
    // showed through - in the toolbar, around the score and in the editors.
    inline juce::Colour uiBackground() { return juce::Colour (0xff131720); }
}
