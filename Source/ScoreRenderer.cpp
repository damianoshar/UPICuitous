#include "ScoreRenderer.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// R5.1 (the review's R043): the score canvas's render.
//
// The bodies are the editor's, body for body. The colours, the order of the
// passes and the wording of the hint lines do not change by a character;
// Source/SelfTestScoreRender.cpp pins the picture.
namespace upic
{
    namespace score_render
    {
        //================ the view ===========================================
        double View::xToTime (float x) const
        {
            float f = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
            return f * durationSec / timeZoom + timeOffset;
        }

        float View::timeToX (double t) const
        {
            double f = (t - timeOffset) * timeZoom / juce::jmax (0.001, durationSec);
            return plot.getX() + (float) f * plot.getWidth();
        }

        double View::yToFreq (float y) const
        {
            float sN = 1.0f - juce::jlimit (0.0f, 1.0f, (y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()));
            double norm = (double) sN / freqZoom + freqOffset;
            return upic::normYToFreqIn ((float) juce::jlimit (0.0, 1.0, norm),
                                        pitchLo, pitchHi);
        }

        float View::freqToY (double f) const
        {
            // NOT clamped: a pitch outside the page's ambitus (or outside the
            // zoomed window) has to be drawn off the edge, where the plot area
            // clips it - clamping it would draw it as a line along the border.
            double sN = (upic::freqToNormYIn (f, pitchLo, pitchHi) - freqOffset) * freqZoom;
            return plot.getBottom() - (float) sN * plot.getHeight();
        }

        juce::Rectangle<float> View::zoneRect (const SieveZone& z) const
        {
            const float x0 = timeToX (juce::jmin (z.t0, z.t1));
            const float x1 = timeToX (juce::jmax (z.t0, z.t1));
            const float yTop  = freqToY (juce::jmax (z.f0, z.f1)); // higher freq = higher on screen
            const float yBot  = freqToY (juce::jmin (z.f0, z.f1));
            return { x0, yTop, juce::jmax (1.0f, x1 - x0), juce::jmax (1.0f, yBot - yTop) };
        }

        // The grid's step: a round number the time lines fall on.
        static double niceStep (double raw)
        {
            double p = std::pow (10.0, std::floor (std::log10 (raw)));
            double m = raw / p;
            double s = m < 1.5 ? 1 : (m < 3.5 ? 2 : (m < 7.5 ? 5 : 10));
            return s * p;
        }

        //================ the render =========================================
        void paint (juce::Graphics& g, const State& s)
        {
            g.fillAll (upic::uiBackground());
            auto r = s.view.plot;

            // frame
            g.setColour(juce::Colour(0xff1e1e26));
            g.fillRect(r);

            // Canvas texts (grid labels, zone labels, arc tags) are collected here
            // and painted in one final pass, so they always sit above zones/arcs.
            struct OverlayText { juce::String s; float fontH; juce::Colour c; int x, y; };
            std::vector<OverlayText> overlay;

            // sieve zones: translucent magnetic regions (under the grid)
            if (s.page)
            {
                for (const auto& z : s.page->zones)
                {
                    auto rect = s.view.zoneRect (z);
                    g.setColour(juce::Colour(0x2a5ac8fa));
                    g.fillRect(rect);
                    g.setColour(z.id == s.selectedZone ? juce::Colour(0xff8ad4ff)
                                                        : juce::Colour(0x805ac8fa));
                    g.drawRect(rect, z.id == s.selectedZone ? 2.0f : 1.0f);
                    juce::String lab = "m" + juce::String(z.m) + "{";
                    if ((int) z.remainders.size() >= z.m)
                        lab += "all";
                    else
                        for (size_t i = 0; i < z.remainders.size(); ++i)
                            lab += (i > 0 ? "," : "") + juce::String(z.remainders[i]);
                    lab += "}";
                    if (z.timeGrid > 0) lab += " " + juce::String(z.timeGrid, 0) + "/s";
                    if (z.stickiness >= 0.0)
                        lab += " s" + juce::String((int) std::round(z.stickiness * 100.0)) + "%";
                    overlay.push_back({ lab, 10.0f, juce::Colour(0xffb8e0ff), (int) rect.getX() + 4, (int) rect.getY() + 13 });
                }
            }

            // grid: time (sec) + pitch (chromatic C0..C9 + 440 line)
            if (s.showGrid && s.page)
            {
                double dur = s.page->durationSec;
                // vertical time lines every nice step
                double step = niceStep(dur / 8.0);
                g.setColour(juce::Colour(0xff2a2a35));
                for (double t = 0; t <= dur + 1e-9; t += step)
                {
                    float x = s.view.timeToX (t);
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
                if (s.layer == 1 || s.layer == 2)
                {
                    const double ref = s.curRef > 0.0 ? s.curRef : upic::kDefaultRefFreq;
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
                            float yh = s.view.freqToY (fh);
                            if (yh >= r.getY() && yh <= r.getBottom())
                            {
                                g.setColour(juce::Colour(0xff26262f));
                                g.drawHorizontalLine((int) yh, r.getX(), r.getRight());
                            }
                        }
                        if (f < upic::kMinFreq || f > upic::kMaxFreq) continue;
                        float y = s.view.freqToY (f);
                        if (y < r.getY() || y > r.getBottom()) continue;
                        const bool isRef = (k == 0);
                        g.setColour(isRef ? juce::Colour(0xffff9f0a) : juce::Colour(0xff3a3a4d));
                        g.drawHorizontalLine((int) y, r.getX(), r.getRight());
                        juce::String lab = ratioText(f / ref);
                        if (isRef) lab += (s.layer == 2 ? " (input)" : " (Ref)");
                        overlay.push_back({ lab, 10.0f, juce::Colour(0xff8a8a99),
                                            (int) r.getX() + 3, (int) y - 3 });
                    }
                }
                else if (s.tuning != nullptr && s.tuning->custom)
                {
                    // M4: with a tuning loaded the guide lines are the scale's
                    // own degrees - the base (1/1) is the strong line, the
                    // other degrees the faint ones.
                    const int n = s.tuning->degrees();
                    int faint = 0;
                    for (const auto& st : s.tuning->axisStepsInRange(upic::kMinFreq, upic::kMaxFreq))
                    {
                        const double f = st.freq;
                        const long long si = (long long) std::llround(st.step);
                        const bool isBase = (((si % n) + n) % n) == 0;
                        float y = s.view.freqToY (f);
                        if (y < r.getY() || y > r.getBottom()) continue;
                        if (! isBase)
                        {
                            // the canvas is not a hatch: a scale with many
                            // degrees keeps only the first of them
                            if (++faint > 120) continue;
                            g.setColour(juce::Colour(0xff26262f));
                            g.drawHorizontalLine((int)y, r.getX(), r.getRight());
                            continue;
                        }
                        g.setColour(juce::Colour(0xff3a3a4d));
                        g.drawHorizontalLine((int)y, r.getX(), r.getRight());
                        const int octave = (int) std::floor(st.step / (double) n);
                        overlay.push_back({ juce::MidiMessage::getMidiNoteName(s.tuning->baseNote + 12 * octave,
                                                                               true, true, 3),
                                            10.0f, juce::Colour(0xff8a8a99), (int)r.getX() + 3, (int)y - 3 });
                    }
                }
                else
                for (int midi = 12; midi <= 120; ++midi)
                {
                    double f = upic::midiToFreq((double)midi);
                    if (f < upic::kMinFreq || f > upic::kMaxFreq) continue;
                    const bool isC = (midi % 12 == 0);
                    const bool isA440 = (midi == 69);
                    float y = s.view.freqToY (f);
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

            // v1.33.2: show the active sieve's allowed pitches as faint guides.
            // Switching the Sieve box on/off changes the page immediately, and a
            // zone's own sieve and time grid are visible inside the zone. The
            // pitch guides are Hz/MIDI, so they belong to the Osc layer only
            // (Sample and Live map the vertical axis to ratios).
            if (s.page)
            {
                const bool pitchGuides = (s.layer == 0);
                auto drawSievePitches = [&](int m, const std::vector<int>& R,
                                            double fLo, double fHi,
                                            float x0, float x1, juce::Colour col)
                {
                    if (m < 2 || R.empty() || x1 <= x0 + 1.0f) return;
                    if (s.tuning == nullptr) return;
                    // collect first: a dense sieve (e.g. all twelve semitones
                    // across nine octaves) would be a solid hatch, not a guide
                    std::vector<float> ys;
                    // M4: the sieve's m/R works on the tuning's axis - the
                    // semitones in 12-tone equal temperament, the scale's
                    // degrees in a tuning.
                    for (const auto& st : s.tuning->axisStepsInRange(fLo, fHi))
                    {
                        const long long si = (long long) std::llround(st.step);
                        const int rr = (int) (((si % m) + m) % m);
                        if (std::find (R.begin(), R.end(), rr) == R.end()) continue;
                        const float y = s.view.freqToY (st.freq);
                        if (y < r.getY() || y > r.getBottom()) continue;
                        if ((int) ys.size() >= 96) return;
                        ys.push_back (y);
                    }
                    g.setColour (col);
                    for (float y : ys)
                        g.drawHorizontalLine ((int) y, x0, x1);
                };

                if (pitchGuides && s.sieveOn)
                    drawSievePitches (s.sieveM, *s.sieveR, s.view.pitchLo, s.view.pitchHi,
                                      r.getX(), r.getRight(), juce::Colour (0x2438bdf8));

                for (const auto& z : s.page->zones)
                {
                    const auto clip = s.view.zoneRect (z).getIntersection (r);
                    if (clip.isEmpty()) continue;
                    if (pitchGuides)
                        drawSievePitches (z.m, z.remainders,
                                          juce::jmin (z.f0, z.f1), juce::jmax (z.f0, z.f1),
                                          clip.getX(), clip.getRight(), juce::Colour (0x3048e0a0));
                    if (z.timeGrid > 0)
                    {
                        g.setColour (juce::Colour (0x2848e0a0));
                        const double step = 1.0 / z.timeGrid;
                        int guard = 0;
                        for (double t = 0.0; t <= s.page->durationSec + 1e-9 && guard < 512;
                             t += step, ++guard)
                        {
                            const float x = s.view.timeToX (t);
                            if (x < clip.getX() || x > clip.getRight()) continue;
                            g.drawVerticalLine ((int) x, clip.getY(), clip.getBottom());
                        }
                    }
                }
            }

            // arcs (each tab draws only its own layer: osc = mode 0,
            // sample = mode 1, live = mic-input arcs, mode 2)
            if (s.page)
            {
                const auto isSel = [&s] (int id)
                {
                    if (s.selArcs == nullptr) return false;
                    return std::find (s.selArcs->begin(), s.selArcs->end(), id) != s.selArcs->end();
                };

                for (const auto& arc : s.page->arcs)
                {
                    if (arc.mode != s.layer) continue;
                    const bool isLive = (arc.mode == 2);
                    juce::Colour c = juce::Colours::cyan;
                    if (s.waves)
                    {
                        for (const auto& w : *s.waves) if (w.id == arc.waveformId) { c = w.colour; break; }
                    }
                    if (isLive) c = juce::Colour(0xffeceff4); // live input: neutral white
                    const bool sel = isSel (arc.id);
                    if (sel) c = c.brighter(0.6f);
                    if (arc.muted) c = c.withAlpha(0.25f);

                    juce::Path p;
                    bool first = true;
                    for (const auto& pt : arc.points)
                    {
                        float x = s.view.timeToX (pt.t), y = s.view.freqToY (pt.freq);
                        if (first) { p.startNewSubPath(x, y); first = false; }
                        else p.lineTo(x, y);
                    }
                    g.setColour(c.withAlpha(arc.muted ? 0.3f : 0.95f));
                    g.strokePath(p, juce::PathStrokeType(sel ? 3.0f : 2.0f));

                    // dots (radius reflects the pressure-intensity curve)
                    g.setColour(c.brighter(0.3f));
                    for (const auto& pt : arc.points)
                    {
                        const float rr = 2.5f * (0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, pt.i));
                        g.fillEllipse((float) s.view.timeToX (pt.t) - rr, (float) s.view.freqToY (pt.freq) - rr,
                                      rr * 2.0f, rr * 2.0f);
                    }

                    // tag at end ("live" marks mic-input arcs)
                    if (!arc.points.empty())
                    {
                        auto lp = arc.points.back();
                        overlay.push_back({ isLive ? "live e" + juce::String(arc.envelopeId)
                                                   : "v" + juce::String(arc.waveformId) + " e" + juce::String(arc.envelopeId),
                                        9.0f, c, (int)s.view.timeToX (lp.t) - 34, (int)s.view.freqToY (lp.freq) - 7 });
                    }
                }
            }

            // in-progress stroke
            if (s.strokePts != nullptr && !s.strokePts->empty())
            {
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                juce::Path p; bool f = true;
                for (const auto& q : *s.strokePts) { if (f) { p.startNewSubPath(q.pos); f = false; } else p.lineTo(q.pos); }
                g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
            }
            if (s.hasLineAnchor) // rubber band
            {
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.drawLine(s.lineAnchor.x, s.lineAnchor.y, s.lastMouse.x, s.lastMouse.y, 1.5f);
            }
            if (s.drawingZone) // zone rubber rectangle
            {
                g.setColour(juce::Colours::white.withAlpha(0.5f));
                g.drawRect(s.zoneRubberRect, 1.0f);
            }

            // marquee selection rectangle (Select tool, drag on empty space)
            if (s.selectingMarquee)
            {
                auto mr = s.marqueeRect;
                g.setColour(juce::Colour(0x30ffffff));
                g.fillRect(mr);
                g.setColour(juce::Colour(0xff8ab0ff));
                g.drawRect(mr, 1.0f);
            }

            // centre scrub line (drag the top edge to move the playhead). No box
            // or time readout: the transport row already shows the playhead time.
            {
                auto grip = s.scrubGrip;
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
            if (s.hasPlayhead)
            {
                float x = s.view.timeToX (s.playhead);
                if (x >= r.getX() && x <= r.getRight())
                {
                    g.setColour(juce::Colour(0xffff375f));
                    if (s.isPlaying || s.scrubbing || s.playhead > 0.0)
                        g.drawVerticalLine((int)x, r.getY(), r.getBottom());
                    juce::Path tri;
                    tri.addTriangle(x - 5, r.getY(), x + 5, r.getY(), x, r.getY() + 8);
                    g.fillPath(tri);
                }
            }

            // border + hint
            g.setColour(juce::Colour(0xff34343f));
            g.drawRect(r, 1);
            // WP9: the workspace's mark - this canvas shows the page the
            // transport plays. The canvas paints it, because the workspace
            // only arranges the canvases (it is not above them).
            if (s.workspaceMarked)
            {
                g.setColour(juce::Colour(0xff30d158));
                g.drawRect(r, 2);
                if (s.workspaceCaption.isNotEmpty())
                {
                    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
                    g.drawText(s.workspaceCaption, (int) r.getX() + 6, (int) r.getY() + 2,
                               juce::jmin((int) r.getWidth() - 12, 170), 16,
                               juce::Justification::centredLeft, false);
                }
            }
            juce::String hint = (s.layer == 2)
                ? juce::String("LIVE tab: arcs multiply the mic input pitch (1x = input, 2x = octave up), pitch mode in Audio  |  Del: delete  |  Tool=") + s.toolName
                : (s.layer == 1)
                    ? juce::String("SAMPLE tab: drawn arcs play the slot; Pitch sets how they transpose it  |  Del: delete  |  Tool=") + s.toolName
                    : juce::String("Draw: drag  |  R-click: menu  |  Shift-click: cycle  |  Ctrl+A: all  |  Ctrl+D: dup  |  Del: delete  |  Tool=") + s.toolName;
            if (s.zoneTool) hint = "Zone: drag a magnetic region (inherits sieve + grid + stick)  |  drag inside to move, an edge to resize  |  R-click: sieve / grid / stick  |  Del: remove  |  Tool=Zone";
            if (s.sieveOn && s.sieveR != nullptr)
            {
                hint += "  |  SIEVE (global) m" + juce::String(s.sieveM) + "{";
                for (size_t i = 0; i < s.sieveR->size(); ++i)
                    hint += (i > 0 ? "," : "") + juce::String((*s.sieveR)[i]);
                hint += "}";
            }
            if (s.timeGrid > 0) hint += "  |  GRID " + juce::String(s.timeGrid, 0) + "/s";
            if (s.stickiness < 0.999) hint += "  |  STICK " + juce::String((int) std::round(s.stickiness * 100)) + "%";
            if (s.recordPressure) hint += "  |  PRESS on";
            if (s.selectedZone >= 0) hint += "  |  ZONE #" + juce::String(s.selectedZone) + " selected (Del removes)";
            // instruction lines: right-aligned to the plot edge, slightly bold
            auto drawHintRight = [&](const juce::String& str, float fontH, juce::Colour col, int y)
            {
                g.setFont(juce::Font(juce::FontOptions(fontH, juce::Font::bold)));
                g.setColour(col);
                const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), str);
                g.drawSingleLineText(str, juce::jmax((int)r.getX() + 4, (int)r.getRight() - w - 6), y);
            };
            if (s.showHints)
            {
                drawHintRight(hint, 11.0f, juce::Colour(0xff9a9aa5), (int)r.getY() + 14);
                drawHintRight("Wheel: pan pitch  |  Alt+wheel: zoom pitch  |  Ctrl+wheel: zoom time  |  Shift+wheel: pan time  |  Arrows: nudge selection  |  drag the centre line: scrub",
                              10.0f, juce::Colour(0xff6f7080), (int)r.getY() + 28);
            }
        }
    }
}
