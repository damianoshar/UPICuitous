#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "SequenceTransport.h"

//==============================================================================
// WP8: the performance view.
//
// The UPIC's page was a surface for a performance: the player sat in front of
// it and moved through it. The score editor is a drawing surface, so the
// things a performer needs - the page selector, the groups' mute / solo /
// gain / transpose, the scrub, reverse, tempo and loop switches and the
// sequence recorder - were scattered over the toolbar, and the path the
// playhead had taken was never shown.
//
// This view gathers them into one screen.
//
// It is a VIEW, not a second transport. The view owns no clock and no
// transport state: every switch calls back into the app, the app runs the
// very same command the toolbar's switch runs, and the app pushes the state
// back into the view. There is one transport in the app, and this is a
// window onto it.
//==============================================================================
class PerformanceView : public juce::Component
{
public:
    // A group as the view shows it: the name and the four performance
    // attributes. The view never edits the model itself.
    struct GroupRow
    {
        juce::String name;
        bool muted = false;
        bool solo = false;
        float gain = 1.0f;
        double transpose = 0.0;
    };

    // --- the commands (the app runs them) ---
    std::function<void (int pageIndex)> onPageChanged;
    std::function<void (int groupIndex, bool muted, bool solo)> onGroupMuteSolo;
    std::function<void (int groupIndex)> onGroupLevelsBegin;
    std::function<void (int groupIndex, float gain, double transpose)> onGroupLevels;
    std::function<void (int groupIndex)> onGroupLevelsEnd;
    std::function<void (bool)> onScrubToggle;
    std::function<void (bool)> onReverseToggle;
    std::function<void (double bpm)> onTempoChange;
    std::function<void (bool)> onLoopToggle;
    std::function<void()> onSequenceRecord, onSequencePlay, onSequenceLoop;
    std::function<void()> onClose;

    //==========================================================================
    PerformanceView()
    {
        setName ("performanceView");
        setWantsKeyboardFocus (true);

        // --- the page selector ---
        pageLab.setText ("Page", juce::dontSendNotification);
        pageLab.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        pageLab.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (pageLab);
        pageBox.setName ("perfPageBox");
        pageBox.setTooltip ("The page the performance is on");
        pageBox.onChange = [this]
        {
            const int id = pageBox.getSelectedId();
            if (id >= 1 && onPageChanged) onPageChanged (id - 1);
        };
        addAndMakeVisible (pageBox);

        // --- the transport switches (the app's own switches) ---
        scrubToggle.setName ("perfScrubToggle");
        scrubToggle.setButtonText ("Scrub");
        scrubToggle.setClickingTogglesState (true);
        scrubToggle.setTooltip ("Audition the score while you scrub it");
        scrubToggle.onClick = [this]
        {
            if (onScrubToggle) onScrubToggle (scrubToggle.getToggleState());
        };
        addAndMakeVisible (scrubToggle);

        reverseToggle.setName ("perfReverseToggle");
        reverseToggle.setButtonText ("Reverse");
        reverseToggle.setClickingTogglesState (true);
        reverseToggle.setTooltip ("Play the page backwards");
        reverseToggle.onClick = [this]
        {
            if (onReverseToggle) onReverseToggle (reverseToggle.getToggleState());
        };
        addAndMakeVisible (reverseToggle);

        loopToggle.setName ("perfLoopToggle");
        loopToggle.setButtonText ("Loop");
        loopToggle.setClickingTogglesState (true);
        loopToggle.setTooltip ("Loop the page");
        loopToggle.onClick = [this]
        {
            if (onLoopToggle) onLoopToggle (loopToggle.getToggleState());
        };
        addAndMakeVisible (loopToggle);

        tempoLab.setText ("Tempo", juce::dontSendNotification);
        tempoLab.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        tempoLab.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (tempoLab);
        tempoSlider.setName ("perfTempoSlider");
        tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        tempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        tempoSlider.setRange (upic::sequence_transport::kMinTempoBpm,
                              upic::sequence_transport::kMaxTempoBpm,
                              upic::sequence_transport::kTempoIntervalBpm);
        tempoSlider.setSkewFactor (upic::sequence_transport::kTempoSkew);
        tempoSlider.setTooltip (upic::sequence_transport::tempoTooltip());
        tempoSlider.onValueChange = [this]
        {
            if (onTempoChange) onTempoChange (tempoSlider.getValue());
        };
        themeSlider (tempoSlider);
        addAndMakeVisible (tempoSlider);

        // --- the sequence (the app's own recorder) ---
        recBtn.setName ("perfRecBtn");
        playSeqBtn.setName ("perfPlayBtn");
        loopSeqBtn.setName ("perfLoopSeqBtn");
        // The sequence transport's wording (R5.8): the view and the card say
        // the same thing.
        recBtn.setTooltip (upic::sequence_transport::tooltip (upic::sequence_transport::Control::Record));
        playSeqBtn.setTooltip (upic::sequence_transport::tooltip (upic::sequence_transport::Control::Play));
        loopSeqBtn.setTooltip (upic::sequence_transport::tooltip (upic::sequence_transport::Control::Loop));
        loopSeqBtn.setClickingTogglesState (true);
        for (auto* b : { &recBtn, &playSeqBtn, &loopSeqBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        recBtn.onClick = [this] { if (onSequenceRecord) onSequenceRecord(); };
        playSeqBtn.onClick = [this] { if (onSequencePlay) onSequencePlay(); };
        loopSeqBtn.onClick = [this] { if (onSequenceLoop) onSequenceLoop(); };

        // --- the groups ---
        groupViewport.setName ("perfGroupViewport");
        groupViewport.setScrollBarsShown (true, false);
        groupViewport.setViewedComponent (&groupList, false);
        addAndMakeVisible (groupViewport);
        groupList.setName ("perfGroupList");

        closeBtn.setName ("perfCloseBtn");
        closeBtn.setTooltip ("Close the performance view");
        closeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
        closeBtn.onClick = [this] { close(); };
        addAndMakeVisible (closeBtn);

        setVisible (false);
    }

    //==========================================================================
    void open()
    {
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    bool isOpen() const { return isVisible(); }

    void close()
    {
        setVisible (false);
        if (onClose) onClose();
    }

    //--- the state the app pushes into the view -----------------------------

    // The pages of the piece. The view draws one lane per page.
    void setPages (const std::vector<juce::String>& names,
                   const std::vector<double>& durations,
                   int current)
    {
        pageDurations = durations;
        currentPage = current;
        // The combo is rebuilt only when the page COUNT changes: rebuilding
        // it while it is handling its own change event would be a mess.
        if ((int) shownPages.size() != (int) names.size())
        {
            shownPages = names;
            pageBox.clear (juce::dontSendNotification);
            for (int i = 0; i < (int) shownPages.size(); ++i)
                pageBox.addItem (shownPages[(size_t) i], i + 1);
        }
        pageBox.setSelectedId (juce::jlimit (0, (int) shownPages.size() - 1, current) + 1,
                               juce::dontSendNotification);
        repaint();
    }

    // The groups of the current page.
    void setGroups (const std::vector<GroupRow>& rows)
    {
        while ((int) groupRows.size() > (int) rows.size())
            groupRows.pop_back();

        while ((int) groupRows.size() < (int) rows.size())
        {
            const int index = (int) groupRows.size();
            auto w = std::make_unique<GroupWidgets>();
            w->name.setFont (juce::Font (juce::FontOptions (11.0f)));
            w->name.setJustificationType (juce::Justification::centredLeft);
            groupList.addAndMakeVisible (w->name);

            w->mute.setName ("perfGroupMuteBtn" + juce::String (index));
            w->mute.setButtonText ("M");
            w->mute.setTooltip ("Silence the group");
            w->mute.onClick = [this, index]
            {
                if (! onGroupMuteSolo || index >= (int) groupRows.size()) return;
                onGroupMuteSolo (index,
                                 groupRows[(size_t) index]->mute.getToggleState(),
                                 groupRows[(size_t) index]->solo.getToggleState());
            };
            groupList.addAndMakeVisible (w->mute);

            w->solo.setName ("perfGroupSoloBtn" + juce::String (index));
            w->solo.setButtonText ("S");
            w->solo.setTooltip ("Play only this group");
            w->solo.onClick = [this, index]
            {
                if (! onGroupMuteSolo || index >= (int) groupRows.size()) return;
                onGroupMuteSolo (index,
                                 groupRows[(size_t) index]->mute.getToggleState(),
                                 groupRows[(size_t) index]->solo.getToggleState());
            };
            groupList.addAndMakeVisible (w->solo);

            w->gain.setName ("perfGroupGainSlider" + juce::String (index));
            w->gain.setSliderStyle (juce::Slider::LinearHorizontal);
            w->gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
            w->gain.setRange (0.0, 2.0, 0.001);
            w->gain.setTooltip ("The group's level");
            w->gain.onDragStart = [this, index]
            {
                if (onGroupLevelsBegin) onGroupLevelsBegin (index);
            };
            w->gain.onValueChange = [this, index]
            {
                if (index >= (int) groupRows.size()) return;
                if (onGroupLevels)
                    onGroupLevels (index, (float) groupRows[(size_t) index]->gain.getValue(),
                                   groupRows[(size_t) index]->transpose.getValue());
            };
            w->gain.onDragEnd = [this, index]
            {
                if (onGroupLevelsEnd) onGroupLevelsEnd (index);
            };
            themeSlider (w->gain);
            groupList.addAndMakeVisible (w->gain);

            w->transpose.setName ("perfGroupTransposeSlider" + juce::String (index));
            w->transpose.setSliderStyle (juce::Slider::LinearHorizontal);
            w->transpose.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
            w->transpose.setRange (-48.0, 48.0, 0.1);
            w->transpose.setTooltip ("The group's transpose in semitones");
            w->transpose.onDragStart = [this, index]
            {
                if (onGroupLevelsBegin) onGroupLevelsBegin (index);
            };
            w->transpose.onValueChange = [this, index]
            {
                if (index >= (int) groupRows.size()) return;
                if (onGroupLevels)
                    onGroupLevels (index, (float) groupRows[(size_t) index]->gain.getValue(),
                                   groupRows[(size_t) index]->transpose.getValue());
            };
            w->transpose.onDragEnd = [this, index]
            {
                if (onGroupLevelsEnd) onGroupLevelsEnd (index);
            };
            themeSlider (w->transpose);
            groupList.addAndMakeVisible (w->transpose);

            groupRows.push_back (std::move (w));
        }

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto& w = *groupRows[(size_t) i];
            w.name.setText (rows[(size_t) i].name, juce::dontSendNotification);
            w.mute.setToggleState (rows[(size_t) i].muted, juce::dontSendNotification);
            w.solo.setToggleState (rows[(size_t) i].solo, juce::dontSendNotification);
            w.gain.setValue (rows[(size_t) i].gain, juce::dontSendNotification);
            w.transpose.setValue (rows[(size_t) i].transpose, juce::dontSendNotification);
        }
        layoutGroups();
    }

    // The transport's state, as the app sees it.
    void setTransportState (bool playingNow, bool recordingNow, bool seqPlayingNow,
                            bool loopNow, bool scrubNow, bool reverseNow,
                            double tempoBpm, double playheadSecNow)
    {
        // A new run starts a new trajectory.
        if (playingNow && ! wasPlaying)
            trail.clear();
        wasPlaying = playingNow;

        playing = playingNow;
        recording = recordingNow;
        seqPlaying = seqPlayingNow;
        loop = loopNow;
        scrub = scrubNow;
        reverse = reverseNow;
        playheadSec = playheadSecNow;

        if (scrubToggle.getToggleState() != scrub)
            scrubToggle.setToggleState (scrub, juce::dontSendNotification);
        if (reverseToggle.getToggleState() != reverse)
            reverseToggle.setToggleState (reverse, juce::dontSendNotification);
        if (loopToggle.getToggleState() != loop)
            loopToggle.setToggleState (loop, juce::dontSendNotification);
        if (std::abs (tempoSlider.getValue() - tempoBpm) > 1e-6)
            tempoSlider.setValue (tempoBpm, juce::dontSendNotification);

        const auto recText = juce::String (upic::sequence_transport::recordButtonText (recording));
        if (recBtn.getButtonText() != recText) recBtn.setButtonText (recText);
        const auto playText = juce::String (upic::sequence_transport::playButtonText (seqPlaying));
        if (playSeqBtn.getButtonText() != playText) playSeqBtn.setButtonText (playText);

        repaint();
    }

    // The sequence's loop is the model's own flag; the app pushes it.
    void setSequenceLoop (bool on)
    {
        if (loopSeqBtn.getToggleState() != on)
            loopSeqBtn.setToggleState (on, juce::dontSendNotification);
    }

    //--- the live playhead trajectory ---------------------------------------

    // One sample of the playhead's path. `timeSec` is the app's clock (the
    // view only uses the differences), `page` is the page the playhead was
    // on and `pos` is the position inside that page.
    void addTrajectoryPoint (int page, double pos, double timeSec)
    {
        if (! trail.empty())
        {
            const auto& last = trail.back();
            // A hold (the playhead standing still) is not a new sample.
            if (last.page == page && std::abs (last.pos - pos) < 1.0e-3)
                return;
        }
        trail.push_back ({ page, pos, timeSec });
        if (trail.size() > 4096) trail.erase (trail.begin());
        repaint();
    }

    void clearTrajectory()
    {
        trail.clear();
        repaint();
    }

    int trajectoryPointCount() const { return (int) trail.size(); }

    // The numeric sliders wear the app's slider theme (the master-volume
    // treatment), like every other slider in the app.
    void themeSliders()
    {
        themeSlider (tempoSlider);
        for (auto& w : groupRows)
        {
            themeSlider (w->gain);
            themeSlider (w->transpose);
        }
    }

    //==========================================================================
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
        g.drawText ("Performance", card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        // the trajectory
        paintTrajectory (g, plotRect());

        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (footerText(), card.reduced (18, 0).removeFromBottom (18),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto card = cardRect().reduced (18, 12);
        auto titleRow = card.removeFromTop (titleH);
        closeBtn.setBounds (titleRow.removeFromRight (64));
        card.removeFromBottom (18);

        auto left = card.removeFromLeft (juce::jmin (430, card.getWidth()));
        left.removeFromTop (4);

        // the page row
        auto pageRow = left.removeFromTop (24);
        pageLab.setBounds (pageRow.removeFromLeft (44));
        pageBox.setBounds (pageRow.removeFromLeft (180));

        // the transport row
        auto tr = left.removeFromTop (26);
        scrubToggle.setBounds (tr.removeFromLeft (74));
        tr.removeFromLeft (4);
        reverseToggle.setBounds (tr.removeFromLeft (82));
        tr.removeFromLeft (4);
        loopToggle.setBounds (tr.removeFromLeft (64));
        tr.removeFromLeft (8);
        tempoLab.setBounds (tr.removeFromLeft (44));
        tempoSlider.setBounds (tr.removeFromLeft (juce::jmin (160, tr.getWidth())));

        // the sequence row
        auto sr = left.removeFromTop (28);
        recBtn.setBounds (sr.removeFromLeft (62));
        sr.removeFromLeft (4);
        playSeqBtn.setBounds (sr.removeFromLeft (62));
        sr.removeFromLeft (4);
        loopSeqBtn.setBounds (sr.removeFromLeft (62));

        // the groups take the rest of the left column
        groupViewport.setBounds (left);
        layoutGroups();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    //==========================================================================
    // The selftest drives the switches without pixels.
    void setPageForTest (int page)
    {
        pageBox.setSelectedId (page + 1, juce::sendNotificationSync);
    }
    int selectedPageForTest() const { return pageBox.getSelectedId() - 1; }
    void setScrubForTest (bool on)
    {
        scrubToggle.setToggleState (on, juce::sendNotificationSync);
    }
    void setReverseForTest (bool on)
    {
        reverseToggle.setToggleState (on, juce::sendNotificationSync);
    }
    void setLoopForTest (bool on)
    {
        loopToggle.setToggleState (on, juce::sendNotificationSync);
    }
    void setTempoForTest (double bpm)
    {
        tempoSlider.setValue (bpm, juce::sendNotificationSync);
    }
    void setGroupMuteForTest (int index, bool on)
    {
        if (index < 0 || index >= (int) groupRows.size()) return;
        groupRows[(size_t) index]->mute.setToggleState (on, juce::sendNotificationSync);
    }
    void setGroupSoloForTest (int index, bool on)
    {
        if (index < 0 || index >= (int) groupRows.size()) return;
        groupRows[(size_t) index]->solo.setToggleState (on, juce::sendNotificationSync);
    }
    void setGroupGainForTest (int index, float v)
    {
        if (index < 0 || index >= (int) groupRows.size()) return;
        groupRows[(size_t) index]->gain.setValue (v, juce::sendNotificationSync);
    }
    void setGroupTransposeForTest (int index, double v)
    {
        if (index < 0 || index >= (int) groupRows.size()) return;
        groupRows[(size_t) index]->transpose.setValue (v, juce::sendNotificationSync);
    }
    void recordForTest() { if (onSequenceRecord) onSequenceRecord(); }
    void playForTest() { if (onSequencePlay) onSequencePlay(); }
    void loopForTest() { if (onSequenceLoop) onSequenceLoop(); }

private:
    //==========================================================================
    struct TrailPoint
    {
        int page = 0;
        double pos = 0.0;
        double t = 0.0;
    };

    struct GroupWidgets
    {
        juce::Label name;
        juce::ToggleButton mute, solo;
        juce::Slider gain, transpose;
    };

    static constexpr int titleH = 26;
    static constexpr int rowH = 26;

    static void themeSlider (juce::Slider& s)
    {
        s.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
        s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
        s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
        s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
    }

    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced (10);
        return juce::Rectangle<int> (juce::jmin (980, r.getWidth()),
                                     juce::jmin (620, r.getHeight())).withCentre (r.getCentre());
    }

    juce::Rectangle<float> plotRect() const
    {
        auto card = cardRect().reduced (18, 12);
        card.removeFromTop (titleH);
        card.removeFromBottom (18);
        auto right = card;
        right.removeFromLeft (juce::jmin (430, card.getWidth()));
        return right.reduced (8, 4).toFloat();
    }

    void layoutGroups()
    {
        const int w = juce::jmax (120, groupViewport.getWidth());
        const int h = juce::jmax (rowH, (int) groupRows.size() * rowH + 4);
        groupList.setSize (w, h);
        for (int i = 0; i < (int) groupRows.size(); ++i)
        {
            auto& w2 = *groupRows[(size_t) i];
            auto row = juce::Rectangle<int> (2, 2 + i * rowH, w - 6, rowH - 2);
            w2.name.setBounds (row.removeFromLeft (90));
            w2.mute.setBounds (row.removeFromLeft (26));
            w2.solo.setBounds (row.removeFromLeft (26));
            row.removeFromLeft (4);
            w2.gain.setBounds (row.removeFromLeft (juce::jmin (120, row.getWidth())));
            row.removeFromLeft (4);
            w2.transpose.setBounds (row.removeFromLeft (juce::jmin (120, row.getWidth())));
        }
    }

    void paintTrajectory (juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f) return;
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (plot);
        g.setColour (juce::Colour (0xff3a4a6a));
        g.drawRect (plot, 1.0f);

        const int pages = juce::jmax (1, (int) shownPages.size());
        const float laneH = plot.getHeight() / (float) pages;

        // the page lanes: page 0 is the bottom lane
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int p = 0; p < pages; ++p)
        {
            const float y = plot.getBottom() - (float) p * laneH;
            g.setColour (juce::Colour (0xff2a2a36));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (juce::Colour (0xff7a7a88));
            g.drawText ("page " + juce::String (p + 1),
                        juce::Rectangle<float> (plot.getX() + 4.0f, y - 11.0f, 70.0f, 10.0f),
                        juce::Justification::centredLeft);
        }

        if (trail.size() >= 2)
        {
            const double t0 = trail.front().t;
            const double t1 = juce::jmax (t0 + 1.0e-3, trail.back().t);
            juce::Path path;
            bool started = false;
            for (const auto& p : trail)
            {
                const float x = plot.getX()
                    + (float) juce::jlimit (0.0, 1.0, (p.t - t0) / (t1 - t0)) * plot.getWidth();
                const float y = yFor (p, pages, laneH, plot);
                if (! started) { path.startNewSubPath (x, y); started = true; }
                else path.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xff5ac8fa));
            g.strokePath (path, juce::PathStrokeType (1.8f));
        }

        // the playhead itself
        if (playing)
        {
            const float x = plot.getRight() - 1.0f;
            const float y = yFor (trail.empty() ? TrailPoint { currentPage, playheadSec, 0.0 }
                                                : trail.back(),
                                  pages, laneH, plot);
            g.setColour (juce::Colour (0xffffd060));
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        }

        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (trail.empty() ? "The playhead's path appears here while it plays"
                                  : juce::String ((int) trail.size()) + " samples",
                    juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                            plot.getWidth(), 12.0f),
                    juce::Justification::centredLeft);
    }

    float yFor (const TrailPoint& p, int pages, float laneH,
                const juce::Rectangle<float>& plot) const
    {
        const int page = juce::jlimit (0, pages - 1, p.page);
        const double dur = (page < (int) pageDurations.size() && pageDurations[(size_t) page] > 0.0)
                             ? pageDurations[(size_t) page]
                             : upic::sequence_transport::kDefaultPageSeconds;
        const float laneBottom = plot.getBottom() - (float) page * laneH;
        const double f = juce::jlimit (0.0, 1.0, p.pos / dur);
        return laneBottom - (float) f * laneH;
    }

    juce::String footerText() const
    {
        juce::String s = "the app's transport - the view only shows it";
        if (playing) s += "  |  playing";
        if (recording) s += "  |  recording";
        if (reverse) s += "  |  reverse";
        if (scrub) s += "  |  scrub";
        if (loop) s += "  |  loop";
        s += "  |  " + juce::String (playheadSec, 2) + " s";
        return s;
    }

    //==========================================================================
    std::vector<juce::String> shownPages;
    std::vector<double> pageDurations;
    std::vector<std::unique_ptr<GroupWidgets>> groupRows;
    std::vector<TrailPoint> trail;

    juce::Viewport groupViewport;
    juce::Component groupList;

    juce::Label pageLab, tempoLab;
    juce::ComboBox pageBox;
    juce::ToggleButton scrubToggle, reverseToggle, loopToggle;
    juce::Slider tempoSlider;
    juce::TextButton recBtn { "Rec" }, playSeqBtn { "Play" },
                     loopSeqBtn { "Loop" }, closeBtn { "Close" };

    bool playing = false;
    bool recording = false;
    bool seqPlaying = false;
    bool loop = false;
    bool scrub = false;
    bool reverse = false;
    bool wasPlaying = false;
    int currentPage = 0;
    double playheadSec = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformanceView)
};
