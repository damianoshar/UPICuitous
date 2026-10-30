#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"
#include <optional>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

//==============================================================================
// Score page canvas: X = time (0..page.duration), Y = pitch log (20Hz..12kHz).
// Tools: Draw (free), Line, Sine, Select/Move, Erase, Sieve zone.
// Left-drag draws with current waveform/envelope/intensity.
//
// Navigation (UPISketch-style wheel behaviour):
//   Ctrl+wheel  = zoom time around cursor      Alt+wheel = zoom pitch around cursor
//   Shift+wheel = pan time                     Wheel     = pan pitch
//
// Selection/editing: right-click = context menu (Duplicate/Mute/Remagnetize/
// Delete/Select all), Shift-click cycles overlapping gestures, Ctrl+A = select
// all, Ctrl+D = duplicate, Del = delete selection, arrow keys nudge the
// selection (Shift = grid-sized / octave steps).
//
// Magnetic sieves (UPISketch 4.5): global sieve + optional translucent ZONES
// drawn with the Zone tool; inside a zone, points snap to the zone's sieve and
// time grid. A stickiness slider softens every snap (0 = free, 1 = magnetic).
//
// A thin line across the plot centre scrubs the playhead when dragged.
//==============================================================================
class ScoreEditor : public juce::Component, public juce::Timer
{
public:
    enum Tool { Draw = 0, Line, SineStroke, Select, Erase, Zone };

    ScoreEditor()
    {
        setWantsKeyboardFocus(true);
    }

    // non-owning bindings (set by MainComponent)
    void bind(upic::Page* p, std::vector<upic::Waveform>* w, double* playhead,
              std::function<void()> changed = nullptr)
    {
        page = p; waves = w; playheadSec = playhead; onChange = changed;
        // page-local selection must not leak across pages (zone ids are
        // page-local; arc ids repeat between pages)
        selectedZone = -1;
        selArcs.clear();
        drawingZone = false; movingArc = false; scrubbing = false;
        repaint();
    }
    // v1.18: while the transport plays, the audio thread owns the playhead
    // and publishes it through MainComponent - the getter lets the editor
    // draw that value, the seek hook tells the audio thread the playhead
    // moved (scrub). Both optional: without them the bound double is used.
    void setPlayheadSource(std::function<double()> getter, std::function<void(double)> seek)
    { playheadGetter = std::move(getter); onSeek = std::move(seek); }
    // Optional: preview audio while scrubbing (used by MainComponent).
    std::function<void(double)> onScrubPreview;
    std::function<void(double)> onScrubFreezeStart;
    std::function<void()> onScrubFreezeStop;
    std::function<void()> onScrubFreezeCancel;
    std::function<void(double)> onScrubDragRelease;
    // The status timer copies the model under this lock to build the audio
    // snapshot; every mutation done by the editor must hold it too, or the
    // snapshot copy can hit a reallocated vector (crash).
    void setModelLock(juce::CriticalSection* l) { modelLock = l; }

    // v1.19: the scrub line runs across the middle of the canvas, so the
    // target is a narrow band at its TOP - a drawing stroke that starts in
    // the middle of the canvas must not move the playhead. Public so the
    // selftest can check the geometry.
    juce::Rectangle<float> timeStripBounds() const { return timeStripRect(); }
    juce::Rectangle<float> scrubTargetRect() const { return timeStripHitRect(); }
    // v1.25: the playhead's top triangle is also a scrub handle, so the
    // playhead can be dragged directly. Public for the selftest.
    juce::Rectangle<float> playheadHandleRect() const
    {
        auto r = plotArea();
        const float x = timeToX(playheadNow());
        const float w = touchSeen ? 32.0f : 24.0f;
        const float h = touchSeen ? 24.0f : 16.0f;
        return { x - w * 0.5f, r.getY(), w, h };
    }
    void setTool(Tool t) { tool = t; if (t != Zone) selectedZone = -1; repaint(); }
    Tool getTool() const { return tool; }
    void setCurrentVoice(int wid, int eid, float inten,
                         int matMode = 0, int sampleId = 1, double refFreq = upic::kDefaultRefFreq)
    { curWave = wid; curEnv = eid; curInten = inten;
      const bool refChanged = (curRef != refFreq);
      curMatMode = matMode; curSampleId = sampleId; curRef = refFreq;
      // the Sample and Live grids are both Ref-relative ratio grids
      if (refChanged && (layer == 1 || layer == 2)) repaint(); }
    // The PM controls are per-arc, but a new PM arc must be born with the
    // values currently shown in the toolbar (the same "what you see is what
    // gets drawn" rule as wave / env / intensity / Ref).
    void setCurrentPmVoice(double ratio, double index, int indexEnv,
                           double feedback, int ops)
    {
        curPmRatio = juce::jlimit(0.1, 32.0, ratio);
        curPmIndex = juce::jlimit(0.0, 20.0, index);
        curPmIndexEnv = juce::jmax(0, indexEnv);
        curPmFeedback = juce::jlimit(0.0, 1.0, feedback);
        curPmOps = (ops == 4 ? 4 : 2);
    }

    // --- selection ---
    // selection is layer-aware: in the live tab only live arcs are selectable
    void setSelectedArc(int id) { selArcs.clear(); if (arcInLayer(id)) selArcs.push_back(id); repaint(); }
    void setSelectedArcs(const std::vector<int>& ids)
    { selArcs.clear(); for (int id : ids) if (arcInLayer(id)) selArcs.push_back(id); repaint(); }
    int getSelectedArc() const { return selArcs.empty() ? -1 : selArcs.front(); }
    const std::vector<int>& getSelectedArcs() const { return selArcs; }
    void selectAll()
    {
        selArcs.clear();
        if (page) for (auto& a : page->arcs) if (arcInLayer(a)) selArcs.push_back(a.id);
        repaint();
    }
    int getSelectedZone() const { return selectedZone; }

    void setSnap(bool s) { snap = s; }
    void setShowGrid(bool s) { showGrid = s; repaint(); }

    // v1.8: one tab strip - each material is a canvas layer (0 osc, 1 sample,
    // 2 live). Same page and timeline as before (time zoom/scroll/playhead
    // are shared, so the tabs stay synchronised), but each tab draws and
    // edits only its own arcs: osc = mode 0, sample = mode 1, live = mode 2.
    // The live layer labels the pitch grid in Hz, osc/sample show note names.
    void setLayer(int m)
    {
        m = juce::jlimit(0, 3, m);
        if (layer == m) return;
        layer = m;
        selArcs.clear();
        drawing = false; movingArc = false; drawingZone = false; scrubbing = false;
        moveStartMap.clear();
        lineAnchor.reset();
        strokePts.clear();
        if (onSelect) onSelect(-1);
        repaint();
    }
    int getLayer() const { return layer; }

    // v1.7: the instruction lines can be hidden (Hints view toggle)
    void setShowHints(bool on) { showHints = on; repaint(); }
    bool getShowHints() const { return showHints; }

    // v1.1: pen pressure -> per-point intensity multiplier (i, default 1)
    void setRecordPressure(bool on) { recordPressure = on; }
    bool getRecordPressure() const { return recordPressure; }
    // v1.1: Xenakis sieve quantizer applied at draw time (takes precedence over snap)
    void setSieve(bool on, int m, std::vector<int> remainders)
    { sieveOn = on; sieveM = juce::jmax(2, m); sieveR = std::move(remainders); repaint(); }
    bool getSieveOn() const { return sieveOn; }

    // v1.2: magnetism controls (UPISketch-style)
    void setStickiness(double s) { stickiness = juce::jlimit(0.0, 1.0, s); }
    double getStickiness() const { return stickiness; }
    void setTimeGrid(double div) { timeGrid = juce::jlimit(0.0, 64.0, div); repaint(); }
    double getTimeGrid() const { return timeGrid; }

    // --- coordinate mapping (through the view: timeZoom/timeOffset,
    //     freqZoom/freqOffset) ---
    double xToTime(float x) const
    {
        auto r = plotArea();
        float f = juce::jlimit(0.0f, 1.0f, (x - r.getX()) / juce::jmax(1.0f, r.getWidth()));
        double dur = page ? page->durationSec : 30.0;
        return f * dur / timeZoom + timeOffset;
    }
    float timeToX(double t) const
    {
        auto r = plotArea();
        double dur = page ? page->durationSec : 30.0;
        double f = (t - timeOffset) * timeZoom / juce::jmax(0.001, dur);
        return r.getX() + (float)f * r.getWidth();
    }
    double yToFreq(float y) const
    {
        auto r = plotArea();
        float sN = 1.0f - juce::jlimit(0.0f, 1.0f, (y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
        double norm = (double) sN / freqZoom + freqOffset;
        return upic::normYToFreq((float) juce::jlimit(0.0, 1.0, norm));
    }
    float freqToY(double f) const
    {
        auto r = plotArea();
        double sN = (upic::freqToNormY(f) - freqOffset) * freqZoom;
        return r.getBottom() - (float) juce::jlimit(0.0, 1.0, sN) * r.getHeight();
    }

    // --- quantization used by draw/line/remagnetize (zone-aware, sticky) ---
    void quantizeXY(double& t, double& f) const
    {
        if (! page) return;
        upic::quantizePoint(snap, sieveOn, sieveM, sieveR, timeGrid, stickiness,
                            page->zones, t, f);
    }

    // --- editing operations (also used by the context menu / shortcuts) ---
    void deleteSelected()
    {
        if (! page || selArcs.empty()) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        auto& v = page->arcs;
        v.erase(std::remove_if(v.begin(), v.end(),
                [&](auto& a){ return std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end(); }),
                v.end());
        selArcs.clear();
        if (selectedZone >= 0 && tool != Zone) selectedZone = -1;
        notify();
    }

    void duplicateSelected()
    {
        if (! page || selArcs.empty()) return;
        std::vector<int> created;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            const double off = juce::jlimit(0.1, 1.0, page->durationSec * 0.05);
            std::vector<upic::Arc> copies;
            for (auto& id : selArcs)
                for (auto& a : page->arcs)
                    if (a.id == id)
                    {
                        upic::Arc c = a;
                        c.id = page->nextArcId++;
                        for (auto& pt : c.points)
                            pt.t = juce::jlimit(0.0, page->durationSec, pt.t + off);
                        created.push_back(c.id);
                        copies.push_back(std::move(c));
                        break;
                    }
            for (auto& c : copies) page->arcs.push_back(std::move(c));
        }
        if (! created.empty()) selArcs = created; // selection follows the copies
        notify();
    }

    void toggleMuteSelected()
    {
        if (! page || selArcs.empty()) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        bool muteTo = false;
        for (auto& a : page->arcs)
            if (std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end())
                { muteTo = ! a.muted; break; } // unify on the first selected arc
        for (auto& a : page->arcs)
            if (std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end())
                a.muted = muteTo;
        notify();
    }

    // "Remagnetize" (UPISketch): re-apply the current sieve/zone/grid to all
    // points of the selection (e.g. after changing the sieve).
    void remagnetizeSelected()
    {
        if (! page || selArcs.empty()) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        for (auto& a : page->arcs)
        {
            if (std::find(selArcs.begin(), selArcs.end(), a.id) == selArcs.end()) continue;
            for (auto& pt : a.points) quantizeXY(pt.t, pt.freq);
            std::sort(a.points.begin(), a.points.end(),
                      [](auto& x, auto& y){ return x.t < y.t; });
        }
        notify();
    }

    // Arrow-key nudge of the selection: dt seconds (Shift = grid-sized step)
    // and +/-1 semitone via the next magnetically allowed pitch (Shift = octave).
    void nudgeSelected(double dt, int dSemi)
    {
        if (! page || selArcs.empty()) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        for (auto& a : page->arcs)
        {
            if (std::find(selArcs.begin(), selArcs.end(), a.id) == selArcs.end()) continue;
            for (auto& pt : a.points)
            {
                pt.t = juce::jlimit(0.0, page->durationSec, pt.t + dt);
                if (dSemi != 0) pt.freq = nudgePitch(pt.t, pt.freq, dSemi);
            }
            std::sort(a.points.begin(), a.points.end(),
                      [](auto& x, auto& y){ return x.t < y.t; });
        }
        notify();
    }

    // Zone tool: create a magnetic region from two corner times/freqs.
    // The zone inherits the current sieve (m/R) and time grid.
    int addZone(double ta, double tb, double fa, double fb)
    {
        if (! page) return -1;
        upic::SieveZone z;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            z.id = page->nextZoneId++;
            z.t0 = juce::jlimit(0.0, page->durationSec, juce::jmin(ta, tb));
            z.t1 = juce::jlimit(0.0, page->durationSec, juce::jmax(ta, tb));
            z.f0 = juce::jlimit(upic::kMinFreq, upic::kMaxFreq, juce::jmin(fa, fb));
            z.f1 = juce::jlimit(upic::kMinFreq, upic::kMaxFreq, juce::jmax(fa, fb));
            z.m = sieveM;
            z.remainders = sieveR;
            z.timeGrid = timeGrid;
            page->zones.push_back(z);
        }
        selectedZone = z.id;
        selArcs.clear();
        notify();
        return z.id;
    }

    void deleteZone(int id)
    {
        if (! page) return;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            auto& v = page->zones;
            v.erase(std::remove_if(v.begin(), v.end(),
                    [&](auto& z){ return z.id == id; }), v.end());
        }
        if (selectedZone == id) selectedZone = -1;
        notify();
    }

    // Time-strip scrub: set the playhead from a canvas x position.
    void scrubTo(float x)
    {
        if (!page || !playheadSec) return;
        double t = juce::jlimit(0.0, page->durationSec, xToTime(x));
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            *playheadSec = t;
        }
        if (onSeek != nullptr) onSeek(t); // v1.18: the audio thread must move too
        if (onScrubPreview != nullptr) onScrubPreview(t);
        repaint();
    }

    void timerCallback() override
    {
        if (!scrubbing) { stopTimer(); return; }
        if (scrubDragged && !freezeAfterDrag)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastDragMs > 80.0)
            {
                freezeAfterDrag = true;
                if (onScrubFreezeStart != nullptr)
                    onScrubFreezeStart(playheadNow());
            }
        }
    }

    //==========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101014));
        auto r = plotArea();

        // frame
        g.setColour(juce::Colour(0xff1e1e26));
        g.fillRect(r);

        // Canvas texts (grid labels, zone labels, arc tags) are collected here
        // and painted in one final pass, so they always sit above zones/arcs.
        struct OverlayText { juce::String s; float fontH; juce::Colour c; int x, y; };
        std::vector<OverlayText> overlay;

        // sieve zones: translucent magnetic regions (under the grid)
        if (page)
        {
            for (auto& z : page->zones)
            {
                auto rect = zoneRect(z);
                g.setColour(juce::Colour(0x2a5ac8fa));
                g.fillRect(rect);
                g.setColour(z.id == selectedZone ? juce::Colour(0xff8ad4ff)
                                                 : juce::Colour(0x805ac8fa));
                g.drawRect(rect, z.id == selectedZone ? 2.0f : 1.0f);
                juce::String lab = "m" + juce::String(z.m) + " {";
                for (size_t i = 0; i < z.remainders.size(); ++i)
                    lab += (i > 0 ? "," : "") + juce::String(z.remainders[i]);
                lab += "}";
                if (z.timeGrid > 0) lab += " " + juce::String(z.timeGrid, 0) + "/s";
                overlay.push_back({ lab, 10.0f, juce::Colour(0xffb8e0ff), (int) rect.getX() + 4, (int) rect.getY() + 13 });
            }
        }

        // grid: time (sec) + pitch (chromatic C0..C9 + 440 line)
        if (showGrid && page)
        {
            double dur = page->durationSec;
            // vertical time lines every nice step
            double step = niceStep(dur / 8.0);
            g.setColour(juce::Colour(0xff2a2a35));
            for (double t = 0; t <= dur + 1e-9; t += step)
            {
                float x = timeToX(t);
                if (x < r.getX() || x > r.getRight()) continue;
                g.drawVerticalLine((int)x, r.getY(), r.getBottom());
                if (x + 3 + 26 <= r.getRight()) // keep the label inside the plot
                    overlay.push_back({ juce::String((int)std::round(t)) + "s", 10.0f,
                                        juce::Colour(0xff8a8a99), (int)x + 3, (int)r.getBottom() - 4 });
            }
            // horizontal pitch lines. Osc tab: C octaves + the A440 line with
            // note names. Sample and Live tabs: the same multiply-ratio grid -
            // strong lines at Ref x 2^k (1x = unison), faint half-octaves
            // between them, labels as multipliers. The Live tab has no
            // frequency reference: 1x is the mic input as-is.
            if (layer == 1 || layer == 2)
            {
                const double ref = curRef > 0.0 ? curRef : upic::kDefaultRefFreq;
                auto ratioText = [](double rt)
                {
                    juce::String t = (rt >= 1.0) ? juce::String((int) std::lround(rt))
                                                 : juce::String(rt, rt >= 0.1 ? 3 : 4);
                    while (t.endsWithChar('0')) t = t.dropLastCharacters(1);
                    if (t.endsWithChar('.')) t = t.dropLastCharacters(1);
                    return t + "x";
                };
                for (int k = -8; k <= 8; ++k)
                {
                    const double f = ref * std::pow(2.0, (double) k);
                    const double fh = f * juce::MathConstants<double>::sqrt2;
                    if (fh >= upic::kMinFreq && fh <= upic::kMaxFreq)
                    {
                        float yh = freqToY(fh);
                        if (yh >= r.getY() && yh <= r.getBottom())
                        {
                            g.setColour(juce::Colour(0xff26262f));
                            g.drawHorizontalLine((int) yh, r.getX(), r.getRight());
                        }
                    }
                    if (f < upic::kMinFreq || f > upic::kMaxFreq) continue;
                    float y = freqToY(f);
                    if (y < r.getY() || y > r.getBottom()) continue;
                    const bool isRef = (k == 0);
                    g.setColour(isRef ? juce::Colour(0xffff9f0a) : juce::Colour(0xff3a3a4d));
                    g.drawHorizontalLine((int) y, r.getX(), r.getRight());
                    juce::String lab = ratioText(f / ref);
                    if (isRef) lab += (layer == 2 ? " (input)" : " (Ref)");
                    overlay.push_back({ lab, 10.0f, juce::Colour(0xff8a8a99),
                                        (int) r.getX() + 3, (int) y - 3 });
                }
            }
            else
            for (int midi = 12; midi <= 120; ++midi)
            {
                double f = upic::midiToFreq((double)midi);
                if (f < upic::kMinFreq || f > upic::kMaxFreq) continue;
                const bool isC = (midi % 12 == 0);
                const bool isA440 = (midi == 69);
                float y = freqToY(f);
                if (y < r.getY() || y > r.getBottom()) continue;
                if (isA440) g.setColour(juce::Colour(0xffff9f0a));
                else if (isC) g.setColour(juce::Colour(0xff3a3a4d));
                else if (midi % 12 == 7 || midi % 12 == 5) g.setColour(juce::Colour(0xff26262f));
                else continue; // only draw some lines to avoid clutter
                g.drawHorizontalLine((int)y, r.getX(), r.getRight());
                if (isC || isA440)
                {
                    overlay.push_back({ juce::MidiMessage::getMidiNoteName(midi, true, true, 3),
                                        10.0f, juce::Colour(0xff8a8a99), (int)r.getX() + 3, (int)y - 3 });
                }
            }
        }

        // arcs (each tab draws only its own layer: osc = mode 0,
        // sample = mode 1, live = mic-input arcs, mode 2)
        if (page)
        {
            for (auto& arc : page->arcs)
            {
                if (! arcInLayer(arc)) continue;
                const bool isLive = (arc.mode == 2);
                juce::Colour c = juce::Colours::cyan;
                if (waves)
                {
                    for (auto& w : *waves) if (w.id == arc.waveformId) { c = w.colour; break; }
                }
                if (isLive) c = juce::Colour(0xffeceff4); // live input: neutral white
                const bool sel = isSel(arc.id);
                if (sel) c = c.brighter(0.6f);
                if (arc.muted) c = c.withAlpha(0.25f);

                juce::Path p;
                bool first = true;
                for (auto& pt : arc.points)
                {
                    float x = timeToX(pt.t), y = freqToY(pt.freq);
                    if (first) { p.startNewSubPath(x, y); first = false; }
                    else p.lineTo(x, y);
                }
                g.setColour(c.withAlpha(arc.muted ? 0.3f : 0.95f));
                g.strokePath(p, juce::PathStrokeType(sel ? 3.0f : 2.0f));

                // dots (radius reflects the pressure-intensity curve)
                g.setColour(c.brighter(0.3f));
                for (auto& pt : arc.points)
                {
                    const float rr = 2.5f * (0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, pt.i));
                    g.fillEllipse((float) timeToX(pt.t) - rr, (float) freqToY(pt.freq) - rr,
                                  rr * 2.0f, rr * 2.0f);
                }

                // tag at end ("live" marks mic-input arcs)
                if (!arc.points.empty())
                {
                    auto lp = arc.points.back();
                    overlay.push_back({ isLive ? "live e" + juce::String(arc.envelopeId)
                                               : "v" + juce::String(arc.waveformId) + " e" + juce::String(arc.envelopeId),
                                        9.0f, c, (int)timeToX(lp.t) - 34, (int)freqToY(lp.freq) - 7 });
                }
            }
        }

        // in-progress stroke
        if (!strokePts.empty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            juce::Path p; bool f = true;
            for (auto& q : strokePts) { if (f) { p.startNewSubPath(q.pos); f = false; } else p.lineTo(q.pos); }
            g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
        }
        if (lineAnchor) // rubber band
        {
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawLine(lineAnchor->x, lineAnchor->y, lastMouse.x, lastMouse.y, 1.5f);
        }
        if (drawingZone) // zone rubber rectangle
        {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRect(zoneScreenRect(), 1.0f);
        }

        // marquee selection rectangle (Select tool, drag on empty space)
        if (selectingMarquee)
        {
            auto mr = marqueeRect();
            g.setColour(juce::Colour(0x30ffffff));
            g.fillRect(mr);
            g.setColour(juce::Colour(0xff8ab0ff));
            g.drawRect(mr, 1.0f);
        }

        // centre scrub line (drag the top edge to move the playhead). No box
        // or time readout: the transport row already shows the playhead time.
        {
            auto grip = timeStripHitRect();
            g.setColour(juce::Colour(0x5042506a));
            g.fillRect(grip.getX(), grip.getY(), grip.getWidth(), 1.0f);
        }

        // final text pass: labels and tags above zones, arcs and the scrub
        // line, each on a dark plate so they stay readable; the playhead
        // still crosses over them
        for (auto& t : overlay)
        {
            g.setFont(juce::Font(juce::FontOptions(t.fontH, juce::Font::bold)));
            const int tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), t.s);
            const int asc  = (int) std::ceil(t.fontH * 0.80f) + 1;
            const int desc = (int) std::ceil(t.fontH * 0.26f) + 1;
            auto plate = juce::Rectangle<int>(t.x - 3, t.y - asc - 2, tw + 6, asc + desc + 4)
                             .toFloat().getIntersection(r.toFloat());
            g.setColour(juce::Colour(0xcc0e0e14));
            if (! plate.isEmpty()) g.fillRoundedRectangle(plate, 3.0f);
            g.setColour(t.c);
            g.drawSingleLineText(t.s, t.x, t.y);
        }

        // playhead: the top triangle is always the scrub handle; the line is
        // only drawn once there is a position to show.
        if (playheadSec && page)
        {
            float x = timeToX(playheadNow());
            if (x >= r.getX() && x <= r.getRight())
            {
                g.setColour(juce::Colour(0xffff375f));
                if (isPlaying || scrubbing || playheadNow() > 0.0)
                    g.drawVerticalLine((int)x, r.getY(), r.getBottom());
                juce::Path tri;
                tri.addTriangle(x - 5, r.getY(), x + 5, r.getY(), x, r.getY() + 8);
                g.fillPath(tri);
            }
        }

        // border + hint
        g.setColour(juce::Colour(0xff34343f));
        g.drawRect(r, 1);
        juce::String hint = (layer == 2)
            ? juce::String("LIVE tab: arcs multiply the mic input pitch (1x = input, 2x = octave up), pitch mode in Audio  |  Del: delete  |  Tool=") + toolName()
            : (layer == 1)
                ? juce::String("SAMPLE tab: drawn arcs play the slot; Pitch sets how they transpose it  |  Del: delete  |  Tool=") + toolName()
                : juce::String("Draw: drag  |  R-click: menu  |  Shift-click: cycle  |  Ctrl+A: all  |  Ctrl+D: dup  |  Del: delete  |  Tool=") + toolName();
        if (tool == Zone) hint = "Zone: drag a magnetic region (inherits sieve + grid)  |  click: select  |  Del: remove  |  Tool=Zone";
        if (sieveOn)
        {
            hint += "  |  SIEVE m" + juce::String(sieveM) + " {";
            for (size_t i = 0; i < sieveR.size(); ++i)
                hint += (i > 0 ? "," : "") + juce::String(sieveR[i]);
            hint += "}";
        }
        if (timeGrid > 0) hint += "  |  GRID " + juce::String(timeGrid, 0) + "/s";
        if (stickiness < 0.999) hint += "  |  STICK " + juce::String((int) std::round(stickiness * 100)) + "%";
        if (recordPressure) hint += "  |  PRESS on";
        if (selectedZone >= 0) hint += "  |  ZONE #" + juce::String(selectedZone) + " selected (Del removes)";
        // instruction lines: right-aligned to the plot edge, slightly bold
        auto drawHintRight = [&](const juce::String& s, float fontH, juce::Colour col, int y)
        {
            g.setFont(juce::Font(juce::FontOptions(fontH, juce::Font::bold)));
            g.setColour(col);
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), s);
            g.drawSingleLineText(s, juce::jmax((int)r.getX() + 4, (int)r.getRight() - w - 6), y);
        };
        if (showHints)
        {
            drawHintRight(hint, 11.0f, juce::Colour(0xff9a9aa5), (int)r.getY() + 14);
            drawHintRight("Wheel: pan pitch  |  Alt+wheel: zoom pitch  |  Ctrl+wheel: zoom time  |  Shift+wheel: pan time  |  Arrows: nudge selection  |  drag the centre line: scrub",
                          10.0f, juce::Colour(0xff6f7080), (int)r.getY() + 28);
        }
    }

    void resized() override { repaint(); }

    //==========================================================================
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!page) return;
        grabKeyboardFocus(); // so Del/Ctrl+A/arrows reach keyPressed()
        lastMouse = e.position;

        // --- multi-touch (tablet/phone): 1st finger behaves like the mouse;
        //     a 2nd finger switches to the pinch/pan view gesture ---
        if (e.source.isTouch())
        {
            touchSeen = true;
            upsertTouch(e);
            if (touches.size() >= 2) { beginPinch(); return; }
            touchDownMs = juce::Time::getMillisecondCounter();
            touchDownPos = e.position;
        }

        auto r = plotArea();
        if (!r.contains(e.position)) return;

        // v1.25: drag the playhead by its top triangle
        if (! e.mods.isRightButtonDown() && playheadHandleRect().contains(e.position))
        {
            scrubbing = true;
            scrubDragged = false;
            freezeAfterDrag = false;
            lastDragMs = juce::Time::getMillisecondCounterHiRes();
            startTimer(40);
            scrubTo(e.position.x);
            if (onScrubFreezeStart != nullptr)
                onScrubFreezeStart(xToTime(e.position.x));
            return;
        }

        // centre line: press inside the scrub target (any tool)
        if (! e.mods.isRightButtonDown() && timeStripHitRect().contains(e.position))
        {
            scrubbing = true;
            scrubDragged = false;
            freezeAfterDrag = false;
            lastDragMs = juce::Time::getMillisecondCounterHiRes();
            startTimer(40);
            scrubTo(e.position.x);
            if (onScrubFreezeStart != nullptr)
                onScrubFreezeStart(xToTime(e.position.x));
            return;
        }

        if (tool == Zone)
        {
            if (e.mods.isRightButtonDown())
            {
                const int zid = pickZone(e.position);
                selectedZone = zid;
                repaint();
                if (zid >= 0) showZoneMenu(zid);
                return;
            }
            const int zid = pickZone(e.position);
            if (zid >= 0) // select existing zone (topmost under cursor)
            {
                selectedZone = zid;
                selArcs.clear();
                repaint();
                return;
            }
            drawingZone = true;
            zoneStart = e.position;
            zoneCur = e.position;
            repaint();
            return;
        }

        if (e.mods.isRightButtonDown())
        {
            const int id = pickArc(e.position);
            if (id >= 0)
            {
                setSelectedArc(id);
                if (onSelect) onSelect(id);
                repaint();
                showArcMenu(id);
            }
            else
            {
                selArcs.clear();
                repaint();
                if (onSelect) onSelect(-1);
            }
            return;
        }

        if (tool == Select)
        {
            const bool ctrl  = e.mods.isCtrlDown() || e.mods.isCommandDown();
            const bool shift = e.mods.isShiftDown();
            const int hit = pickArc(e.position);

            if (hit >= 0)
            {
                if (ctrl)
                {
                    if (isSel(hit))
                        selArcs.erase(std::remove(selArcs.begin(), selArcs.end(), hit), selArcs.end());
                    else selArcs.push_back(hit);
                    if (onSelect) onSelect(selArcs.empty() ? -1 : selArcs.front());
                }
                else if (shift)
                {
                    if (! isSel(hit)) selArcs.push_back(hit);
                    if (onSelect) onSelect(hit);
                }
                else
                {
                    if (! isSel(hit)) { selArcs.clear(); selArcs.push_back(hit); if (onSelect) onSelect(hit); }
                }
            }
            else
            {
                marqueeAdditive = shift;
                marqueeStart = marqueeCur = e.position;
                selectingMarquee = true;
                if (! shift) { selArcs.clear(); if (onSelect) onSelect(-1); }
        }
            // start a move only when pressing ON a selected arc
            const int hit2 = pickArc(e.position);
            if (hit2 >= 0 && isSel(hit2))
            {
                movingArc = true;
                moveStartPos = e.position;
                const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
                moveStartMap.clear();
                for (auto& a : page->arcs)
                    if (isSel(a.id)) moveStartMap[a.id] = a.points;
                if (onArcEditingChanged != nullptr) onArcEditingChanged(true);
            }
            return;
        }

        if (tool == Erase)
        {
            int id = pickArc(e.position);
            if (id >= 0) { selArcs = { id }; deleteSelected(); }
            return;
        }
        if (tool == Draw || tool == SineStroke)
        {
            drawing = true;
            strokePts.clear();
            strokePts.push_back({ e.position, curPress(e) });
            if (onArcEditingChanged != nullptr) onArcEditingChanged(true);
        }
        else if (tool == Line)
        {
            lineAnchor = e.position;
            if (onArcEditingChanged != nullptr) onArcEditingChanged(true);
            lastMouse = e.position;
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!page) return;
        lastMouse = e.position;

        if (e.source.isTouch())
        {
            upsertTouch(e);
            if (touches.size() >= 2 || pinchMode) { updatePinch(); return; }
        }

        if (scrubbing) {
            if (! scrubDragged) {
                scrubDragged = true;
                if (onScrubFreezeCancel != nullptr) onScrubFreezeCancel();
            } else if (freezeAfterDrag) {
                freezeAfterDrag = false;
                if (onScrubFreezeCancel != nullptr) onScrubFreezeCancel();
            }
            lastDragMs = juce::Time::getMillisecondCounterHiRes();
            scrubTo(e.position.x); // also publishes the scrub preview/rate
            return;
        }

        if (drawingZone)
        {
            zoneCur = e.position;
            repaint();
            return;
        }

        if (selectingMarquee)
        {
            marqueeCur = e.position;
            auto rect = marqueeRect();
            // arcs that have any point inside the marquee
            std::vector<int> hits;
            if (page) for (auto& a : page->arcs)
            {
                if (! arcInLayer(a)) continue;
                for (auto& pt : a.points)
                {
                    juce::Point<float> pp(timeToX(pt.t), freqToY(pt.freq));
                    if (rect.contains(pp)) { hits.push_back(a.id); break; }
                }
            }
            if (marqueeAdditive)
            {
                for (int id : hits) if (std::find(selArcs.begin(), selArcs.end(), id) == selArcs.end()) selArcs.push_back(id);
            }
            else
            {
                selArcs = hits;
            }
            if (onSelect) onSelect(selArcs.empty() ? -1 : selArcs.front());
            repaint();
            return;
        }

        if (movingArc)
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            const float dx = e.position.x - moveStartPos.x;
            double dt = xToTime(moveStartPos.x + dx) - xToTime(moveStartPos.x);
            // freq ratio from end/start pixel positions (log view correct)
            const double f0 = juce::jmax(1.0, yToFreq(moveStartPos.y));
            const double f1 = juce::jmax(1.0, yToFreq(e.position.y));
            const double ratio = f1 / f0;
            for (auto& a : page->arcs)
            {
                auto it = moveStartMap.find(a.id);
                if (it == moveStartMap.end()) continue;
                a.points = it->second;
                for (auto& p : a.points)
                {
                    p.t = juce::jlimit(0.0, page->durationSec, p.t + dt);
                    p.freq = juce::jlimit(upic::kMinFreq, upic::kMaxFreq, p.freq * ratio);
                }
                std::sort(a.points.begin(), a.points.end(), [](auto& x, auto& y){ return x.t < y.t; });
            }
            repaint();
            return;
        }

        if (drawing)
        {
            // resample: only push if moved 4px in time
            if (strokePts.empty() || std::abs(e.position.x - strokePts.back().pos.x) > 3.0f
                                    || std::abs(e.position.y - strokePts.back().pos.y) > 3.0f)
            {
                auto r = plotArea();
                juce::Point<float> cl{ juce::jlimit(r.getX(), r.getRight(), e.position.x), juce::jlimit(r.getY(), r.getBottom(), e.position.y) };
                strokePts.push_back({ cl, curPress(e) });
                repaint();
            }
        }
        else repaint(); // rubber band
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!page) { drawing = false; movingArc = false; lineAnchor.reset(); drawingZone = false; scrubbing = false; touches.clear(); pinchMode = false; return; }

        if (e.source.isTouch())
        {
            const bool wasPinch = pinchMode;
            removeTouch(e.source.getIndex());
            if (touches.size() < 2) { pinchMode = false; }
            if (wasPinch) { repaint(); return; } // gesture consumed: no click/commit

            // long-press in Select/Zone tool = context menu (the touch twin of right-click)
            if (! scrubbing && ! drawing && ! movingArc && ! drawingZone
                && (tool == Select || tool == Zone)
                && juce::Time::getMillisecondCounter() - touchDownMs >= 450
                && (e.position - touchDownPos).getDistanceFromOrigin() < 8.0f)
            {
                showTouchMenu(e.position);
                return;
            }
        }

        if (scrubbing) {
            stopTimer();
            const bool wasDrag = scrubDragged;
            scrubbing = false;
            scrubDragged = false;
            freezeAfterDrag = false;
            if (! wasDrag) {
                if (onScrubFreezeStop != nullptr) onScrubFreezeStop();
            } else {
                if (freezeAfterDrag) { /* already stopped? */ }
                // ensure any freeze tail stops
                if (onScrubFreezeStop != nullptr) onScrubFreezeStop();
            }
            repaint();
            return;
        }

        if (drawingZone)
        {
            drawingZone = false;
            auto rect = zoneScreenRect();
            if (rect.getWidth() >= 8.0f && rect.getHeight() >= 8.0f)
                addZone(xToTime(zoneStart.x), xToTime(zoneCur.x),
                        yToFreq(zoneStart.y), yToFreq(zoneCur.y));
            repaint();
            return;
        }

        if (selectingMarquee)
        {
            selectingMarquee = false;
            // tiny drag = click on empty: keep the (already cleared) selection
            if ((marqueeCur - marqueeStart).getDistanceFromOrigin() < 4.0f && ! marqueeAdditive)
                selArcs.clear();
            if (onSelect) onSelect(selArcs.empty() ? -1 : selArcs.front());
            repaint();
            return;
        }

        if (movingArc) {
            movingArc = false; moveStartMap.clear(); if (onArcEditingChanged != nullptr) onArcEditingChanged(false); notify(); repaint(); return;
        }

        if (tool == Line && lineAnchor)
        {
            if (onArcEditingChanged != nullptr) onArcEditingChanged(false);
            commitLine(*lineAnchor, e.position, curPress(e));
            lineAnchor.reset();
            return;
        }
        if (drawing)
        {
            drawing = false;
            if (onArcEditingChanged != nullptr) onArcEditingChanged(false);
            strokePts.push_back({ e.position, curPress(e) });
            commitStroke();
            strokePts.clear();
            repaint();
        }
        lineAnchor.reset();
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        const bool cmd = k.getModifiers().isCommandDown();
        const bool shift = k.getModifiers().isShiftDown();
        const int code = k.getKeyCode();

        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (tool == Zone && selectedZone >= 0) { deleteZone(selectedZone); return true; }
            if (! selArcs.empty()) { deleteSelected(); return true; }
            return false;
        }
        if (cmd && (code == 'A' || code == 'a')) { selectAll(); if (onSelect) onSelect(getSelectedArc()); return true; }
        if (cmd && (code == 'D' || code == 'd')) { if (! selArcs.empty()) duplicateSelected(); return true; }

        // arrow nudge: fine steps, Shift = grid-sized time / octave pitch
        const bool arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                        || code == juce::KeyPress::upKey   || code == juce::KeyPress::downKey;
        if (arrow && ! selArcs.empty())
        {
            double dt = 0.0; int dsemi = 0;
            if (shift && timeGrid > 0) dt = 1.0 / timeGrid;
            else if (shift) dt = 0.25;
            if (code == juce::KeyPress::leftKey)  dt = -dt;
            if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::rightKey)
            {
                if (! shift) dt = (code == juce::KeyPress::rightKey ? 0.02 : -0.02);
                nudgeSelected(dt, 0);
            }
            else
            {
                dsemi = (code == juce::KeyPress::upKey) ? (shift ? 12 : 1)
                                                       : (shift ? -12 : -1);
                nudgeSelected(0.0, dsemi);
            }
            return true;
        }
        return false;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! page) return;
        const float d = (std::abs(w.deltaY) >= std::abs(w.deltaX)) ? w.deltaY : w.deltaX;
        if (d == 0.0f) return;
        const double dur = page->durationSec;
        constexpr double kStep = 1.12;

        if (e.mods.isCommandDown() && ! e.mods.isShiftDown())
        {
            // Ctrl+wheel: zoom time around the cursor
            const double tCur = xToTime(e.position.x);
            const double oldZoom = timeZoom;
            timeZoom = juce::jlimit(1.0, 16.0, timeZoom * (d > 0 ? kStep : 1.0 / kStep));
            timeOffset = tCur - (tCur - timeOffset) * (timeZoom / oldZoom);
        }
        else if (e.mods.isShiftDown())
        {
            // Shift+wheel: pan time
            timeOffset -= (double) d * 0.12 * dur / timeZoom;
        }
        else if (e.mods.isAltDown())
        {
            // Alt+wheel: zoom pitch around the cursor
            auto r = plotArea();
            const double sN = 1.0 - juce::jlimit(0.0f, 1.0f,
                        (e.position.y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
            const double nCur = sN / freqZoom + freqOffset;
            const double oldZoom = freqZoom;
            freqZoom = juce::jlimit(1.0, 8.0, freqZoom * (d > 0 ? kStep : 1.0 / kStep));
            freqOffset = nCur - (nCur - freqOffset) * (freqZoom / oldZoom);
        }
        else
        {
            // plain wheel: pan pitch (window bottom moves in norm space)
            freqOffset += (double) d * 0.10 / freqZoom;
        }

        timeOffset = juce::jlimit(0.0, juce::jmax(0.0, dur - dur / timeZoom), timeOffset);
        freqOffset = juce::jlimit(0.0, juce::jmax(0.0, 1.0 - 1.0 / freqZoom), freqOffset);
        repaint();
    }

    std::function<void(int)> onSelect;
    std::function<void(bool)> onArcEditingChanged;
    // sample-duration snapping (granular mode)
    std::map<int,double> sampleDurations;
    bool sampleGranularTab = false;
    void setPlaying(bool p) { isPlaying = p; repaint(); }

private:
    upic::Page* page = nullptr;
    std::vector<upic::Waveform>* waves = nullptr;
    double* playheadSec = nullptr;
    // v1.18: the playhead the audio thread owns is read through the getter;
    // scrubbing goes back through the seek hook.
    std::function<double()> playheadGetter;
    std::function<void(double)> onSeek;

    double playheadNow() const
    {
        if (playheadGetter != nullptr) return playheadGetter();
        return playheadSec != nullptr ? *playheadSec : 0.0;
    }

    std::function<void()> onChange;
    juce::CriticalSection* modelLock = nullptr;
    juce::CriticalSection dummyLock; // used when no model lock was provided
    Tool tool = Draw;
    int curWave = 0, curEnv = 0;
    float curInten = 0.8f;
    int curMatMode = 0, curSampleId = 1;
    double curRef = upic::kDefaultRefFreq;
    double curPmRatio = 2.0, curPmIndex = 1.5, curPmFeedback = 0.0;
    int curPmIndexEnv = 0, curPmOps = 2;
    std::vector<int> selArcs;   // multi-selection (sorted by insertion)
    int selectedZone = -1;
    bool snap = true, showGrid = true, isPlaying = false;
    int layer = 0;               // v1.8: 0 osc / 1 sample / 2 live - the tab is the material
    bool showHints = true;       // v1.7: instruction lines on/off
    double timeZoom = 1.0, timeOffset = 0.0;
    double freqZoom = 1.0, freqOffset = 0.0; // pitch view window in norm space

    bool drawing = false, movingArc = false;
    bool selectingMarquee = false, marqueeAdditive = false;
    juce::Point<float> marqueeStart, marqueeCur;
    bool drawingZone = false, scrubbing = false, scrubDragged = false, freezeAfterDrag = false;
    double lastDragMs = 0.0;
    struct StrokePt { juce::Point<float> pos; float press = 1.0f; };
    std::vector<StrokePt> strokePts;
    std::optional<juce::Point<float>> lineAnchor;
    juce::Point<float> lastMouse, moveStartPos, zoneStart, zoneCur;
    std::map<int, std::vector<upic::ArcPoint>> moveStartMap;

    // --- multi-touch view gesture (pinch = zoom, 2-finger drag = pan) ---
    struct TouchPt { int id; juce::Point<float> pos; };
    std::vector<TouchPt> touches;
    bool pinchMode = false;
    bool touchSeen = false;              // a touch pointer has used this editor
    juce::Point<float> pinchCentroid0, touchDownPos;
    float pinchDist0 = 1.0f;
    double pinchTime0 = 0.0, pinchNorm0 = 0.0;
    double pinchTimeZoom0 = 1.0, pinchFreqZoom0 = 1.0;
    unsigned int touchDownMs = 0;

    bool recordPressure = false;
    bool sieveOn = false;
    int sieveM = 12;
    std::vector<int> sieveR { 0 };
    double stickiness = 1.0; // 0..1 magnetic strength
    double timeGrid = 0.0;   // divisions per second, 0 = off
    static constexpr double kSimplifyEps = 0.006; // RDP tolerance (normalized box)

    // pen pressure of an event -> intensity multiplier (1 = no pressure info)
    static float pressureOf(const juce::MouseEvent& e)
    {
        return (e.isPressureValid() && e.pressure > 0.0f)
                 ? juce::jlimit(0.02f, 1.0f, e.pressure) : 1.0f;
    }
    float curPress(const juce::MouseEvent& e) const
    { return recordPressure ? pressureOf(e) : 1.0f; }

    juce::Rectangle<float> plotArea() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop(4); b.removeFromBottom(6); b.removeFromLeft(6); b.removeFromRight(6);
        return b;
    }
    juce::Rectangle<float> timeStripRect() const
    {
        auto r = plotArea();
        const float cy = r.getCentreY();
        return { r.getX(), cy - 7.0f, r.getWidth(), 14.0f };
    }
    // The scrub target is the TOP of the line. It runs across the middle of
    // the canvas (the pitch-centre line), so a drawing stroke that starts
    // there used to scrub instead of draw - and the grab band reached 10 dp
    // beyond the strip, straight into the drawing area. The target is now a
    // narrow band at the top of the line and it never leaves it, so the
    // canvas stays free for drawing.
    juce::Rectangle<float> timeStripHitRect() const
    {
        auto s = timeStripRect();
        const float h = touchSeen ? 6.0f : 4.0f;
        return { s.getX(), s.getY(), s.getWidth(), h };
    }

    //==========================================================================
    // multi-touch helpers
    void upsertTouch(const juce::MouseEvent& e)
    {
        for (auto& t : touches)
            if (t.id == e.source.getIndex()) { t.pos = e.position; return; }
        touches.push_back({ e.source.getIndex(), e.position });
    }
    void removeTouch(int id)
    {
        touches.erase(std::remove_if(touches.begin(), touches.end(),
                                     [&](const TouchPt& t){ return t.id == id; }),
                      touches.end());
    }
    juce::Point<float> touchCentroid() const
    {
        if (touches.empty()) return {};
        juce::Point<float> s;
        for (auto& t : touches) s += t.pos;
        return s / (float) touches.size();
    }
    float touchSpread() const
    {
        if (touches.empty()) return 1.0f;
        const auto c = touchCentroid();
        float sum = 0.0f;
        for (auto& t : touches) sum += (t.pos - c).getDistanceFromOrigin();
        return juce::jmax(1.0f, sum / (float) touches.size());
    }
    // A 2nd finger cancels whatever the 1st was doing (no half-drawn arcs),
    // then the pair becomes a view gesture: pinch = zoom (anchored at the
    // original centroid), centroid drag = pan. Mirrors the wheel bindings.
    void cancelInteraction()
    {
        if (movingArc && page)
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            for (auto& a : page->arcs)
            {
                auto it = moveStartMap.find(a.id);
                if (it != moveStartMap.end()) a.points = it->second;
            }
        }
        moveStartMap.clear();
        movingArc = false;
        drawing = false;    strokePts.clear();
        drawingZone = false;
        scrubbing = false;
        lineAnchor.reset();
    }
    void beginPinch()
    {
        if (pinchMode || touches.size() < 2) return;
        cancelInteraction();
        auto r = plotArea();
        pinchCentroid0  = touchCentroid();
        pinchDist0      = touchSpread();
        pinchTime0      = xToTime(pinchCentroid0.x);
        pinchTimeZoom0  = timeZoom;
        pinchFreqZoom0  = freqZoom;
        const double sN0 = 1.0 - juce::jlimit(0.0, 1.0,
            (double) ((pinchCentroid0.y - r.getY()) / juce::jmax(1.0f, r.getHeight())));
        pinchNorm0 = sN0 / freqZoom + freqOffset;   // pitch under the fingers
        pinchMode = true;
        repaint();
    }
    void updatePinch()
    {
        beginPinch(); // idempotent: also starts one if the flag got cleared
        if (! page || ! pinchMode) return;
        auto r = plotArea();
        const auto c = touchCentroid();
        const double ratio = (double) (touchSpread() / pinchDist0);
        const double ntz = juce::jlimit(1.0, 16.0, pinchTimeZoom0 * ratio);
        const double nfz = juce::jlimit(1.0,  8.0, pinchFreqZoom0 * ratio);
        const double dur = page->durationSec;
        const double invW = 1.0 / juce::jmax(1.0f, r.getWidth());
        const double invH = 1.0 / juce::jmax(1.0f, r.getHeight());
        const double u = juce::jlimit(0.0, 1.0, (double) ((pinchCentroid0.x - r.getX()) * invW));

        timeZoom = ntz;
        const double panDx = (double) (c.x - pinchCentroid0.x);
        timeOffset = pinchTime0 - u * dur / ntz - panDx * dur * invW / ntz;
        timeOffset = juce::jlimit(0.0, juce::jmax(0.0, dur - dur / ntz), timeOffset);

        const double sN0 = 1.0 - juce::jlimit(0.0, 1.0,
            (double) ((pinchCentroid0.y - r.getY()) * invH));
        const double sNnew = juce::jlimit(0.0, 1.0,
            sN0 - (double) (c.y - pinchCentroid0.y) * invH);
        freqZoom = nfz;
        freqOffset = juce::jlimit(0.0, juce::jmax(0.0, 1.0 - 1.0 / nfz),
                                  pinchNorm0 - sNnew / nfz);
        repaint();
    }
    // long-press menu: arc menu on an arc, zone menu inside a zone,
    // otherwise the page-level "select all" menu
    void showTouchMenu(juce::Point<float> pos)
    {
        if (tool == Zone)
        {
            const int zid = pickZone(pos);
            selectedZone = zid;
            repaint();
            if (zid >= 0) showZoneMenu(zid);
            return;
        }
        const int id = pickArc(pos);
        if (id >= 0)
        {
            setSelectedArc(id);
            if (onSelect) onSelect(id);
            repaint();
            showArcMenu(id);
            return;
        }
        juce::PopupMenu m;
        m.addItem(1, "Select all arcs");
        juce::Component::SafePointer<ScoreEditor> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [sp](int result)
        {
            if (sp == nullptr || result != 1) return;
            sp->selectAll();
            if (sp->onSelect) sp->onSelect(sp->getSelectedArc());
            sp->repaint();
        });
    }
    juce::Rectangle<float> zoneScreenRect() const
    {
        return { juce::jmin(zoneStart.x, zoneCur.x), juce::jmin(zoneStart.y, zoneCur.y),
                 std::abs(zoneCur.x - zoneStart.x), std::abs(zoneCur.y - zoneStart.y) };
    }
    juce::Rectangle<float> zoneRect(const upic::SieveZone& z) const
    {
        const float x0 = timeToX(juce::jmin(z.t0, z.t1));
        const float x1 = timeToX(juce::jmax(z.t0, z.t1));
        const float yTop  = freqToY(juce::jmax(z.f0, z.f1)); // higher freq = higher on screen
        const float yBot  = freqToY(juce::jmin(z.f0, z.f1));
        return { x0, yTop, juce::jmax(1.0f, x1 - x0), juce::jmax(1.0f, yBot - yTop) };
    }
    juce::String toolName() const
    {
        switch (tool) { case Draw: return "Draw"; case Line: return "Line"; case SineStroke: return "Sine";
                        case Select: return "Select"; case Erase: return "Erase"; case Zone: return "Zone"; }
        return "?";
    }
    static double niceStep(double raw)
    {
        double p = std::pow(10.0, std::floor(std::log10(raw)));
        double m = raw / p;
        double s = m < 1.5 ? 1 : (m < 3.5 ? 2 : (m < 7.5 ? 5 : 10));
        return s * p;
    }
    bool arcInLayer(const upic::Arc& a) const { return a.mode == layer; }
    bool arcInLayer(int id) const
    {
        if (! page) return false;
        for (auto& a : page->arcs) if (a.id == id) return arcInLayer(a);
        return false;
    }

    bool isSel(int id) const { return std::find(selArcs.begin(), selArcs.end(), id) != selArcs.end(); }
    juce::Rectangle<float> marqueeRect() const
    {
        const float x0 = juce::jmin(marqueeStart.x, marqueeCur.x);
        const float x1 = juce::jmax(marqueeStart.x, marqueeCur.x);
        const float y0 = juce::jmin(marqueeStart.y, marqueeCur.y);
        const float y1 = juce::jmax(marqueeStart.y, marqueeCur.y);
        return { x0, y0, juce::jmax(1.0f, x1 - x0), juce::jmax(1.0f, y1 - y0) };
    }

    int curMode() const { return layer; }
    upic::Arc* findArc(int id)
    {
        if (!page) return nullptr;
        for (auto& a : page->arcs) if (a.id == id) return &a;
        return nullptr;
    }

    // All arcs whose polyline passes within maxD pixels of pos, nearest first.
    // Touch pointers get a wider grab band (fat fingers, no hover).
    std::vector<int> arcsNear(juce::Point<float> pos, float maxD = -1.0f) const
    {
        if (maxD <= 0.0f) maxD = touchSeen ? 20.0f : 12.0f;
        std::vector<std::pair<float, int>> hits;
        if (! page) return {};
        for (auto& a : page->arcs)
        {
            if (! arcInLayer(a)) continue;
            float bestD = maxD;
            for (auto& pt : a.points)
                bestD = juce::jmin(bestD, (float) std::hypot(timeToX(pt.t) - pos.x,
                                                             freqToY(pt.freq) - pos.y));
            for (size_t i = 1; i < a.points.size(); ++i)
            {
                juce::Point<float> p0{ timeToX(a.points[i-1].t), freqToY(a.points[i-1].freq) };
                juce::Point<float> p1{ timeToX(a.points[i].t),   freqToY(a.points[i].freq) };
                juce::Line<float> seg(p0, p1);
                juce::Point<float> nearest;
                const float d = seg.getDistanceFromPoint(pos, nearest);
                if (d < bestD && pos.x >= juce::jmin(p0.x, p1.x) - 4
                              && pos.x <= juce::jmax(p0.x, p1.x) + 4)
                    bestD = d;
            }
            if (bestD < maxD) hits.push_back({ bestD, a.id });
        }
        std::sort(hits.begin(), hits.end(), [](auto& x, auto& y){ return x.first < y.first; });
        std::vector<int> out;
        for (auto& h : hits) out.push_back(h.second);
        return out;
    }
    int pickArc(juce::Point<float> pos)
    {
        auto v = arcsNear(pos);
        return v.empty() ? -1 : v.front();
    }
    int pickZone(juce::Point<float> pos) const
    {
        if (! page) return -1;
        const double t = xToTime(pos.x), f = yToFreq(pos.y);
        // topmost (last drawn) first
        for (auto it = page->zones.rbegin(); it != page->zones.rend(); ++it)
            if (it->contains(t, f)) return it->id;
        return -1;
    }

    void showArcMenu(int id)
    {
        juce::PopupMenu m;
        m.addItem(1, "Duplicate");
        {
            bool muted = false;
            if (auto* a = findArc(id)) muted = a->muted;
            m.addItem(2, muted ? "Unmute" : "Mute");
        }
        m.addItem(3, "Remagnetize (re-apply sieve)");
        m.addSeparator();
        m.addItem(4, "Delete");
        m.addItem(5, "Select all");
        juce::Component::SafePointer<ScoreEditor> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [sp, id](int result)
        {
            if (sp == nullptr || result == 0) return;
            switch (result)
            {
                case 1: sp->duplicateSelected(); break;
                case 2: sp->toggleMuteSelected(); break;
                case 3: sp->remagnetizeSelected(); break;
                case 4: sp->setSelectedArc(id); sp->deleteSelected(); break;
                case 5: sp->selectAll(); if (sp->onSelect) sp->onSelect(sp->getSelectedArc()); break;
                default: break;
            }
            sp->repaint();
        });
    }

    void showZoneMenu(int id)
    {
        juce::PopupMenu m;
        m.addItem(1, "Delete zone");
        m.addItem(2, "Delete all zones");
        juce::Component::SafePointer<ScoreEditor> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [sp, id](int result)
        {
            if (sp == nullptr || result == 0 || sp->page == nullptr) return;
            if (result == 1) { sp->deleteZone(id); return; }
            if (result == 2)
            {
                std::vector<int> ids;
                for (auto& z : sp->page->zones) ids.push_back(z.id);
                for (int zid : ids) sp->deleteZone(zid);
            }
        });
    }

    // Active pitch magnet at (t, f): zone > sieve > chromatic snap.
    // Returns false when pitch is free (no snap / inert sieve).
    bool pitchMagnet(int& m, std::vector<int>& R, double t, double f) const
    {
        if (! page) return false;
        if (const upic::SieveZone* z = upic::zoneAt(page->zones, t, f))
        {
            m = z->m; R = z->remainders;
            return ! R.empty(); // empty zone remainders = free inside the zone
        }
        if (sieveOn) { m = sieveM; R = sieveR; return ! R.empty(); }
        if (snap)    { m = 12; R = { 0,1,2,3,4,5,6,7,8,9,10,11 }; return true; }
        return false;
    }

    // Next magnetically allowed pitch strictly above (dir>0) or below (dir<0)
    // the current one — so arrow steps always land ON the sieve.
    double nextAllowedPitch(double f, int dir, double t) const
    {
        int m = 12; std::vector<int> R;
        if (! pitchMagnet(m, R, t, f))
            return juce::jlimit(upic::kMinFreq, upic::kMaxFreq,
                                f * std::pow(2.0, dir / 12.0));
        const double n = upic::freqToMidi(f);
        double best = dir > 0 ? 1.0e18 : -1.0e18;
        for (int r : R)
        {
            const double rr = (double) (((r % m) + m) % m);
            double q = std::floor((n - rr) / (double) m);
            double cand = q * m + rr;
            if (dir > 0) { if (cand <= n + 1e-9) cand += m; best = juce::jmin(best, cand); }
            else         { if (cand >= n - 1e-9) cand -= m; best = juce::jmax(best, cand); }
        }
        if (best < 12.0 || best > 120.0)
            return juce::jlimit(upic::kMinFreq, upic::kMaxFreq, f); // edge: stay
        return juce::jlimit(upic::kMinFreq, upic::kMaxFreq, upic::midiToFreq(best));
    }

    double nudgePitch(double t, double f, int dSemi) const
    {
        if (std::abs(dSemi) == 1) return nextAllowedPitch(f, dSemi > 0 ? 1 : -1, t);
        int m = 12; std::vector<int> R;
        const double raw = f * std::pow(2.0, dSemi / 12.0);
        if (! pitchMagnet(m, R, t, f))
            return juce::jlimit(upic::kMinFreq, upic::kMaxFreq, raw);
        return upic::sieveQuantizeHz(raw, m, R);
    }

    void commitStroke()
    {
        if (!page || strokePts.size() < 2) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        // sort by x, convert to (t,f), drop duplicates in time
        std::sort(strokePts.begin(), strokePts.end(), [](auto& a, auto& b){ return a.pos.x < b.pos.x; });
        upic::Arc a;
        a.id = page->nextArcId++;
        a.waveformId = curWave; a.envelopeId = curEnv; a.intensity = curInten;
        a.mode = curMode(); a.sampleId = curSampleId; a.refFreq = curRef;
        if (a.mode == 3)
        {
            a.pmRatio = curPmRatio; a.pmIndex = curPmIndex;
            a.pmIndexEnv = curPmIndexEnv; a.pmFeedback = curPmFeedback;
            a.pmOps = curPmOps;
        }
        double lastT = -1;
        for (auto& q : strokePts)
        {
            double t = juce::jlimit(0.0, page->durationSec, xToTime(q.pos.x));
            if (t <= lastT + 0.015) continue; // min spacing 15ms
            double f = yToFreq(q.pos.y);
            a.points.push_back({ t, f, q.press });
            lastT = t;
        }
        // simplify the raw freehand curve to a manageable control-point count
        upic::simplifyArcPoints(a.points, kSimplifyEps);
        if (tool == SineStroke && a.points.size() > 3)
        {
            // fit a sine overlay: replace freq with sine between first/last freq
            double f0 = a.points.front().freq, f1 = a.points.back().freq;
            double l0 = std::log2(f0), l1 = std::log2(f1);
            double t0 = a.points.front().t, t1 = a.points.back().t;
            for (auto& p : a.points)
            {
                double u = (p.t - t0) / juce::jmax(1e-6, t1 - t0);
                double vib = std::sin(u * juce::MathConstants<double>::twoPi * 3.0) * 0.5;
                double l = l0 + (l1 - l0) * u + vib * 0.35;
                p.freq = std::pow(2.0, l);
            }
        }
        // magnets: zone > sieve > chromatic snap, softened by stickiness;
        // the time grid (global or zone) snaps t in the same call
        for (auto& p : a.points) quantizeXY(p.t, p.freq);
        // re-drop points collapsed onto each other by grid snapping
        {
            std::vector<upic::ArcPoint> dedup;
            lastT = -1;
            for (auto& p : a.points)
            {
                if (p.t <= lastT + 0.015) continue;
                dedup.push_back(p);
                lastT = p.t;
            }
            a.points = std::move(dedup);
        }
        if (a.points.size() >= 2) { page->arcs.push_back(a); selArcs = { a.id }; notify(); }
    }

    void commitLine(juce::Point<float> p0, juce::Point<float> p1, float press = 1.0f)
    {
        if (!page) return;
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        double t0 = juce::jlimit(0.0, page->durationSec, xToTime(p0.x));
        double t1 = juce::jlimit(0.0, page->durationSec, xToTime(p1.x));
        if (std::abs(t1 - t0) < 0.05) return;
        if (t1 < t0) { std::swap(t0, t1); std::swap(p0, p1); }
        upic::Arc a; a.id = page->nextArcId++;
        a.waveformId = curWave; a.envelopeId = curEnv; a.intensity = curInten;
        a.mode = curMode(); a.sampleId = curSampleId; a.refFreq = curRef;
        if (a.mode == 3)
        {
            a.pmRatio = curPmRatio; a.pmIndex = curPmIndex;
            a.pmIndexEnv = curPmIndexEnv; a.pmFeedback = curPmFeedback;
            a.pmOps = curPmOps;
        }
        // subdivide line into ~ every 0.25s for later editing
        int n = juce::jlimit(2, 64, (int)std::ceil((t1 - t0) / 0.25));
        for (int i = 0; i <= n; ++i)
        {
            double u = (double)i / n;
            float y = p0.y + (p1.y - p0.y) * (float)u;
            double t = t0 + (t1 - t0) * u;
            double f = yToFreq(y);
            a.points.push_back({ t, f, press });
        }
        for (auto& p : a.points) quantizeXY(p.t, p.freq);
        page->arcs.push_back(a); selArcs = { a.id }; notify();
    }

    void notify() { repaint(); if (onChange) onChange(); }
};
