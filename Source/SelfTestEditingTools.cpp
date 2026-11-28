#include <JuceHeader.h>
#include "SelfTest.h"
#include "ScoreEditor.h"

#include <cmath>

//==============================================================================
// R5.4 (the review's R046, section 24): the editing tools - the contract.
//
// The score canvas's mouse handlers hold a condition for every drawing mode:
// the freehand stroke, the straight line, the sine stroke, the selection, the
// marquee, the move, the erase, the zone, the scrub. The review asks for an
// explicit tool controller instead:
//
//   "Rather than allowing mouse handlers to accumulate conditions for every
//    drawing mode, create an explicit tool controller." (section 24)
//
// This suite is the editing tools' contract, and it comes before the move, as
// every phase in this wave does. It pins what the tools do today:
//
// - the freehand stroke: one arc per drag, the points sorted by time, the
//   release's point part of the stroke, the stroke clipped to the page, the
//   stroke's simplification (and the fine mode's exemption from it), the
//   refusal of a stroke that is too short, the current material, the layer,
//   and the stroke staying temporary until the release;
// - the undo transaction: one labelled transaction per gesture;
// - the sine stroke: one arc, and the sine's wobble around the line;
// - the line: one arc that follows the drag, and its own label;
// - the erase: the arc under the cursor is deleted, and nothing else;
// - the selection: a click selects, a ctrl-click toggles, a shift-click adds,
//   the marquee takes the arcs inside it, and a click on empty space clears;
// - the move: a selected arc follows the pointer;
// - the zone: a drag draws a zone, and a click takes the one under it;
// - the scrub: the centre band scrubs with any tool, and draws nothing.
//
// The suite drives the editor with synthesised mouse events, the way the
// app's own UI tests do it.

namespace
{
    struct Rig
    {
        std::vector<upic::Waveform> waves;
        upic::Page page;
        double playhead = 0.0;
        juce::CriticalSection lock;
        ScoreEditor ed;

        // The gesture's bookkeeping, watched by the suite.
        int editsStarted = 0;
        int editsEnded = 0;
        juce::String lastLabel;
        juce::String lastNotice;

        Rig()
        {
            waves.emplace_back (7, "Red", juce::Colours::red);
            page.durationSec = 8.0;

            ed.setModelLock (&lock);
            ed.bind (&page, &waves, &playhead, nullptr);
            ed.setSize (800, 400);

            ed.onEditStart = [this] (const juce::String& what)
            {
                ++editsStarted;
                lastLabel = what;
            };
            ed.onEditEnd = [this] { ++editsEnded; };
            ed.onNotice = [this] (const juce::String& what) { lastNotice = what; };
        }

        // An arc with one point, in the osc layer.
        void addPointArc (int id, double t, double freq)
        {
            upic::Arc a;
            a.id = id;
            a.mode = 0;
            a.waveformId = 7;
            a.points = { { t, freq } };
            page.arcs.push_back (a);
        }
    };

    // The pointer's events, the way the app's own UI tests do it.
    juce::MouseEvent event (ScoreEditor& ed, juce::Point<float> pos,
                             juce::ModifierKeys mods, bool dragged,
                             juce::Point<float> downPos)
    {
        const auto now = juce::Time::getCurrentTime();
        const auto src = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent (src, pos, mods, 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now, downPos, now, 1, dragged);
    }

    void down (Rig& rig, juce::Point<float> pos,
               juce::ModifierKeys mods = juce::ModifierKeys())
    {
        rig.ed.mouseDown (event (rig.ed, pos, mods, false, pos));
    }

    void drag (Rig& rig, juce::Point<float> pos, juce::Point<float> downPos,
               juce::ModifierKeys mods = juce::ModifierKeys())
    {
        rig.ed.mouseDrag (event (rig.ed, pos, mods, true, downPos));
    }

    void up (Rig& rig, juce::Point<float> pos, juce::Point<float> downPos,
             juce::ModifierKeys mods = juce::ModifierKeys())
    {
        rig.ed.mouseUp (event (rig.ed, pos, mods, false, downPos));
    }
}

void upic::runEditingToolChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the freehand stroke -------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (200.0f, 120.0f);
        const juce::Point<float> p2 (300.0f, 140.0f);

        down (rig, p0);
        check (rig.page.arcs.empty(),
               "R5.4 the stroke stays temporary until the release",
               juce::String (rig.page.arcs.size()));

        drag (rig, p1, p0);
        check (rig.page.arcs.empty(),
               "R5.4 the stroke is not committed while the pointer moves",
               juce::String (rig.page.arcs.size()));

        up (rig, p2, p0);

        check (rig.page.arcs.size() == 1,
               "R5.4 a freehand drag makes one arc",
               juce::String (rig.page.arcs.size()));

        if (rig.page.arcs.size() == 1)
        {
            const auto& a = rig.page.arcs.front();
            bool sorted = true;
            for (size_t i = 1; i < a.points.size(); ++i)
                if (a.points[i].t < a.points[i - 1].t) sorted = false;
            check (sorted, "R5.4 the freehand arc's points are sorted by time");

            bool onPage = ! a.points.empty();
            for (const auto& p : a.points)
                if (p.t < -1e-9 || p.t > rig.page.durationSec + 1e-9
                    || p.freq < upic::kMinFreq - 1e-6 || p.freq > upic::kMaxFreq + 1e-6)
                    onPage = false;
            check (onPage, "R5.4 the freehand arc's points stay on the page");

            const double releaseT = rig.ed.xToTime (p2.x);
            check (a.points.size() >= 2
                   && std::abs (a.points.back().t - releaseT) < 0.02,
                   "R5.4 the release's point is part of the stroke",
                   juce::String (a.points.empty() ? -1.0 : a.points.back().t, 4)
                       + " vs " + juce::String (releaseT, 4));

            bool fullPressure = ! a.points.empty();
            for (const auto& p : a.points)
                if (std::abs (p.i - 1.0f) > 1e-6f) fullPressure = false;
            check (fullPressure,
                   "R5.4 the stroke's points carry the pressure (none here)",
                   a.points.empty() ? juce::String ("no points")
                                    : juce::String (a.points.front().i, 3));
        }

        check (rig.editsStarted == 1 && rig.editsEnded == 1,
               "R5.4 a freehand drag is one undo transaction",
               juce::String (rig.editsStarted) + " / " + juce::String (rig.editsEnded));

        check (rig.lastLabel == "Draw",
               "R5.4 the freehand transaction is labelled",
               rig.lastLabel);

        check (rig.ed.getSelectedArc() == (rig.page.arcs.empty() ? -1 : rig.page.arcs.front().id),
               "R5.4 the new arc is the selection",
               juce::String (rig.ed.getSelectedArc()));
    }

    // --- the stroke's material ------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);
        rig.ed.setCurrentVoice (7, 3, 0.5f, 0, 1, 440.0);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (300.0f, 140.0f);
        down (rig, p0);
        drag (rig, p1, p0);
        up (rig, p1, p0);

        check (rig.page.arcs.size() == 1
               && rig.page.arcs.front().waveformId == 7
               && rig.page.arcs.front().envelopeId == 3
               && std::abs (rig.page.arcs.front().intensity - 0.5f) < 1e-6f,
               "R5.4 the stroke keeps the current material",
               rig.page.arcs.empty() ? juce::String ("no arc")
                                     : juce::String (rig.page.arcs.front().waveformId) + " / "
                                         + juce::String (rig.page.arcs.front().envelopeId));
    }

    // --- the stroke's layer ---------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);
        rig.ed.setLayer (1);   // the sample layer

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (300.0f, 140.0f);
        down (rig, p0);
        drag (rig, p1, p0);
        up (rig, p1, p0);

        check (rig.page.arcs.size() == 1 && rig.page.arcs.front().mode == 1,
               "R5.4 the stroke's arc belongs to the layer it was drawn in",
               rig.page.arcs.empty() ? juce::String ("no arc")
                                     : juce::String (rig.page.arcs.front().mode));
    }

    // --- the stroke's simplification ------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);
        rig.ed.setSnap (false);   // the pitch is free, so the shape is the shape

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (200.0f, 120.0f);
        const juce::Point<float> p2 (300.0f, 140.0f);
        down (rig, p0);
        drag (rig, p1, p0);
        up (rig, p2, p0);

        check (rig.page.arcs.size() == 1 && rig.page.arcs.front().points.size() == 2,
               "R5.4 a straight drag is simplified to its two ends",
               rig.page.arcs.empty() ? juce::String ("no arc")
                                     : juce::String (rig.page.arcs.front().points.size()));
    }

    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);
        rig.ed.setSnap (false);
        rig.ed.setFineDrawing (true);   // the fine mode skips the simplification

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (200.0f, 120.0f);
        const juce::Point<float> p2 (300.0f, 140.0f);
        down (rig, p0);
        drag (rig, p1, p0);
        up (rig, p2, p0);

        check (rig.page.arcs.size() == 1 && rig.page.arcs.front().points.size() == 3,
               "R5.4 the fine mode keeps every sampled point",
               rig.page.arcs.empty() ? juce::String ("no arc")
                                     : juce::String (rig.page.arcs.front().points.size()));
    }

    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);
        rig.ed.setSnap (false);

        // tiny steps: the pointer moves, but not far enough to be a new point
        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        for (int i = 1; i <= 20; ++i)
            drag (rig, { 100.0f + (float) i, 100.0f }, p0);
        up (rig, { 120.0f, 100.0f }, p0);

        check (rig.page.arcs.size() == 1 && rig.page.arcs.front().points.size() <= 2,
               "R5.4 a stroke of tiny steps keeps few points",
               rig.page.arcs.empty() ? juce::String ("no arc")
                                     : juce::String (rig.page.arcs.front().points.size()));
    }

    // --- the stroke that is too short -----------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        up (rig, p0, p0);

        check (rig.page.arcs.empty(),
               "R5.4 a stroke that is too short makes no arc",
               juce::String (rig.page.arcs.size()));

        check (rig.lastNotice == "The stroke is too short to draw",
               "R5.4 the short stroke says so",
               rig.lastNotice);
    }

    // --- the stroke that is cancelled ------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        drag (rig, { 200.0f, 120.0f }, p0);
        rig.ed.setLayer (1);   // switching the layer cancels the gesture
        up (rig, { 200.0f, 120.0f }, p0);

        check (rig.page.arcs.empty(),
               "R5.4 switching the layer cancels the stroke",
               juce::String (rig.page.arcs.size()));
    }

    // --- the sine stroke -------------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::SineStroke);
        rig.ed.setSnap (false);
        rig.ed.setFineDrawing (true);   // every sampled point, so the sine has room

        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        for (int i = 1; i <= 10; ++i)
            drag (rig, { 100.0f + 30.0f * (float) i,
                         100.0f + 60.0f * (float) std::sin (juce::MathConstants<double>::pi * i / 10.0) }, p0);
        up (rig, { 400.0f, 100.0f }, p0);

        check (rig.page.arcs.size() == 1,
               "R5.4 the sine stroke makes one arc",
               juce::String (rig.page.arcs.size()));

        if (rig.page.arcs.size() == 1 && rig.page.arcs.front().points.size() >= 4)
        {
            const auto& pts = rig.page.arcs.front().points;
            const double l0 = std::log2 (juce::jmax (1.0, pts.front().freq));
            double maxDev = 0.0;
            for (const auto& p : pts)
                maxDev = juce::jmax (maxDev, std::abs (std::log2 (juce::jmax (1.0, p.freq)) - l0));
            check (maxDev > 0.05,
                   "R5.4 the sine stroke wobbles around the line",
                   juce::String (maxDev, 3) + " octaves");
            check (maxDev < 0.3,
                   "R5.4 the sine stroke wobbles gently",
                   juce::String (maxDev, 3) + " octaves");
        }
        else
        {
            check (false, "R5.4 the sine stroke wobbles around the line",
                   "the arc has too few points");
            check (false, "R5.4 the sine stroke wobbles gently",
                   "the arc has too few points");
        }
    }

    // --- the line --------------------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Line);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (300.0f, 140.0f);
        down (rig, p0);
        check (rig.page.arcs.empty(),
               "R5.4 the line stays temporary until the release",
               juce::String (rig.page.arcs.size()));
        drag (rig, p1, p0);
        up (rig, p1, p0);

        check (rig.page.arcs.size() == 1,
               "R5.4 a line drag makes one arc",
               juce::String (rig.page.arcs.size()));

        if (rig.page.arcs.size() == 1)
        {
            const auto& pts = rig.page.arcs.front().points;
            const double t0 = rig.ed.xToTime (p0.x);
            const double t1 = rig.ed.xToTime (p1.x);
            check (pts.size() >= 2
                   && std::abs (pts.front().t - t0) < 0.02
                   && std::abs (pts.back().t - t1) < 0.02,
                   "R5.4 the line's arc runs from the drag's start to its end",
                   pts.empty() ? juce::String ("no points")
                               : juce::String (pts.front().t, 3) + " .. "
                                   + juce::String (pts.back().t, 3));

            check (pts.size() > 2,
                   "R5.4 the line's arc is subdivided along the drag",
                   juce::String (pts.size()));
        }

        check (rig.editsStarted == 1 && rig.editsEnded == 1 && rig.lastLabel == "Draw a line",
               "R5.4 a line is one undo transaction, with its own label",
               rig.lastLabel + " (" + juce::String (rig.editsStarted) + "/"
                   + juce::String (rig.editsEnded) + ")");
    }

    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Line);

        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        up (rig, { 101.0f, 100.0f }, p0);   // a pixel: shorter than the page's time step

        check (rig.page.arcs.empty(),
               "R5.4 a line that is too short makes no arc",
               juce::String (rig.page.arcs.size()));

        check (rig.lastNotice == "The line is too short to draw - drag a little farther",
               "R5.4 the short line says so",
               rig.lastNotice);
    }

    // --- the erase ---------------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);

        rig.ed.setTool (ScoreEditor::Erase);
        const auto p = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));
        down (rig, p);
        up (rig, p, p);

        check (rig.page.arcs.empty(),
               "R5.4 the erase tool deletes the arc under the cursor",
               juce::String (rig.page.arcs.size()));

        check (rig.editsStarted == 1 && rig.lastLabel == "Delete",
               "R5.4 the erase is one undo transaction, labelled",
               rig.lastLabel);
    }

    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Erase);
        const auto p = juce::Point<float> (rig.ed.timeToX (6.0), rig.ed.freqToY (200.0));
        down (rig, p);
        up (rig, p, p);

        check (rig.page.arcs.empty() && rig.editsStarted == 0,
               "R5.4 the erase tool on empty space deletes nothing",
               juce::String (rig.editsStarted));
    }

    // --- the selection -------------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        rig.addPointArc (2, 4.0, 800.0);

        rig.ed.setTool (ScoreEditor::Select);
        const auto p1 = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));
        const auto p2 = juce::Point<float> (rig.ed.timeToX (4.0), rig.ed.freqToY (800.0));

        down (rig, p1);
        up (rig, p1, p1);
        check (rig.ed.getSelectedArc() == 1,
               "R5.4 a click with the Select tool selects the arc under the cursor",
               juce::String (rig.ed.getSelectedArc()));

        down (rig, p1, juce::ModifierKeys::ctrlModifier);
        up (rig, p1, p1, juce::ModifierKeys::ctrlModifier);
        check (rig.ed.getSelectedArcs().empty(),
               "R5.4 a ctrl-click unselects the arc",
               juce::String (rig.ed.getSelectedArcs().size()));

        down (rig, p1);
        up (rig, p1, p1);
        down (rig, p2, juce::ModifierKeys::shiftModifier);
        up (rig, p2, p2, juce::ModifierKeys::shiftModifier);
        check (rig.ed.getSelectedArcs().size() == 2,
               "R5.4 a shift-click adds another arc to the selection",
               juce::String (rig.ed.getSelectedArcs().size()));

        down (rig, juce::Point<float> (600.0f, 330.0f));
        up (rig, juce::Point<float> (600.0f, 330.0f), juce::Point<float> (600.0f, 330.0f));
        check (rig.ed.getSelectedArcs().empty(),
               "R5.4 a click on empty space clears the selection",
               juce::String (rig.ed.getSelectedArcs().size()));
    }

    // --- the marquee ----------------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        rig.addPointArc (2, 4.0, 800.0);

        rig.ed.setTool (ScoreEditor::Select);
        const juce::Point<float> from (100.0f, 100.0f);
        const juce::Point<float> to (500.0f, 300.0f);
        down (rig, from);
        drag (rig, to, from);
        up (rig, to, from);

        check (rig.ed.getSelectedArcs().size() == 2,
               "R5.4 the marquee takes the arcs inside it",
               juce::String (rig.ed.getSelectedArcs().size()));
    }

    // --- the move ----------------------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);

        rig.ed.setTool (ScoreEditor::Select);
        const auto p = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));
        down (rig, p);
        up (rig, p, p);

        down (rig, p);
        drag (rig, { p.x + 100.0f, p.y }, p);
        up (rig, { p.x + 100.0f, p.y }, p);

        const double movedT = rig.page.arcs.empty() ? -1.0 : rig.page.arcs.front().points.front().t;
        const double expected = rig.ed.xToTime (p.x + 100.0f);
        check (std::abs (movedT - expected) < 0.02,
               "R5.4 a selected arc follows the pointer",
               juce::String (movedT, 3) + " vs " + juce::String (expected, 3));

        check (rig.lastLabel == "Move the arc",
               "R5.4 the move is one undo transaction, labelled",
               rig.lastLabel);
    }

    // --- the zone --------------------------------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Zone);

        const juce::Point<float> from (200.0f, 150.0f);
        const juce::Point<float> to (400.0f, 250.0f);
        down (rig, from);
        drag (rig, to, from);
        up (rig, to, from);

        check (rig.page.zones.size() == 1,
               "R5.4 a drag with the Zone tool draws a zone",
               juce::String (rig.page.zones.size()));

        if (rig.page.zones.size() == 1)
        {
            const auto& z = rig.page.zones.front();
            const double t0 = rig.ed.xToTime (from.x);
            const double t1 = rig.ed.xToTime (to.x);
            check (std::abs (z.t0 - t0) < 0.02 && std::abs (z.t1 - t1) < 0.02,
                   "R5.4 the zone's times follow the drag",
                   juce::String (z.t0, 3) + " .. " + juce::String (z.t1, 3));
            const double f0 = juce::jmin (rig.ed.yToFreq (from.y), rig.ed.yToFreq (to.y));
            const double f1 = juce::jmax (rig.ed.yToFreq (from.y), rig.ed.yToFreq (to.y));
            check (std::abs (z.f0 - f0) < 1.0 && std::abs (z.f1 - f1) < 1.0,
                   "R5.4 the zone's pitches follow the drag",
                   juce::String (z.f0, 1) + " .. " + juce::String (z.f1, 1));
        }

        check (rig.lastLabel == "Add a sieve zone",
               "R5.4 the zone is one undo transaction, labelled",
               rig.lastLabel);
    }

    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);

        rig.ed.setTool (ScoreEditor::Zone);
        const auto p = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));
        down (rig, p);
        up (rig, p, p);

        check (rig.ed.getSelectedZone() == -1,
               "R5.4 the Zone tool on an arc takes no zone",
               juce::String (rig.ed.getSelectedZone()));
    }

    // --- the scrub ----------------------------------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        // the centre band: the scrub target, whatever the tool is
        const auto band = rig.ed.timeStripHitRectForTest();
        const juce::Point<float> p (300.0f, band.getCentreY());
        down (rig, p);
        up (rig, p, p);

        check (rig.page.arcs.empty(),
               "R5.4 the scrub band draws nothing",
               juce::String (rig.page.arcs.size()));

        check (std::abs (rig.playhead - rig.ed.xToTime (p.x)) < 0.02,
               "R5.4 the scrub band moves the playhead",
               juce::String (rig.playhead, 3) + " vs " + juce::String (rig.ed.xToTime (p.x), 3));
    }
}
