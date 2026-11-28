#pragma once

//==============================================================================
// R5.3 (the review's R045): the score canvas's hit tester.
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
// This is that hit tester. It answers from what it is handed: the mapper
// (where the plot is and how the page's time and pitch map onto the screen),
// the page (the arcs and the zones), the layer (only this layer's arcs are
// under the cursor) and whether a touch pointer has been seen (a finger gets
// a wider grab band). It reads them and writes nothing.
//
// The bodies are the editor's, body for body; the suite
// (Source/SelfTestScoreHitTest.cpp) pins them.
//
// It is a header of its own so that the editor, the render and the tools can
// all ask the same question and get the same answer. The editor's gesture
// stays in the editor: the hit tester answers, the editor acts.

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "ScoreCoordinateMapper.h"
#include "UpicModel.h"

namespace upic
{
    namespace score_hit_test
    {
        //================ the hit tester =====================================
        struct ScoreHitTester
        {
            // The mapping the questions are asked in.
            score_coordinates::ScoreCoordinateMapper mapper;

            // The page the arcs and the zones are on.
            const Page* page = nullptr;

            // Only this layer's arcs are under the cursor (0 osc / 1 sample /
            // 2 live).
            int layer = 0;

            // A touch pointer has been seen: the grab band is wider (fat
            // fingers, no hover).
            bool touchSeen = false;

            // -- the arcs ------------------------------------------------------
            // All arcs whose polyline passes within maxD pixels of pos, nearest
            // first. Touch pointers get a wider grab band.
            std::vector<int> arcsNear (juce::Point<float> pos, float maxD = -1.0f) const
            {
                if (maxD <= 0.0f) maxD = touchSeen ? 20.0f : 12.0f;
                std::vector<std::pair<float, int>> hits;
                if (! page) return {};
                for (const auto& a : page->arcs)
                {
                    if (a.mode != layer) continue;
                    float bestD = maxD;
                    for (const auto& pt : a.points)
                        bestD = juce::jmin(bestD, (float) std::hypot(mapper.timeToX(pt.t) - pos.x,
                                                                     mapper.freqToY(pt.freq) - pos.y));
                    for (size_t i = 1; i < a.points.size(); ++i)
                    {
                        juce::Point<float> p0{ mapper.timeToX(a.points[i-1].t), mapper.freqToY(a.points[i-1].freq) };
                        juce::Point<float> p1{ mapper.timeToX(a.points[i].t),   mapper.freqToY(a.points[i].freq) };
                        juce::Line<float> seg(p0, p1);
                        juce::Point<float> nearest;
                        const float d = seg.getDistanceFromPoint(pos, nearest);
                        if (d < bestD && pos.x >= juce::jmin(p0.x, p1.x) - 4
                                      && pos.x <= juce::jmax(p0.x, p1.x) + 4)
                            bestD = d;
                    }
                    if (bestD < maxD) hits.push_back({ bestD, a.id });
                }
                std::sort(hits.begin(), hits.end(), [](auto& x, auto& y){ return x.first < y.first; });
                std::vector<int> out;
                for (const auto& h : hits) out.push_back(h.second);
                return out;
            }

            // The nearest arc under pos (-1 if none).
            int pickArc (juce::Point<float> pos) const
            {
                auto v = arcsNear(pos);
                return v.empty() ? -1 : v.front();
            }

            // -- the zones -----------------------------------------------------
            // The zone under pos (-1 if none). The topmost (last drawn) first.
            // A few pixels of slack let the border itself be grabbed for a
            // move/resize (the exact edge can round to just outside the
            // time/frequency rectangle).
            int pickZone (juce::Point<float> pos) const
            {
                if (! page) return -1;
                const double t = mapper.xToTime(pos.x), f = mapper.yToFreq(pos.y);
                for (auto it = page->zones.rbegin(); it != page->zones.rend(); ++it)
                    if (it->contains(t, f) || mapper.zoneRect(*it).expanded (4.0f).contains (pos))
                        return it->id;
                return -1;
            }

            // Which edge or corner of the zone is under pos?
            // 0 = none (move the whole zone), 1 = left, 2 = right, 3 = top,
            // 4 = bottom, 5..8 = the corners (top-left, top-right,
            // bottom-left, bottom-right).
            int zoneHandleAt (const SieveZone& z, juce::Point<float> pos) const
            {
                const auto r = mapper.zoneRect (z);
                const float h = 7.0f;
                const bool onL = std::abs (pos.x - r.getX())      <= h;
                const bool onR = std::abs (pos.x - r.getRight())  <= h;
                const bool onT = std::abs (pos.y - r.getY())      <= h;
                const bool onB = std::abs (pos.y - r.getBottom()) <= h;
                if (onL && onT) return 5;
                if (onR && onT) return 6;
                if (onL && onB) return 7;
                if (onR && onB) return 8;
                if (onL) return 1;
                if (onR) return 2;
                if (onT) return 3;
                if (onB) return 4;
                return 0;
            }
        };
    }
}
