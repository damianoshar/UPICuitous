#pragma once

//==============================================================================
// R5.11 (the review's R053, section 29): the overlay card shell.
//
// The app cannot use a floating dialog window: on Android there is no title
// bar and no close button, so a dialog could never be dismissed. Every editor
// and overlay is therefore a plain Component that paints a backdrop and a
// card in the middle of the window, and a tap on the backdrop dismisses it.
//
// Eight of those shells wrote the same rule again:
//
//     auto r = getLocalBounds().reduced (10);
//     return juce::Rectangle<int> (juce::jmin (maxW, r.getWidth()),
//                                  juce::jmin (maxH, r.getHeight())).withCentre (r.getCentre());
//
// This header is that rule: the window loses a margin, the card never grows
// past the shell's maximum, and the card sits in the middle.
//
// It holds no widget and no document, and it is free of JUCE: the geometry is
// data, and the data can be checked without a window.
//
// The colours and the corner radius stay where they are - they are the
// theme's business (the review's R055).
//==============================================================================

#include "LayoutRect.h"

namespace upic::overlay_card
{
    using upic::layout::Rect;

    /** The shells that draw a card in the middle of the window. */
    enum class Shell
    {
        AudioSettings = 0,   // the audio settings and the MIDI + OSC settings
        ClearPage,           // the "clear page?" warning
        Group,               // the arc-group panel
        Channel,             // the mixer
        Sequence,            // the sequence panel
        AmpTables,           // the amplitude-table bank
        FreqTable,           // the frequency-table editor
        Performance,         // the performance view
        count
    };

    /** The card's maximum size: the card never grows past it. */
    struct Size
    {
        int maxWidth = 0;
        int maxHeight = 0;
    };

    /** The size a shell's card may take. */
    inline Size sizeFor (Shell shell)
    {
        switch (shell)
        {
            case Shell::AudioSettings: return { 560, 600 };
            case Shell::ClearPage:     return { 480, 210 };
            case Shell::Group:         return { 840, 600 };
            case Shell::Channel:       return { 760, 500 };
            case Shell::Sequence:      return { 820, 540 };
            case Shell::AmpTables:     return { 780, 570 };
            case Shell::FreqTable:     return { 760, 560 };
            case Shell::Performance:   return { 980, 620 };
            default:                   return {};
        }
    }

    /** The window's margin: the card lives inside it. */
    constexpr int kOuterMargin = 10;

    /** The card's padding: the content lives inside it. */
    constexpr int kPaddingX = 18;
    constexpr int kPaddingY = 12;

    /** The card's rectangle inside a window of the given size.

        The window loses the outer margin, the card never grows past the
        shell's maximum, and the card sits in the middle of what is left. The
        rectangle is in the shell's own coordinates - the same ones the old
        `getLocalBounds()` rule used - and the arithmetic is JUCE's own (the
        integer division of the centre), so the card lands on the same pixel
        it landed on before.
    */
    inline Rect cardFor (Shell shell, int windowW, int windowH)
    {
        const Size s = sizeFor (shell);
        // the window loses the margin ...
        const int rw = windowW - kOuterMargin * 2;
        const int rh = windowH - kOuterMargin * 2;
        // ... the card never grows past the shell's maximum ...
        const int cw = s.maxWidth < rw ? s.maxWidth : rw;
        const int ch = s.maxHeight < rh ? s.maxHeight : rh;
        // ... and the card sits in the middle of what is left.
        return { kOuterMargin + rw / 2 - cw / 2,
                 kOuterMargin + rh / 2 - ch / 2, cw, ch };
    }

    /** The card's content rectangle: the card's padding.

        The padding follows JUCE's `reduced()`: it does not clamp, so a card
        that is too small for its padding keeps the negative size the old
        rule gave it.
    */
    inline Rect paddedFor (const Rect& card)
    {
        return { card.x + kPaddingX, card.y + kPaddingY,
                 card.w - kPaddingX * 2, card.h - kPaddingY * 2 };
    }
}
