#pragma once
#include <JuceHeader.h>
#include "LibraryManager.h"
#include "UpicModel.h"

//==============================================================================
// WP4: the performance-sequence panel.
//
// A sequence is the performer's movement through the score: performance time
// across, page lanes down, the page-local position inside each lane. The
// panel shows that trajectory (forward, hold and reverse are all visible),
// lets a point be dragged, and offers the two edits the recorder cannot do:
// Trim (drop everything after the selected point) and Replace (re-record from
// the selected point, keeping everything before it).
//
// The transport switches are the recorder's: they call back into the main
// component, which owns the clock and the undo transaction.
//==============================================================================
class SequencePanel : public juce::Component
{
public:
    std::function<void()> onRecordToggle, onPlayToggle, onClear, onLoopToggle, onClose;
    std::function<void (const std::vector<upic::PerformancePoint>&)> onPointsEdited;
    std::function<void()> onTrim, onReplace;

    // WP7: the panel's own sequence controls stay here. The shared manager
    // supplies only the Piece/User bridge and the library rows.
    LibraryManager library { "seq" };

    SequencePanel()
    {
        setName ("sequencePanel");
        setWantsKeyboardFocus (true);

        for (auto* b : { &recBtn, &playBtn, &clearBtn, &loopBtn,
                         &trimBtn, &replaceBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa04a4a));
            addAndMakeVisible (b);
        }
        recBtn.setName ("sqRecBtn");        playBtn.setName ("sqPlayBtn");
        clearBtn.setName ("sqClearBtn");    loopBtn.setName ("sqLoopBtn");
        trimBtn.setName ("sqTrimBtn");      replaceBtn.setName ("sqReplaceBtn");
        closeBtn.setName ("sqCloseBtn");

        recBtn.setClickingTogglesState (true);
        loopBtn.setClickingTogglesState (true);
        recBtn.setTooltip ("Record the performance (page + playhead against time)");
        playBtn.setTooltip ("Replay the recorded performance");
        clearBtn.setTooltip ("Erase the recorded points");
        loopBtn.setTooltip ("Loop the performance");
        trimBtn.setTooltip ("Drop the points after the selected one");
        replaceBtn.setTooltip ("Re-record from the selected point, keeping what came before");
        closeBtn.setTooltip ("Close the sequence panel");

        recBtn.onClick = [this] { if (onRecordToggle) onRecordToggle(); };
        playBtn.onClick = [this] { if (onPlayToggle) onPlayToggle(); };
        clearBtn.onClick = [this] { if (onClear) onClear(); };
        loopBtn.onClick = [this] { if (onLoopToggle) onLoopToggle(); };
        trimBtn.onClick = [this] { if (onTrim) onTrim(); };
        replaceBtn.onClick = [this] { if (onReplace) onReplace(); };
        closeBtn.onClick = [this] { close(); };

        // WP7: the source switch, the user-library selector and the
        // Publish/Import/Folder commands live in the shared manager. The
        // panel keeps its own recorder and editing commands.
        addAndMakeVisible (library);
        library.setPieceCommandsVisible (false);
        library.setSelector (libraryBox);

        setVisible (false);
    }

    void setSequences (const std::vector<upic::PerformanceSequence>& bank,
                       const std::vector<double>& pageDurations,
                       int index, bool recording, bool playing)
    {
        seqs = bank;
        durations = pageDurations;
        sel = seqs.empty() ? -1 : juce::jlimit (0, (int) seqs.size() - 1, index);
        if (sel != lastSel) selectedPoint = -1;
        lastSel = sel;
        recBtn.setToggleState (recording, juce::dontSendNotification);
        recBtn.setButtonText (recording ? "Stop" : "Record");
        playBtn.setButtonText (playing ? "Stop" : "Play");
        loopBtn.setToggleState (sel >= 0 && seqs[(size_t) sel].loop,
                                juce::dontSendNotification);
        const bool on = sel >= 0 && ! recording;
        clearBtn.setEnabled (on);
        trimBtn.setEnabled (on && selectedPoint >= 0);
        replaceBtn.setEnabled (on && selectedPoint >= 0);
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
        auto r = getLocalBounds().reduced (10);
        return juce::Rectangle<int> (juce::jmin (820, r.getWidth()),
                                     juce::jmin (540, r.getHeight())).withCentre (r.getCentre());
    }

    juce::Rectangle<float> plotRect() const
    {
        auto card = cardRect().reduced (18, 12);
        card.removeFromTop (titleH);
        card.removeFromTop (libraryH);
        card.removeFromBottom (buttonH);
        return card.toFloat();
    }

    int pageCount() const
    {
        int pages = juce::jmax (1, (int) durations.size());
        if (sel >= 0 && sel < (int) seqs.size())
            for (auto& p : seqs[(size_t) sel].points)
                pages = juce::jmax (pages, p.page + 1);
        return pages;
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

        const auto* s = (sel >= 0 && sel < (int) seqs.size()) ? &seqs[(size_t) sel] : nullptr;
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc8c8d0));
        g.drawText (s != nullptr ? "Performance sequence - " + s->name
                                 : juce::String ("Performance sequence"),
                    card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        const auto plot = plotRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (plot);
        g.setColour (juce::Colour (0xff3a4a6a));
        g.drawRect (plot, 1.0f);

        const int pages = pageCount();
        const float laneH = plot.getHeight() / (float) pages;

        // page lanes: page 0 is the bottom lane
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

        if (s != nullptr && s->points.size() >= 2)
        {
            const double len = juce::jmax (1.0e-6, s->lengthSec());
            const auto& line = dragging ? dragPoints : s->points;
            juce::Path path;
            bool started = false;
            for (auto& p : line)
            {
                const float x = plot.getX()
                    + (float) juce::jlimit (0.0, 1.0, p.t / len) * plot.getWidth();
                const float y = yFor (p, pages);
                if (! started) { path.startNewSubPath (x, y); started = true; }
                else path.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xff5ac8fa));
            g.strokePath (path, juce::PathStrokeType (1.8f));

            // the selected point, for Trim / Replace
            if (selectedPoint >= 0 && selectedPoint < (int) line.size())
            {
                const auto& p = line[(size_t) selectedPoint];
                g.setColour (juce::Colour (0xffffd060));
                g.fillEllipse (xFor (p, len) - 4.0f, yFor (p, pages) - 4.0f, 8.0f, 8.0f);
            }
        }

        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        const juce::String info = s != nullptr
            ? juce::String ((int) s->points.size()) + " points  /  "
              + juce::String (s->lengthSec(), 2) + " s"
              + (s->loop ? "  /  loop" : juce::String())
              + (selectedPoint >= 0 ? "  /  point " + juce::String (selectedPoint + 1) : juce::String())
            : juce::String ("No sequence yet - press Record while you play or scrub");
        g.drawText (info, juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                                  plot.getWidth(), 12.0f),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto card = cardRect().reduced (18, 12);
        card.removeFromTop (titleH);
        library.setBounds (card.removeFromTop (libraryH).reduced (0, 2));
        auto row = card.removeFromBottom (buttonH).reduced (0, 4);
        recBtn.setBounds (row.removeFromLeft (66));
        row.removeFromLeft (6);
        playBtn.setBounds (row.removeFromLeft (60));
        row.removeFromLeft (6);
        loopBtn.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (12);
        trimBtn.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (6);
        replaceBtn.setBounds (row.removeFromLeft (74));
        row.removeFromLeft (12);
        clearBtn.setBounds (row.removeFromLeft (56));
        closeBtn.setBounds (row.removeFromRight (64));
    }

    //--------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition())) { close(); return; }
        const auto plot = plotRect();
        if (! plot.contains (e.position)) return;
        const int hit = hitPoint (e.position);
        if (hit < 0) { selectedPoint = -1; refreshEditButtons(); repaint(); return; }
        selectedPoint = hit;
        dragging = true;
        dragPoints = seqs[(size_t) sel].points;
        refreshEditButtons();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || sel < 0 || selectedPoint < 0 || selectedPoint >= (int) dragPoints.size())
            return;
        const auto plot = plotRect();
        const int pages = pageCount();
        const float laneH = plot.getHeight() / (float) pages;
        const double len = juce::jmax (1.0e-6, seqs[(size_t) sel].lengthSec());

        auto& p = dragPoints[(size_t) selectedPoint];
        p.t = juce::jlimit (0.0, (double) upic::limits::kMaxSequenceSeconds,
                            (double) juce::jlimit (0.0f, 1.0f,
                                (e.position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth())) * len);
        p.page = juce::jlimit (0, pages - 1,
                               (int) ((plot.getBottom() - e.position.y) / juce::jmax (1.0f, laneH)));
        const double dur = pageDuration (p.page);
        const float laneBottom = plot.getBottom() - (float) p.page * laneH;
        const double f = juce::jlimit (0.0, 1.0,
            (double) ((laneBottom - e.position.y) / juce::jmax (1.0f, laneH)));
        p.pos = juce::jlimit (0.0, dur, f * dur);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (sel < 0) return;
        std::sort (dragPoints.begin(), dragPoints.end(),
                   [] (const upic::PerformancePoint& a, const upic::PerformancePoint& b)
                   { return a.t < b.t; });
        if (onPointsEdited) onPointsEdited (dragPoints);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    //--------------------------------------------------------------------------
    // The selftest drives the switches and the point edits without pixels.
    void recordForTest() { if (onRecordToggle) onRecordToggle(); }
    void playForTest() { if (onPlayToggle) onPlayToggle(); }
    void clearForTest() { if (onClear) onClear(); }
    void loopForTest() { if (onLoopToggle) onLoopToggle(); }
    void selectPointForTest (int i)
    {
        if (sel < 0 || i < 0 || i >= (int) seqs[(size_t) sel].points.size()) return;
        selectedPoint = i;
        refreshEditButtons();
        repaint();
    }
    void trimForTest() { if (onTrim) onTrim(); }
    void dragPointForTest (int i, double t, int page, double pos)
    {
        if (sel < 0 || i < 0 || i >= (int) seqs[(size_t) sel].points.size()) return;
        selectedPoint = i;
        dragPoints = seqs[(size_t) sel].points;
        auto& p = dragPoints[(size_t) i];
        p.t = juce::jlimit (0.0, (double) upic::limits::kMaxSequenceSeconds, t);
        p.page = juce::jlimit (0, pageCount() - 1, page);
        p.pos = juce::jlimit (0.0, pageDuration (p.page), pos);
        std::sort (dragPoints.begin(), dragPoints.end(),
                   [] (const upic::PerformancePoint& a, const upic::PerformancePoint& b)
                   { return a.t < b.t; });
        if (onPointsEdited) onPointsEdited (dragPoints);
    }
    void replaceForTest() { if (onReplace) onReplace(); }
    int selectedPointIndex() const { return selectedPoint; }
    std::vector<upic::PerformancePoint> dragPointsForTest() const { return dragPoints; }

private:
    static constexpr int titleH = 26;
    static constexpr int libraryH = 28;
    static constexpr int buttonH = 36;

    double pageDuration (int page) const
    {
        if (page >= 0 && page < (int) durations.size() && durations[(size_t) page] > 0.0)
            return durations[(size_t) page];
        return 30.0;
    }

    float xFor (const upic::PerformancePoint& p, double len) const
    {
        const auto plot = plotRect();
        return plot.getX() + (float) juce::jlimit (0.0, 1.0, p.t / len) * plot.getWidth();
    }

    float yFor (const upic::PerformancePoint& p, int pages) const
    {
        const auto plot = plotRect();
        const float laneH = plot.getHeight() / (float) pages;
        const float laneBottom = plot.getBottom() - (float) p.page * laneH;
        const double f = juce::jlimit (0.0, 1.0, p.pos / pageDuration (p.page));
        return laneBottom - (float) f * laneH;
    }

    int hitPoint (juce::Point<float> at) const
    {
        if (sel < 0) return -1;
        const auto& pts = seqs[(size_t) sel].points;
        if (pts.empty()) return -1;
        const double len = juce::jmax (1.0e-6, seqs[(size_t) sel].lengthSec());
        const int pages = pageCount();
        int best = -1;
        float bestDist = 10.0f * 10.0f;
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const float dx = xFor (pts[(size_t) i], len) - at.x;
            const float dy = yFor (pts[(size_t) i], pages) - at.y;
            const float d = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    void refreshEditButtons()
    {
        const bool on = sel >= 0 && selectedPoint >= 0;
        trimBtn.setEnabled (on);
        replaceBtn.setEnabled (on);
    }

    std::vector<upic::PerformanceSequence> seqs;
    std::vector<double> durations;
    int sel = -1;
    int lastSel = -1;
    int selectedPoint = -1;
    bool dragging = false;
    std::vector<upic::PerformancePoint> dragPoints;

    juce::TextButton recBtn { "Record" }, playBtn { "Play" },
                     clearBtn { "Clear" }, loopBtn { "Loop" },
                     trimBtn { "Trim" }, replaceBtn { "Replace" },
                     closeBtn { "Close" };
    juce::ComboBox libraryBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencePanel)
};
