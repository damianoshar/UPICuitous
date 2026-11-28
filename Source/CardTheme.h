#pragma once

//==============================================================================
// R5.12 (the review's R055): the card's look.
//
// Every shell that draws a card draws the same frame: the backdrop behind the
// card, the card's own surface, the card's border and the corner radius.
// The eight shells wrote those values again, once each:
//
//     g.fillAll (juce::Colour (0xcc000000));
//     auto card = cardRect();
//     g.setColour (juce::Colour (0xff1e1e26));
//     g.fillRoundedRectangle (card.toFloat(), 6.0f);
//     g.setColour (juce::Colour (0xff34343f));
//     g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);
//
// This header is that frame: the values the card is drawn with, written down
// once. R5.11 moved the card's *geometry* (`Source/OverlayCard.h`); this is
// the card's *look*.
//
// It holds no widget and no document, and it is free of JUCE: the colours are
// ARGB values and the radius is a number, so the frame can be checked
// without a window.
//
// The rest of the app's colours - the panels' lists, the plots, the text -
// are still the shells' own. They move with the rest of the theme registry
// (the review's R055), not with the card's frame.
//==============================================================================

#include <cstdint>

namespace upic::card_theme
{
    /** The backdrop behind the card: the window's dimmed cover. */
    constexpr std::uint32_t kBackdrop = 0xcc000000;

    /** The card's own surface. */
    constexpr std::uint32_t kFill = 0xff1e1e26;

    /** The card's border. */
    constexpr std::uint32_t kBorder = 0xff34343f;

    /** The border's width, in pixels. */
    constexpr float kBorderWidth = 1.0f;

    /** The card's corner radius, in pixels. */
    constexpr float kRadius = 6.0f;

    /** The border's inset: the stroke sits half its width inside the card. */
    constexpr float kBorderInset = kBorderWidth * 0.5f;
}
