#pragma once

//==============================================================================
// R5.2 (the review's R044): the score canvas's coordinate mapper.
//
// The score canvas maps the page's time and pitch onto the screen: X is the
// time, Y is the pitch (logarithmic, over the page's ambitus). The drawing,
// the hit testing and the cursor's readout all need that mapping, and they
// have to agree with each other - the review says so:
//
//   "Rendering, hit testing and coordinate conversion should agree, but they
//    should not be implemented as one operation." (R044, section 24)
//   "`ScoreCoordinateMapper` should supply consistent mappings between
//    musical and screen coordinates." (section 24)
//
// This is that mapper. It is a small value: where the plot is on the screen,
// how long the page is, over which ambitus the pitch runs and where the view
// is zoomed and scrolled. It converts between the musical coordinates
// (seconds, Hz) and the screen coordinates (pixels), and it owns nothing:
// it reads the values it is given and writes nothing.
//
// The bodies are the editor's, body for body; the suite
// (Source/SelfTestCoordinateMapper.cpp) pins them.
//
// It is a header of its own so that the render (R043), the hit tester
// (R045) and the editor can all reach the same mapping without reaching
// into each other. Before this, the mapping lived inside the editor and the
// renderer borrowed it; now the mapping is the thing they share.

#include <JuceHeader.h>

#include "UpicModel.h"

namespace upic
{
    namespace score_coordinates
    {
        //================ the mapper =========================================
        // The musical coordinates and the screen coordinates, and the way
        // from one to the other.
        struct ScoreCoordinateMapper
        {
            // The plot's rectangle on the screen.
            juce::Rectangle<float> plot;

            // The page's length and ambitus.
            double durationSec = 30.0;
            double pitchLo = kDefaultPitchLo;
            double pitchHi = kDefaultPitchHi;

            // The view's zoom and scroll.
            double timeZoom = 1.0, timeOffset = 0.0;
            double freqZoom = 1.0, freqOffset = 0.0;

            // -- the time axis: seconds <-> pixels ---------------------------
            // A cursor outside the plot is clamped to the page's ends.
            double xToTime (float x) const
            {
                float f = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
                return f * durationSec / timeZoom + timeOffset;
            }

            float timeToX (double t) const
            {
                double f = (t - timeOffset) * timeZoom / juce::jmax (0.001, durationSec);
                return plot.getX() + (float) f * plot.getWidth();
            }

            // -- the pitch axis: Hz <-> pixels (logarithmic) ------------------
            double yToFreq (float y) const
            {
                float sN = 1.0f - juce::jlimit (0.0f, 1.0f, (y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()));
                double norm = (double) sN / freqZoom + freqOffset;
                return upic::normYToFreqIn ((float) juce::jlimit (0.0, 1.0, norm),
                                            pitchLo, pitchHi);
            }

            float freqToY (double f) const
            {
                // NOT clamped: a pitch outside the page's ambitus (or outside the
                // zoomed window) has to be drawn off the edge, where the plot area
                // clips it - clamping it would draw it as a line along the border.
                double sN = (upic::freqToNormYIn (f, pitchLo, pitchHi) - freqOffset) * freqZoom;
                return plot.getBottom() - (float) sN * plot.getHeight();
            }

            // -- a zone's rectangle -------------------------------------------
            // The times and the frequencies are order-independent, so the
            // corners are sorted.
            juce::Rectangle<float> zoneRect (const SieveZone& z) const
            {
                const float x0 = timeToX (juce::jmin (z.t0, z.t1));
                const float x1 = timeToX (juce::jmax (z.t0, z.t1));
                const float yTop  = freqToY (juce::jmax (z.f0, z.f1)); // higher freq = higher on screen
                const float yBot  = freqToY (juce::jmin (z.f0, z.f1));
                return { x0, yTop, juce::jmax (1.0f, x1 - x0), juce::jmax (1.0f, yBot - yTop) };
            }
        };
    }
}
