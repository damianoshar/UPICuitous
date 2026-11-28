#pragma once

//==============================================================================
// R5.1 (the review's R043): the score canvas's render.
//
// ScoreEditor paints the page: the frame, the grid, the sieve zones, the
// arcs, the in-progress stroke, the selection marquee, the playhead, the
// workspace's mark and the hint lines. The review asks for the render to be
// a thing of its own - "Separate ScoreRenderer from ScoreEditor" (R043) -
// so that the drawing can be read and tested on its own, and so that the
// hit testing (R045) and the coordinate mapping (R044) can agree with the
// drawing instead of being one operation with it.
//
// This is that render. It draws what it is handed: a View (where the plot
// is, and how the page's time and pitch map onto the screen) and a State
// (the page, the waves, the layers, the flags and the in-progress gesture).
// It owns nothing and it changes nothing: the editor keeps the gesture, the
// selection and the model.
//
// The bodies are the editor's paint(), body for body. The colours, the
// order of the passes (the zones under the grid, the arcs over it, the
// labels above them, the playhead over the labels) and the wording of the
// hint lines do not change by a character; the suite
// (Source/SelfTestScoreRender.cpp) pins the picture.
//
// The View is the mapping the editor and the render share, so the drawing
// and the hit testing cannot drift apart. The review gives the mapping a
// name of its own in R044 ("Introduce ScoreCoordinateMapper"); until then it
// lives here, next to the render that needs it.
#include <JuceHeader.h>

#include <vector>

#include "UpicModel.h"

namespace upic
{
    namespace score_render
    {
        //================ the view ===========================================
        // Where the plot is and how the page's time and pitch map onto it.
        // The editor builds one of these for every use of the mapping, so
        // the two never disagree about where a time or a pitch is.
        struct View
        {
            juce::Rectangle<float> plot;
            double durationSec = 30.0;
            double pitchLo = kDefaultPitchLo;
            double pitchHi = kDefaultPitchHi;
            double timeZoom = 1.0, timeOffset = 0.0;
            double freqZoom = 1.0, freqOffset = 0.0;

            double xToTime (float x) const;

            float timeToX (double t) const;

            double yToFreq (float y) const;

            float freqToY (double f) const;

            // The zone's rectangle on the screen. A zone's times and
            // frequencies are order-independent, so the corners are sorted.
            juce::Rectangle<float> zoneRect (const SieveZone& z) const;
        };

        //================ what is drawn ======================================
        // One point of the stroke in progress.
        struct StrokePoint
        {
            juce::Point<float> pos;
            float press = 1.0f;
        };

        // The state the render draws. The pointers borrow the editor's data
        // (the page, the waves, the tuning, the selection, the sieve's
        // remainders and the stroke); the render reads them and writes
        // nothing.
        struct State
        {
            View view;

            const Page* page = nullptr;
            const std::vector<Waveform>* waves = nullptr;
            const Tuning* tuning = nullptr;
            const std::vector<int>* selArcs = nullptr;
            const std::vector<int>* sieveR = nullptr;

            int layer = 0;            // 0 osc / 1 sample / 2 live
            bool showGrid = true;
            bool showHints = true;
            bool sieveOn = false;
            int sieveM = 12;
            double timeGrid = 0.0;    // divisions per second (0 = off)
            double stickiness = 1.0;  // 0..1 magnetic strength
            bool recordPressure = false;
            bool workspaceMarked = false;
            juce::String workspaceCaption;
            juce::String toolName;
            bool zoneTool = false;    // the Zone tool has its own hint
            double curRef = kDefaultRefFreq;
            int selectedZone = -1;

            // the in-progress gesture
            const std::vector<StrokePoint>* strokePts = nullptr;
            bool hasLineAnchor = false;
            juce::Point<float> lineAnchor;
            juce::Point<float> lastMouse;
            bool drawingZone = false;
            juce::Rectangle<float> zoneRubberRect;
            bool selectingMarquee = false;
            juce::Rectangle<float> marqueeRect;
            juce::Rectangle<float> scrubGrip;

            // the transport
            bool hasPlayhead = false;
            double playhead = 0.0;
            bool isPlaying = false;
            bool scrubbing = false;
        };

        //================ the render =========================================
        // Draws the page into the graphics.
        void paint (juce::Graphics& g, const State& s);
    }
}
