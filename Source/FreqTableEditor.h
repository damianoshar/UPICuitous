#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"

//==============================================================================
// WP1: the drawn frequency-table editor (the New UPIC's editable conversion
// tables). The editor is an in-app overlay for the same reason the audio and
// MIDI settings are: Android has no closable dialog windows.
//
// It edits ONE of the page's four spaces. The page's vertical axis is the
// x axis (left = bottom of the drawing, right = top), and the frequency the
// position sounds at is the y axis, on a log scale across the page's ambitus.
//
// Editing is committed on mouse-up (never during the drag), so the model is
// only touched once per gesture - the host wraps that commit in an undo
// transaction and republishes the snapshot. The audio thread keeps rendering
// the old table until the commit, which is exactly the behaviour a drawn
// curve wants.
//==============================================================================
class FreqTableEditor : public juce::Component
{
public:
    // The host applies these to the model (under the model lock) and wraps
    // the call in its undo transaction.
    std::function<void (const std::vector<upic::TablePoint>&)> onCommitCurve;
    std::function<void (bool inverted, bool stepped, int divisions)> onCommitFlags;
    std::function<void()> onReset;
    std::function<void()> onClose;

    FreqTableEditor()
    {
        setName ("freqTableEd");
        setWantsKeyboardFocus (true);

        for (auto* b : { &lineBtn, &drawBtn, &invertBtn, &stepBtn, &resetBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a6a9a));
            addAndMakeVisible (b);
        }
        lineBtn.setName ("ftLineBtn");   drawBtn.setName ("ftDrawBtn");
        invertBtn.setName ("ftInvertBtn"); stepBtn.setName ("ftStepBtn");
        resetBtn.setName ("ftResetBtn"); closeBtn.setName ("ftCloseBtn");

        lineBtn.setClickingTogglesState (true);
        lineBtn.setToggleState (true, juce::dontSendNotification);
        lineBtn.setTooltip ("Drag a straight line from the bottom to the top of the page");
        drawBtn.setClickingTogglesState (true);
        drawBtn.setTooltip ("Draw the curve freehand (one stroke replaces the table)");
        invertBtn.setClickingTogglesState (true);
        invertBtn.setTooltip ("Mirror the drawn table top to bottom");
        stepBtn.setClickingTogglesState (true);
        stepBtn.setTooltip ("Quantise the table to equal divisions per octave");
        resetBtn.setTooltip ("Back to the page's plain mapping (the preset)");
        closeBtn.setTooltip ("Close the table editor");

        lineBtn.onClick = [this] { drawBtn.setToggleState (! lineBtn.getToggleState(),
                                                          juce::dontSendNotification); };
        drawBtn.onClick = [this] { lineBtn.setToggleState (! drawBtn.getToggleState(),
                                                          juce::dontSendNotification); };
        invertBtn.onClick = [this] { commitFlags(); };
        stepBtn.onClick   = [this] { commitFlags(); };
        cutBox.onChange   = [this] { commitFlags(); };
        resetBtn.onClick  = [this] { if (onReset) onReset(); };
        closeBtn.onClick  = [this] { close(); };

        cutBox.setName ("ftCutBox");
        cutBox.setTooltip ("The table's staircase: equal divisions per octave");
        addAndMakeVisible (cutBox);
        int common[] = { 1, 2, 3, 4, 6, 7, 8, 12, 16, 19, 24, 31, 48, 53, 72, 96 };
        for (int d : common) cutBox.addItem (juce::String (d) + " / oct", d);
        cutBox.setSelectedId (12, juce::dontSendNotification);

        setVisible (false);
    }

    //--------------------------------------------------------------------------
    void open (const upic::FreqSpaceDef& def, double ambLo, double ambHi,
               const juce::String& titleText)
    {
        shownDef = def;
        ambLoHz = juce::jmax (1.0e-3, ambLo);
        ambHiHz = juce::jmax (ambLoHz * 1.001, ambHi);
        title = titleText;
        dragging = false;
        dragPts.clear();
        invertBtn.setToggleState (shownDef.inverted, juce::dontSendNotification);
        stepBtn.setToggleState (shownDef.stepped, juce::dontSendNotification);
        cutBox.setSelectedId (juce::jlimit (1, 99, shownDef.stepDivisions > 0
                                                     ? shownDef.stepDivisions : 12),
                              juce::dontSendNotification);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

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
        return juce::Rectangle<int> (juce::jmin (760, r.getWidth()),
                                     juce::jmin (560, r.getHeight())).withCentre (r.getCentre());
    }

    juce::Rectangle<float> plotRect() const
    {
        auto card = cardRect().reduced (18, 12);
        card.removeFromTop (titleH);
        card.removeFromBottom (buttonH);
        return card.toFloat();
    }

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
        g.drawText (title, card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        const auto plot = plotRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (plot);

        // octave grid + labels (the lines are frequencies, so they are the
        // real pitch landmarks the drawing is mapped to)
        const double loLog = std::log2 (ambLoHz), hiLog = std::log2 (ambHiHz);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int oct = (int) std::ceil (loLog); oct <= (int) std::floor (hiLog); ++oct)
        {
            const float yy = yForLog2 ((double) oct);
            g.setColour (juce::Colour (0xff2a2a36));
            g.drawHorizontalLine ((int) yy, plot.getX(), plot.getRight());
            g.setColour (juce::Colour (0xff7a7a88));
            g.drawText (juce::String (std::lround (std::exp2 ((double) oct))) + " Hz",
                        juce::Rectangle<float> (plot.getX() + 4.0f, yy - 11.0f, 70.0f, 10.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (juce::Colour (0xff2a2a36));
        for (int q = 0; q <= 4; ++q)
        {
            const float xx = plot.getX() + plot.getWidth() * (float) q / 4.0f;
            g.drawVerticalLine ((int) xx, plot.getY(), plot.getBottom());
        }

        // the space's frequency range, so the user sees where the table lands
        double sLo = 0.0, sHi = 0.0;
        upic::freqSpaceRange (ambLoHz, ambHiHz, juce::jlimit (0, 6, shownDef.preset), sLo, sHi);
        g.setColour (juce::Colour (0xff3a4a6a));
        g.drawRect (plot, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colour (0xff7a8aa8));
        g.drawText (juce::String (sLo, 1) + " - " + juce::String (sHi, 1) + " Hz",
                    juce::Rectangle<float> (plot.getRight() - 130.0f, plot.getBottom() + 2.0f,
                                            126.0f, 12.0f),
                    juce::Justification::centredRight);

        // the curve (the drag preview wins while a gesture is in flight)
        const std::vector<upic::TablePoint>& pts = dragging ? dragPts : shownDef.points;
        juce::Path path;
        const bool usePts = pts.size() >= 2;
        if (usePts)
        {
            path.startNewSubPath (xForNorm (pts.front().x), yForLog2 (pts.front().log2Hz));
            for (size_t i = 1; i < pts.size(); ++i)
                path.lineTo (xForNorm (pts[i].x), yForLog2 (pts[i].log2Hz));
        }
        else
        {
            // no drawn table yet: show the preset's straight line as a ghost
            path.startNewSubPath (xForNorm (0.0f), yForLog2 (std::log2 (sLo)));
            path.lineTo (xForNorm (1.0f), yForLog2 (std::log2 (sHi)));
        }
        g.setColour (usePts || dragging ? juce::Colour (0xff5ac8fa) : juce::Colour (0x805ac8fa));
        g.strokePath (path, juce::PathStrokeType (2.0f));

        if (shownDef.inverted || invertBtn.getToggleState())
        {
            g.setColour (juce::Colour (0xffc07830));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText ("inverted", plot.reduced (6.0f).removeFromBottom (14.0f),
                        juce::Justification::centredRight);
        }

        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("drawing position  (bottom -> top of the page)",
                    juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                            plot.getWidth() * 0.5f, 12.0f),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto card = cardRect().reduced (18, 12);
        card.removeFromTop (titleH);
        auto row = card.removeFromBottom (buttonH).reduced (0, 4);
        const int h = row.getHeight();
        lineBtn.setBounds (row.removeFromLeft (58).withHeight (h));
        row.removeFromLeft (4);
        drawBtn.setBounds (row.removeFromLeft (58).withHeight (h));
        row.removeFromLeft (12);
        invertBtn.setBounds (row.removeFromLeft (66).withHeight (h));
        row.removeFromLeft (4);
        stepBtn.setBounds (row.removeFromLeft (54).withHeight (h));
        row.removeFromLeft (4);
        cutBox.setBounds (row.removeFromLeft (78).withHeight (h));
        closeBtn.setBounds (row.removeFromRight (64).withHeight (h));
        row.removeFromRight (8);
        resetBtn.setBounds (row.removeFromRight (64).withHeight (h));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition()))
        {
            if (onClose) close();
            return;
        }
        if (! plotRect().contains (e.position)) return;

        dragging = true;
        dragPts.clear();
        if (lineBtn.getToggleState())
            anchor = e.position;
        dragPts.push_back (pointAt (e.position));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto p = e.position;
        if (lineBtn.getToggleState())
        {
            dragPts.clear();
            dragPts.push_back (pointAt (anchor));
            dragPts.push_back (pointAt (p));
        }
        else
        {
            const auto pt = pointAt (p);
            // one point per ~3 px, so a freehand stroke stays a compact table
            const float minStep = 3.0f / juce::jmax (1.0f, plotRect().getWidth());
            if (dragPts.empty() || std::abs (pt.x - dragPts.back().x) >= minStep)
                dragPts.push_back (pt);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        // a click without a drag (or a degenerate line) leaves the table alone
        if (dragPts.size() >= 2 && std::abs (dragPts.back().x - dragPts.front().x) > 1.0e-4f)
        {
            if (onCommitCurve) onCommitCurve (dragPts);
            shownDef.points = dragPts;
            shownDef.editable = true;
        }
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

    // The selftest drives the commit paths without mouse pixels.
    void commitCurveForTest (const std::vector<upic::TablePoint>& pts)
    {
        if (onCommitCurve) onCommitCurve (pts);
        shownDef.points = pts;
        shownDef.editable = true;
        repaint();
    }
    void commitFlagsForTest()
    {
        commitFlags();
    }

private:
    static constexpr int titleH = 26;
    static constexpr int buttonH = 36;

    upic::TablePoint pointAt (juce::Point<float> p) const
    {
        const auto plot = plotRect();
        const float nx = juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        const float ny = juce::jlimit (0.0f, 1.0f, (p.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()));
        const double loLog = std::log2 (ambLoHz), hiLog = std::log2 (ambHiHz);
        upic::TablePoint tp;
        tp.x = nx;
        tp.log2Hz = (float) (hiLog - (double) ny * (hiLog - loLog));
        return tp;
    }

    float xForNorm (float n) const
    {
        const auto plot = plotRect();
        return plot.getX() + juce::jlimit (0.0f, 1.0f, n) * plot.getWidth();
    }

    float yForLog2 (double l) const
    {
        const auto plot = plotRect();
        const double loLog = std::log2 (ambLoHz), hiLog = std::log2 (ambHiHz);
        const double n = juce::jlimit (0.0, 1.0, (hiLog - l) / juce::jmax (1.0e-6, hiLog - loLog));
        return plot.getY() + (float) n * plot.getHeight();
    }

    void commitFlags()
    {
        shownDef.inverted = invertBtn.getToggleState();
        shownDef.stepped  = stepBtn.getToggleState();
        shownDef.stepDivisions = stepBtn.getToggleState()
                                     ? juce::jlimit (1, 99, cutBox.getSelectedId())
                                     : 0;
        if (onCommitFlags)
            onCommitFlags (shownDef.inverted, shownDef.stepped, shownDef.stepDivisions);
        repaint();
    }

    juce::String title { "Frequency table" };
    upic::FreqSpaceDef shownDef;
    double ambLoHz = 20.0, ambHiHz = 12000.0;
    std::vector<upic::TablePoint> dragPts;
    juce::Point<float> anchor;
    bool dragging = false;

    juce::TextButton lineBtn { "Line" }, drawBtn { "Draw" },
                     invertBtn { "Invert" }, stepBtn { "Step" },
                     resetBtn { "Reset" }, closeBtn { "Close" };
    juce::ComboBox cutBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreqTableEditor)
};
