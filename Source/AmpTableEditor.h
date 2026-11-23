#pragma once
#include <JuceHeader.h>
#include "LibraryManager.h"
#include "UpicModel.h"

//==============================================================================
// WP2: the amplitude conversion-table bank editor.
//
// The New UPIC separated the SHAPE of a sound's amplitude (the envelope) from
// the MAPPING applied to it. This overlay is the bank of those mappings: a
// list of transfer curves (envelope output 0..1 -> gain 0..1), a canvas for
// drawing one, the historical starting points (linear, square, compress,
// expand, S), and the commands (New, Duplicate, Rename, Delete, Use on the
// selection).
//
// It is an in-app overlay for the same reason the frequency-table editor is:
// Android has no closable dialog windows. Edits are committed on mouse-up,
// so the host applies one undo transaction per gesture.
//==============================================================================
class AmpTableEditor : public juce::Component
{
public:
    std::function<void (int index)> onSelectIndex;
    std::function<void (int index, const std::vector<upic::EnvPoint>&)> onCurve;
    std::function<void (int index, const juce::String&)> onRename;
    std::function<void()> onNew, onDuplicate, onDelete, onClose;
    std::function<void (int index)> onAssign;

    // WP7: the panel's own piece-bank commands stay here. The shared
    // manager is only the Piece/User bridge, so it hides its duplicate
    // New/Dup/Rename/Delete/Unique buttons.
    LibraryManager library { "amp" };

    AmpTableEditor()
    {
        setName ("ampTableEd");
        setWantsKeyboardFocus (true);

        for (auto* b : { &newBtn, &dupBtn, &delBtn, &renameBtn, &assignBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        newBtn.setName ("atNewBtn");       dupBtn.setName ("atDupBtn");
        delBtn.setName ("atDelBtn");       renameBtn.setName ("atRenameBtn");
        assignBtn.setName ("atAssignBtn"); closeBtn.setName ("atCloseBtn");

        newBtn.setTooltip ("Add a table to the bank");
        dupBtn.setTooltip ("Copy the selected table");
        delBtn.setTooltip ("Delete the selected table (refused while arcs use it)");
        renameBtn.setTooltip ("Rename the selected table");
        assignBtn.setTooltip ("Give the selected arcs this amplitude table");
        closeBtn.setTooltip ("Close the amplitude tables");

        newBtn.onClick = [this] { if (onNew) onNew(); };
        dupBtn.onClick = [this] { if (onDuplicate) onDuplicate(); };
        delBtn.onClick = [this] { if (onDelete) onDelete(); };
        renameBtn.onClick = [this]
        {
            if (onRename) onRename (sel, nameEd.getText().trim());
        };
        assignBtn.onClick = [this] { if (onAssign) onAssign (sel); };
        closeBtn.onClick = [this] { close(); };

        nameEd.setName ("atNameEd");
        nameEd.setTooltip ("The table's name");
        nameEd.onReturnKey = [this] { if (renameBtn.isEnabled()) renameBtn.triggerClick(); };
        addAndMakeVisible (nameEd);

        const int presets[] = { upic::ampLinear, upic::ampSquare, upic::ampCompress,
                                upic::ampExpand, upic::ampS };
        for (int i = 0; i < 5; ++i)
        {
            presetBtn[i].setButtonText (upic::ampPresetName (presets[i]));
            presetBtn[i].setName ("atPreset" + juce::String (i));
            presetBtn[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            presetBtn[i].setTooltip ("Start from the " + juce::String (upic::ampPresetName (presets[i]))
                                     + " curve");
            presetBtn[i].onClick = [this, p = presets[i]]
            {
                std::vector<upic::EnvPoint> pts;
                upic::ampPresetPoints (p, pts);
                commitCurve (pts);
            };
            addAndMakeVisible (presetBtn[i]);
        }

        // WP7: the source switch, the user-library selector and the
        // Publish/Import/Folder commands live in the shared manager. The
        // panel keeps its own piece-bank list and commands.
        addAndMakeVisible (library);
        library.setPieceCommandsVisible (false);
        library.setSelector (libraryBox);

        setVisible (false);
    }

    //--------------------------------------------------------------------------
    void open (const std::vector<upic::Envelope>& tableBank,
               const std::vector<int>& tableUses, int index)
    {
        setBank (tableBank, tableUses, index);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    void setBank (const std::vector<upic::Envelope>& tableBank,
                  const std::vector<int>& tableUses, int index)
    {
        bank = tableBank;
        uses = tableUses;
        sel = bank.empty() ? -1 : juce::jlimit (0, (int) bank.size() - 1, index);
        if (sel >= 0) nameEd.setText (bank[(size_t) sel].name, false);
        const bool canEdit = sel >= 0;
        for (auto* b : { &dupBtn, &delBtn, &renameBtn, &assignBtn }) b->setEnabled (canEdit);
        nameEd.setEnabled (canEdit);
        for (auto& b : presetBtn) b.setEnabled (canEdit);
        delBtn.setEnabled (canEdit && sel >= 0 && sel < (int) uses.size() && uses[(size_t) sel] == 0);
        repaint();
    }

    int selectedIndex() const { return sel; }
    bool isOpen() const { return isVisible(); }

    void close()
    {
        dragging = false;
        dragPts.clear();
        setVisible (false);
        if (onClose) onClose();
    }

    //--------------------------------------------------------------------------
    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced (10);
        return juce::Rectangle<int> (juce::jmin (780, r.getWidth()),
                                     juce::jmin (570, r.getHeight())).withCentre (r.getCentre());
    }

    juce::Rectangle<int> listRect() const
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        inner.removeFromTop (libraryH);
        inner.removeFromBottom (bottomH);
        return inner.removeFromLeft (listW);
    }

    juce::Rectangle<float> plotRect() const
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        inner.removeFromTop (libraryH);
        inner.removeFromBottom (bottomH);
        inner.removeFromLeft (listW + 16);
        inner.removeFromBottom (presetH);
        return inner.toFloat();
    }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xcc000000));
        auto card = cardRect();
        g.setColour (juce::Colour (0xff1e1e26));
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc8c8d0));
        g.drawText ("Amplitude tables", card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        // --- the bank list ---
        auto list = listRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (list);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRect (list, 1);
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            auto row = juce::Rectangle<int> (list.getX() + 2, list.getY() + 2 + i * rowH,
                                             list.getWidth() - 4, rowH);
            if (! list.contains (row)) break;
            if (i == sel)
            {
                g.setColour (juce::Colour (0xff2c3e58));
                g.fillRect (row);
            }
            g.setColour (bank[(size_t) i].colour);
            g.fillRect (row.removeFromLeft (10).reduced (2, 5));
            g.setColour (juce::Colour (0xffd0d0d8));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            const juce::String u = (i < (int) uses.size() && uses[(size_t) i] > 0)
                                       ? "  (" + juce::String (uses[(size_t) i]) + ")"
                                       : juce::String();
            g.drawText (bank[(size_t) i].name + u, row.reduced (6, 0),
                        juce::Justification::centredLeft, true);
        }

        // --- the transfer curve ---
        const auto plot = plotRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (plot);
        g.setColour (juce::Colour (0xff2a2a36));
        for (int q = 1; q < 4; ++q)
        {
            const float x = plot.getX() + plot.getWidth() * (float) q / 4.0f;
            const float y = plot.getY() + plot.getHeight() * (float) q / 4.0f;
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        }
        g.setColour (juce::Colour (0xff3a4a6a));
        g.drawRect (plot, 1.0f);

        // the identity diagonal (what "no table" means)
        g.setColour (juce::Colour (0x506a7a96));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

        // the curve: from the selected table, or the drag preview
        const std::vector<upic::EnvPoint>* pts = nullptr;
        if (dragging) pts = &dragPts;
        else if (sel >= 0 && sel < (int) bank.size()) pts = &bank[(size_t) sel].points;
        if (pts != nullptr && pts->size() >= 2)
        {
            juce::Path path;
            path.startNewSubPath (xFor ((*pts)[0].x), yFor ((*pts)[0].y));
            for (size_t i = 1; i < pts->size(); ++i)
                path.lineTo (xFor ((*pts)[i].x), yFor ((*pts)[i].y));
            g.setColour (juce::Colour (0xff5ac8fa));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }

        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("envelope output ->", juce::Rectangle<float> (plot.getX(),
                                                                  plot.getBottom() + 2.0f,
                                                                  plot.getWidth() * 0.5f, 12.0f),
                    juce::Justification::centredLeft);
        g.drawText ("gain", juce::Rectangle<float> (plot.getX() - 40.0f, plot.getY(),
                                                    38.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    void resized() override
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        library.setBounds (inner.removeFromTop (libraryH).reduced (0, 2));
        auto bottom = inner.removeFromBottom (bottomH).reduced (0, 4);
        auto list = inner.removeFromLeft (listW);
        list.removeFromTop ((int) bank.size() * rowH + 4);
        auto listBtns = list.removeFromTop (28);
        newBtn.setBounds (listBtns.removeFromLeft (58));
        listBtns.removeFromLeft (4);
        dupBtn.setBounds (listBtns.removeFromLeft (52));
        listBtns.removeFromLeft (4);
        delBtn.setBounds (listBtns.removeFromLeft (54));

        // bottom row: rename field + command, close at the right
        closeBtn.setBounds (bottom.removeFromRight (64));
        bottom.removeFromRight (8);
        renameBtn.setBounds (bottom.removeFromRight (64));
        nameEd.setBounds (bottom.removeFromLeft (juce::jmin (200, bottom.getWidth() - 80)));

        inner.removeFromLeft (16);
        auto presets = inner.removeFromBottom (presetH).reduced (0, 4);
        int w = juce::jmax (40, presets.getWidth() / 5 - 4);
        for (auto& b : presetBtn)
        {
            b.setBounds (presets.removeFromLeft (w));
            presets.removeFromLeft (4);
        }
        assignBtn.setBounds (inner.removeFromBottom (24).removeFromRight (150));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition()))
        {
            close();
            return;
        }
        auto list = listRect();
        if (list.contains (e.getPosition()))
        {
            const int i = (e.getPosition().y - list.getY() - 2) / rowH;
            if (i >= 0 && i < (int) bank.size())
            {
                sel = i;
                nameEd.setText (bank[(size_t) i].name, false);
                library.selectAsset (bank[(size_t) i].id);
                if (onSelectIndex) onSelectIndex (sel);
                const bool canEdit = true;
                for (auto* b : { &dupBtn, &delBtn, &renameBtn, &assignBtn }) b->setEnabled (canEdit);
                nameEd.setEnabled (canEdit);
                for (auto& b : presetBtn) b.setEnabled (canEdit);
                delBtn.setEnabled (sel < (int) uses.size() && uses[(size_t) sel] == 0);
                repaint();
            }
            return;
        }
        if (! plotRect().contains (e.position)) return;

        dragging = true;
        dragPts.clear();
        dragPts.push_back (pointAt (e.position));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto pt = pointAt (e.position);
        const float minStep = 3.0f / juce::jmax (1.0f, plotRect().getWidth());
        if (dragPts.empty() || std::abs (pt.x - dragPts.back().x) >= minStep)
            dragPts.push_back (pt);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (dragPts.size() >= 2)
            commitCurve (dragPts);
        dragPts.clear();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }

    // The selftest drives the gesture paths without pixels.
    void selectRowForTest (int i)
    {
        if (i < 0 || i >= (int) bank.size()) return;
        sel = i;
        nameEd.setText (bank[(size_t) i].name, false);
        library.selectAsset (bank[(size_t) i].id);
        if (onSelectIndex) onSelectIndex (sel);
        repaint();
    }
    void drawCurveForTest (const std::vector<upic::EnvPoint>& pts) { commitCurve (pts); }
    void newForTest() { if (onNew) onNew(); }
    void duplicateForTest() { if (onDuplicate) onDuplicate(); }
    void deleteForTest() { if (onDelete) onDelete(); }
    void renameForTest (const juce::String& name) { if (onRename) onRename (sel, name); }

private:
    static constexpr int titleH = 26;
    static constexpr int libraryH = 28;
    static constexpr int bottomH = 34;
    static constexpr int presetH = 30;
    static constexpr int listW = 210;
    static constexpr int rowH = 22;

    upic::EnvPoint pointAt (juce::Point<float> p) const
    {
        const auto plot = plotRect();
        upic::EnvPoint pt;
        pt.x = juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        pt.y = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - p.y) / juce::jmax (1.0f, plot.getHeight()));
        return pt;
    }

    float xFor (float x) const
    {
        const auto plot = plotRect();
        return plot.getX() + juce::jlimit (0.0f, 1.0f, x) * plot.getWidth();
    }

    float yFor (float y) const
    {
        const auto plot = plotRect();
        return plot.getBottom() - juce::jlimit (0.0f, 1.0f, y) * plot.getHeight();
    }

    void commitCurve (const std::vector<upic::EnvPoint>& pts)
    {
        if (sel < 0 || sel >= (int) bank.size()) return;
        if (onCurve) onCurve (sel, pts);
        bank[(size_t) sel].points = pts;
        repaint();
    }

    std::vector<upic::Envelope> bank;
    std::vector<int> uses;
    int sel = -1;
    std::vector<upic::EnvPoint> dragPts;
    bool dragging = false;

    juce::TextButton newBtn { "New" }, dupBtn { "Dup" }, delBtn { "Del" },
                     renameBtn { "Rename" }, assignBtn { "Use on sel." },
                     closeBtn { "Close" };
    juce::TextEditor nameEd;
    juce::TextButton presetBtn[5];
    juce::ComboBox libraryBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpTableEditor)
};
