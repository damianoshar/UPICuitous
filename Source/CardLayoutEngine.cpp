/*
    UPICuitous - the card layout engine (refactoring wave R1.1).

    The arithmetic is a straight port of the layout that used to live inside
    MainComponent::resized(). It is kept expression-for-expression so the
    extraction cannot move a single pixel; the characterization suite in
    SelfTestLayout.cpp is the gate.
*/

#include "CardLayoutEngine.h"

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

        int atLeast (int value, int minimum) noexcept
        {
            return value < minimum ? minimum : value;
        }

        int maxOf (int a, int b) noexcept { return a > b ? a : b; }
        int minOf (int a, int b) noexcept { return a < b ? a : b; }

        // A cell with its width resolved. The item index points back into the
        // card's specification list.
        struct CardCell
        {
            int item = -1;
            int widthDp = 0;
        };

        // The height a cell needs - the same rule every card pass uses.
        int cellHeight (const CellSpecification& item, float scale,
                        const LayoutMetrics& m) noexcept
        {
            const int captionH = atLeast (8, scaled (m.knobCellCaptionH, scale));
            const int ctrlH = atLeast (14, scaled (m.cardControlH, scale));
            if (item.flex) return ctrlH;
            if (item.knob) return atLeast (8, scaled (m.knobCellHeight, scale));
            return (item.hasLabel && ! item.inlineLabel ? captionH : 0) + ctrlH;
        }

        // A card's cells, measured and ordered: the plain controls keep their
        // order, then the knobs keep theirs.
        std::vector<CardCell> makeCells (const CardSpecification& card,
                                         const LayoutMetrics& m)
        {
            std::vector<CardCell> cells;
            cells.reserve (card.items.size());
            for (size_t i = 0; i < card.items.size(); ++i)
            {
                const auto& item = card.items[i];
                CardCell cell;
                cell.item = (int) i;
                if (item.flex)
                    cell.widthDp = maxOf (item.minSliceDp, item.statusTextWidthDp + 16);
                else if (item.knob)
                    cell.widthDp = maxOf (m.knobCellWidth, item.captionWidthDp);
                else if (item.inlineLabel)
                    cell.widthDp = item.labelWidthDp + m.inlineGapDp + item.widthDp;
                else
                    cell.widthDp = maxOf (item.widthDp, item.labelWidthDp + 4);
                cells.push_back (cell);
            }

            std::stable_partition (cells.begin(), cells.end(),
                                   [&card] (const CardCell& cell)
                                   {
                                       return ! card.items[(size_t) cell.item].knob;
                                   });
            return cells;
        }

        // A row of cells inside a card.
        struct CardLine
        {
            int width = 0, height = 0;
            std::vector<size_t> cells;
        };

        // A card whose knob spans the card's rows (the Pitch card): the knob
        // takes the right column, the other cells share the left one, and the
        // knob's disc is cut in half by the boundary between their rows.
        struct SpannerLayout
        {
            int index = -1;
            int knobW = 0, knobH = 0, half = 0, bodyH = 0, leftW = 0;
            std::vector<CardLine> lines;
        };

        SpannerLayout layoutSpanner (const std::vector<CardCell>& cells,
                                     const std::vector<CellSpecification>& items,
                                     float scale, int maxW, int gap,
                                     const LayoutMetrics& m)
        {
            SpannerLayout out;
            for (size_t i = 0; i < cells.size(); ++i)
                if (items[(size_t) cells[i].item].spansRows)
                {
                    out.index = (int) i;
                    break;
                }
            if (out.index < 0)
                return out;

            const auto& sp = cells[(size_t) out.index];
            out.knobW = atLeast (8, scaled (sp.widthDp, scale));
            out.knobH = atLeast (8, scaled (m.knobCellHeight, scale));
            const int capH = atLeast (8, scaled (m.knobCellCaptionH, scale));
            const int pillH = atLeast (6, scaled (m.knobCellPillH, scale));
            const int knobGap = atLeast (1, scaled (m.knobCellGap, scale));
            const int side = maxOf (0, minOf (out.knobW,
                                              out.knobH - capH - pillH - 2 * knobGap));
            out.half = capH + knobGap + side / 2;   // the disc's middle

            const int leftMax = atLeast (8, maxW - out.knobW - gap);
            CardLine cur;
            int curW = 0, curH = 0;
            auto closeLine = [&]
            {
                if (! cur.cells.empty())
                {
                    cur.width = curW;
                    cur.height = curH;
                    out.lines.push_back (cur);
                }
                cur = CardLine();
                curW = 0;
                curH = 0;
            };
            for (size_t i = 0; i < cells.size(); ++i)
            {
                if ((int) i == out.index) continue;
                const auto& c = cells[i];
                const auto& item = items[(size_t) c.item];
                const int cw = atLeast (8, scaled (c.widthDp, scale));
                const int ch = cellHeight (item, scale, m);
                if (item.newLine && curW > 0) closeLine();
                if (curW > 0 && curW + gap + cw > leftMax) closeLine();
                curW += (curW > 0 ? gap : 0) + cw;
                curH = maxOf (curH, ch);
                cur.cells.push_back (i);
            }
            closeLine();

            // the first row reaches the knob's disc middle; the rows below
            // share what is left, so the knob bridges the boundary
            int used = 0;
            for (size_t li = 0; li < out.lines.size(); ++li)
            {
                auto& line = out.lines[li];
                int h = line.height;
                if (li == 0)
                    h = maxOf (h, out.half);
                else
                    h = maxOf (h, (out.knobH - used) / (int) (out.lines.size() - li));
                line.height = h;
                used += h;
                out.leftW = maxOf (out.leftW, line.width);
            }
            out.bodyH = maxOf (out.knobH, used);
            return out;
        }

        struct CardMetrics
        {
            int width = 0, height = 0;
        };

        CardMetrics cardMetrics (const CardSpecification& card, float scale,
                                 int maxW, const LayoutMetrics& m)
        {
            const int padX = atLeast (2, scaled (m.cardPadX, scale));
            const int padY = atLeast (1, scaled (m.cardPadY, scale));
            const int headerH = atLeast (8, scaled (m.cardHeaderH, scale));
            const int gap = atLeast (2, scaled (m.cardCellGap, scale));
            const int rowGap = atLeast (2, scaled (m.cardRowGap, scale));
            const int innerMax = atLeast (1, maxW - 2 * padX);

            const auto cells = makeCells (card, m);
            int lineW = 0, widest = 0, bodyH = 0, lineH = 0;

            const auto spanner = layoutSpanner (cells, card.items, scale,
                                                innerMax, gap, m);
            if (spanner.index >= 0)
            {
                const int headerPx = scaled (card.headerWidthDp, scale);
                CardMetrics out;
                out.width = minOf (maxW, maxOf (spanner.leftW + gap + spanner.knobW,
                                                headerPx) + 2 * padX);
                out.height = headerH + 2 * padY + spanner.bodyH;
                return out;
            }

            for (auto& c : cells)
            {
                const auto& item = card.items[(size_t) c.item];
                const int cw = atLeast (8, scaled (c.widthDp, scale));
                const int ch = cellHeight (item, scale, m);
                if (item.newLine && lineW > 0)
                {
                    widest = maxOf (widest, lineW);
                    bodyH += lineH + rowGap;
                    lineW = 0;
                    lineH = 0;
                }
                if (lineW > 0 && lineW + gap + cw > innerMax)
                {
                    widest = maxOf (widest, lineW);
                    bodyH += lineH + rowGap;
                    lineW = 0;
                    lineH = 0;
                }
                lineW += (lineW > 0 ? gap : 0) + cw;
                lineH = maxOf (lineH, ch);
            }
            widest = maxOf (widest, lineW);
            bodyH += lineH;

            const int headerPx = scaled (card.headerWidthDp, scale);
            CardMetrics out;
            out.width = minOf (maxW, maxOf (widest, headerPx) + 2 * padX);
            out.height = headerH + 2 * padY + bodyH;
            return out;
        }

        // One cell in the placement pass: where its line starts and how tall
        // the line is.
        struct PlacedCell
        {
            int item = -1;
            int x = 0, y = 0, w = 0, lineH = 0;
        };

        // The card's body: the rectangle under the header, inside the pads.
        Rect cardBody (Rect bounds, int padX, int padY, int headerH) noexcept
        {
            return { bounds.x + padX,
                     bounds.y + headerH + padY,
                     atLeast (0, bounds.w - 2 * padX),
                     atLeast (0, bounds.h - headerH - 2 * padY) };
        }

        void placeCardCells (const CardSpecification& card, Rect bounds,
                             float scale, const LayoutMetrics& m,
                             CardPlacement& out)
        {
            const int padX = atLeast (2, scaled (m.cardPadX, scale));
            const int padY = atLeast (1, scaled (m.cardPadY, scale));
            const int headerH = atLeast (8, scaled (m.cardHeaderH, scale));
            const int gap = atLeast (2, scaled (m.cardCellGap, scale));
            const int rowGap = atLeast (2, scaled (m.cardRowGap, scale));
            const int captionH = atLeast (8, scaled (m.knobCellCaptionH, scale));
            const int ctrlH = atLeast (14, scaled (m.cardControlH, scale));
            const int knobH = scaled (m.knobCellHeight, scale);

            if (card.hasHeader)
            {
                out.hasHeader = true;
                out.header = { bounds.x + padX, bounds.y + padY,
                               atLeast (8, bounds.w - 2 * padX),
                               atLeast (8, headerH) };
            }

            const Rect body = cardBody (bounds, padX, padY, headerH);
            const auto cells = makeCells (card, m);

            auto add = [&out] (int item) -> CellPlacement&
            {
                out.cells.push_back ({});
                out.cells.back().item = item;
                return out.cells.back();
            };

            // v1.34.4: the Pitch card's knob spans the card's rows - it takes
            // the right column and the other cells share the left one.
            const auto spanner = layoutSpanner (cells, card.items, scale,
                                                body.w, gap, m);
            if (spanner.index >= 0)
            {
                const int knobItem = cells[(size_t) spanner.index].item;
                {
                    auto& p = add (knobItem);
                    p.hasComp = card.items[(size_t) knobItem].hasComp;
                    p.hideLabel = true;
                    p.comp = { body.getRight() - spanner.knobW, body.y,
                               spanner.knobW, minOf (spanner.knobH, body.h) };
                }

                // the left column: the rows are flush, so the knob bridges
                int rowY = body.y;
                for (const auto& line : spanner.lines)
                {
                    int x = body.x;
                    for (auto ci : line.cells)
                    {
                        const auto& c = cells[ci];
                        const auto& item = card.items[(size_t) c.item];
                        const int cw = minOf (atLeast (8, scaled (c.widthDp, scale)),
                                              atLeast (8, body.w - spanner.knobW - gap));
                        auto& p = add (c.item);
                        p.hasLabel = item.hasLabel;
                        p.hasComp = item.hasComp;
                        if (item.flex)
                        {
                            p.comp = { x, rowY, cw, line.height };
                        }
                        else if (item.inlineLabel)
                        {
                            const int gapPx = atLeast (2, scaled (m.inlineGapDp, scale));
                            const int lw = minOf (scaled (item.labelWidthDp, scale),
                                                  atLeast (0, cw - gapPx));
                            const int cy = rowY + (line.height - ctrlH) / 2;
                            p.label = { x, cy, lw, ctrlH };
                            p.comp  = { x + lw + gapPx, cy,
                                        atLeast (1, cw - lw - gapPx), ctrlH };
                        }
                        else
                        {
                            const int capH = item.hasLabel ? captionH : 0;
                            const int cy = rowY + (line.height - (capH + ctrlH)) / 2;
                            p.label = { x, cy, cw, capH };
                            p.comp  = { x, cy + capH, cw, ctrlH };
                        }
                        x += cw + gap;
                    }
                    rowY += line.height;
                }
                return;
            }

            std::vector<PlacedCell> placed;
            size_t lineStart = 0;
            int x = body.x, y = body.y, lineH = 0;
            auto closeLine = [&]
            {
                for (size_t k = lineStart; k < placed.size(); ++k)
                    placed[k].lineH = lineH;
                y += lineH + rowGap;
                lineStart = placed.size();
                lineH = 0;
                x = body.x;
            };
            for (auto& c : cells)
            {
                const auto& item = card.items[(size_t) c.item];
                // v1.34.1: a narrowed card (the flex status) must not let its
                // cell hang over the card's edge; the cell follows the body.
                const int cw = minOf (atLeast (8, scaled (c.widthDp, scale)),
                                      atLeast (8, body.w));
                const int ch = cellHeight (item, scale, m);
                if (item.newLine && x > body.x)
                    closeLine();
                if (x > body.x && x + cw > body.getRight())
                    closeLine();
                placed.push_back ({ c.item, x, y, cw, ch });
                x += cw + gap;
                lineH = maxOf (lineH, ch);
            }
            closeLine();

            // v1.34.3: a card whose only cell is a knob centres it under the
            // header instead of hugging the left edge (Tempo, Acceleration).
            if (placed.size() == 1 && card.items[(size_t) placed[0].item].knob)
                placed[0].x = body.x + (body.w - placed[0].w) / 2;

            // then place everything against its line's bottom, so the
            // controls and value pills line up however tall the neighbour is
            for (auto& pc : placed)
            {
                const auto& item = card.items[(size_t) pc.item];
                const int bottom = pc.y + pc.lineH;
                auto& p = add (pc.item);
                p.hasLabel = item.hasLabel;
                p.hasComp = item.hasComp;

                if (item.flex)
                {
                    // v1.34.5: the status label owns the card's whole body -
                    // the measured cell can be narrower than the header.
                    p.ownsBody = true;
                    p.comp = { body.x, pc.y, body.w, pc.lineH };
                }
                else if (item.knob)
                {
                    p.hideLabel = true;
                    p.comp = { pc.x, bottom - knobH, pc.w, knobH };
                }
                else if (item.inlineLabel)
                {
                    const int gapPx = atLeast (2, scaled (m.inlineGapDp, scale));
                    const int lw = minOf (scaled (item.labelWidthDp, scale),
                                          atLeast (0, pc.w - gapPx));
                    const int cellY = item.topAlign ? pc.y : bottom - ctrlH;
                    p.label = { pc.x, cellY, lw, ctrlH };
                    p.comp  = { pc.x + lw + gapPx, cellY,
                                atLeast (1, pc.w - lw - gapPx), ctrlH };
                }
                else
                {
                    const int labelY = item.topAlign ? pc.y : bottom - ctrlH - captionH;
                    const int compY  = item.topAlign ? pc.y + captionH : bottom - ctrlH;
                    p.label = { pc.x, labelY, pc.w, captionH };
                    p.comp  = { pc.x, compY, pc.w, ctrlH };
                }
            }
        }
    } // namespace

    CardLayoutResult layoutCards (const std::vector<CardSpecification>& list,
                                  const CardLayoutConstraints& constraints)
    {
        const auto& m = constraints.metrics;
        const int gap = atLeast (2, scaled (m.cardGap, constraints.scale));
        const int areaW = constraints.area.w;

        struct Card
        {
            int index = -1;
            int w = 0, h = 0;
        };

        std::vector<Card> cards;
        cards.reserve (list.size());
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto metrics = cardMetrics (list[i], constraints.scale, areaW, m);
            cards.push_back ({ (int) i, metrics.width, metrics.height });
        }

        // v1.34.5: every card keeps its own width - the status card is
        // measured to its text, so it packs into a row like the others
        // instead of being stretched over the rest of the line.
        std::vector<std::vector<size_t>> rowCards;
        std::vector<int> rowW, rowH;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            if (rowCards.empty() || rowW.back() + gap + cards[i].w > areaW)
            {
                rowCards.push_back ({});
                rowW.push_back (0);
                rowH.push_back (0);
            }
            rowCards.back().push_back (i);
            rowW.back() += (rowCards.back().size() > 1 ? gap : 0) + cards[i].w;
            rowH.back() = maxOf (rowH.back(), cards[i].h);
        }

        CardLayoutResult out;
        int y = constraints.area.y;
        for (size_t r = 0; r < rowCards.size(); ++r)
        {
            int x = constraints.area.x;
            for (auto k : rowCards[r])
            {
                CardPlacement placed;
                placed.card = cards[k].index;
                placed.bounds = { x, y, cards[k].w, cards[k].h };
                placeCardCells (list[(size_t) placed.card], placed.bounds,
                                constraints.scale, m, placed);
                out.cards.push_back (std::move (placed));
                x += cards[k].w + gap;
            }
            y += rowH[r] + gap;
            ++out.rows;
        }

        out.height = maxOf (0, y - gap - constraints.area.y);
        return out;
    }

    StripLayoutResult layoutStrip (const std::vector<CardSpecification>& list,
                                   const CardLayoutConstraints& constraints)
    {
        const auto& m = constraints.metrics;
        const int rh = scaled (m.stripRowH, constraints.scale);
        const int pad = atLeast (1, scaled (m.stripPad, constraints.scale));
        const int gap = atLeast (1, scaled (m.stripGroupGap, constraints.scale));
        const int right = constraints.area.getRight();
        int x = constraints.area.x, y = constraints.area.y;

        StripLayoutResult out;
        out.rowHeight = rh;

        for (size_t gi = 0; gi < list.size(); ++gi)
        {
            const auto& card = list[gi];
            int groupW = 0;
            for (const auto& it : card.items)
                groupW += it.flex ? it.minSliceDp : it.labelWidthDp + it.widthDp;
            const int gw = scaled (groupW, constraints.scale);

            // does the group fit on the current line?
            if (x > constraints.area.x && x + gw + gap > right)
            {
                x = constraints.area.x;
                y += rh;
            }
            int segX = x, segY = y;

            StripCardPlacement placed;
            placed.card = (int) gi;

            auto closeSegment = [&]
            {
                if (x > segX)
                    placed.segments.push_back ({ { segX, segY,
                                                   atLeast (1, x - segX), rh } });
            };

            for (size_t ii = 0; ii < card.items.size(); ++ii)
            {
                const auto& it = card.items[ii];
                CellPlacement p;
                p.item = (int) ii;
                p.hasLabel = it.hasLabel;
                p.hasComp = it.hasComp;

                if (it.flex)
                {
                    // the status line takes the rest of its line
                    const int minPx = scaled (it.minSliceDp, constraints.scale);
                    const int fw = maxOf (minPx, right - x - pad);
                    p.comp = { x + pad, y + pad,
                               atLeast (8, fw - 2 * pad), rh - 2 * pad };
                    placed.cells.push_back (p);
                    x += fw;
                    continue;
                }

                const int lw = scaled (it.labelWidthDp, constraints.scale);
                const int cw = scaled (it.widthDp, constraints.scale);
                if (x > constraints.area.x && x + lw + cw > right)
                {
                    closeSegment();
                    x = constraints.area.x;
                    y += rh;
                    segX = x;
                    segY = y;
                }
                p.label = { x + pad, y + pad,
                            atLeast (1, lw - 2 * pad), rh - 2 * pad };
                p.comp  = { x + lw + pad, y + pad,
                            atLeast (1, cw - 2 * pad), rh - 2 * pad };
                placed.cells.push_back (p);
                x += lw + cw;
            }
            closeSegment();
            x += gap;
            out.cards.push_back (std::move (placed));
        }

        out.rows = rh > 0 ? (y - constraints.area.y) / rh + 1 : 0;
        out.height = out.rows * rh;
        return out;
    }
}
