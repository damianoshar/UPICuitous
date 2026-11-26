/*
    UPICuitous - the bottom panel layout engine (refactoring wave R1.2).

    The card engine (R1.1) took the toolbar. This one takes the panels below
    it: the height budget that the editors and the canvas share, the view bar
    row, the editor columns and their library/preset rows, the voice controls
    (a strip in landscape, a column in portrait), the tab strip, the arc list
    and the workspace's canvas area.

    It is deliberately free of JUCE, like CardLayoutEngine: the input is a
    specification in design units, the output is rectangles. MainComponent
    keeps every widget, every font measurement and every setBounds() call.

    The arithmetic is the arithmetic that used to sit inside
    MainComponent::resized(), expression for expression, so the layout is
    unchanged. The R1.2 characterization suite is the gate.
*/

#pragma once

#include "LayoutRect.h"

#include <vector>

namespace upic::layout
{
    /** One editor column: the bank manager row, the preset buttons, the editor. */
    struct EditorColumnLayout
    {
        Rect manager;
        Rect focusButton;
        std::vector<Rect> buttons;
        Rect editor;
    };

    /** The voice controls, in their strip (landscape) or column (portrait). */
    struct VoiceLayout
    {
        Rect label, assign, del, mute, dup, selectAll;
    };

    /** The row of view toggles under the toolbar. */
    struct TabStripLayout
    {
        Rect osc, sample, live, pm;
        bool pmVisible = false;
        Rect scoreFocus;
        bool scoreFocusVisible = false;
        Rect hints, env, wave;
        Rect workspace;
        Rect arcList;
        bool arcListVisible = false;
    };

    /** Everything the bottom layout needs besides the area and the scale. */
    struct BottomLayoutConstraints
    {
        Rect area;                       // the area left below the toolbar
        float scale = 1.0f;

        bool waveEditor = false;
        bool envelopeEditor = false;
        bool landscape = false;          // the voice controls become a strip

        bool arcList = false;
        bool workspace = false;
        double workspaceSplit = 0.5;

        // The preset buttons' ideal widths (dp), one entry per button.
        std::vector<int> waveIdealsDp;
        std::vector<int> envelopeIdealsDp;
    };

    struct BottomLayoutResult
    {
        Rect viewBar;
        Rect voiceStrip, voiceColumn;

        EditorColumnLayout waveEditor, envelopeEditor;
        VoiceLayout voice;
        TabStripLayout tabStrip;

        Rect arcList;
        bool arcListVisible = false;

        Rect canvas;                     // the score's area, or the workspace's
        Rect splitter;
        bool splitterVisible = false;
    };

    /** The library rows and the preset row of one editor column. */
    EditorColumnLayout layoutEditorColumn (const Rect& column, float scale,
                                           const std::vector<int>& idealWidthsDp,
                                           bool hasFocusButton = true);

    /** The view toggles of the tab strip, left to right. */
    TabStripLayout layoutTabStrip (const Rect& row, float scale);

    /** The whole bottom half: editors, voice, tab strip, arc list, canvas. */
    BottomLayoutResult layoutBottom (const BottomLayoutConstraints& constraints);
}
