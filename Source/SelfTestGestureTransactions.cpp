#include <JuceHeader.h>
#include "SelfTest.h"
#include "ScoreEditor.h"
#include "CommandDispatcher.h"

#include <algorithm>
#include <vector>

//==============================================================================
// R5.5 (the review's R047, section 25): the score canvas's gesture
// transactions - the contract.
//
// A gesture on the score canvas changes the document in one step, not in
// hundreds: the pointer's path is temporary geometry in the editor, and the
// release produces one command - one labelled undo step. The review asks for
// that lifecycle to be explicit:
//
//   "A stroke should have an explicit lifecycle: during drawing, temporary
//    geometry should normally remain in the editor. On release, produce one
//    validated editing command." (section 25)
//
// and for the live audition to be a bounded preview rather than hundreds of
// document changes:
//
//   "For live audition, publish a bounded transient preview rather than
//    committing hundreds of intermediate document changes." (section 25)
//
// This suite is the gesture's transaction contract, and it comes before the
// move, as every phase in this wave does. It pins what the canvas does to the
// document's transaction today:
//
// - the stroke: the transaction opens at the press, with its label, and
//   closes at the release; the temporary geometry does not touch the model;
//   the release is one undo step, and the document changed once;
// - the line and the move: the same, each with its own label;
// - the move's drag: the model moves while the pointer moves (the audio
//   follows the gesture), but the document has not changed yet - the
//   revision waits for the release;
// - the refused stroke (too short): the transaction opens and closes, and
//   the document is not changed - a gesture that changed nothing is not an
//   undo step;
// - the erase and the zone: one command each, with their own labels;
// - the cancelled gesture: the model is left alone.
//
// The suite drives the editor with synthesised mouse events, the way the
// app's own UI tests do it, and it watches the document's transaction
// (Source/CommandDispatcher.h) - the same one the app uses.

namespace
{
    struct Rig
    {
        std::vector<upic::Waveform> waves;
        upic::Page page;
        double playhead = 0.0;
        juce::CriticalSection lock;
        ScoreEditor ed;

        // The document's transaction - the same class the app uses. The
        // editor's hooks are wired to it the way MainComponent wires them.
        upic::CommandDispatcher dispatcher;

        // What the transaction saw.
        int steps = 0;              // the undo steps that were committed
        juce::String lastStepLabel; // the label of the newest step
        int epochMoves = 0;         // "the model moved" - the audio follows
        int revisionMoves = 0;      // "the document changed once"

        Rig()
        {
            waves.emplace_back (7, "Red", juce::Colours::red);
            page.durationSec = 8.0;

            ed.setModelLock (&lock);
            // The app's wiring: the editor says when the model changed (the
            // change callback that bind() takes), and the light hook says
            // so while a drag edits the model in place.
            ed.bind (&page, &waves, &playhead, [this]{ dispatcher.reportChange (true); });
            ed.setSize (800, 400);

            dispatcher.install (
                [this]
                {
                    auto m = std::make_unique<upic::PieceModel>();
                    m->pages.push_back (page);
                    m->currentPage = 0;
                    return m;
                },
                [this] (upic::PieceModel&&, const juce::String& label)
                {
                    ++steps;
                    lastStepLabel = label;
                },
                [this] (bool) { ++epochMoves; },
                [this] { ++revisionMoves; });

            ed.onModelEdited = [this]{ dispatcher.reportChange (true); };
            ed.onEditStart = [this] (const juce::String& what) { dispatcher.beginCommand (what); };
            ed.onEditEnd   = [this] { dispatcher.endCommand(); };
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
            if (id >= page.nextArcId) page.nextArcId = id + 1;
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

void upic::runGestureTransactionChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the freehand stroke ------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (300.0f, 140.0f);

        down (rig, p0);

        check (rig.dispatcher.isCommandOpen(),
               "R5.5 the stroke opens a transaction",
               rig.dispatcher.commandLabel());
        check (rig.dispatcher.commandLabel() == "Draw",
               "R5.5 the stroke's transaction is labelled",
               rig.dispatcher.commandLabel());
        check (rig.epochMoves == 0 && rig.revisionMoves == 0,
               "R5.5 the stroke's temporary geometry does not touch the model",
               juce::String (rig.epochMoves) + " / " + juce::String (rig.revisionMoves));

        drag (rig, p1, p0);

        check (rig.epochMoves == 0 && rig.revisionMoves == 0,
               "R5.5 the pointer's path does not change the document",
               juce::String (rig.epochMoves) + " / " + juce::String (rig.revisionMoves));
        check (rig.dispatcher.isCommandOpen(),
               "R5.5 the transaction stays open while the pointer moves");

        up (rig, p1, p0);

        check (! rig.dispatcher.isCommandOpen(),
               "R5.5 the release closes the transaction");
        check (rig.steps == 1 && rig.lastStepLabel == "Draw",
               "R5.5 the stroke is one undo step, labelled",
               rig.lastStepLabel + " (" + juce::String (rig.steps) + ")");
        check (rig.epochMoves == 1,
               "R5.5 the stroke moves the model once (the commit)",
               juce::String (rig.epochMoves));
        check (rig.revisionMoves == 1,
               "R5.5 the stroke changes the document once",
               juce::String (rig.revisionMoves));
    }

    // --- the straight line --------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Line);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (300.0f, 140.0f);

        down (rig, p0);
        check (rig.dispatcher.commandLabel() == "Draw a line",
               "R5.5 the line's transaction is labelled",
               rig.dispatcher.commandLabel());

        drag (rig, p1, p0);
        up (rig, p1, p0);

        check (rig.steps == 1 && rig.lastStepLabel == "Draw a line",
               "R5.5 the line is one undo step, labelled",
               rig.lastStepLabel + " (" + juce::String (rig.steps) + ")");
        check (! rig.dispatcher.isCommandOpen(),
               "R5.5 the line's release closes the transaction");
    }

    // --- the move -----------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        rig.ed.setTool (ScoreEditor::Select);
        // The arc is taken through the editor's own door: the selection is
        // not a change of the document, so it must not colour what the
        // gesture's transaction does.
        rig.ed.setSelectedArc (1);

        const auto p = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));

        down (rig, p);       // the move starts
        check (rig.dispatcher.isCommandOpen()
               && rig.dispatcher.commandLabel() == "Move the arc",
               "R5.5 the move opens a transaction, labelled",
               rig.dispatcher.commandLabel());

        drag (rig, { p.x + 100.0f, p.y }, p);

        check (rig.epochMoves > 0,
               "R5.5 the move is heard while the pointer moves",
               juce::String (rig.epochMoves));
        check (rig.revisionMoves == 0,
               "R5.5 the move's drag does not change the document yet",
               juce::String (rig.revisionMoves));

        up (rig, { p.x + 100.0f, p.y }, p);

        check (rig.steps == 1 && rig.lastStepLabel == "Move the arc",
               "R5.5 the move is one undo step, labelled",
               rig.lastStepLabel + " (" + juce::String (rig.steps) + ")");
        check (rig.revisionMoves == 1,
               "R5.5 the move changes the document once",
               juce::String (rig.revisionMoves));
    }

    // --- the stroke that is refused -----------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        down (rig, p0);
        up (rig, p0, p0);   // too short to draw

        check (! rig.dispatcher.isCommandOpen(),
               "R5.5 the refused stroke closes its transaction");
        check (rig.steps == 0,
               "R5.5 the refused stroke is not an undo step",
               juce::String (rig.steps));
        check (rig.revisionMoves == 0,
               "R5.5 the refused stroke does not change the document",
               juce::String (rig.revisionMoves));
    }

    // --- the erase ------------------------------------------------------------
    {
        Rig rig;
        rig.addPointArc (1, 2.0, 1000.0);
        rig.ed.setTool (ScoreEditor::Erase);

        const auto p = juce::Point<float> (rig.ed.timeToX (2.0), rig.ed.freqToY (1000.0));
        down (rig, p);
        up (rig, p, p);

        check (rig.steps == 1 && rig.lastStepLabel == "Delete",
               "R5.5 the erase is one undo step, labelled",
               rig.lastStepLabel + " (" + juce::String (rig.steps) + ")");
        check (rig.revisionMoves == 1,
               "R5.5 the erase changes the document once",
               juce::String (rig.revisionMoves));
    }

    // --- the zone --------------------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Zone);

        const juce::Point<float> from (200.0f, 150.0f);
        const juce::Point<float> to (400.0f, 250.0f);
        down (rig, from);
        drag (rig, to, from);
        up (rig, to, from);

        check (rig.steps == 1 && rig.lastStepLabel == "Add a sieve zone",
               "R5.5 the zone is one undo step, labelled",
               rig.lastStepLabel + " (" + juce::String (rig.steps) + ")");
        check (rig.revisionMoves == 1,
               "R5.5 the zone changes the document once",
               juce::String (rig.revisionMoves));
    }

    // --- the cancelled gesture ---------------------------------------------------
    {
        Rig rig;
        rig.ed.setTool (ScoreEditor::Draw);

        const juce::Point<float> p0 (100.0f, 100.0f);
        const juce::Point<float> p1 (200.0f, 120.0f);

        down (rig, p0);
        drag (rig, p1, p0);
        rig.ed.setLayer (1);   // the layer switch cancels the gesture
        up (rig, p1, p0);

        check (rig.page.arcs.empty(),
               "R5.5 a cancelled stroke makes no arc",
               juce::String (rig.page.arcs.size()));
        check (rig.steps == 0,
               "R5.5 a cancelled stroke is not an undo step",
               juce::String (rig.steps));
        check (rig.epochMoves == 0 && rig.revisionMoves == 0,
               "R5.5 a cancelled stroke leaves the model alone",
               juce::String (rig.epochMoves) + " / " + juce::String (rig.revisionMoves));
    }

    // --- the release path's cost ---------------------------------------------------
    //
    // Section 25 asks for the mouse-release behaviour to be profiled: "large
    // curves, empty pages, maximum-sized pages, simultaneous playback, active
    // preview, undo history growth and snapshot compilation". The numbers
    // below are the first three - they are notes, not checks: the baseline a
    // later phase compares against.
    {
        struct PageCase
        {
            const char* name;
            int arcs;
            int points;
        };

        const PageCase cases[] = {
            { "an empty page", 0, 0 },
            { "a page with a large curve", 1, upic::limits::kMaxPointsPerArc },
            { "a page at the arc limit", upic::limits::kMaxArcsPerPage - 1, 1 }
        };

        for (const auto& c : cases)
        {
            Rig rig;
            rig.page.arcs.clear();
            rig.page.nextArcId = 1;
            for (int i = 0; i < c.arcs; ++i)
            {
                upic::Arc a;
                a.id = rig.page.nextArcId++;
                a.mode = 0;
                a.waveformId = 7;
                for (int p = 0; p < c.points; ++p)
                    a.points.push_back ({ rig.page.durationSec * (double) p / (double) juce::jmax (1, c.points), 440.0 });
                rig.page.arcs.push_back (std::move (a));
            }

            // three runs, and the median of them
            double pressMs = 0.0, dragMs = 0.0, releaseMs = 0.0;
            for (int run = 0; run < 3; ++run)
            {
                const juce::Point<float> p0 (100.0f, 100.0f);
                const juce::Point<float> p1 (300.0f, 140.0f);

                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                down (rig, p0);
                const auto t1 = juce::Time::getMillisecondCounterHiRes();
                drag (rig, p1, p0);
                const auto t2 = juce::Time::getMillisecondCounterHiRes();
                up (rig, p1, p0);
                const auto t3 = juce::Time::getMillisecondCounterHiRes();

                pressMs   += (t1 - t0);
                dragMs    += (t2 - t1);
                releaseMs += (t3 - t2);
            }

            t.note ("R5.5 the stroke on " + juce::String (c.name) + ": the press "
                    + juce::String (pressMs / 3.0, 3) + " ms, the drag "
                    + juce::String (dragMs / 3.0, 3) + " ms, the release "
                    + juce::String (releaseMs / 3.0, 3) + " ms");
        }
    }
}
