#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"

// Drawable single-cycle wavetable editor (1024 samples).
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
    bool beginEdit() { return onEditStart ? onEditStart() : true; }

    // Read-only preview of loaded sample material (non-null => sample view).
    void setSampleView(const std::vector<upic::SampleSlot>* slots, int idx)
    { sampleSlots = slots; sampleIdx = idx; drawing = false; repaint(); }
    void setPlayhead(double p) { playhead = p; repaint(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141419));
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff8a8a99));
        g.setFont(11.0f);
        g.drawSingleLineText(sampleSlots != nullptr ? "MATERIAL (sample, read-only)"
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
            g.setColour(juce::Colour(0xff64d2ff));
            for (int px = 0; px < (int) plot.getWidth(); ++px)
            {
                int a = (int) ((double) px / (double) plot.getWidth() * n);
                int b = (int) ((double) (px + 1) / (double) plot.getWidth() * n);
                if (b <= a) b = a + 1;
                b = juce::jmin(b, n);
                float mn = 1.0f, mx = -1.0f;
                for (int i = a; i < b; ++i)
                {
                    float v = buf.getSample(0, i);
                    if (ch > 1) v = 0.5f * (v + buf.getSample(1, i));
                    mn = juce::jmin(mn, v); mx = juce::jmax(mx, v);
                }
                g.drawVerticalLine((int) (plot.getX() + (float) px),
                                   plot.getCentreY() - mx * halfH,
                                   plot.getCentreY() - mn * halfH);
            }
            g.setColour(juce::Colour(0xffc9c9d4));
            g.setFont(10.0f);
            g.drawSingleLineText(s->name + "  " + juce::String(s->durationSec(), 1) + "s @ "
                                 + juce::String((int) s->sampleRate) + "Hz  [slot " + juce::String(s->id) + "]",
                                 (int)plot.getX() + 6, (int)plot.getBottom() - 6);
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
        if (currentSample() != nullptr) return; // sample view is read-only
        if (current() == nullptr) return;
        if (! plotRect().contains(e.position)) return; // header / border: no edit
        if (! beginEdit()) return; // the owner may fork the selection first
        drawing = true;
        paintAt(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (drawing) paintAt(e.position); }
    void mouseUp(const juce::MouseEvent&) override { drawing = false; }

private:
    std::vector<upic::Waveform>* waves = nullptr;
    int* selected = nullptr;
    std::function<void()> onChange;
    std::function<bool()> onEditStart;
    juce::CriticalSection* modelLock = nullptr;
    juce::CriticalSection dummyLock;
    bool drawing = false;
    const std::vector<upic::SampleSlot>* sampleSlots = nullptr;
    int sampleIdx = -1;
    double playhead = -1.0;

    const upic::SampleSlot* currentSample() const
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
