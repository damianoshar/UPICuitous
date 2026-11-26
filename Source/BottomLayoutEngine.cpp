/*
    UPICuitous - the bottom panel layout engine (refactoring wave R1.2).

    Expression-for-expression port of the bottom half of
    MainComponent::resized(). The JUCE rectangle helpers below repeat the
    juce::Rectangle semantics (including the clamping) so the extraction
    cannot move a pixel.
*/

#include "BottomLayoutEngine.h"

#include <algorithm>
#include <cmath>

namespace upic::layout
{
    namespace
    {
        int scaled (int dp, float scale) noexcept
        {
            return (int) std::lround ((float) dp * scale);
        }

        int maxOf (int a, int b) noexcept { return a > b ? a : b; }
        int minOf (int a, int b) noexcept { return a < b ? a : b; }
        int clamp (int value, int lo, int hi) noexcept
        {
            return value < lo ? lo : (value > hi ? hi : value);
        }

        // juce::Rectangle::reduced
        Rect reduced (Rect r, int deltaX, int deltaY) noexcept
        {
            return { r.x + deltaX, r.y + deltaY,
                     maxOf (0, r.w - deltaX * 2),
                     maxOf (0, r.h - deltaY * 2) };
        }

        // juce::Rectangle::removeFromTop
        Rect removeFromTop (Rect& r, int amount) noexcept
        {
            const int h = minOf (amount, r.h);
            const Rect out { r.x, r.y, r.w, h };
            r.y += h;
            r.h -= h;
            return out;
        }

        // juce::Rectangle::removeFromBottom
        Rect removeFromBottom (Rect& r, int amount) noexcept
        {
            const int newHeight = maxOf (0, r.h - amount);
            const Rect out { r.x, r.y + newHeight, r.w, r.h - newHeight };
            r.h = newHeight;
            return out;
        }

        // juce::Rectangle::removeFromLeft
        Rect removeFromLeft (Rect& r, int amount) noexcept
        {
            const int w = minOf (amount, r.w);
            const Rect out { r.x, r.y, w, r.h };
            r.x += w;
            r.w -= w;
            return out;
        }

        // juce::Rectangle::removeFromRight
        Rect removeFromRight (Rect& r, int amount) noexcept
        {
            const int newWidth = maxOf (0, r.w - amount);
            const Rect out { r.x + newWidth, r.y, r.w - newWidth, r.h };
            r.w = newWidth;
            return out;
        }

        EditorColumnLayout layoutEditor (const Rect& column, float scale,
                                         const std::vector<int>& ideals,
                                         bool hasFocusButton)
        {
            EditorColumnLayout out;
            Rect col = column;

            out.manager = reduced (removeFromTop (col, scaled (26, scale)), 2, 2);
            Rect t = removeFromTop (col, scaled (26, scale));

            if (hasFocusButton)
                out.focusButton = reduced (removeFromRight (t, scaled (64, scale)), 1, 1);

            int idealSum = 0;
            for (int ideal : ideals)
                idealSum += ideal;

            const int avail = t.w;
            for (size_t i = 0; i < ideals.size(); ++i)
            {
                const int rem = t.w;
                if (rem <= 0) break;
                const int w = clamp ((int) std::lround ((double) avail
                                            * (double) ideals[i]
                                            / (double) maxOf (1, idealSum)),
                                     1, rem);
                out.buttons.push_back (reduced (removeFromLeft (t, w), 1, 1));
            }

            out.editor = reduced (col, 2, 2);
            return out;
        }
    }

    EditorColumnLayout layoutEditorColumn (const Rect& column, float scale,
                                           const std::vector<int>& idealWidthsDp,
                                           bool hasFocusButton)
    {
        return layoutEditor (column, scale, idealWidthsDp, hasFocusButton);
    }

    TabStripLayout layoutTabStrip (const Rect& row, float scale)
    {
        TabStripLayout out;
        Rect tabs = row;

        out.osc = reduced (removeFromLeft (tabs, scaled (56, scale)), 1, 1);
        out.sample = reduced (removeFromLeft (tabs, scaled (76, scale)), 1, 1);
        out.live = reduced (removeFromLeft (tabs, scaled (56, scale)), 1, 1);

        if (tabs.w >= scaled (40, scale))
        {
            out.pm = reduced (removeFromLeft (tabs, scaled (40, scale)), 1, 1);
            out.pmVisible = true;
        }

        if (tabs.w >= scaled (60, scale))
        {
            out.scoreFocus = reduced (removeFromRight (tabs, scaled (60, scale)), 1, 1);
            out.scoreFocusVisible = true;
        }

        out.hints = reduced (removeFromRight (tabs, scaled (56, scale)), 1, 1);
        out.env = reduced (removeFromRight (tabs, scaled (46, scale)), 1, 1);
        out.wave = reduced (removeFromRight (tabs, scaled (52, scale)), 1, 1);

        if (tabs.w >= scaled (72, scale))
            out.workspace = reduced (removeFromRight (tabs, scaled (72, scale)), 1, 1);

        if (tabs.w >= scaled (52, scale))
        {
            out.arcList = reduced (removeFromRight (tabs, scaled (52, scale)), 1, 1);
            out.arcListVisible = true;
        }

        return out;
    }

    BottomLayoutResult layoutBottom (const BottomLayoutConstraints& constraints)
    {
        BottomLayoutResult out;

        const float s = constraints.scale;
        const bool anyEditors = constraints.waveEditor || constraints.envelopeEditor;
        const bool stripMode = constraints.landscape || ! anyEditors;

        const int minScoreH = 150, minEdH = 110, wantEdH = 300;
        const float wantEdHNow = (constraints.waveEditor && constraints.envelopeEditor)
                                   ? (float) wantEdH : 240.0f;
        int edH = anyEditors ? (int) std::lround (wantEdHNow * s) : 0;
        const int stripH = stripMode ? scaled (34, s) : 0;
        const int budget = constraints.area.h - scaled (minScoreH, s) - 6 - stripH;
        const int edCap = maxOf (scaled (minEdH, s),
                                 (int) std::lround ((float) maxOf (0, constraints.area.h
                                                                         - stripH)
                                                    * 0.42f));
        if (anyEditors)
            edH = minOf (edH, maxOf (scaled (minEdH, s), minOf (budget, edCap)));

        Rect area = constraints.area;
        Rect bottom = removeFromBottom (area, edH + stripH);
        if (anyEditors)
            out.viewBar = removeFromTop (bottom, scaled (26, s));
        else
            out.viewBar = removeFromBottom (area, scaled (26, s));

        if (stripMode)
            out.voiceStrip = removeFromBottom (bottom, stripH);
        else
            out.voiceColumn = removeFromRight (bottom,
                maxOf (scaled (150, s), bottom.w * 12 / 100));

        Rect waveCol, envCol;
        if (constraints.waveEditor && constraints.envelopeEditor)
        {
            if (bottom.w >= scaled (460, s))
            {
                waveCol = removeFromLeft (bottom, bottom.w * 42 / 100);
                envCol = bottom;
            }
            else
            {
                waveCol = removeFromTop (bottom, bottom.h / 2);
                envCol = bottom;
            }
        }
        else if (constraints.waveEditor)
        {
            waveCol = bottom;
        }
        else if (constraints.envelopeEditor)
        {
            envCol = bottom;
        }

        if (constraints.waveEditor)
            out.waveEditor = layoutEditor (waveCol, s,
                                           constraints.waveIdealsDp, true);
        if (constraints.envelopeEditor)
            out.envelopeEditor = layoutEditor (envCol, s,
                                               constraints.envelopeIdealsDp, true);

        if (stripMode)
        {
            Rect v = reduced (out.voiceStrip, 4, 4);
            out.voice.label = reduced (removeFromLeft (v, scaled (140, s)), 2, 2);
            const int bw = maxOf (1, v.w / 5);
            out.voice.assign = reduced (removeFromLeft (v, bw), 2, 2);
            out.voice.del    = reduced (removeFromLeft (v, bw), 2, 2);
            out.voice.mute   = reduced (removeFromLeft (v, bw), 2, 2);
            out.voice.dup    = reduced (removeFromLeft (v, bw), 2, 2);
            out.voice.selectAll = reduced (v, 2, 2);
        }
        else
        {
            Rect v = reduced (out.voiceColumn, 4, 4);
            out.voice.label = removeFromTop (v, scaled (20, s));

            Rect row = removeFromTop (v, scaled (26, s));
            out.voice.assign = reduced (removeFromLeft (row, row.w / 2), 2, 2);
            out.voice.del    = reduced (removeFromLeft (row, row.w / 2), 2, 2);
            out.voice.mute   = reduced (row, 2, 2);

            Rect row2 = removeFromTop (v, scaled (26, s));
            const int bw2 = maxOf (1, row2.w / 2);
            out.voice.dup = reduced (removeFromLeft (row2, bw2), 2, 2);
            out.voice.selectAll = reduced (row2, 2, 2);
        }

        removeFromBottom (area, 6);
        out.tabStrip = layoutTabStrip (removeFromTop (area, scaled (26, s)), s);

        if (constraints.arcList)
        {
            const int panelW = minOf (scaled (300, s),
                                      maxOf (0, area.w - scaled (240, s)));
            if (panelW > 60)
            {
                out.arcList = reduced (removeFromRight (area, panelW), 2, 2);
                out.arcListVisible = true;
            }
        }

        out.canvas = area;
        if (constraints.workspace)
        {
            const int dividerX = out.canvas.x
                               + (int) std::lround ((double) out.canvas.w
                                                    * constraints.workspaceSplit)
                               - 3;
            out.splitter = { dividerX, out.canvas.y, 6, out.canvas.h };
            out.splitterVisible = true;
        }

        return out;
    }
}
