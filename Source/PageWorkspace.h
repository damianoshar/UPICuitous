#pragma once
#include <JuceHeader.h>
#include <vector>
#include "ScoreEditor.h"

//==============================================================================
// WP9: the multi-page workspace.
//
// The UPIC showed one page at a time. The workspace shows 2..4 of them side
// by side, each on its own canvas, each with its own zoom and scroll, and
// all of them following the app's ONE transport - there is no second
// transport, no second clock and no second audio page. The page the
// transport plays is marked, so the musician knows where the sound comes
// from.
//
// The workspace is a VIEW. It owns no page, no transport and no clock: the
// app binds the canvases to the pages and gives them the transport's
// playhead, and the workspace only arranges them and says which one is
// playing.
//
// The layout travels with the piece ("workspace": {pages, split}), so a
// piece that was arranged on four canvases opens the same way.
//==============================================================================
class PageWorkspace : public juce::Component
{
public:
    static constexpr int kMinCanvases = 2;
    static constexpr int kMaxCanvases = 4;

    PageWorkspace()
    {
        // the workspace never takes the mouse away from the canvases
        setInterceptsMouseClicks (false, false);
    }

    // The canvases, side by side. They belong to the app (the first one is
    // the app's own canvas); the workspace only arranges them.
    void setCanvases (const std::vector<ScoreEditor*>& newCanvases)
    {
        canvases = newCanvases;
        if (pageOf.size() != canvases.size())
            pageOf.assign (canvases.size(), 0);
        updateMarks();
        repaint();
    }

    int canvasCount() const { return (int) canvases.size(); }

    ScoreEditor* canvas (int i) const
    {
        return (i >= 0 && i < (int) canvases.size()) ? canvases[(size_t) i] : nullptr;
    }

    // The page each canvas shows.
    void setPageForCanvas (int index, int page)
    {
        if (index < 0 || index >= (int) pageOf.size()) return;
        pageOf[(size_t) index] = page;
        repaint();
    }

    int pageForCanvas (int index) const
    {
        return (index >= 0 && index < (int) pageOf.size()) ? pageOf[(size_t) index] : -1;
    }

    // The page the transport plays - the one that is marked.
    void setActivePage (int page)
    {
        if (activePage == page) return;
        activePage = page;
        updateMarks();
        repaint();
    }

    int getActivePage() const { return activePage; }

    // The canvas that shows the active page (-1 when it is not shown).
    int activeCanvas() const
    {
        for (int i = 0; i < (int) pageOf.size(); ++i)
            if (pageOf[(size_t) i] == activePage) return i;
        return -1;
    }

    // The mark. The workspace is not above the canvases (it only arranges
    // them), so the canvas that shows the playing page paints the mark
    // itself.
    void updateMarks()
    {
        const int active = activeCanvas();
        const juce::String caption = juce::String ("playing page ")
                                    + juce::String (activePage + 1);
        for (int i = 0; i < (int) canvases.size(); ++i)
            if (auto* c = canvases[(size_t) i])
                c->setWorkspaceMarked (i == active, caption);
    }

    // The workspace is closed: no canvas carries the mark any more.
    void clearMarks()
    {
        for (auto* c : canvases)
            if (c != nullptr) c->setWorkspaceMarked (false);
    }

    // The split: the first canvas's share of the width (0.2..0.8).
    void setSplit (double f)
    {
        split = juce::jlimit (0.2, 0.8, f);
        repaint();
    }

    double getSplit() const { return split; }

    // The bounds of one canvas, in the workspace's own coordinates.
    juce::Rectangle<int> boundsForCanvas (int index) const
    {
        const int n = (int) canvases.size();
        if (n <= 0 || index < 0 || index >= n) return {};
        const auto area = getLocalBounds();
        if (n == 1) return area;

        const int firstWidth = juce::roundToInt ((double) area.getWidth() * split);
        if (index == 0)
            return { area.getX(), area.getY(), firstWidth, area.getHeight() };

        auto rest = area.withTrimmedLeft (firstWidth);
        const int restCount = n - 1;
        const int each = rest.getWidth() / restCount;
        const int x = rest.getX() + each * (index - 1);
        const int w = (index == restCount) ? (rest.getX() + rest.getWidth() - x) : each;
        return { x, area.getY(), w, area.getHeight() };
    }

    void resized() override
    {
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // The workspace arranges the canvases; it paints nothing itself
        // (the canvases are not its children, so it cannot paint over
        // them - the canvas that shows the playing page paints the mark).
    }

    // The layout as the piece carries it lives in the piece model
    // (Source/Piece.h) - the app captures it there, so the piece and the
    // worker thread that writes it agree.

private:    std::vector<ScoreEditor*> canvases;
    std::vector<int> pageOf;
    int activePage = 0;
    double split = 0.5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageWorkspace)
};

//==============================================================================
// The divider between the first canvas and the rest.
//
// The workspace only arranges the canvases, so the divider belongs to the
// app (it sits next to the canvases). It says where the split is, and the
// app gives it the canvas area, so a drag can be turned into a fraction.
class WorkspaceSplitter : public juce::Component
{
public:
    std::function<void(double)> onSplit;   // the new fraction (0.2..0.8)

    WorkspaceSplitter()
    {
        setInterceptsMouseClicks (true, false);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    // The canvas area, in the parent's coordinates. The divider stands
    // where the split is, and a drag is turned into a fraction of the
    // area.
    void setArea (juce::Rectangle<int> areaInParent)
    {
        area = areaInParent;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff30d158));
        g.fillRect (getLocalBounds());
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = true;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent&) override
    {
        if (! dragging || onSplit == nullptr) return;
        auto* parent = getParentComponent();
        if (parent == nullptr || area.getWidth() <= 0) return;
        // The mouse is asked for its screen position: the divider moves
        // while it is dragged, so its own position is not a fixed point.
        const auto screen = juce::Desktop::getInstance().getMousePosition();
        const auto origin = parent->getScreenBounds().getPosition();
        const double x = (double) (screen.x - origin.x);
        const double fraction = (x - (double) area.getX()) / (double) area.getWidth();
        onSplit (juce::jlimit (0.2, 0.8, fraction));
    }

private:
    juce::Rectangle<int> area;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorkspaceSplitter)
};
