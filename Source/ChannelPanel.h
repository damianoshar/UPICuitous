#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"
#include "OverlayCard.h"
#include "CardTheme.h"

//==============================================================================
// WP6.4: the mixer panel. The channel list (with the name, the level, the
// mute and the solo), the channel's gain curve across the page, the curve
// presets and the new / delete / close commands.
//
// It is an in-app overlay for the same reason the other editors are: Android
// has no closable dialog windows. Channel 0 is Main and is implicit, so a
// piece that never opens this panel keeps the legacy audio path.
//==============================================================================
class ChannelPanel : public juce::Component,
                    private juce::ScrollBar::Listener
{
public:
    std::function<void (int index)> onSelectIndex;
    std::function<void (int index, const juce::String&)> onRename;
    std::function<void (int index, bool muted, bool solo)> onMuteSolo;
    std::function<void (int index, float gain)> onGain;
    // A gain DRAG is one command: the gesture opens the undo transaction
    // (onGainBegin) and closes it (onGainEnd), so a drag is one undo step
    // instead of one per mouse-move tick.
    std::function<void (int index)> onGainBegin, onGainEnd;
    std::function<void (int index, const std::vector<upic::EnvPoint>&)> onCurve;
    std::function<void()> onNew, onDelete, onClose;

    ChannelPanel()
    {
        setName ("channelPanel");
        setWantsKeyboardFocus (true);

        for (auto* b : { &newBtn, &delBtn, &renameBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        newBtn.setName ("chNewBtn");       delBtn.setName ("chDelBtn");
        renameBtn.setName ("chRenameBtn"); closeBtn.setName ("chCloseBtn");
        newBtn.setTooltip ("Add a mixer channel");
        delBtn.setTooltip ("Delete the selected channel (its arcs return to Main)");
        renameBtn.setTooltip ("Rename the selected channel");
        closeBtn.setTooltip ("Close the mixer");

        newBtn.onClick = [this] { if (onNew) onNew(); };
        delBtn.onClick = [this] { if (onDelete) onDelete(); };
        renameBtn.onClick = [this] { commitRename(); };
        closeBtn.onClick = [this] { close(); };

        nameEd.setName ("chNameEd");
        nameEd.onReturnKey = [this] { commitRename(); };
        addAndMakeVisible (nameEd);

        muteBtn.setName ("chMuteBtn"); soloBtn.setName ("chSoloBtn");
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa04a4a));
        soloBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a8a4a));
        muteBtn.setTooltip ("Silence the channel");
        soloBtn.setTooltip ("Play only the soloed channel(s)");
        muteBtn.onClick = [this] { commitMuteSolo(); };
        soloBtn.onClick = [this] { commitMuteSolo(); };
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);

        gainSl.setName ("chGainSl");
        gainSl.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        gainSl.setRange (0.0, 2.0, 0.001);
        gainSl.setTooltip ("The channel's level");
        gainSl.onValueChange = [this]
        {
            if (sel < 0 || sel >= (int) shown.size()) return;
            if (onGain) onGain (sel, (float) gainSl.getValue());
        };
        gainSl.onDragStart = [this]
        {
            if (sel < 0 || sel >= (int) shown.size()) return;
            if (onGainBegin) onGainBegin (sel);
        };
        gainSl.onDragEnd = [this]
        {
            if (sel < 0 || sel >= (int) shown.size()) return;
            // The slider's notification is asynchronous, so the last tick of a
            // drag can arrive AFTER the drag ended. Committing the final
            // value here - inside the drag's transaction - means that trailing
            // notification finds the value already applied and cannot open a
            // second undo transaction.
            if (onGain) onGain (sel, (float) gainSl.getValue());
            if (onGainEnd) onGainEnd (sel);
        };
        addAndMakeVisible (gainSl);
        gainLab.setFont (juce::Font (juce::FontOptions (11.0f)));
        gainLab.setText ("Gain", juce::dontSendNotification);
        addAndMakeVisible (gainLab);

        for (auto* b : { &flatBtn, &riseBtn, &fallBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        flatBtn.setName ("chFlatBtn"); riseBtn.setName ("chRiseBtn"); fallBtn.setName ("chFallBtn");
        flatBtn.setTooltip ("A flat channel curve");
        riseBtn.setTooltip ("A curve rising across the page");
        fallBtn.setTooltip ("A curve falling across the page");
        flatBtn.onClick = [this]{ commitPreset (0); };
        riseBtn.onClick = [this]{ commitPreset (1); };
        fallBtn.onClick = [this]{ commitPreset (2); };

        // L3: the channel list scrolls. The piece format allows 16 mixer
        // channels and the list is shorter than that in a small window, so
        // without a scroll bar the channels below the fold could not be
        // reached at all - they could not even be selected.
        listScrollBar.setName ("chScrollBar");
        listScrollBar.setColour (juce::ScrollBar::backgroundColourId,
                                 juce::Colour (0xff14141a));
        listScrollBar.setColour (juce::ScrollBar::thumbColourId,
                                 juce::Colour (0xff5a6a8a));
        listScrollBar.setColour (juce::ScrollBar::trackColourId,
                                 juce::Colour (0xff24242c));
        listScrollBar.setSingleStepSize (1.0);
        listScrollBar.setAutoHide (true);
        listScrollBar.addListener (this);
        addAndMakeVisible (listScrollBar);

        setVisible (false);
    }

    // The toolbar slider theme (the slider is a child of the main component,
    // so the interface checks count it).
    void themeSliders()
    {
        gainSl.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
        gainSl.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
        gainSl.setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
        gainSl.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
        gainSl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
    }

    void setChannels (const std::vector<upic::Channel>& bank, int index)
    {
        shown = bank;
        sel = shown.empty() ? -1 : juce::jlimit (0, (int) shown.size() - 1, index);
        const bool on = sel >= 0;
        for (auto* b : { &delBtn, &renameBtn, &flatBtn, &riseBtn, &fallBtn }) b->setEnabled (on);
        nameEd.setEnabled (on);
        muteBtn.setEnabled (on);
        soloBtn.setEnabled (on);
        gainSl.setEnabled (on);
        if (on)
        {
            const auto& c = shown[(size_t) sel];
            nameEd.setText (c.name, false);
            muteBtn.setToggleState (c.muted, juce::dontSendNotification);
            soloBtn.setToggleState (c.solo, juce::dontSendNotification);
            gainSl.setValue (c.gain, juce::dontSendNotification);
        }
        // L3: the list's scroll position follows the bank (a shorter bank
        // cannot stay scrolled past its end) and the selected channel stays
        // in sight.
        listScroll = juce::jlimit (0, maxListScroll(), listScroll);
        ensureSelectionVisible();
        updateListScrollBar();
        repaint();
    }

    bool isOpen() const { return isVisible(); }
    void close()
    {
        setVisible (false);
        if (onClose) onClose();
    }

    juce::Rectangle<int> cardRect() const
    {
        // R5.11: the card's rectangle is the overlay shell's rule.
        const auto r = upic::overlay_card::cardFor (upic::overlay_card::Shell::Channel,
                                                    getWidth(), getHeight());
        return juce::Rectangle<int> (r.x, r.y, r.w, r.h);
    }
    // The channel column: the list, then the New / Delete row under it.
    juce::Rectangle<int> listColumn() const
    {
        auto inner = cardRect().reduced (upic::overlay_card::kPaddingX, upic::overlay_card::kPaddingY);
        inner.removeFromTop (titleH);
        inner.removeFromBottom (bottomH);
        return inner.removeFromLeft (listW);
    }

    // How many rows fit in the list (the paint starts the rows 2 px down).
    int visibleRows() const
    {
        auto l = listColumn();
        l.removeFromBottom (listButtonH + 4);
        return juce::jmax (1, (l.getHeight() - 2) / rowH);
    }

    // L3: does the list need a scroll bar?
    bool listOverflows() const { return (int) shown.size() > visibleRows(); }
    int maxListScroll() const { return juce::jmax (0, (int) shown.size() - visibleRows()); }

    // The rows' area. The scroll bar takes a slice off its right edge when
    // the list overflows.
    juce::Rectangle<int> listRect() const
    {
        auto l = listColumn();
        l.removeFromBottom (listButtonH + 4);
        if (listOverflows()) l.removeFromRight (scrollW);
        return l;
    }

    // The scroll bar's own area (the right edge of the channel column).
    juce::Rectangle<int> scrollBarRect() const
    {
        auto l = listColumn();
        l.removeFromBottom (listButtonH + 4);
        return l.removeFromRight (scrollW);
    }

    juce::Rectangle<float> plotRect() const
    {
        auto inner = cardRect().reduced (upic::overlay_card::kPaddingX, upic::overlay_card::kPaddingY);
        inner.removeFromTop (titleH);
        inner.removeFromBottom (bottomH + presetH);
        inner.removeFromLeft (listW + 16);
        inner.removeFromTop (56); // gain + mute/solo rows
        return inner.toFloat();
    }

    void paint (juce::Graphics& g) override
    {
        // R5.12: the card's frame is the theme's frame.
        g.fillAll (juce::Colour (upic::card_theme::kBackdrop));
        auto card = cardRect();
        g.setColour (juce::Colour (upic::card_theme::kFill));
        g.fillRoundedRectangle (card.toFloat(), upic::card_theme::kRadius);
        g.setColour (juce::Colour (upic::card_theme::kBorder));
        g.drawRoundedRectangle (card.toFloat().reduced (upic::card_theme::kBorderInset),
                                upic::card_theme::kRadius, upic::card_theme::kBorderWidth);

        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc8c8d0));
        g.drawText ("Mixer channels", card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        auto list = listRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (list);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRect (list, 1);
        for (int i = listScroll; i < (int) shown.size(); ++i)
        {
            // L3: the first visible row is the one the list is scrolled to.
            auto row = juce::Rectangle<int> (list.getX() + 2,
                                             list.getY() + 2 + (i - listScroll) * rowH,
                                             list.getWidth() - 4, rowH);
            if (! list.contains (row)) break;
            const auto& c = shown[(size_t) i];
            if (i == sel) { g.setColour (juce::Colour (0xff2c3e58)); g.fillRect (row); }
            g.setColour (c.colour);
            g.fillRect (row.removeFromLeft (10).reduced (2, 5));
            g.setColour (juce::Colour (0xffd0d0d8));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            juce::String t = c.name;
            if (c.muted) t += " M";
            if (c.solo) t += " S";
            g.drawText (t, row.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        if (shown.empty())
        {
            g.setColour (juce::Colour (0xff7a7a88));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("No channels. Arcs play on Main.\nPress New to add one.",
                              list.reduced (10), juce::Justification::centred, 3);
        }

        const auto plot = plotRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (plot);
        g.setColour (juce::Colour (0xff3a4a6a));
        g.drawRect (plot, 1.0f);
        g.setColour (juce::Colour (0xff2a2a36));
        for (int q = 1; q < 4; ++q)
            g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * (float) q / 4.0f),
                                  plot.getX(), plot.getRight());
        const std::vector<upic::EnvPoint>* pts = nullptr;
        if (dragging) pts = &dragPts;
        else if (sel >= 0 && sel < (int) shown.size() && ! shown[(size_t) sel].envelope.empty())
            pts = &shown[(size_t) sel].envelope;
        if (pts != nullptr)
        {
            juce::Path path;
            path.startNewSubPath (xFor ((*pts)[0].x), yFor ((*pts)[0].y));
            for (size_t i = 1; i < pts->size(); ++i)
                path.lineTo (xFor ((*pts)[i].x), yFor ((*pts)[i].y));
            g.setColour (juce::Colour (0xff5ac8fa));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
        else
        {
            g.setColour (juce::Colour (0x605ac8fa));
            g.drawHorizontalLine ((int) yFor (1.0f), plot.getX(), plot.getRight());
        }
        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("channel gain curve (draw, or use a preset)",
                    juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                            plot.getWidth(), 12.0f),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto inner = cardRect().reduced (upic::overlay_card::kPaddingX, upic::overlay_card::kPaddingY);
        inner.removeFromTop (titleH);
        auto bottom = inner.removeFromBottom (bottomH).reduced (0, 4);
        inner.removeFromLeft (listW + 16); // the channel column and the gap

        // L3: the New / Delete row sits at the BOTTOM of the channel column,
        // so it stays where it is however many channels the bank has (it
        // used to follow the last row and could leave the card).
        auto lrow = listColumn().removeFromBottom (listButtonH);
        newBtn.setBounds (lrow.removeFromLeft (58));
        lrow.removeFromLeft (4);
        delBtn.setBounds (lrow.removeFromLeft (54));

        // the scroll bar (it hides itself when the list fits)
        listScroll = juce::jlimit (0, maxListScroll(), listScroll);
        listScrollBar.setBounds (scrollBarRect());
        updateListScrollBar();

        closeBtn.setBounds (bottom.removeFromRight (64));
        bottom.removeFromRight (8);
        renameBtn.setBounds (bottom.removeFromRight (66));
        nameEd.setBounds (bottom.removeFromLeft (juce::jmin (180, bottom.getWidth() - 80)));

        auto gainRow = inner.removeFromTop (28);
        gainLab.setBounds (gainRow.removeFromLeft (44));
        gainSl.setBounds (gainRow.removeFromLeft (juce::jmin (260, gainRow.getWidth())));
        auto ms = inner.removeFromTop (28);
        muteBtn.setBounds (ms.removeFromLeft (60));
        ms.removeFromLeft (6);
        soloBtn.setBounds (ms.removeFromLeft (60));
        auto presets = inner.removeFromBottom (presetH).reduced (0, 4);
        flatBtn.setBounds (presets.removeFromLeft (56));
        presets.removeFromLeft (6);
        riseBtn.setBounds (presets.removeFromLeft (84));
        presets.removeFromLeft (6);
        fallBtn.setBounds (presets.removeFromLeft (84));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition())) { close(); return; }
        auto list = listRect();
        if (list.contains (e.getPosition()))
        {
            // L3: the row under the pointer is counted from the list's
            // scroll position, so a scrolled list selects the row that is
            // really there.
            const int i = listScroll
                        + (e.getPosition().y - list.getY() - 2) / rowH;
            if (i >= 0 && i < (int) shown.size())
            {
                sel = i;
                if (onSelectIndex) onSelectIndex (sel);
                setChannels (shown, sel);
            }
            return;
        }
        if (plotRect().contains (e.position) && sel >= 0)
        {
            dragging = true;
            dragPts = shown[(size_t) sel].envelope;
            pushCurvePoint (pointAt (e.position));
            repaint();
        }
    }

    // L3: the wheel scrolls the list. JUCE reports a wheel notch as a
    // fraction (about 0.23 on Windows), so the deltas are accumulated.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override
    {
        if (! listOverflows()) return;
        if (! listRect().contains (e.getPosition())) return;
        wheelDelta += d.deltaY * 4.5f;
        const int steps = (int) wheelDelta;
        if (steps == 0) return;
        wheelDelta -= (float) steps;
        scrollListTo (listScroll - steps);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || sel < 0 || sel >= (int) shown.size()) return;
        pushCurvePoint (pointAt (e.position));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (sel < 0 || sel >= (int) shown.size()) { dragPts.clear(); return; }
        if (dragPts.size() >= 2 && onCurve) onCurve (sel, dragPts);
        shown[(size_t) sel].envelope = dragPts;
        dragPts.clear();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    // The selftest drives the commands without pixels.
    void newForTest() { if (onNew) onNew(); }
    void curveForTest (const std::vector<upic::EnvPoint>& pts)
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        // the same cap the drawn curve obeys
        std::vector<upic::EnvPoint> capped = pts;
        if ((int) capped.size() > maxCurvePoints)
            capped.resize ((size_t) maxCurvePoints);
        if (onCurve) onCurve (sel, capped);
        shown[(size_t) sel].envelope = capped;
        repaint();
    }
    void deleteForTest() { if (onDelete) onDelete(); }
    void gainDragStartForTest()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        if (onGainBegin) onGainBegin (sel);
    }
    void gainDragEndForTest()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        if (onGain) onGain (sel, (float) gainSl.getValue());
        if (onGainEnd) onGainEnd (sel);
    }
    void selectForTest (int i)
    {
        if (i < 0 || i >= (int) shown.size()) return;
        sel = i;
        if (onSelectIndex) onSelectIndex (i);
        ensureSelectionVisible();
        repaint();
    }
    // L3: the selftest checks that the list really scrolls.
    int listScrollForTest() const { return listScroll; }
    int visibleRowsForTest() const { return visibleRows(); }
    void scrollListToForTest (int row) { scrollListTo (row); }
    void muteForTest (bool on) { muteBtn.setToggleState (on, juce::sendNotificationSync); }
    void soloForTest (bool on) { soloBtn.setToggleState (on, juce::sendNotificationSync); }
    void gainForTest (float v) { gainSl.setValue (v, juce::sendNotificationSync); }
    void renameForTest (const juce::String& n) { nameEd.setText (n, false); commitRename(); }

private:
    static constexpr int titleH = 26;
    static constexpr int bottomH = 34;
    static constexpr int listW = 220;
    static constexpr int rowH = 22;
    static constexpr int presetH = 30;
    // L3: the New / Delete row under the channel list, and the scroll bar's
    // width.
    static constexpr int listButtonH = 28;
    static constexpr int scrollW = 12;
    // The piece format's cap for an envelope's points. The drawn channel
    // curve is an envelope, so it obeys the same cap.
    static constexpr int maxCurvePoints = upic::limits::kMaxPointsPerEnvelope;

    // L3: the scroll bar and the list's scroll position (the first visible
    // row).
    juce::ScrollBar listScrollBar { true };
    int listScroll = 0;
    float wheelDelta = 0.0f;   // the wheel's not-yet-applied fraction

    // L3: the scroll bar moved - the list follows it.
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override
    {
        listScroll = juce::jlimit (0, maxListScroll(), (int) newRangeStart);
        repaint();
    }

    // Move the list's scroll position (and the scroll bar with it).
    void scrollListTo (int row)
    {
        const int target = juce::jlimit (0, maxListScroll(), row);
        if (target == listScroll) return;
        listScroll = target;
        updateListScrollBar();
        repaint();
    }

    // Keep the selected channel in sight.
    void ensureSelectionVisible()
    {
        if (sel < 0) return;
        if (sel < listScroll) scrollListTo (sel);
        else if (sel >= listScroll + visibleRows()) scrollListTo (sel - visibleRows() + 1);
    }

    // The scroll bar's range follows the bank and the panel's height.
    void updateListScrollBar()
    {
        listScrollBar.setRangeLimits (0.0, (double) shown.size());
        listScrollBar.setCurrentRange ((double) listScroll,
                                       (double) visibleRows(),
                                       juce::dontSendNotification);
        listScrollBar.setVisible (true);   // it hides itself when it fits
    }

    void commitRename()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        const auto n = nameEd.getText().trim();
        if (n.isEmpty()) return;
        if (onRename) onRename (sel, n);
    }
    void commitMuteSolo()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        if (onMuteSolo) onMuteSolo (sel, muteBtn.getToggleState(), soloBtn.getToggleState());
    }

    upic::EnvPoint pointAt (juce::Point<float> p) const
    {
        const auto plot = plotRect();
        upic::EnvPoint pt;
        pt.x = juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        pt.y = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - p.y) / juce::jmax (1.0f, plot.getHeight()));
        return pt;
    }
    // The curve's point count is capped (the piece format's limit - the
    // loader truncates a longer curve, so a curve that grew past the cap
    // would silently change when the piece was reloaded). The cap is kept by
    // DECIMATING: every second point is dropped, so the curve keeps its shape
    // instead of being cut off at the cap.
    void pushCurvePoint (const upic::EnvPoint& pt)
    {
        if ((int) dragPts.size() >= maxCurvePoints)
        {
            std::vector<upic::EnvPoint> kept;
            kept.reserve (dragPts.size() / 2 + 2);
            for (size_t i = 0; i < dragPts.size(); i += 2)
                kept.push_back (dragPts[i]);
            dragPts.swap (kept);
        }
        dragPts.push_back (pt);
        std::sort (dragPts.begin(), dragPts.end(),
                   [] (const upic::EnvPoint& a, const upic::EnvPoint& b) { return a.x < b.x; });
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
    void commitPreset (int p)
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        std::vector<upic::EnvPoint> pts;
        if (p == 1)      pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
        else if (p == 2) pts = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
        if (onCurve) onCurve (sel, pts);
        shown[(size_t) sel].envelope = pts;
        repaint();
    }
    std::vector<upic::Channel> shown;
    int sel = -1;

    juce::TextButton newBtn { "New" }, delBtn { "Del" }, renameBtn { "Rename" },
                     closeBtn { "Close" }, muteBtn { "Mute" }, soloBtn { "Solo" },
                     flatBtn { "Flat" }, riseBtn { "Rise" }, fallBtn { "Fall" };
    std::vector<upic::EnvPoint> dragPts;
    bool dragging = false;
    juce::TextEditor nameEd;
    juce::Slider gainSl;
    juce::Label gainLab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPanel)
};