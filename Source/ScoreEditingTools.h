#pragma once

//==============================================================================
// R5.4 (the review's R046, section 24): the score canvas's editing tools.
//
// The tools are the canvas's controllers. Each one knows what a press, a
// drag and a release mean in its own drawing mode, so the canvas's mouse
// handlers do not have to: they hand the pointer to the active tool and the
// tool acts on the canvas.
//
// The bodies are the editor's, body for body; the suite
// (Source/SelfTestEditingTools.cpp) pins them.
//
// The tools are the canvas's own controllers, so they act on the editor's
// gesture state and call the editor's commands - which is why the editor
// declares them friends. The canvas keeps the state (the render borrows it)
// and the commands (the model); the tools keep the decisions.
//
// The canvas's own targets - the playhead's handle, the centre band's scrub
// target, the pinch - are not tools: they belong to the canvas, whatever
// tool is active. The right button is a tool's business (the arc's menu,
// except with the Zone tool, which has its own).

#include <JuceHeader.h>

#include <algorithm>
#include <vector>

#include "ScoreEditingTool.h"
#include "ScoreEditor.h"

namespace upic
{
    namespace score_tools
    {
        //================ the freehand stroke (Draw and SineStroke) ==========
        // The stroke in progress: the pointer's path, sampled. The Draw and
        // the Sine tools share the gesture; they differ in the commit (the
        // sine's curve fit), which is the canvas's command.
        class FreehandTool : public EditingTool
        {
        public:
            explicit FreehandTool (ScoreEditor& ed) : editor (ed) {}

            void begin (const PointerInput& in) override
            {
                if (in.mods.isRightButtonDown()) { editor.beginArcMenu (in.position); return; }

                editor.drawing = true;
                editor.strokePts.clear();
                editor.strokePts.push_back ({ in.position, in.pressure });
                // v1.33.4 (L15, part 2): the undo transaction
                // R5.5: the gesture's transaction - the canvas's own door.
                editor.beginGesture ("Draw");
                if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (true);
            }

            void update (const PointerInput& in) override
            {
                if (! editor.drawing) return;

                // resample: only push if moved 4px in time
                if (editor.strokePts.empty() || std::abs (in.position.x - editor.strokePts.back().pos.x) > 3.0f
                                             || std::abs (in.position.y - editor.strokePts.back().pos.y) > 3.0f)
                {
                    auto r = editor.plotArea();
                    juce::Point<float> cl { juce::jlimit (r.getX(), r.getRight(), in.position.x),
                                            juce::jlimit (r.getY(), r.getBottom(), in.position.y) };
                    editor.strokePts.push_back ({ cl, in.pressure });
                    editor.repaint();
                }
            }

            void end (const PointerInput& in) override
            {
                if (! editor.drawing) return;

                editor.drawing = false;
                if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (false);
                editor.strokePts.push_back ({ in.position, in.pressure });
                editor.commitStroke();
                editor.strokePts.clear();
                // v1.33.4 (L15, part 2): the transaction ends after the stroke
                // is committed, so the app can see that something really changed.
                // R5.5: the gesture ends in a command - one undo step.
                editor.commitGesture();
                editor.repaint();
            }

            void cancel() override
            {
                editor.drawing = false;
                editor.strokePts.clear();
                // R5.5: the gesture produced nothing - the transaction is
                // closed and dropped.
                editor.cancelGesture();
            }

        private:
            ScoreEditor& editor;
        };

        //================ the straight line =================================
        // The line in progress: the anchor and the pointer (the rubber band
        // between them).
        class LineTool : public EditingTool
        {
        public:
            explicit LineTool (ScoreEditor& ed) : editor (ed) {}

            void begin (const PointerInput& in) override
            {
                if (in.mods.isRightButtonDown()) { editor.beginArcMenu (in.position); return; }

                editor.lineAnchor = in.position;
                // v1.33.4 (L15, part 2): the undo transaction
                // R5.5: the gesture's transaction - the canvas's own door.
                editor.beginGesture ("Draw a line");
                if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (true);
                editor.lastMouse = in.position;
                editor.repaint();
            }

            void update (const PointerInput&) override
            {
                // the rubber band: the anchor to the pointer (the canvas
                // keeps the pointer's position; the band is drawn from it)
                editor.repaint();
            }

            void end (const PointerInput& in) override
            {
                if (! editor.lineAnchor) return;

                if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (false);
                editor.commitLine (*editor.lineAnchor, in.position, in.pressure);
                editor.lineAnchor.reset();
                // v1.33.4 (L15, part 2): the transaction ends after the line is
                // committed, so the app can see that something really changed.
                // R5.5: the gesture ends in a command - one undo step.
                editor.commitGesture();
            }

            void cancel() override
            {
                editor.lineAnchor.reset();
                // R5.5: the gesture produced nothing - the transaction is
                // closed and dropped.
                editor.cancelGesture();
            }

        private:
            ScoreEditor& editor;
        };

        //================ the selection and the move =========================
        // A click takes the arc under the cursor (a ctrl-click toggles it, a
        // shift-click adds it), a click on empty space starts the marquee,
        // and a press on a selected arc starts the move.
        class SelectTool : public EditingTool
        {
        public:
            explicit SelectTool (ScoreEditor& ed) : editor (ed) {}

            void begin (const PointerInput& in) override
            {
                if (in.mods.isRightButtonDown()) { editor.beginArcMenu (in.position); return; }

                const bool ctrl  = in.mods.isCtrlDown() || in.mods.isCommandDown();
                const bool shift = in.mods.isShiftDown();
                const int hit = editor.pickArc (in.position);

                if (hit >= 0)
                {
                    if (ctrl)
                    {
                        if (editor.isSel (hit))
                            editor.selArcs.erase (std::remove (editor.selArcs.begin(), editor.selArcs.end(), hit), editor.selArcs.end());
                        else editor.selArcs.push_back (hit);
                        if (editor.onSelect) editor.onSelect (editor.selArcs.empty() ? -1 : editor.selArcs.front());
                    }
                    else if (shift)
                    {
                        if (! editor.isSel (hit)) editor.selArcs.push_back (hit);
                        if (editor.onSelect) editor.onSelect (hit);
                    }
                    else
                    {
                        if (! editor.isSel (hit)) { editor.selArcs.clear(); editor.selArcs.push_back (hit); if (editor.onSelect) editor.onSelect (hit); }
                    }
                }
                else
                {
                    editor.marqueeAdditive = shift;
                    editor.marqueeStart = editor.marqueeCur = in.position;
                    editor.selectingMarquee = true;
                    if (! shift) { editor.selArcs.clear(); if (editor.onSelect) editor.onSelect (-1); }
                }

                // start a move only when pressing ON a selected arc
                const int hit2 = editor.pickArc (in.position);
                if (hit2 >= 0 && editor.isSel (hit2))
                {
                    editor.movingArc = true;
                    editor.moveStartPos = in.position;
                    // v1.33.4 (L15, part 2): the whole drag is one undo step -
                    // the pointer moves do not push anything.
                    // R5.5: the gesture's transaction - the canvas's own door.
                    editor.beginGesture ("Move the arc");
                    const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                    editor.moveStartMap.clear();
                    for (auto& a : editor.page->arcs)
                        if (editor.isSel (a.id)) editor.moveStartMap[a.id] = a.points;
                    if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (true);
                }
            }

            void update (const PointerInput& in) override
            {
                if (editor.selectingMarquee)
                {
                    editor.marqueeCur = in.position;
                    auto rect = editor.marqueeRect();
                    // arcs that have any point inside the marquee
                    std::vector<int> hits;
                    if (editor.page) for (auto& a : editor.page->arcs)
                    {
                        if (! editor.arcInLayer (a)) continue;
                        for (auto& pt : a.points)
                        {
                            juce::Point<float> pp (editor.timeToX (pt.t), editor.freqToY (pt.freq));
                            if (rect.contains (pp)) { hits.push_back (a.id); break; }
                        }
                    }
                    if (editor.marqueeAdditive)
                    {
                        for (int id : hits) if (std::find (editor.selArcs.begin(), editor.selArcs.end(), id) == editor.selArcs.end()) editor.selArcs.push_back (id);
                    }
                    else
                    {
                        editor.selArcs = hits;
                    }
                    if (editor.onSelect) editor.onSelect (editor.selArcs.empty() ? -1 : editor.selArcs.front());
                    editor.repaint();
                    return;
                }

                if (editor.movingArc)
                {
                    const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                    // F6: Alt = fine movement. It is the same path with a quarter
                    // of the sensitivity - not a second code path.
                    const float fine = in.mods.isAltDown() ? 0.25f : 1.0f;
                    const float dx = (in.position.x - editor.moveStartPos.x) * fine;
                    const float dy = (in.position.y - editor.moveStartPos.y) * fine;
                    double dt = editor.xToTime (editor.moveStartPos.x + dx) - editor.xToTime (editor.moveStartPos.x);
                    // freq ratio from end/start pixel positions (log view correct)
                    const double f0 = juce::jmax (1.0, editor.yToFreq (editor.moveStartPos.y));
                    const double f1 = juce::jmax (1.0, editor.yToFreq (editor.moveStartPos.y + dy));
                    const double ratio = f1 / f0;
                    for (auto& a : editor.page->arcs)
                    {
                        auto it = editor.moveStartMap.find (a.id);
                        if (it == editor.moveStartMap.end()) continue;
                        a.points = it->second;
                        for (auto& p : a.points)
                        {
                            p.t = juce::jlimit (0.0, editor.page->durationSec, p.t + dt);
                            p.freq = juce::jlimit (upic::kMinFreq, upic::kMaxFreq, p.freq * ratio);
                        }
                        std::stable_sort (a.points.begin(), a.points.end(), [] (auto& x, auto& y){ return x.t < y.t; });
                    }
                    // v1.33.4 (M22): the arc moved in the model - mark it, so the
                    // audio thread sees the moved arc while the drag is still going
                    // (the full onChange is too heavy for every mouse move)
                    if (editor.onModelEdited) editor.onModelEdited();
                    editor.repaint();
                    return;
                }
            }

            void end (const PointerInput&) override
            {
                if (editor.selectingMarquee)
                {
                    editor.selectingMarquee = false;
                    // tiny drag = click on empty: keep the (already cleared) selection
                    if ((editor.marqueeCur - editor.marqueeStart).getDistanceFromOrigin() < 4.0f && ! editor.marqueeAdditive)
                        editor.selArcs.clear();
                    if (editor.onSelect) editor.onSelect (editor.selArcs.empty() ? -1 : editor.selArcs.front());
                    editor.repaint();
                    return;
                }

                if (editor.movingArc)
                {
                    editor.movingArc = false;
                    // R5.5: did the move really move something? A click that
                    // only takes an arc did not - and a gesture that changed
                    // nothing is not an undo step.
                    const bool moved = movedSomething();
                    editor.moveStartMap.clear();
                    if (editor.onArcEditingChanged != nullptr) editor.onArcEditingChanged (false);
                    if (moved)
                    {
                        editor.notify();
                        // R5.5: the gesture ends in a command - one undo step.
                        editor.commitGesture();
                    }
                    else
                    {
                        // R5.5: the gesture produced nothing - the transaction
                        // is closed and dropped.
                        editor.cancelGesture();
                    }
                    editor.repaint();
                    return;
                }
            }

            void cancel() override
            {
                editor.movingArc = false;
                // R5.5: the gesture is cancelled - the arcs go back to where
                // they were, and the transaction is closed and dropped.
                restoreMovedArcs();
                editor.moveStartMap.clear();
                editor.cancelGesture();
            }

        private:
            ScoreEditor& editor;

            // Did the move really move something? The arcs are compared with
            // the state the move started from.
            bool movedSomething() const
            {
                if (editor.page == nullptr) return false;

                const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                for (auto& a : editor.page->arcs)
                {
                    auto it = editor.moveStartMap.find (a.id);
                    if (it == editor.moveStartMap.end()) continue;
                    if (a.points.size() != it->second.size()) return true;
                    for (size_t i = 0; i < a.points.size(); ++i)
                        if (a.points[i].t != it->second[i].t
                            || a.points[i].freq != it->second[i].freq
                            || a.points[i].i != it->second[i].i) return true;
                }
                return false;
            }

            // The move is undone: the arcs go back to where they were.
            void restoreMovedArcs() const
            {
                if (editor.page == nullptr) return;

                const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                for (auto& a : editor.page->arcs)
                {
                    auto it = editor.moveStartMap.find (a.id);
                    if (it != editor.moveStartMap.end()) a.points = it->second;
                }
            }
        };

        //================ the erase ==========================================
        // The erase has no gesture of its own: a press takes the arc under
        // the cursor and deletes it.
        class EraseTool : public EditingTool
        {
        public:
            explicit EraseTool (ScoreEditor& ed) : editor (ed) {}

            void begin (const PointerInput& in) override
            {
                if (in.mods.isRightButtonDown()) { editor.beginArcMenu (in.position); return; }

                int id = editor.pickArc (in.position);
                if (id >= 0) { editor.selArcs = { id }; editor.deleteSelected(); }
            }

            void update (const PointerInput&) override {}
            void end (const PointerInput&) override {}
            void cancel() override {}

        private:
            ScoreEditor& editor;
        };

        //================ the zone ===========================================
        // A press on an existing zone takes it (and lets it be moved or
        // resized), a press on empty space starts a new one, and the right
        // button opens the zone's own menu.
        class ZoneTool : public EditingTool
        {
        public:
            explicit ZoneTool (ScoreEditor& ed) : editor (ed) {}

            void begin (const PointerInput& in) override
            {
                if (in.mods.isRightButtonDown())
                {
                    const int zid = editor.pickZone (in.position);
                    editor.selectedZone = zid;
                    editor.repaint();
                    if (zid >= 0) editor.showZoneMenu (zid);
                    return;
                }

                const int zid = editor.pickZone (in.position);
                if (zid >= 0) // an existing zone: select it, and let it be moved
                {
                    editor.selectedZone = zid;
                    editor.selArcs.clear();
                    editor.beginZoneDrag (in.position);
                    editor.repaint();
                    return;
                }

                editor.drawingZone = true;
                editor.zoneStart = in.position;
                editor.zoneCur = in.position;
                editor.repaint();
            }

            void update (const PointerInput& in) override
            {
                if (editor.drawingZone)
                {
                    editor.zoneCur = in.position;
                    editor.repaint();
                    return;
                }

                // v1.33: dragging a zone - moving it, or resizing it by an edge or
                // a corner. The zone keeps its shape in pitch (the move is a ratio,
                // like an arc's move) and its sieve and time grid.
                if (editor.draggingZone)
                {
                    editor.dragZoneTo (in.position);
                    editor.repaint();
                    return;
                }
            }

            void end (const PointerInput&) override
            {
                if (editor.drawingZone)
                {
                    editor.drawingZone = false;
                    auto rect = editor.zoneScreenRect();
                    if (rect.getWidth() >= 8.0f && rect.getHeight() >= 8.0f)
                        editor.addZone (editor.xToTime (editor.zoneStart.x), editor.xToTime (editor.zoneCur.x),
                                        editor.yToFreq (editor.zoneStart.y), editor.yToFreq (editor.zoneCur.y));
                    editor.repaint();
                    return;
                }

                // the zone drag is over - the model changed, so publish it
                if (editor.draggingZone)
                {
                    editor.draggingZone = false;
                    editor.zoneHandle = 0;
                    // R5.5: did the zone really move? A click that only takes
                    // a zone did not - and a gesture that changed nothing is
                    // not an undo step.
                    if (movedSomething())
                    {
                        editor.notify();
                        // v1.33.4 (L15, part 2): the transaction ends here
                        // R5.5: the gesture ends in a command - one undo step.
                        editor.commitGesture();
                    }
                    else
                    {
                        // R5.5: the gesture produced nothing - the transaction
                        // is closed and dropped.
                        editor.cancelGesture();
                    }
                    editor.repaint();
                    return;
                }
            }

            void cancel() override
            {
                editor.drawingZone = false;
                // R5.5: the gesture is cancelled - the zone goes back to
                // where it was, and the transaction is closed and dropped.
                if (editor.draggingZone)
                {
                    editor.draggingZone = false;
                    editor.zoneHandle = 0;
                    restoreZone();
                }
                editor.cancelGesture();
            }

        private:
            ScoreEditor& editor;

            // Did the zone really move? It is compared with the state the
            // drag started from.
            bool movedSomething() const
            {
                if (editor.page == nullptr) return false;

                const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                if (auto* z = editor.findZone (editor.zoneDragId))
                    return z->t0 != editor.zoneDragT0 || z->t1 != editor.zoneDragT1
                        || z->f0 != editor.zoneDragF0 || z->f1 != editor.zoneDragF1;
                return false;
            }

            // The drag is undone: the zone goes back to where it was.
            void restoreZone() const
            {
                if (editor.page == nullptr) return;

                const juce::ScopedLock modelSl (editor.modelLock != nullptr ? *editor.modelLock : editor.dummyLock);
                if (auto* z = editor.findZone (editor.zoneDragId))
                {
                    z->t0 = editor.zoneDragT0; z->t1 = editor.zoneDragT1;
                    z->f0 = editor.zoneDragF0; z->f1 = editor.zoneDragF1;
                }
            }
        };
    }
}
