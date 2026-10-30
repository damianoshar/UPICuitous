#pragma once
#include <JuceHeader.h>
#include <algorithm>

//==============================================================================
// The arc list: the score's inventory, on the right of the display.
//
// The canvas shows the arcs as shapes; this shows what they ARE - one row
// per arc with its page, its pitch range and its time span - grouped by
// material (osc / sample / live). The sample and the live rows carry the
// per-arc controls: a Rev box (the material backwards) and a mode dropdown
// (the pitch-shift algorithm).
//
// It is a view: the panel's visibility is session state, and the rows are
// rebuilt from the model whenever it changes.
//
// The rows are drawn by hand (not a ListBox): each row carries its own
// controls, and a variable number of child components would be awkward.
class ArcListPanel : public juce::Component
{
public:
    // One row: a group header, or an arc.
    struct Row
    {
        enum Kind { group, arc };
        Kind kind = group;
        juce::String title;      // the group name, or the arc's id
        juce::String info;       // the position info (pitch, time, page)
        int arcId = -1;
        int page = -1;
        int mode = -1;           // the arc's pitch mode (-1 = inherit)
        bool reversed = false;
        bool showReverse = false;  // the row has a Rev box
        bool showMode = false;     // the row has a mode dropdown
        bool live = false;         // a live arc (the live mode names)
        juce::Colour colour { 0xffc9c9d4 };
    };

    ArcListPanel() = default;

    void setRows (std::vector<Row> newRows)
    {
        rows = std::move (newRows);
        // keep the selection for rows that still exist (a rebuild must not
        // collapse a multi-selection); arc ids repeat across pages, so the
        // page has to match too
        std::vector<int> keep;
        for (int id : selectedArcs)
            for (const auto& r : rows)
                if (r.kind == Row::arc && r.page == selectionPage && r.arcId == id)
                { keep.push_back (id); break; }
        selectedArcs = std::move (keep);
        selectedArc = selectedArcs.empty() ? -1 : selectedArcs.front();
        repaint();
    }

    void setSelectedArcs (int page, const std::vector<int>& ids)
    {
        if (selectionPage == page && selectedArcs == ids) return;
        selectionPage = page;
        selectedArcs = ids;
        selectedArc = ids.empty() ? -1 : ids.front();
        repaint();
    }

    void setSelectedArc (int page, int arcId)
    {
        setSelectedArcs (page, arcId >= 0 ? std::vector<int> { arcId } : std::vector<int> {});
    }

    const std::vector<int>& getSelectedArcs() const { return selectedArcs; }
    bool isArcSelected (int page, int arcId) const
    {
        return page == selectionPage
               && std::find (selectedArcs.begin(), selectedArcs.end(), arcId)
                      != selectedArcs.end();
    }
    const std::vector<Row>& getRows() const { return rows; }

    //--- the callbacks ---------------------------------------------------
    // additive = Ctrl/Cmd/Shift-click: toggle this arc in the selection
    std::function<void (int arcId, int page, bool additive)> onArcClicked;
    std::function<void (int page, int arcId, bool reversed)> onReverseToggled;
    std::function<void (int page, int arcId, int mode)> onModeChosen;
    std::function<void (int page)> onPageClicked;

    //--- painting --------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff14141a));
        g.setColour (juce::Colour (0xff24242c));
        g.drawRect (getLocalBounds(), 1);

        const float s = uiScale();
        const int rowH = (int) std::lround (20.0f * s);
        const int headH = (int) std::lround (22.0f * s);
        const int pad   = (int) std::lround (6.0f * s);

        juce::Font body (juce::FontOptions (12.0f));
        juce::Font bold (juce::FontOptions (12.0f, juce::Font::bold));
        juce::Font small (juce::FontOptions (11.0f));

        int y = pad;
        for (const auto& r : rows)
        {
            if (y + rowH > getHeight() - pad) break;

            if (r.kind == Row::group)
            {
                g.setFont (bold);
                g.setColour (r.colour.withAlpha (0.9f));
                g.drawText (r.title, pad, y, getWidth() - 2 * pad, headH, juce::Justification::centredLeft);
                y += headH;
                continue;
            }

            const bool selected = isSelected (r);
            if (selected)
            {
                g.setColour (juce::Colour (0x30ffffff));
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) (pad - 2), (float) y,
                                                               (float) (getWidth() - 2 * pad + 4), (float) rowH), 3.0f);
            }

            g.setFont (body);
            g.setColour (selected ? juce::Colours::white : juce::Colour (0xffd6d6e0));
            g.drawText (r.title, pad, y, (int) std::lround (34.0f * s), rowH, juce::Justification::centredLeft);

            g.setFont (small);
            g.setColour (juce::Colour (0xff9a9aa8));
            g.drawText (r.info, pad + (int) std::lround (34.0f * s), y,
                        getWidth() - 2 * pad - (int) std::lround (34.0f * s) - controlWidth (s),
                        rowH, juce::Justification::centredLeft);

            // the per-arc controls (right aligned)
            int x = getWidth() - pad;
            if (r.showMode)
            {
                const int w = (int) std::lround (96.0f * s);
                x -= w;
                modeBox (g, r, juce::Rectangle<int> (x, y + 2, w, rowH - 4), body);
                x -= (int) std::lround (6.0f * s);
            }
            if (r.showReverse)
            {
                const int w = (int) std::lround (18.0f * s);
                x -= w;
                revBox (g, r, juce::Rectangle<int> (x, y + 3, w, rowH - 6));
                x -= (int) std::lround (6.0f * s);
            }

            y += rowH;
        }
    }

    //--- mouse -----------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        const float s = uiScale();
        const int rowH = (int) std::lround (20.0f * s);
        const int headH = (int) std::lround (22.0f * s);
        const int pad   = (int) std::lround (6.0f * s);

        int y = pad;
        for (const auto& r : rows)
        {
            if (r.kind == Row::group) { y += headH; continue; }

            if (y <= e.position.y && e.position.y < y + rowH)
            {
                // the controls first (they are on the right)
                int x = getWidth() - pad;
                if (r.showMode)
                {
                    const int w = (int) std::lround (96.0f * s);
                    x -= w;
                    if (e.position.x >= x) { chooseMode (r); return; }
                    x -= (int) std::lround (6.0f * s);
                }
                if (r.showReverse)
                {
                    const int w = (int) std::lround (18.0f * s);
                    x -= w;
                    if (e.position.x >= x)
                    {
                        if (onReverseToggled) onReverseToggled (r.page, r.arcId, ! r.reversed);
                        return;
                    }
                }
                // otherwise: select the arc (Ctrl/Cmd/Shift = add to selection)
                if (onArcClicked)
                    onArcClicked (r.arcId, r.page,
                                  e.mods.isCommandDown() || e.mods.isShiftDown()
                                      || e.mods.isCtrlDown());
                return;
            }
            y += rowH;
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // a double click on the page chip jumps to that page
        const float s = uiScale();
        const int rowH = (int) std::lround (20.0f * s);
        const int headH = (int) std::lround (22.0f * s);
        const int pad   = (int) std::lround (6.0f * s);
        int y = pad;
        for (const auto& r : rows)
        {
            if (r.kind == Row::group) { y += headH; continue; }
            if (y <= e.position.y && e.position.y < y + rowH)
            {
                if (onPageClicked) onPageClicked (r.page);
                return;
            }
            y += rowH;
        }
    }

    //--- helpers ---------------------------------------------------------
    static juce::String modeName (int mode, bool live)
    {
        if (mode < 0) return "Follow tab";
        if (live)
            return mode == 0 ? "Granular" : mode == 1 ? "Tape" : "Vocoder";
        return mode == 0 ? "Varispeed" : mode == 1 ? "Granular" : "Vocoder";
    }

private:
    std::vector<Row> rows;
    std::vector<int> selectedArcs;
    int selectionPage = -1;
    int selectedArc = -1;

    bool isSelected (const Row& r) const
    {
        return r.page == selectionPage
               && std::find (selectedArcs.begin(), selectedArcs.end(), r.arcId)
                      != selectedArcs.end();
    }

    float uiScale() const
    {
        const auto r = getLocalBounds();
        return juce::jlimit (0.6f, 2.0f, r.getHeight() / 620.0f);
    }

    int controlWidth (float s) const
    {
        int w = 0;
        for (const auto& r : rows)
        {
            int rw = 0;
            if (r.showMode)  rw += (int) std::lround (96.0f * s) + (int) std::lround (6.0f * s);
            if (r.showReverse) rw += (int) std::lround (18.0f * s) + (int) std::lround (6.0f * s);
            w = juce::jmax (w, rw);
        }
        return w;
    }

    void revBox (juce::Graphics& g, const Row& r, juce::Rectangle<int> bounds)
    {
        g.setColour (r.reversed ? juce::Colour (0xff64d2ff) : juce::Colour (0xff3a3a46));
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff8a8a99));
        g.drawRoundedRectangle (bounds.toFloat(), 2.0f, 1.0f);
        if (r.reversed)
        {
            g.setColour (juce::Colour (0xff10101a));
            g.drawText ("R", bounds.reduced (1), juce::Justification::centred);
        }
    }

    void modeBox (juce::Graphics& g, const Row& r, juce::Rectangle<int> bounds,
                  const juce::Font& f)
    {
        g.setColour (juce::Colour (0xff23232b));
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff4a4a56));
        g.drawRoundedRectangle (bounds.toFloat(), 2.0f, 1.0f);
        g.setFont (f);
        g.setColour (juce::Colour (0xffc7d2e0));
        g.drawText (modeName (r.mode, r.live), bounds.reduced (3, 0),
                    juce::Justification::centredLeft);
    }

    void chooseMode (const Row& r)
    {
        if (! onModeChosen) return;
        const bool live = r.live;
        juce::PopupMenu menu;
        menu.addItem (1, "Follow the tab");
        if (live)
        {
            menu.addItem (2, "Granular");
            menu.addItem (3, "Tape");
            menu.addItem (4, "Phase vocoder");
        }
        else
        {
            menu.addItem (2, "Varispeed");
            menu.addItem (3, "Granular");
            menu.addItem (4, "Phase vocoder");
        }
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (this),
                            [this, r] (int id)
                            {
                                int m = -1;
                                if (id == 2) m = 0;
                                else if (id == 3) m = 1;
                                else if (id == 4) m = 2;
                                if (onModeChosen) onModeChosen (r.page, r.arcId, m);
                            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArcListPanel)
};
