#include <JuceHeader.h>
#include "SelfTest.h"
#include "ScoreEditor.h"
#include "UpicTheme.h"

#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
// R5.1 (the review's R043): the score canvas's render - the contract.
//
// ScoreEditor paints the page: the frame, the grid, the sieve zones, the
// arcs, the in-progress stroke, the selection marquee, the playhead, the
// workspace's mark and the hint lines. The review asks for that render to be
// a thing of its own (R043, "Separate ScoreRenderer from ScoreEditor"), so
// the drawing can be read, tested and changed without the editor's event
// handling around it.
//
// This suite is the render's contract, and it comes before the move, as
// every phase in this wave does. It pins what the canvas draws today:
//
// - the background outside the plot and the frame inside it;
// - the time grid (on and off);
// - the pitch lines: the A440 line, a C line, and the sample layer's Ref
//   line;
// - the arcs: the layer rule (an osc arc is not drawn in the sample layer),
//   the dot's colour, and the selection's brighter colour;
// - a zone's translucent fill;
// - the playhead, and its absence at zero;
// - the workspace's mark;
// - the hints (on and off change the image);
// - the render is deterministic, and painting does not mutate the model or
//   the view.
//
// The checks read single pixels at positions the editor's own public mapping
// gives them (timeToX / freqToY), so they do not hard-code the mapping - the
// coordinate mapper is R044's subject. The image is painted off-screen into
// a juce::Image, so no window, no device and no message loop are needed.
//
// The colours are the canvas's own (the same literals ScoreEditor.cpp
// paints); the suite pins them so a renderer extraction cannot change the
// picture by a shade.

namespace
{
    juce::Image render (ScoreEditor& ed)
    {
        juce::Image img (juce::Image::RGB, ed.getWidth(), ed.getHeight(), true);
        juce::Graphics g (img);
        ed.paint (g);
        return img;
    }

    juce::Colour at (const juce::Image& img, int x, int y)
    {
        return img.getPixelAt (juce::jlimit (0, img.getWidth() - 1, x),
                               juce::jlimit (0, img.getHeight() - 1, y));
    }

    int countColour (const juce::Image& img, juce::Colour c)
    {
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (img.getPixelAt (x, y) == c)
                    ++n;
        return n;
    }

    std::uint64_t imageHash (const juce::Image& img)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto argb = img.getPixelAt (x, y).getARGB();
                for (int b = 0; b < 4; ++b)
                {
                    h ^= (std::uint8_t) ((argb >> (b * 8)) & 0xffu);
                    h *= 1099511628211ull;
                }
            }
        return h;
    }

    // A page with two waves, an arc and a zone, bound to a standalone editor.
    struct Rig
    {
        std::vector<upic::Waveform> waves;
        upic::Page page;
        double playhead = 0.0;
        juce::CriticalSection lock;
        ScoreEditor ed;

        Rig()
        {
            waves.emplace_back (7, "Red", juce::Colours::red);
            waves.emplace_back (8, "Green", juce::Colours::lime);
            page.durationSec = 8.0;

            ed.setModelLock (&lock);
            ed.bind (&page, &waves, &playhead, nullptr);
            ed.setSize (800, 400);
        }
    };

    // The plot's own margins (the same four numbers ScoreEditor.cpp uses).
    constexpr int kPlotLeft = 6, kPlotTop = 4;
    constexpr int kPlotRight = 794, kPlotBottom = 394;
}

void upic::runScoreRenderChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the canvas and its frame -------------------------------------------
    {
        Rig rig;
        rig.ed.setShowGrid (false);
        const auto img = render (rig.ed);

        check (at (img, 2, 2) == upic::uiBackground(),
               "R5.1 the canvas paints the background outside the plot",
               at (img, 2, 2).toDisplayString (true));

        check (at (img, 400, 200) == juce::Colour (0xff1e1e26),
               "R5.1 the plot frame is filled",
               at (img, 400, 200).toDisplayString (true));

        bool border = false;
        for (int x = kPlotLeft - 1; x <= kPlotLeft + 2; ++x)
            border = border || at (img, x, 200) == juce::Colour (0xff34343f);
        check (border,
               "R5.1 the plot has a border",
               at (img, kPlotLeft, 200).toDisplayString (true));
    }

    // --- the grid ------------------------------------------------------------
    {
        Rig rig;
        const int x = (int) rig.ed.timeToX (1.5);

        rig.ed.setShowGrid (true);
        auto img = render (rig.ed);

        check (countColour (img, juce::Colour (0xff2a2a35)) > 100,
               "R5.1 the time grid is drawn",
               juce::String (countColour (img, juce::Colour (0xff2a2a35))));

        const int yA = (int) rig.ed.freqToY (440.0);
        check (at (img, x, yA) == juce::Colour (0xffff9f0a),
               "R5.1 the A440 line is orange",
               at (img, x, yA).toDisplayString (true));

        const int yC = (int) rig.ed.freqToY (upic::midiToFreq (60.0));
        check (at (img, x, yC) == juce::Colour (0xff3a3a4d),
               "R5.1 a C line is drawn",
               at (img, x, yC).toDisplayString (true));

        rig.ed.setShowGrid (false);
        img = render (rig.ed);
        check (countColour (img, juce::Colour (0xff2a2a35)) == 0,
               "R5.1 the grid can be switched off",
               juce::String (countColour (img, juce::Colour (0xff2a2a35))));
    }

    // --- the sample layer's Ref line ----------------------------------------
    {
        Rig rig;
        rig.ed.setLayer (1);
        rig.ed.setCurrentVoice (0, 0, 0.8f, 0, 1, 440.0);

        const auto img = render (rig.ed);
        const int x = (int) rig.ed.timeToX (1.5);
        const int y = (int) rig.ed.freqToY (440.0);
        check (at (img, x, y) == juce::Colour (0xffff9f0a),
               "R5.1 the sample layer's Ref line is orange",
               at (img, x, y).toDisplayString (true));
    }

    // --- the arcs ------------------------------------------------------------
    {
        Rig rig;
        upic::Arc arc;
        arc.id = 1;
        arc.mode = 0;
        arc.waveformId = 7;
        arc.points = { { 2.0, 300.0 } };
        rig.page.arcs.push_back (arc);
        rig.ed.setShowGrid (false);

        auto img = render (rig.ed);
        const int ax = (int) rig.ed.timeToX (2.0);
        const int ay = (int) rig.ed.freqToY (300.0);

        check (at (img, ax, ay) == juce::Colours::red.brighter (0.3f),
               "R5.1 the arc's dot is drawn in its wave's colour",
               at (img, ax, ay).toDisplayString (true));

        rig.ed.setLayer (1);
        img = render (rig.ed);
        check (at (img, ax, ay) == juce::Colour (0xff1e1e26),
               "R5.1 the osc arc is not drawn in the sample layer",
               at (img, ax, ay).toDisplayString (true));

        rig.ed.setLayer (0);
        rig.ed.setSelectedArc (1);
        img = render (rig.ed);
        check (at (img, ax, ay) == juce::Colours::red.brighter (0.6f).brighter (0.3f),
               "R5.1 a selected arc's dot is brighter",
               at (img, ax, ay).toDisplayString (true));
        rig.ed.setSelectedArc (-1);
    }

    // --- a sieve zone --------------------------------------------------------
    {
        Rig rig;
        upic::SieveZone zone;
        zone.id = 1;
        zone.t0 = 3.0;
        zone.t1 = 5.0;
        zone.f0 = 200.0;
        zone.f1 = 800.0;
        zone.m = 12;
        zone.remainders = { 0 };
        zone.timeGrid = 0.0;
        zone.stickiness = 1.0;
        rig.page.zones.push_back (zone);

        rig.ed.setShowGrid (false);
        rig.ed.setLayer (1); // no pitch guides in this layer

        const auto img = render (rig.ed);
        const int zx = (int) rig.ed.timeToX (4.0);
        const int zy = (int) rig.ed.freqToY (400.0);
        const auto expected = juce::Colour (0xff1e1e26)
                                  .overlaidWith (juce::Colour (0x2a5ac8fa));

        check (at (img, zx, zy) == expected,
               "R5.1 a zone is a translucent fill",
               at (img, zx, zy).toDisplayString (true) + " vs "
                   + expected.toDisplayString (true));
    }

    // --- the playhead --------------------------------------------------------
    {
        Rig rig;
        rig.playhead = 6.0;
        rig.ed.setPlaying (true);

        auto img = render (rig.ed);
        const int x = (int) rig.ed.timeToX (6.0);
        check (at (img, x, 200) == juce::Colour (0xffff375f),
               "R5.1 the playhead is drawn",
               at (img, x, 200).toDisplayString (true));

        rig.ed.setPlaying (false);
        rig.playhead = 0.0;
        img = render (rig.ed);
        check (at (img, x, 200) != juce::Colour (0xffff375f),
               "R5.1 no playhead is drawn at zero",
               at (img, x, 200).toDisplayString (true));
    }

    // --- the workspace's mark ------------------------------------------------
    {
        Rig rig;
        rig.ed.setWorkspaceMarked (true, "P1");

        const auto img = render (rig.ed);
        bool left = false;
        for (int x = kPlotLeft - 1; x <= kPlotLeft + 2; ++x)
            left = left || at (img, x, 200) == juce::Colour (0xff30d158);
        check (left,
               "R5.1 the workspace's mark is drawn on the border",
               at (img, kPlotLeft, 200).toDisplayString (true) + " / "
                   + at (img, kPlotLeft + 1, 200).toDisplayString (true));
    }

    // --- the hints, the determinism and the untouched model -----------------
    {
        Rig rig;
        rig.ed.setShowHints (true);
        const auto on = render (rig.ed);
        rig.ed.setShowHints (false);
        const auto off = render (rig.ed);

        check (imageHash (on) != imageHash (off),
               "R5.1 the hints are part of the render");

        rig.ed.setShowHints (true);
        const auto first = render (rig.ed);
        const auto second = render (rig.ed);
        check (imageHash (first) == imageHash (second),
               "R5.1 the render is deterministic");

        const auto arcs = rig.page.arcs.size();
        const auto zones = rig.page.zones.size();
        const auto zoom = rig.ed.getTimeZoom();
        const auto offset = rig.ed.getTimeOffset();
        (void) render (rig.ed);
        check (rig.page.arcs.size() == arcs && rig.page.zones.size() == zones
               && rig.page.durationSec == 8.0
               && rig.ed.getTimeZoom() == zoom && rig.ed.getTimeOffset() == offset,
               "R5.1 painting does not mutate the model or the view");
    }
}
