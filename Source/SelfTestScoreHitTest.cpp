#include <JuceHeader.h>
#include "SelfTest.h"
#include "ScoreEditor.h"

#include <cmath>

//==============================================================================
// R5.3 (the review's R045): the score canvas's hit tester - the contract.
//
// What is under the cursor? The score canvas answers that question three
// times over: which arcs are near the point (the nearest one wins, the rest
// are the shift-click's cycle), which zone is under the point (the topmost
// one, and its border is grabbable), and which of the zone's edges or
// corners is under the point.
//
// The review asks for the hit testing to be a thing of its own (R045,
// "Extract ScoreHitTester") and says what it is for:
//
//   "`ScoreHitTester` should identify arcs, points or handles under a given
//    coordinate." (section 24)
//
// This suite is the hit tester's contract, and it comes before the move, as
// every phase in this wave does. It pins what the canvas answers today:
//
// - the arcs near a point, nearest first, and the nearest arc alone;
// - the grab band: 12 pixels for the mouse (20 for a touch), and the band
//   can be given;
// - the line of an arc is hit along its length, not only at its points;
// - the layer rule: an arc of another layer is not under the cursor;
// - the zone under a point, the topmost one first, and the border's slack;
// - the zone's handles: the four edges, the four corners, and none.
//
// The suite reaches the hit testing through the editor's own doors
// (arcsNearForTest(), pickArcForTest(), pickZoneForTest(),
// zoneHandleAtForTest()) and, for the last group, through the editor's real
// mouse handling - a click with the Select tool selects the arc under the
// cursor, a click with the Zone tool selects the zone under it.
//
// The probes use the editor's own mapping (timeToX(), freqToY()) to place
// the cursor, so they do not hard-code where a time or a pitch is.

namespace
{
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
            page.durationSec = 8.0;

            ed.setModelLock (&lock);
            ed.bind (&page, &waves, &playhead, nullptr);
            ed.setSize (800, 400);
        }

        // An arc with one point, in the osc layer.
        void addPointArc (int id, double t, double freq, int mode = 0)
        {
            upic::Arc a;
            a.id = id;
            a.mode = mode;
            a.waveformId = 7;
            a.points = { { t, freq } };
            page.arcs.push_back (a);
        }

        // Where a time and a pitch are on the screen.
        juce::Point<float> at (double t, double freq) const
        {
            return { ed.timeToX (t), ed.freqToY (freq) };
        }
    };

    // A click on the canvas, with the editor's own mouse handling.
    void click (ScoreEditor& ed, juce::Point<float> pos,
                juce::ModifierKeys mods = juce::ModifierKeys())
    {
        const auto now = juce::Time::getCurrentTime();
        const auto src = juce::Desktop::getInstance().getMainMouseSource();
        juce::MouseEvent down (src, pos, mods, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now, pos, now, 1, false);
        ed.mouseDown (down);
        juce::MouseEvent up (src, pos, mods, 1.0f,
                             0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now, pos, now, 1, false);
        ed.mouseUp (up);
    }

    bool contains (const std::vector<int>& v, int id)
    {
        return std::find (v.begin(), v.end(), id) != v.end();
    }
}

void upic::runScoreHitTestChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the arcs near a point ----------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        rig.addPointArc (2, 2.0, 950.0);   // a few pixels below the first

        const auto p1 = rig.at (2.0, 1000.0);
        const auto p2 = rig.at (2.0, 950.0);

        const auto near1 = rig.ed.arcsNearForTest (p1);
        check (near1.size() == 2 && near1.front() == 1,
               "R5.3 the arcs near a point are nearest first",
               juce::String (near1.size()) + " / " + juce::String (near1.front()));

        const auto near2 = rig.ed.arcsNearForTest (p2);
        check (near2.size() == 2 && near2.front() == 2,
               "R5.3 the nearest arc is the one under the cursor",
               juce::String (near2.size()) + " / " + juce::String (near2.front()));

        check (contains (near1, 2),
               "R5.3 the other arc nearby is in the list too");

        check (rig.ed.pickArcForTest (p1) == 1,
               "R5.3 the picked arc is the nearest one",
               juce::String (rig.ed.pickArcForTest (p1)));

        check (rig.ed.pickArcForTest (p2) == 2,
               "R5.3 the picked arc follows the cursor",
               juce::String (rig.ed.pickArcForTest (p2)));

        // far away: nothing
        const auto far = rig.at (6.0, 200.0);
        check (rig.ed.arcsNearForTest (far).empty(),
               "R5.3 a point far from every arc finds nothing",
               juce::String (rig.ed.arcsNearForTest (far).size()));

        check (rig.ed.pickArcForTest (far) == -1,
               "R5.3 a point far from every arc picks nothing",
               juce::String (rig.ed.pickArcForTest (far)));
    }

    // --- the grab band -------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        const auto p = rig.at (2.0, 1000.0);

        // 8 px below the point: inside the 12 px band
        const auto near = rig.ed.arcsNearForTest ({ p.x, p.y + 8.0f });
        check (near.size() == 1 && near.front() == 1,
               "R5.3 the grab band is 12 pixels wide",
               juce::String (near.size()));

        // 30 px below the point: outside the band
        const auto outside = rig.ed.arcsNearForTest ({ p.x, p.y + 30.0f });
        check (outside.empty(),
               "R5.3 a point outside the band finds nothing",
               juce::String (outside.size()));

        // the band can be given
        const auto wider = rig.ed.arcsNearForTest ({ p.x, p.y + 30.0f }, 40.0f);
        check (wider.size() == 1 && wider.front() == 1,
               "R5.3 the grab band can be given",
               juce::String (wider.size()));
    }

    // --- the line of an arc --------------------------------------------------
    {
        Rig rig;
        upic::Arc a;
        a.id = 1;
        a.mode = 0;
        a.waveformId = 7;
        a.points = { { 1.0, 1000.0 }, { 3.0, 1000.0 } };
        rig.page.arcs.push_back (a);

        // the middle of the line: far from both points, right on the line
        const auto mid = rig.at (2.0, 1000.0);
        const auto near = rig.ed.arcsNearForTest (mid);
        check (near.size() == 1 && near.front() == 1,
               "R5.3 an arc is hit along its line, not only at its points",
               juce::String (near.size()));

        // past the end of the line, outside the band
        const auto past = rig.at (3.0 + 1.0, 1000.0);
        check (rig.ed.arcsNearForTest (past).empty(),
               "R5.3 a point past the end of the line misses the arc",
               juce::String (rig.ed.arcsNearForTest (past).size()));
    }

    // --- the layer rule ------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0, 0);   // osc
        rig.addPointArc (2, 2.0, 1000.0, 1);   // sample
        const auto p = rig.at (2.0, 1000.0);

        rig.ed.setLayer (0);
        check (rig.ed.pickArcForTest (p) == 1,
               "R5.3 the osc arc is picked in the osc layer",
               juce::String (rig.ed.pickArcForTest (p)));

        rig.ed.setLayer (1);
        check (rig.ed.pickArcForTest (p) == 2,
               "R5.3 the sample arc is picked in the sample layer",
               juce::String (rig.ed.pickArcForTest (p)));

        rig.ed.setLayer (2);
        check (rig.ed.pickArcForTest (p) == -1,
               "R5.3 an arc of another layer is not under the cursor",
               juce::String (rig.ed.pickArcForTest (p)));
    }

    // --- the zone under a point ----------------------------------------------
    {
        Rig rig;
        const int zid = rig.ed.addZone (3.0, 5.0, 400.0, 1200.0);
        const auto inside = rig.at (4.0, 800.0);

        check (rig.ed.pickZoneForTest (inside) == zid,
               "R5.3 the zone under the cursor is picked",
               juce::String (rig.ed.pickZoneForTest (inside)));

        const auto outside = rig.at (1.0, 800.0);
        check (rig.ed.pickZoneForTest (outside) == -1,
               "R5.3 a point outside every zone picks none",
               juce::String (rig.ed.pickZoneForTest (outside)));

        // the border's slack: 2 px outside the rectangle, still picked
        const auto rect = rig.ed.zoneRectForTest (zid);
        const auto onBorder = juce::Point<float> (rect.getRight() + 2.0f, rect.getCentreY());
        check (rig.ed.pickZoneForTest (onBorder) == zid,
               "R5.3 the zone's border is grabbable",
               juce::String (rig.ed.pickZoneForTest (onBorder)));

        // 8 px outside the rectangle: not picked
        const auto off = juce::Point<float> (rect.getRight() + 8.0f, rect.getCentreY());
        check (rig.ed.pickZoneForTest (off) == -1,
               "R5.3 a point outside the zone's border picks none",
               juce::String (rig.ed.pickZoneForTest (off)));
    }

    // --- the topmost zone ----------------------------------------------------
    {
        Rig rig;
        const int first  = rig.ed.addZone (3.0, 5.0, 400.0, 1200.0);
        const int second = rig.ed.addZone (3.0, 5.0, 400.0, 1200.0);
        const auto inside = rig.at (4.0, 800.0);

        check (rig.ed.pickZoneForTest (inside) == second,
               "R5.3 the topmost zone under the cursor wins",
               juce::String (rig.ed.pickZoneForTest (inside)));

        rig.ed.deleteZone (second);
        check (rig.ed.pickZoneForTest (inside) == first,
               "R5.3 the zone below it is picked once the top one is gone",
               juce::String (rig.ed.pickZoneForTest (inside)));
    }

    // --- the zone's handles --------------------------------------------------
    {
        Rig rig;
        const int zid = rig.ed.addZone (3.0, 5.0, 400.0, 1200.0);
        const auto rect = rig.ed.zoneRectForTest (zid);

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getCentreY() }) == 1,
               "R5.3 the left edge is handle 1",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getCentreY() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getCentreY() }) == 2,
               "R5.3 the right edge is handle 2",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getCentreY() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getCentreX(), rect.getY() }) == 3,
               "R5.3 the top edge is handle 3",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getCentreX(), rect.getY() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getCentreX(), rect.getBottom() }) == 4,
               "R5.3 the bottom edge is handle 4",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getCentreX(), rect.getBottom() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getY() }) == 5,
               "R5.3 the top-left corner is handle 5",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getY() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getY() }) == 6,
               "R5.3 the top-right corner is handle 6",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getY() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getBottom() }) == 7,
               "R5.3 the bottom-left corner is handle 7",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getX(), rect.getBottom() })));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getBottom() }) == 8,
               "R5.3 the bottom-right corner is handle 8",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getRight(), rect.getBottom() })));

        check (rig.ed.zoneHandleAtForTest (zid, rect.getCentre()) == 0,
               "R5.3 the middle of a zone has no handle",
               juce::String (rig.ed.zoneHandleAtForTest (zid, rect.getCentre())));

        check (rig.ed.zoneHandleAtForTest (zid, { rect.getX() - 40.0f, rect.getCentreY() }) == 0,
               "R5.3 a point far outside the zone has no handle",
               juce::String (rig.ed.zoneHandleAtForTest (zid, { rect.getX() - 40.0f, rect.getCentreY() })));
    }

    // --- the editor's own mouse handling -------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        const auto p = rig.at (2.0, 1000.0);

        rig.ed.setTool (ScoreEditor::Select);
        click (rig.ed, p);
        check (rig.ed.getSelectedArc() == 1,
               "R5.3 a click with the Select tool selects the arc under the cursor",
               juce::String (rig.ed.getSelectedArc()));

        click (rig.ed, rig.at (6.0, 200.0));
        check (rig.ed.getSelectedArcs().empty(),
               "R5.3 a click on empty space selects nothing",
               juce::String (rig.ed.getSelectedArcs().size()));
    }

    {
        Rig rig;
        const int zid = rig.ed.addZone (3.0, 5.0, 400.0, 1200.0);

        rig.ed.setTool (ScoreEditor::Zone);
        click (rig.ed, rig.at (4.0, 800.0));
        check (rig.ed.getSelectedZone() == zid,
               "R5.3 a click with the Zone tool selects the zone under the cursor",
               juce::String (rig.ed.getSelectedZone()));
    }
}
