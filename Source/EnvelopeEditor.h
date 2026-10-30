#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"
#include <algorithm>
#include <functional>

// Drawable amplitude envelope editor: x = 0..1 (arc lifetime), y = 0..1 gain.
//
// v1.24 (wave 21): the points are edited with the two header buttons:
//   +  adds a point - at the spot last tapped on the curve, else after the
//      selected point, else in the widest gap. The new point sits on the
//      current curve, so the shape only changes when you drag it.
//   -  removes the selected point(s) (any point, but two must remain).
// Tapping a dot selects it (and starts a drag); tapping elsewhere in the plot
// places the insertion marker. Ctrl/Cmd/Shift-click adds or removes a point
// from the selection, and dragging any selected point moves the whole group.
// The old mouse shortcuts still work: double-click adds at the cursor,
// right-click deletes the nearest point.
class EnvelopeEditor : public juce::Component
{
public:
    EnvelopeEditor()
    {
        addBtn.setName("envAddBtn");
        delBtn.setName("envDelBtn");
        addBtn.setButtonText("+");
        delBtn.setButtonText("-");
        addBtn.setTooltip("Add a point - tap the curve first to place it");
        delBtn.setTooltip("Remove the selected point(s)");
        addBtn.onClick = [this] { if (addPoint()) notify(); };
        delBtn.onClick = [this] { if (removePoint()) notify(); };
        addAndMakeVisible(addBtn);
        addAndMakeVisible(delBtn);
    }

    void bind(std::vector<upic::Envelope>* e, int* sel, std::function<void()> changed = nullptr)
    {
        envs = e; selected = sel; onChange = changed;
        clearSelection(); candX = -1.0f; dragBase.clear();
        updateButtons();
        repaint();
    }

    // audio thread reads points under this lock — see ScoreEditor::setModelLock
    void setModelLock(juce::CriticalSection* l) { modelLock = l; }

    // --- the point API (the buttons and the selftest use the same calls) ---

    int pointCount() const
    {
        auto* e = current();
        return e != nullptr ? (int) e->points.size() : 0;
    }
    upic::EnvPoint pointAt(int i) const
    {
        auto* e = current();
        if (e == nullptr || i < 0 || i >= (int) e->points.size()) return { 0.0f, 0.0f };
        return e->points[(size_t) i];
    }
    int selectedPoint() const { return selIdxs.empty() ? -1 : selIdxs.front(); }
    const std::vector<int>& selectedPoints() const { return selIdxs; }

    // Select one point. `additive` toggles it in/out of the current selection
    // (the mouse uses this for Ctrl/Cmd/Shift-click).
    void selectPoint(int i, bool additive = false)
    {
        const int n = pointCount();
        if (i < 0 || i >= n)
        {
            if (! additive) clearSelection();
            updateButtons();
            repaint();
            return;
        }
        if (additive)
        {
            const auto it = std::find(selIdxs.begin(), selIdxs.end(), i);
            if (it != selIdxs.end()) selIdxs.erase(it);
            else selIdxs.push_back(i);
        }
        else
        {
            selIdxs.clear();
            selIdxs.push_back(i);
        }
        candX = -1.0f;
        updateButtons();
        repaint();
    }

    // Replace the selection with exactly these points (selftest / automation).
    void selectPoints(const std::vector<int>& ids)
    {
        selIdxs.clear();
        const int n = pointCount();
        for (int i : ids)
            if (i >= 0 && i < n && ! isSelected(i)) selIdxs.push_back(i);
        candX = -1.0f;
        updateButtons();
        repaint();
    }

    // The tap-to-place spot: the x where + inserts next (-1 = none).
    void placePointAt(float x)
    {
        candX = juce::jlimit(0.0f, 1.0f, x);
        clearSelection();
        updateButtons();
        repaint();
    }
    float insertionPoint() const { return candX; }

    // The + action. Returns true when a point was inserted.
    bool addPoint()
    {
        auto* env = current();
        if (env == nullptr || (int) env->points.size() >= kMaxPoints) return false;

        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        const int n = (int) env->points.size();
        float x = 0.5f;
        const int primary = selectedPoint();

        if (candX >= 0.0f)
            x = candX;
        else if (primary >= 0 && primary < n)
        {
            // halfway to the next point; the last point splits the gap before it
            if (primary == n - 1)
                x = (env->points[(size_t) (n - 2)].x + env->points[(size_t) n - 1].x) * 0.5f;
            else
                x = (env->points[(size_t) primary].x + env->points[(size_t) primary + 1].x) * 0.5f;
        }
        else
        {
            // nothing selected: split the widest gap (a "densify" press)
            float widest = -1.0f;
            for (int i = 0; i + 1 < n; ++i)
            {
                const float w = env->points[(size_t) i + 1].x - env->points[(size_t) i].x;
                if (w > widest) { widest = w; x = env->points[(size_t) i].x + w * 0.5f; }
            }
        }

        x = juce::jlimit(0.0f, 1.0f, x);
        const upic::EnvPoint p { x, env->valueAt(x) };   // on the curve: no jump
        auto ins = std::upper_bound(env->points.begin(), env->points.end(), p,
                                    [](const upic::EnvPoint& a, const upic::EnvPoint& b){ return a.x < b.x; });
        const int newIndex = (int) (ins - env->points.begin());
        env->points.insert(ins, p);
        selIdxs.clear();
        selIdxs.push_back(newIndex);
        candX = -1.0f; dragBase.clear();
        updateButtons();
        repaint();
        return true;
    }

    // The - action. Returns true when at least one point was removed.
    bool removePoint()
    {
        auto* env = current();
        if (env == nullptr || selIdxs.empty()) return false;
        const int n = (int) env->points.size();
        const int canRemove = n - 2;   // the curve needs its two ends
        if (canRemove <= 0) return false;

        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        auto indices = selIdxs;
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        if ((int) indices.size() > canRemove) indices.resize((size_t) canRemove);
        for (int idx : indices)
            if (idx >= 0 && idx < (int) env->points.size())
                env->points.erase(env->points.begin() + idx);

        clearSelection(); candX = -1.0f; dragBase.clear();
        updateButtons();
        repaint();
        return true;
    }

    void paint(juce::Graphics& g) override
    {
        updateButtons();   // a preset applied elsewhere may have changed the count
        g.fillAll(juce::Colour(0xff141419));
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff8a8a99));
        g.setFont(11.0f);
        const upic::Envelope* e = current();
        g.drawSingleLineText("ENVELOPE"
                             + (e != nullptr ? "   " + juce::String((int) e->points.size()) + " pts"
                                             : juce::String()),
                             (int) r.getX() + 6, (int) r.getY() + 17);

        auto plot = plotRect();
        g.setColour(juce::Colour(0xff1e1e26)); g.fillRect(plot);
        g.setColour(juce::Colour(0xff34343f)); g.drawRect(plot, 1);

        if (e == nullptr) return;

        // filled curve
        juce::Path p;
        p.startNewSubPath(plot.getX(), plot.getBottom());
        for (int i = 0; i <= 200; ++i)
        {
            float t = i / 200.0f;
            float v = e->valueAt(t);
            p.lineTo(plot.getX() + t * plot.getWidth(), plot.getBottom() - v * plot.getHeight());
        }
        p.lineTo(plot.getRight(), plot.getBottom());
        p.closeSubPath();
        g.setColour(e->colour.withAlpha(0.3f)); g.fillPath(p);

        // line
        juce::Path l; bool f = true;
        for (int i = 0; i <= 200; ++i)
        {
            float t = i / 200.0f;
            float v = e->valueAt(t);
            juce::Point<float> q(plot.getX() + t * plot.getWidth(), plot.getBottom() - v * plot.getHeight());
            if (f) { l.startNewSubPath(q); f = false; } else l.lineTo(q);
        }
        g.setColour(e->colour); g.strokePath(l, juce::PathStrokeType(1.8f));

        // points (selected points keep a ring so - has an obvious target)
        for (size_t i = 0; i < e->points.size(); ++i)
        {
            auto q = toPixel(e->points[i], plot);
            const bool isSel = isSelected((int) i);
            g.setColour(isSel ? juce::Colours::white : e->colour.brighter(0.4f));
            g.fillEllipse(q.x - 4, q.y - 4, 8, 8);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(q.x - 4, q.y - 4, 8, 8, 1.0f);
            if (isSel)
            {
                g.setColour(juce::Colours::white);
                g.drawEllipse(q.x - 7, q.y - 7, 14, 14, 1.5f);
            }
        }

        // the tap-to-place marker (where + will insert)
        if (candX >= 0.0f)
        {
            auto q = toPixel({ candX, e->valueAt(candX) }, plot);
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawEllipse(q.x - 5, q.y - 5, 10, 10, 1.2f);
            g.drawLine(q.x, q.y - 9, q.x, q.y + 9, 1.0f);
        }

        g.setColour(juce::Colour(0xffc9c9d4)); g.setFont(10.0f);
        g.drawSingleLineText(e->name + "  [" + juce::String(e->id) + "]", (int)plot.getX() + 6, (int)plot.getBottom() - 6);
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop(kHeaderH);
        const int w = 28;
        addBtn.setBounds(top.removeFromRight(w).reduced(2, 2));
        delBtn.setBounds(top.removeFromRight(w).reduced(2, 2));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* env = current(); if (!env) return;
        auto plot = plotRect();
        if (!plot.contains(e.position)) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        if (e.mods.isRightButtonDown())
        {
            int idx = nearest(e.position, plot);
            if (idx >= 0 && env->points.size() > 2)
            {
                env->points.erase(env->points.begin() + idx);
                sortPts(env);
                clearSelection(); candX = -1.0f;
                dragBase.clear();
                updateButtons();
                notify();
            }
            return;
        }
        const bool additive = e.mods.isCommandDown() || e.mods.isShiftDown()
                              || e.mods.isCtrlDown();
        int idx = nearest(e.position, plot, 10.0f);
        if (idx >= 0)
        {
            if (additive)
            {
                const auto it = std::find(selIdxs.begin(), selIdxs.end(), idx);
                if (it != selIdxs.end()) selIdxs.erase(it);
                else selIdxs.push_back(idx);
                candX = -1.0f;
                if (isSelected(idx)) beginDrag(env, e.position);
                else { dragBase.clear(); }
            }
            else
            {
                if (! isSelected(idx))
                {
                    selIdxs.clear();
                    selIdxs.push_back(idx);
                }
                candX = -1.0f;
                beginDrag(env, e.position);
            }
        }
        else
        {
            // a tap on the curve: place the insertion spot, drop the selection
            dragBase.clear();
            if (! additive) clearSelection();
            candX = juce::jlimit(0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
        }
        updateButtons();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto* env = current(); if (!env || dragBase.empty()) return;
        auto plot = plotRect();
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);

        const float dx = (e.position.x - dragStartPos.x) / plot.getWidth();
        const float dy = -(e.position.y - dragStartPos.y) / plot.getHeight();
        auto items = dragBase;
        for (auto& item : items)
        {
            if (! item.sel) continue;
            item.p.x = juce::jlimit(0.0f, 1.0f, item.p.x + dx);
            item.p.y = juce::jlimit(0.0f, 1.0f, item.p.y + dy);
            if (item.pinFirst) item.p.x = 0.0f;
            if (item.pinLast)  item.p.x = 1.0f;
        }
        std::stable_sort(items.begin(), items.end(),
                         [](const DragItem& a, const DragItem& b){ return a.p.x < b.p.x; });

        env->points.clear();
        selIdxs.clear();
        for (size_t i = 0; i < items.size(); ++i)
        {
            env->points.push_back(items[i].p);
            if (items[i].sel) selIdxs.push_back((int) i);
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (! dragBase.empty()) { dragBase.clear(); notify(); }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        auto* env = current(); if (!env) return;
        auto plot = plotRect();
        if (!plot.contains(e.position) || (int)env->points.size() >= kMaxPoints) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        upic::EnvPoint p{
            juce::jlimit(0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth()),
            juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - plot.getY()) / plot.getHeight())
        };
        auto ins = std::upper_bound(env->points.begin(), env->points.end(), p,
                                    [](const upic::EnvPoint& a, const upic::EnvPoint& b){ return a.x < b.x; });
        const int newIndex = (int) (ins - env->points.begin());
        env->points.insert(ins, p);
        selIdxs.clear();
        selIdxs.push_back(newIndex);
        candX = -1.0f; dragBase.clear();
        updateButtons();
        notify();
    }

private:
    static constexpr int kMaxPoints = 32;   // plenty for a long arc, cheap per sample
    static constexpr int kHeaderH = 26;

    struct DragItem
    {
        upic::EnvPoint p;
        bool sel = false;
        bool pinFirst = false;
        bool pinLast = false;
    };

    std::vector<upic::Envelope>* envs = nullptr;
    int* selected = nullptr;
    std::function<void()> onChange;
    juce::CriticalSection* modelLock = nullptr;
    juce::CriticalSection dummyLock;
    std::vector<int> selIdxs;  // the selected point indices
    float candX = -1.0f;       // the tap-to-place spot for +
    std::vector<DragItem> dragBase;
    juce::Point<float> dragStartPos;
    juce::TextButton addBtn{ "+" }, delBtn{ "-" };

    void clearSelection() { selIdxs.clear(); }
    bool isSelected(int i) const
    {
        return std::find(selIdxs.begin(), selIdxs.end(), i) != selIdxs.end();
    }
    void beginDrag(upic::Envelope* env, juce::Point<float> pos)
    {
        dragStartPos = pos;
        dragBase.clear();
        dragBase.reserve(env->points.size());
        for (size_t i = 0; i < env->points.size(); ++i)
        {
            DragItem item;
            item.p = env->points[i];
            item.sel = isSelected((int) i);
            item.pinFirst = item.p.x <= 1.0e-4f;
            item.pinLast  = item.p.x >= 1.0f - 1.0e-4f;
            dragBase.push_back(item);
        }
    }
    void updateButtons()
    {
        const int n = pointCount();
        // a preset applied elsewhere may have shrunk the curve under us
        selIdxs.erase(std::remove_if(selIdxs.begin(), selIdxs.end(),
                                     [n](int i){ return i < 0 || i >= n; }),
                      selIdxs.end());
        addBtn.setEnabled(n > 0 && n < kMaxPoints);
        delBtn.setEnabled(n > 2 && ! selIdxs.empty());
    }

    upic::Envelope* current()
    {
        if (!envs || !selected || envs->empty()) return nullptr;
        for (auto& x : *envs) if (x.id == *selected) return &x;
        return &envs->front();
    }
    const upic::Envelope* current() const
    {
        if (!envs || !selected || envs->empty()) return nullptr;
        for (auto& x : *envs) if (x.id == *selected) return &x;
        return &envs->front();
    }
    juce::Rectangle<float> plotRect() const
    { auto r = getLocalBounds().toFloat().reduced(6); r.removeFromTop(kHeaderH); return r; }
    juce::Point<float> toPixel(upic::EnvPoint p, juce::Rectangle<float> plot) const
    { return { plot.getX() + p.x * plot.getWidth(), plot.getBottom() - p.y * plot.getHeight() }; }
    int nearest(juce::Point<float> pos, juce::Rectangle<float> plot, float maxD = 12.0f) const
    {
        auto* env = current(); if (!env) return -1;
        int best = -1; float bd = maxD;
        for (size_t i = 0; i < env->points.size(); ++i)
        {
            float d = toPixel(env->points[i], plot).getDistanceFrom(pos);
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }
    static void sortPts(upic::Envelope* env)
    { std::sort(env->points.begin(), env->points.end(), [](auto& a, auto& b){ return a.x < b.x; }); }
    void notify() { repaint(); if (onChange) onChange(); }
};
