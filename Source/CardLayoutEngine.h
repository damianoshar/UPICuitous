/*
    UPICuitous - the card layout engine (refactoring wave R1.1).

    This file is deliberately free of JUCE: it turns measured, design-unit
    card specifications into pixel rectangles. MainComponent builds the
    specifications from its live widgets (the adapter), applies the result
    with setBounds() and keeps every JUCE call. The engine never sees a
    juce::Component, a font or a Component's idea of "now".

    The arithmetic here is the arithmetic that used to sit inside
    MainComponent::resized(): same order, same spacing, same rounding, so
    the layout is unchanged.
*/

#pragma once

#include "LayoutRect.h"

#include <cstddef>
#include <vector>

namespace upic::layout
{
    /** The card layout's design-unit constants and the live knob metrics. */
    struct LayoutMetrics
    {
        // The knob's own cell metrics (upic::Knob::kCell*). The adapter
        // copies them so the engine needs no Knob header.
        int knobCellWidth = 0;
        int knobCellHeight = 0;
        int knobCellCaptionH = 0;
        int knobCellPillH = 0;
        int knobCellGap = 0;

        // The card's design rules (dp).
        int cardPadX = 4, cardPadY = 3, cardGap = 6, cardHeaderH = 20;
        int cardCellGap = 4, cardRowGap = 4, cardControlH = 26;
        int inlineGapDp = 4;

        // The permanent strip's design rules (dp).
        int stripRowH = 36, stripGroupGap = 6, stripPad = 2;
    };

    /** One cell of a card, with every text measurement already resolved. */
    struct CellSpecification
    {
        int labelWidthDp = 0;        // the caption's reserved width
        int widthDp = 0;             // the control's natural width
        int minSliceDp = 0;          // a flex cell's minimum
        int statusTextWidthDp = 0;   // a flex cell's own text (status line)
        int captionWidthDp = 0;      // a knob's caption text

        bool hasLabel = false, hasComp = false;
        bool flex = false;
        bool knob = false;
        bool inlineLabel = false;
        bool newLine = false;
        bool topAlign = false;
        bool spansRows = false;
    };

    /** One card: its cells and its header measurement. */
    struct CardSpecification
    {
        std::vector<CellSpecification> items;
        bool hasHeader = false;
        int headerWidthDp = 0;
    };

    /** Everything the engine needs besides the specifications. */
    struct CardLayoutConstraints
    {
        Rect area;
        float scale = 1.0f;
        LayoutMetrics metrics;
    };

    /** One placed cell. Indices refer to the card's item list. */
    struct CellPlacement
    {
        int item = -1;
        Rect label, comp;
        bool hasLabel = false, hasComp = false;
        bool hideLabel = false;   // a knob draws its caption instead
        bool ownsBody = false;    // a flex cell takes the card's whole body
    };

    /** One placed card. */
    struct CardPlacement
    {
        int card = -1;            // index into the specification list
        Rect bounds;
        Rect header;
        bool hasHeader = false;
        std::vector<CellPlacement> cells;
    };

    struct CardLayoutResult
    {
        int rows = 0;
        int height = 0;
        std::vector<CardPlacement> cards;
    };

    /** Lay the cards out in rows, measuring and placing them. */
    CardLayoutResult layoutCards (const std::vector<CardSpecification>& cards,
                                  const CardLayoutConstraints& constraints);

    /** One line segment of the permanent strip (a group may wrap). */
    struct StripSegment
    {
        Rect bounds;
    };

    struct StripCardPlacement
    {
        int card = -1;
        std::vector<StripSegment> segments;
        std::vector<CellPlacement> cells;
    };

    struct StripLayoutResult
    {
        int rows = 0;
        int rowHeight = 0;
        int height = 0;
        std::vector<StripCardPlacement> cards;
    };

    /** Lay the permanent strip out: compact rows, no cards. */
    StripLayoutResult layoutStrip (const std::vector<CardSpecification>& cards,
                                   const CardLayoutConstraints& constraints);
}
