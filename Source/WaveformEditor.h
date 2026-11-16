#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"

// Drawable single-cycle wavetable editor (1024 samples).
//
// v1.8: the same panel shows the loaded sample material (read-only).
// v1.33.4 (M5): the material's loop region (the trim) can be dragged
// there - the two edges of the region are handles, and the frames outside
// it are not played.
class WaveformEditor : public juce::Component
{
public:
    WaveformEditor() { setWantsKeyboardFocus(true); }

    void bind(std::vector<upic::Waveform>* w, int* sel, std::function<void()> changed = nullptr)
    { waves = w; selected = sel; onChange = changed; repaint(); }

    // audio thread reads samples under this lock — see ScoreEditor::setModelLock
    void setModelLock(juce::CriticalSection* l) { modelLock = l; }

    // The owner can give the selection a private wavetable before the first
    // sample changes; returning false cancels the edit (e.g. a read-only tab).
    void setOnEditStart(std::function<bool()> cb) { onEditStart = std::move(cb); }
    // v1.33.4 (L15, part 2): the end of the edit (the undo transaction)
    void setOnEditEnd(std::function<void()> cb) { onEditEnd = std::move(cb); }
    bool beginEdit() { return onEditStart ? onEditStart() : true; }

    // M5: the material's trim is an edit of its own - the undo
    // transaction, the end of it, and the model's publication.
    void setOnSampleEditStart(std::function<bool()> cb) { onSampleEditStart = std::move(cb); }
    void setOnSampleEditEnd(std::function<void()> cb) { onSampleEditEnd = std::move(cb); }
    void setOnSampleChanged(std::function<void()> cb) { onSampleChanged = std::move(cb); }

    // The loaded sample material (non-null => sample view).
    void setSampleView(std::vector<upic::SampleSlot>* slots, int idx)
    { sampleSlots = slots; sampleIdx = idx; draggingEdge = -1; repaint(); }
    void setPlayhead(double p) { playhead = p; repaint(); }

    // M5: the selftest drives the trim directly (a synthesised mouse event
    // needs a real MouseInputSource). Returns false when there is no
    // material to trim.
    bool setSampleRegionForTest (int startFrame, int endFrame)
    {
        auto* s = currentSampleMutable();
        if (s == nullptr) return false;
        const int n = s->totalFrames();
        if (n <= 0) return false;
        s->loopStart = juce::jlimit (0, n - 1, startFrame);
        s->loopEnd = juce::jlimit (s->loopStart + 1, n, endFrame);
        repaint();
        return true;
    }
    int sampleRegionStartForTest() const
    {
        const auto* s = currentSample();
        return s != nullptr ? s->regionStart() : -1;
    }
    int sampleRegionEndForTest() const
    {
        const auto* s = currentSample();
        return s != nullptr ? s->regionEnd() : -1;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll (upic::uiBackground());
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff8a8a99));
        g.setFont(11.0f);
        g.drawSingleLineText(sampleSlots != nullptr ? "MATERIAL (sample - drag the edges to trim)"
                                                    : "WAVETABLE (draw / drag)",
                             (int)r.getX() + 6, (int)r.getY() + 14);

        auto plot = r.reduced(6); plot.removeFromTop(18);
        g.setColour(juce::Colour(0xff1e1e26)); g.fillRect(plot);
        g.setColour(juce::Colour(0xff34343f)); g.drawRect(plot, 1);

        // --- sample preview: min/max envelope of the loaded material ---
        if (const upic::SampleSlot* s = currentSample(); s != nullptr && s->data != nullptr)
        {
            const auto& buf = *s->data;
            g.setColour(juce::Colour(0xff2e2e3a));
            g.drawHorizontalLine((int) plot.getCentreY(), plot.getX(), plot.getRight());

            const int n = buf.getNumSamples();
            const int ch = juce::jmin(2, buf.getNumChannels());
            const float halfH = plot.getHeight() * 0.48f;
            // M5: the material's gain (what Normalize set) is drawn, so the
            // view is what the ear hears.
            const float gain = juce::jlimit (0.0f, 8.0f, s->gain);
            // M5: the loop region (the trim). The frames outside it are
            // dimmed - they are not played.
            const int rStart = s->regionStart(), rEnd = s->regionEnd();
            for (int px = 0; px < (int) plot.getWidth(); ++px)
            {
                int a = (int) ((double) px / (double) plot.getWidth() * n);
                int b = (int) ((double) (px + 1) / (double) plot.getWidth() * n);
                if (b <= a) b = a + 1;
                b = juce::jmin(b, n);
                float mn = 1.0f, mx = -1.0f;
                for (int i = a; i < b; ++i)
                {
                    float v = buf.getSample(0, i) * gain;
                    if (ch > 1) v = 0.5f * (v + buf.getSample(1, i) * gain);
                    mn = juce::jmin(mn, v); mx = juce::jmax(mx, v);
                }
                const bool outside = (a < rStart || a >= rEnd);
                g.setColour(outside ? juce::Colour(0xff2a5a70) : juce::Colour(0xff64d2ff));
                g.drawVerticalLine((int) (plot.getX() + (float) px),
                                   plot.getCentreY() - mx * halfH,
                                   plot.getCentreY() - mn * halfH);
            }

            // M5: the region's two edges - the handles that trim it.
            if (n > 0)
            {
                const float x0 = edgeX (*s, rStart, plot);
                const float x1 = edgeX (*s, rEnd, plot);
                g.setColour(juce::Colour(0xffffd166));
                g.drawVerticalLine((int) x0, plot.getY(), plot.getBottom());
                g.drawVerticalLine((int) x1, plot.getY(), plot.getBottom());
                g.fillRect(juce::Rectangle<float> (x0 - 2.0f, plot.getY(), 4.0f, 7.0f));
                g.fillRect(juce::Rectangle<float> (x1 - 2.0f, plot.getY(), 4.0f, 7.0f));
            }

            g.setColour(juce::Colour(0xffc9c9d4));
            g.setFont(10.0f);
            juce::String info = s->name + "  " + juce::String(s->durationSec(), 1) + "s @ "
                              + juce::String((int) s->sampleRate) + "Hz  [slot " + juce::String(s->id) + "]";
            // M5: the region's own numbers, so a trim is visible.
            if (rStart > 0 || rEnd < n)
                info += "  (trim " + juce::String(rStart) + ".." + juce::String(rEnd) + " of "
                      + juce::String(n) + ")";
            if (s->gain != 1.0f)
                info += "  x" + juce::String(s->gain, 2);
            // M5: the slot's level and the material's Ref are in the line
            // too, so the view says what the slot is set to.
            if (s->level != 1.0f)
                info += "  level " + juce::String(s->level, 2);
            if (s->refFreq > 0.0)
                info += "  ref " + juce::String(s->refFreq, 0) + "Hz";
            g.drawSingleLineText(info, (int)plot.getX() + 6, (int)plot.getBottom() - 6);
            if (playhead >= 0.0 && playhead <= 1.0)
            {
                int px = (int) (plot.getX() + playhead * plot.getWidth());
                g.setColour(juce::Colour(0xffffd166));
                g.drawVerticalLine(px, plot.getY(), plot.getBottom());
            }
            return;
        }

        const upic::Waveform* w = current();
        if (w == nullptr) { g.setColour(juce::Colours::grey); g.drawSingleLineText("no waveform", (int)plot.getX()+8, (int)plot.getCentreY()); return; }

        // zero line
        g.setColour(juce::Colour(0xff2e2e3a));
        g.drawHorizontalLine((int)plot.getCentreY(), plot.getX(), plot.getRight());

        // ghost of other waves faint
        // main wave
        juce::Path p; bool first = true;
        for (int i = 0; i < upic::kWaveSize; ++i)
        {
            float x = plot.getX() + (float)i / upic::kWaveSize * plot.getWidth();
            float y = plot.getCentreY() - w->samples[(size_t)i] * plot.getHeight() * 0.48f;
            if (first) { p.startNewSubPath(x, y); first = false; } else p.lineTo(x, y);
        }
        // Do not close the stroke path: for a saw/square the last sample is
        // far from the first one, so closing it would draw a diagonal across
        // the whole display. Fill down to the zero line instead.
        juce::Path fill = p;
        fill.lineTo(plot.getRight(), plot.getCentreY());
        fill.lineTo(plot.getX(), plot.getCentreY());
        fill.closeSubPath();
        g.setColour(w->colour.withAlpha(0.25f)); g.fillPath(fill);
        g.setColour(w->colour); g.strokePath(p, juce::PathStrokeType(1.6f));

        g.setColour(juce::Colour(0xffc9c9d4));
        g.setFont(10.0f);
        g.drawSingleLineText(w->name + "  [" + juce::String(w->id) + "]", (int)plot.getX() + 6, (int)plot.getBottom() - 6);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // M5: the material's trim - the two edges of the loop region are
        // the handles. The wavetable below is unchanged.
        if (auto* s = currentSampleMutable(); s != nullptr)
        {
            const auto plot = plotRect();
            if (! plot.contains (e.position)) return;
            const float x0 = edgeX (*s, s->regionStart(), plot);
            const float x1 = edgeX (*s, s->regionEnd(), plot);
            if (std::abs (e.position.x - x0) <= 7.0f) draggingEdge = 0;
            else if (std::abs (e.position.x - x1) <= 7.0f) draggingEdge = 1;
            else return;
            if (onSampleEditStart && ! onSampleEditStart()) { draggingEdge = -1; return; }
            dragMaterial (e.position);
            return;
        }
        if (current() == nullptr) return;
        if (! plotRect().contains(e.position)) return; // header / border: no edit
        if (! beginEdit()) return; // the owner may fork the selection first
        drawing = true;
        paintAt(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingEdge >= 0) { dragMaterial (e.position); return; }
        if (drawing) paintAt(e.position);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (draggingEdge >= 0)
        {
            draggingEdge = -1;
            if (onSampleEditEnd) onSampleEditEnd();
            return;
        }
        drawing = false;
        // v1.33.4 (L15, part 2): the edit is over - the owner keeps the
        // state from before it as an undo step.
        if (onEditEnd) onEditEnd();
    }

private:
    std::vector<upic::Waveform>* waves = nullptr;
    int* selected = nullptr;
    std::function<void()> onChange;
    std::function<bool()> onEditStart;
    // v1.33.4 (L15, part 2): the end of the edit (the undo transaction)
    std::function<void()> onEditEnd;
    // M5: the material's trim (the undo transaction, its end, and the
    // model's publication)
    std::function<bool()> onSampleEditStart;
    std::function<void()> onSampleEditEnd;
    std::function<void()> onSampleChanged;
    juce::CriticalSection* modelLock = nullptr;
    juce::CriticalSection dummyLock;
    bool drawing = false;
    std::vector<upic::SampleSlot>* sampleSlots = nullptr;
    int sampleIdx = -1;
    double playhead = -1.0;
    int draggingEdge = -1;   // M5: 0 = the region's start, 1 = its end

    const upic::SampleSlot* currentSample() const
    {
        if (sampleSlots == nullptr || sampleIdx < 0 || sampleIdx >= (int) sampleSlots->size())
            return nullptr;
        auto& s = (*sampleSlots)[(size_t) sampleIdx];
        return s.loaded() ? &s : nullptr;
    }

    // M5: the same slot, but writable - the trim is written here.
    upic::SampleSlot* currentSampleMutable() const
    {
        if (sampleSlots == nullptr || sampleIdx < 0 || sampleIdx >= (int) sampleSlots->size())
            return nullptr;
        auto& s = (*sampleSlots)[(size_t) sampleIdx];
        return s.loaded() ? &s : nullptr;
    }

    upic::Waveform* current()
    {
        if (!waves || !selected || waves->empty()) return nullptr;
        for (auto& w : *waves) if (w.id == *selected) return &w;
        return &waves->front();
    }
    const upic::Waveform* current() const
    {
        if (!waves || !selected || waves->empty()) return nullptr;
        for (auto& w : *waves) if (w.id == *selected) return &w;
        return &waves->front();
    }
    juce::Rectangle<float> plotRect() const
    {
        auto r = getLocalBounds().toFloat().reduced(6); r.removeFromTop(18); return r;
    }

    // M5: the x of a frame in the material view.
    static float edgeX (const upic::SampleSlot& s, int frame, const juce::Rectangle<float>& plot)
    {
        const int n = s.totalFrames();
        if (n <= 0) return plot.getX();
        return plot.getX() + (float) frame / (float) n * plot.getWidth();
    }

    // M5: the drag - the region's edge follows the pointer. The region
    // keeps at least one frame, and it stays inside the material.
    void dragMaterial (juce::Point<float> pos)
    {
        auto* s = currentSampleMutable();
        if (s == nullptr || draggingEdge < 0) return;
        const auto plot = plotRect();
        const int n = s->totalFrames();
        if (n <= 0 || plot.getWidth() <= 0.0f) return;
        const double u = (double) (pos.x - plot.getX()) / (double) plot.getWidth();
        const int frame = juce::jlimit (0, n, (int) std::round (u * (double) n));
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            if (draggingEdge == 0)
                s->loopStart = juce::jlimit (0, s->regionEnd() - 1, frame);
            else
                s->loopEnd = juce::jlimit (s->regionStart() + 1, n, frame);
        }
        repaint();
        if (onSampleChanged) onSampleChanged();
    }

    void paintAt(juce::Point<float> pos)
    {
        auto* w = current(); if (!w) return;
        auto plot = plotRect();
        if (!plot.contains(pos)) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        // brush: set samples around x with cosine falloff, y from mouse
        float fx = (pos.x - plot.getX()) / plot.getWidth() * upic::kWaveSize;
        float v = -(pos.y - plot.getCentreY()) / (plot.getHeight() * 0.48f);
        v = juce::jlimit(-1.0f, 1.0f, v);
        int c = (int) fx;
        int rad = juce::jmax(1, upic::kWaveSize / 128);
        for (int d = -rad; d <= rad; ++d)
        {
            float fall = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * d / (rad + 1)));
            int idx = (c + d + upic::kWaveSize * 4) % upic::kWaveSize;
            w->samples[(size_t)idx] = w->samples[(size_t)idx] * (1.0f - fall) + v * fall;
        }
        // keep loop-continuous: blend edges slightly? (skip — user controls)
        repaint();
        if (onChange) onChange();
    }
};
