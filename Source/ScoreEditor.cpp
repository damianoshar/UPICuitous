#include "ScoreEditor.h"

//==============================================================================
// The ScoreEditor implementation. It used to live inside the class in the
// header; moving it here keeps the header (and every translation unit that
// includes MainComponent.h) from parsing the editor's bodies.
//==============================================================================

ScoreEditor::ScoreEditor()
{
        setWantsKeyboardFocus(true);
    }

void ScoreEditor::bind(upic::Page* p, std::vector<upic::Waveform>* w, double* playhead, std::function<void()> changed)
{
        page = p; waves = w; playheadSec = playhead; onChange = changed;
        // page-local selection must not leak across pages (zone ids are
        // page-local; arc ids repeat between pages)
        selectedZone = -1;
        selArcs.clear();
        drawingZone = false; movingArc = false; scrubbing = false;
        draggingZone = false; zoneHandle = 0; zoneDragId = -1;
        repaint();
    }

void ScoreEditor::setPlayheadSource(std::function<double()> getter, std::function<void(double)> seek)
{ playheadGetter = std::move(getter); onSeek = std::move(seek); }

void ScoreEditor::setModelLock(juce::CriticalSection* l)
{ modelLock = l; }

juce::Rectangle<float> ScoreEditor::timeStripBounds() const
{ return timeStripRect(); }

juce::Rectangle<float> ScoreEditor::scrubTargetRect() const
{ return timeStripHitRect(); }

juce::Rectangle<float> ScoreEditor::playheadHandleRect() const
{
        auto r = plotArea();
        const float x = timeToX(playheadNow());
        const float w = touchSeen ? 32.0f : 24.0f;
        const float h = touchSeen ? 24.0f : 16.0f;
        return { x - w * 0.5f, r.getY(), w, h };
    }

void ScoreEditor::setTool(Tool t)
{ tool = t; if (t != Zone) selectedZone = -1; repaint(); }

ScoreEditor::Tool ScoreEditor::getTool() const
{ return tool; }

void ScoreEditor::setCurrentVoice(int wid, int eid, float inten, int matMode, int sampleId, double refFreq)
{ curWave = wid; curEnv = eid; curInten = inten;
      const bool refChanged = (curRef != refFreq);
      curMatMode = matMode; curSampleId = sampleId; curRef = refFreq;
      // the Sample and Live grids are both Ref-relative ratio grids
      if (refChanged && (layer == 1 || layer == 2)) repaint(); }

void ScoreEditor::setCurrentStep(int divisions)
{ curStep = juce::jlimit(0, 99, divisions); }

void ScoreEditor::setCurrentSpace(int space)
{ curSpace = juce::jlimit(0, 3, space); }

int ScoreEditor::getCurrentSpace() const
{ return curSpace; }

void ScoreEditor::setCurrentPmVoice(double ratio, double index, int indexEnv, double feedback, int ops)
{
        curPmRatio = juce::jlimit(0.1, 32.0, ratio);
        curPmIndex = juce::jlimit(0.0, 20.0, index);
        curPmIndexEnv = juce::jmax(0, indexEnv);
        curPmFeedback = juce::jlimit(0.0, 1.0, feedback);
        curPmOps = (ops == 4 ? 4 : 2);
    }

void ScoreEditor::setSelectedArc(int id)
{ selArcs.clear(); if (arcInLayer(id)) selArcs.push_back(id); repaint(); }

void ScoreEditor::setSelectedArcs(const std::vector<int>& ids)
{ selArcs.clear(); for (int id : ids) if (arcInLayer(id)) selArcs.push_back(id); repaint(); }

int ScoreEditor::getSelectedArc() const
{ return selArcs.empty() ? -1 : selArcs.front(); }

const std::vector<int>& ScoreEditor::getSelectedArcs() const
{ return selArcs; }

void ScoreEditor::selectAll()
{
        selArcs.clear();
        if (page) for (auto& a : page->arcs) if (arcInLayer(a)) selArcs.push_back(a.id);
        repaint();
    }

int ScoreEditor::getSelectedZone() const
{ return selectedZone; }

juce::Rectangle<float> ScoreEditor::zoneRectForTest(int id) const
{
        if (page == nullptr) return {};
        for (auto& z : page->zones) if (z.id == id) return zoneRect (z);
        return {};
    }

double ScoreEditor::zoneTimeForTest(int id, int which) const
{
        if (page == nullptr) return -1.0;
        for (auto& z : page->zones)
            if (z.id == id) return which == 0 ? z.t0 : z.t1;
        return -1.0;
    }

void ScoreEditor::beginZoneDragForTest(juce::Point<float> pos)
{ beginZoneDrag (pos); }

void ScoreEditor::dragZoneToForTest(juce::Point<float> pos)
{ dragZoneTo (pos); }

bool ScoreEditor::commitLineForTest(juce::Point<float> p0, juce::Point<float> p1)
{ return commitLine (p0, p1); }

bool ScoreEditor::commitStrokeForTest(const std::vector<juce::Point<float>>& points)
{
    strokePts.clear();
    for (const auto& point : points)
        strokePts.push_back ({ point, 1.0f });
    const bool added = commitStroke();
    strokePts.clear();
    return added;
}

bool ScoreEditor::lastArcPointsSortedForTest() const
{
        if (page == nullptr || page->arcs.empty()) return false;
        const auto& pts = page->arcs.back().points;
        for (size_t i = 1; i < pts.size(); ++i)
            if (pts[i].t < pts[i - 1].t) return false;
        return true;
    }

bool ScoreEditor::lastArcHasStaircaseForTest() const
{
        if (page == nullptr || page->arcs.empty()) return false;
        const auto& pts = page->arcs.back().points;
        for (size_t i = 1; i < pts.size(); ++i)
            if (std::abs (pts[i].t - pts[i - 1].t) < 1e-9
                && std::abs (pts[i].freq - pts[i - 1].freq) > 1e-9)
                return true;
        return false;
    }

std::vector<double> ScoreEditor::lastArcFreqsForTest() const
{
        std::vector<double> f;
        if (page == nullptr || page->arcs.empty()) return f;
        for (auto& p : page->arcs.back().points) f.push_back (p.freq);
        return f;
    }

void ScoreEditor::applyZoneMenuForTest(int id, int action)
{ applyZoneMenu (id, action); }

int ScoreEditor::lastArcPointCountForTest() const
{
    if (page == nullptr || page->arcs.empty()) return 0;
    return (int) page->arcs.back().points.size();
}

juce::String ScoreEditor::remaindersTextForTest(const std::vector<int>& r)
{ return remaindersText (r); }

int ScoreEditor::zoneSieveMForTest(int id) const
{
        if (page == nullptr) return -1;
        for (auto& z : page->zones) if (z.id == id) return z.m;
        return -1;
    }

std::vector<int> ScoreEditor::zoneSieveRForTest(int id) const
{
        if (page == nullptr) return {};
        for (auto& z : page->zones) if (z.id == id) return z.remainders;
        return {};
    }

double ScoreEditor::zoneTimeGridForTest(int id) const
{
        if (page == nullptr) return -1.0;
        for (auto& z : page->zones) if (z.id == id) return z.timeGrid;
        return -1.0;
    }

double ScoreEditor::zoneStickinessForTest(int id) const
{
        if (page == nullptr) return -2.0;
        for (auto& z : page->zones) if (z.id == id) return z.stickiness;
        return -2.0;
    }

void ScoreEditor::setSnap(bool s)
{ snap = s; }

void ScoreEditor::setShowGrid(bool s)
{ showGrid = s; repaint(); }

void ScoreEditor::setLayer(int m)
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

int ScoreEditor::getLayer() const
{ return layer; }

void ScoreEditor::setShowHints(bool on)
{ showHints = on; repaint(); }

bool ScoreEditor::getShowHints() const
{ return showHints; }

void ScoreEditor::setRecordPressure(bool on)
{ recordPressure = on; }

bool ScoreEditor::getRecordPressure() const
{ return recordPressure; }

void ScoreEditor::setSieve(bool on, int m, std::vector<int> remainders)
{
        sieveOn = on;
        sieveM = juce::jmax(2, m);
        // the global sieve's empty remainder list is the *off* state, not a
        // sieve: keep the previous set, so a cleared field cannot turn the
        // checkbox into a no-op (a zone may still be empty = free pitch there)
        if (! remainders.empty()) sieveR = std::move(remainders);
        repaint();
    }

bool ScoreEditor::getSieveOn() const
{ return sieveOn; }

int ScoreEditor::getSieveM() const
{ return sieveM; }

const std::vector<int>& ScoreEditor::getSieveR() const
{ return sieveR; }

void ScoreEditor::setStickiness(double s)
{ stickiness = juce::jlimit(0.0, 1.0, s); }

double ScoreEditor::getStickiness() const
{ return stickiness; }

void ScoreEditor::setTimeGrid(double div)
{ timeGrid = juce::jlimit(0.0, 64.0, div); repaint(); }

double ScoreEditor::getTimeGrid() const
{ return timeGrid; }

void ScoreEditor::setTuning (const upic::Tuning& t)
{
    tuning = t;
    repaint();
}

const upic::Tuning& ScoreEditor::getTuning() const
{ return tuning; }

double ScoreEditor::xToTime(float x) const
{
        auto r = plotArea();
        float f = juce::jlimit(0.0f, 1.0f, (x - r.getX()) / juce::jmax(1.0f, r.getWidth()));
        double dur = page ? page->durationSec : 30.0;
        return f * dur / timeZoom + timeOffset;
    }

float ScoreEditor::timeToX(double t) const
{
        auto r = plotArea();
        double dur = page ? page->durationSec : 30.0;
        double f = (t - timeOffset) * timeZoom / juce::jmax(0.001, dur);
        return r.getX() + (float)f * r.getWidth();
    }

double ScoreEditor::ambitusLo() const
{ return page != nullptr ? page->pitchLo : upic::kDefaultPitchLo; }

double ScoreEditor::ambitusHi() const
{ return page != nullptr ? page->pitchHi : upic::kDefaultPitchHi; }

double ScoreEditor::yToFreq(float y) const
{
        auto r = plotArea();
        float sN = 1.0f - juce::jlimit(0.0f, 1.0f, (y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
        double norm = (double) sN / freqZoom + freqOffset;
        return upic::normYToFreqIn((float) juce::jlimit(0.0, 1.0, norm),
                                   ambitusLo(), ambitusHi());
    }

float ScoreEditor::freqToY(double f) const
{
        auto r = plotArea();
        // NOT clamped: a pitch outside the page's ambitus (or outside the
        // zoomed window) has to be drawn off the edge, where the plot area
        // clips it - clamping it would draw it as a line along the border.
        double sN = (upic::freqToNormYIn(f, ambitusLo(), ambitusHi()) - freqOffset) * freqZoom;
        return r.getBottom() - (float) sN * r.getHeight();
    }

//==============================================================================
// F6 (2026-11-17): the view commands.
//
// They work on the same fields the cursor-anchored wheel zoom uses, so the
// anchoring still works after them - and the view's centre is kept, so a
// zoom in and out returns to where it started.
void ScoreEditor::zoomBy (double factor)
{
    if (page == nullptr) return;
    const double dur = page->durationSec;
    const double cT = timeOffset + dur / (2.0 * timeZoom);
    const double cN = freqOffset + 0.5 / freqZoom;
    timeZoom = juce::jlimit (1.0, 16.0, timeZoom * factor);
    freqZoom = juce::jlimit (1.0, 8.0, freqZoom * factor);
    timeOffset = cT - dur / (2.0 * timeZoom);
    freqOffset = cN - 0.5 / freqZoom;
    if (onViewChanged) onViewChanged();
    repaint();
}

void ScoreEditor::zoomIn()  { zoomBy (1.25); }
void ScoreEditor::zoomOut() { zoomBy (1.0 / 1.25); }

void ScoreEditor::fitPage()
{
    timeZoom = 1.0; timeOffset = 0.0;
    freqZoom = 1.0; freqOffset = 0.0;
    if (onViewChanged) onViewChanged();
    repaint();
}

void ScoreEditor::resetZoom()
{
    if (page == nullptr) return;
    // back to 1x, but the view's centre stays where it was (unlike Fit
    // Page, which goes to the start of the page)
    const double dur = page->durationSec;
    const double cT = timeOffset + dur / (2.0 * timeZoom);
    const double cN = freqOffset + 0.5 / freqZoom;
    timeZoom = 1.0; freqZoom = 1.0;
    timeOffset = cT - dur / 2.0;
    freqOffset = cN - 0.5;
    if (onViewChanged) onViewChanged();
    repaint();
}

void ScoreEditor::fitSelection()
{
    if (page == nullptr) { fitPage(); return; }

    double t0 = 0.0, t1 = 0.0, f0 = 0.0, f1 = 0.0;
    bool any = false;
    for (auto& a : page->arcs)
    {
        if (! isSel (a.id)) continue;
        for (auto& p : a.points)
        {
            if (! any) { t0 = t1 = p.t; f0 = f1 = p.freq; any = true; continue; }
            t0 = juce::jmin (t0, p.t); t1 = juce::jmax (t1, p.t);
            f0 = juce::jmin (f0, p.freq); f1 = juce::jmax (f1, p.freq);
        }
    }
    if (! any) { fitPage(); return; }

    // the selection fills 80% of the view (a margin on each side)
    const double dur = page->durationSec;
    const double tSpan = juce::jmax (1e-6, t1 - t0);
    timeZoom = juce::jlimit (1.0, 16.0, 0.8 * dur / tSpan);
    timeOffset = t0 - 0.1 * (dur / timeZoom);

    // the pitch: the norm space (logarithmic) the editor maps through
    const double n0 = upic::freqToNormYIn (f0, ambitusLo(), ambitusHi());
    const double n1 = upic::freqToNormYIn (f1, ambitusLo(), ambitusHi());
    const double nSpan = juce::jmax (1e-6, n1 - n0);
    freqZoom = juce::jlimit (1.0, 8.0, 0.8 / nSpan);
    freqOffset = n0 - 0.1 * (1.0 / freqZoom);

    if (onViewChanged) onViewChanged();
    repaint();
}

void ScoreEditor::setFineDrawing (bool on)
{
    fineDrawing = on;
    if (onViewChanged) onViewChanged();
}

//==============================================================================
// F6: the numeric edit of the selection.
//
// The fields move the selected arcs the same way a drag does: the time is
// shifted and the frequency is scaled by a ratio. The undo transaction and
// the model revision see it, because it goes through the same path.
bool ScoreEditor::selectionAnchor (double& tSec, double& hz) const
{
    if (page == nullptr) return false;
    for (auto& a : page->arcs)
    {
        if (! isSel (a.id) || a.points.empty()) continue;
        tSec = a.points.front().t;
        hz = a.points.front().freq;
        return true;
    }
    return false;
}

bool ScoreEditor::setSelectionTime (double tSec)
{
    if (page == nullptr || selArcs.empty()) return false;
    double aT = 0.0, aF = 0.0;
    if (! selectionAnchor (aT, aF)) return false;
    const double dt = tSec - aT;
    if (std::abs (dt) < 1e-9) return false;

    {
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        for (auto& a : page->arcs)
        {
            if (! isSel (a.id)) continue;
            for (auto& p : a.points)
                p.t = juce::jlimit (0.0, page->durationSec, p.t + dt);
            std::stable_sort (a.points.begin(), a.points.end(),
                              [] (const upic::ArcPoint& x, const upic::ArcPoint& y){ return x.t < y.t; });
        }
    }
    notify();
    if (onViewChanged) onViewChanged();
    return true;
}

bool ScoreEditor::setSelectionFreq (double hz)
{
    if (page == nullptr || selArcs.empty()) return false;
    double aT = 0.0, aF = 0.0;
    if (! selectionAnchor (aT, aF)) return false;
    const double ratio = hz / juce::jmax (1e-6, aF);
    if (std::abs (ratio - 1.0) < 1e-9) return false;

    {
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        for (auto& a : page->arcs)
        {
            if (! isSel (a.id)) continue;
            for (auto& p : a.points)
                p.freq = juce::jlimit (upic::kMinFreq, upic::kMaxFreq, p.freq * ratio);
        }
    }
    notify();
    if (onViewChanged) onViewChanged();
    return true;
}

void ScoreEditor::mouseMove (const juce::MouseEvent& e)
{
    // F6: the live readout - the cursor's position through the editor's
    // own mapping (the same one the hit tests use).
    if (onCursorMove != nullptr && page != nullptr)
    {
        const auto r = plotArea();
        if (r.contains (e.position))
            onCursorMove (xToTime (e.position.x), yToFreq (e.position.y));
    }
}

void ScoreEditor::quantizeXY(double& t, double& f) const
{
        if (! page) return;
        upic::quantizePoint(snap, sieveOn, sieveM, sieveR, timeGrid, stickiness,
                            page->zones, tuning, t, f);
        // v1.33: the page's ambitus - the drawing lives inside the page's
        // interval, so a stroke cannot leave the paper.
        f = juce::jlimit (page->pitchLo, page->pitchHi, f);
    }

void ScoreEditor::deleteSelected()
{
        if (! page || selArcs.empty()) return;
        // v1.33.4 (L15, part 2): the undo transaction - the app keeps the
        // state from before the change.
        if (onEditStart) onEditStart ("Delete");
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            auto& v = page->arcs;
            v.erase(std::remove_if(v.begin(), v.end(),
                    [&](auto& a){ return std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end(); }),
                    v.end());
        }
        selArcs.clear();
        if (selectedZone >= 0 && tool != Zone) selectedZone = -1;
        // The callback refreshes the controls and takes the model lock itself;
        // it must never run while that lock is held.
        notify();
        if (onEditEnd) onEditEnd();
    }

void ScoreEditor::duplicateSelected()
{
        if (! page || selArcs.empty()) return;
        std::vector<int> created;
        // v1.33.4 (L15, part 2): the undo transaction
        if (onEditStart) onEditStart ("Duplicate");
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            const double off = juce::jlimit(0.1, 1.0, page->durationSec * 0.05);
            std::vector<upic::Arc> copies;
            for (auto& id : selArcs)
                for (auto& a : page->arcs)
                    if (a.id == id)
                    {
                        // v1.33.4 (H13): the page's arc count is capped (the
                        // piece format refuses more), so the copy that does
                        // not fit is not made - the loader would drop it
                        // silently on the next open.
                        if ((int) page->arcs.size() + (int) copies.size()
                                >= upic::limits::kMaxArcsPerPage)
                        {
                            notice ("This page already holds " + juce::String (upic::limits::kMaxArcsPerPage)
                                    + " arcs (the limit) - delete one before duplicating");
                            break;
                        }
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
        if (onEditEnd) onEditEnd();
    }

void ScoreEditor::toggleMuteSelected()
{
        if (! page || selArcs.empty()) return;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            bool muteTo = false;
            for (auto& a : page->arcs)
                if (std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end())
                    { muteTo = ! a.muted; break; } // unify on the first selected arc
            for (auto& a : page->arcs)
                if (std::find(selArcs.begin(), selArcs.end(), a.id) != selArcs.end())
                    a.muted = muteTo;
        }
        notify();
    }

void ScoreEditor::remagnetizeSelected()
{
        if (! page || selArcs.empty()) return;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            for (auto& a : page->arcs)
            {
                if (std::find(selArcs.begin(), selArcs.end(), a.id) == selArcs.end()) continue;
                for (auto& pt : a.points) quantizeXY(pt.t, pt.freq);
                std::stable_sort(a.points.begin(), a.points.end(),
                                 [](auto& x, auto& y){ return x.t < y.t; });
            }
        }
        notify();
    }

void ScoreEditor::nudgeSelected(double dt, int dSemi)
{
        if (! page || selArcs.empty()) return;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            for (auto& a : page->arcs)
            {
                if (std::find(selArcs.begin(), selArcs.end(), a.id) == selArcs.end()) continue;
                for (auto& pt : a.points)
                {
                    pt.t = juce::jlimit(0.0, page->durationSec, pt.t + dt);
                    if (dSemi != 0) pt.freq = nudgePitch(pt.t, pt.freq, dSemi);
                }
                std::stable_sort(a.points.begin(), a.points.end(),
                                 [](auto& x, auto& y){ return x.t < y.t; });
            }
        }
        notify();
    }

int ScoreEditor::addZone(double ta, double tb, double fa, double fb)
{
        if (! page) return -1;
        // v1.33.4 (H13): the page's zone count is capped by the piece format.
        if (atZoneLimit())
        {
            notice ("This page already holds " + juce::String (upic::limits::kMaxSieveZones)
                    + " sieve zones (the limit) - delete one before drawing another");
            return -1;
        }
        upic::SieveZone z;
        // v1.33.4 (L15, part 2): the undo transaction
        if (onEditStart) onEditStart ("Add a sieve zone");
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
            z.stickiness = juce::jlimit(0.0, 1.0, stickiness);
            page->zones.push_back(z);
        }
        selectedZone = z.id;
        selArcs.clear();
        notify();
        if (onEditEnd) onEditEnd();
        return z.id;
    }

void ScoreEditor::deleteZone(int id)
{
        if (! page) return;
        // v1.33.4 (L15, part 2): the undo transaction
        if (onEditStart) onEditStart ("Delete a sieve zone");
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            auto& v = page->zones;
            v.erase(std::remove_if(v.begin(), v.end(),
                    [&](auto& z){ return z.id == id; }), v.end());
        }
        if (selectedZone == id) selectedZone = -1;
        notify();
        if (onEditEnd) onEditEnd();
    }

void ScoreEditor::scrubTo(float x)
{ scrubToTime(xToTime(x)); }

void ScoreEditor::scrubToTime(double t)
{
        if (!page || !playheadSec) return;
        t = juce::jlimit(0.0, page->durationSec, t);
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            *playheadSec = t;
        }
        if (onSeek != nullptr) onSeek(t); // v1.18: the audio thread must move too
        if (onScrubPreview != nullptr) onScrubPreview(t);
        repaint();
    }

void ScoreEditor::externalScrubStart(double t)
{
        if (!page || !playheadSec) return;
        scrubbing = true;
        scrubDragged = true;
        freezeAfterDrag = true;
        lastDragMs = juce::Time::getMillisecondCounterHiRes();
        startTimer(40);
        scrubToTime(t);
    }

void ScoreEditor::externalScrubTo(double t)
{
        if (!scrubbing) { externalScrubStart(t); return; }
        scrubToTime(t);
    }

void ScoreEditor::externalScrubEnd()
{
        if (!scrubbing) return;
        stopTimer();
        scrubbing = false;
        scrubDragged = false;
        freezeAfterDrag = false;
        if (onScrubFreezeStop != nullptr) onScrubFreezeStop();
        repaint();
    }

void ScoreEditor::timerCallback()
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

void ScoreEditor::paint(juce::Graphics& g)
{
        g.fillAll (upic::uiBackground());
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
            else if (tuning.custom)
            {
                // M4: with a tuning loaded the guide lines are the scale's
                // own degrees - the base (1/1) is the strong line, the
                // other degrees the faint ones.
                const int n = tuning.degrees();
                int faint = 0;
                for (const auto& s : tuning.axisStepsInRange(upic::kMinFreq, upic::kMaxFreq))
                {
                    const double f = s.freq;
                    const long long si = (long long) std::llround(s.step);
                    const bool isBase = (((si % n) + n) % n) == 0;
                    float y = freqToY(f);
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
                    const int octave = (int) std::floor(s.step / (double) n);
                    overlay.push_back({ juce::MidiMessage::getMidiNoteName(tuning.baseNote + 12 * octave,
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

        // v1.33.2: show the active sieve's allowed pitches as faint guides.
        // Switching the Sieve box on/off changes the page immediately, and a
        // zone's own sieve and time grid are visible inside the zone. The
        // pitch guides are Hz/MIDI, so they belong to the Osc layer only
        // (Sample and Live map the vertical axis to ratios).
        if (page)
        {
            const bool pitchGuides = (layer == 0);
            auto drawSievePitches = [&](int m, const std::vector<int>& R,
                                        double fLo, double fHi,
                                        float x0, float x1, juce::Colour col)
            {
                if (m < 2 || R.empty() || x1 <= x0 + 1.0f) return;
                // collect first: a dense sieve (e.g. all twelve semitones
                // across nine octaves) would be a solid hatch, not a guide
                std::vector<float> ys;
                // M4: the sieve's m/R works on the tuning's axis - the
                // semitones in 12-tone equal temperament, the scale's
                // degrees in a tuning.
                for (const auto& s : tuning.axisStepsInRange(fLo, fHi))
                {
                    const long long si = (long long) std::llround(s.step);
                    const int rr = (int) (((si % m) + m) % m);
                    if (std::find (R.begin(), R.end(), rr) == R.end()) continue;
                    const float y = freqToY (s.freq);
                    if (y < r.getY() || y > r.getBottom()) continue;
                    if ((int) ys.size() >= 96) return;
                    ys.push_back (y);
                }
                g.setColour (col);
                for (float y : ys)
                    g.drawHorizontalLine ((int) y, x0, x1);
            };

            if (pitchGuides && sieveOn)
                drawSievePitches (sieveM, sieveR, ambitusLo(), ambitusHi(),
                                  r.getX(), r.getRight(), juce::Colour (0x2438bdf8));

            for (auto& z : page->zones)
            {
                const auto clip = zoneRect (z).getIntersection (r);
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
                    for (double t = 0.0; t <= page->durationSec + 1e-9 && guard < 512;
                         t += step, ++guard)
                    {
                        const float x = timeToX (t);
                        if (x < clip.getX() || x > clip.getRight()) continue;
                        g.drawVerticalLine ((int) x, clip.getY(), clip.getBottom());
                    }
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
        if (tool == Zone) hint = "Zone: drag a magnetic region (inherits sieve + grid + stick)  |  drag inside to move, an edge to resize  |  R-click: sieve / grid / stick  |  Del: remove  |  Tool=Zone";
        if (sieveOn)
        {
            hint += "  |  SIEVE (global) m" + juce::String(sieveM) + " {";
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

void ScoreEditor::resized()
{ repaint(); }

void ScoreEditor::mouseDown(const juce::MouseEvent& e)
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
            if (zid >= 0) // an existing zone: select it, and let it be moved
            {
                selectedZone = zid;
                selArcs.clear();
                beginZoneDrag (e.position);
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
                // v1.33.4 (L15, part 2): the whole drag is one undo step -
                // the pointer moves do not push anything.
                if (onEditStart) onEditStart ("Move the arc");
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
            // v1.33.4 (L15, part 2): the undo transaction
            if (onEditStart) onEditStart ("Draw");
            if (onArcEditingChanged != nullptr) onArcEditingChanged(true);
        }
        else if (tool == Line)
        {
            lineAnchor = e.position;
            // v1.33.4 (L15, part 2): the undo transaction
            if (onEditStart) onEditStart ("Draw a line");
            if (onArcEditingChanged != nullptr) onArcEditingChanged(true);
            lastMouse = e.position;
            repaint();
        }
    }

void ScoreEditor::mouseDrag(const juce::MouseEvent& e)
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

        // v1.33: dragging a zone - moving it, or resizing it by an edge or
        // a corner. The zone keeps its shape in pitch (the move is a ratio,
        // like an arc's move) and its sieve and time grid.
        if (draggingZone)
        {
            dragZoneTo (e.position);
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
            // F6: Alt = fine movement. It is the same path with a quarter
            // of the sensitivity - not a second code path.
            const float fine = e.mods.isAltDown() ? 0.25f : 1.0f;
            const float dx = (e.position.x - moveStartPos.x) * fine;
            const float dy = (e.position.y - moveStartPos.y) * fine;
            double dt = xToTime(moveStartPos.x + dx) - xToTime(moveStartPos.x);
            // freq ratio from end/start pixel positions (log view correct)
            const double f0 = juce::jmax(1.0, yToFreq(moveStartPos.y));
            const double f1 = juce::jmax(1.0, yToFreq(moveStartPos.y + dy));
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
                std::stable_sort(a.points.begin(), a.points.end(), [](auto& x, auto& y){ return x.t < y.t; });
            }
            // v1.33.4 (M22): the arc moved in the model - mark it, so the
            // audio thread sees the moved arc while the drag is still going
            // (the full onChange is too heavy for every mouse move)
            if (onModelEdited) onModelEdited();
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

void ScoreEditor::mouseUp(const juce::MouseEvent& e)
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

        // v1.33: the zone drag is over - the model changed, so publish it
        if (draggingZone)
        {
            draggingZone = false;
            zoneHandle = 0;
            notify();
            // v1.33.4 (L15, part 2): the transaction ends here
            if (onEditEnd) onEditEnd();
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
            movingArc = false; moveStartMap.clear(); if (onArcEditingChanged != nullptr) onArcEditingChanged(false); notify(); repaint(); if (onEditEnd) onEditEnd(); return;
        }

        if (tool == Line && lineAnchor)
        {
            if (onArcEditingChanged != nullptr) onArcEditingChanged(false);
            commitLine(*lineAnchor, e.position, curPress(e));
            lineAnchor.reset();
            // v1.33.4 (L15, part 2): the transaction ends after the line is
            // committed, so the app can see that something really changed.
            if (onEditEnd) onEditEnd();
            return;
        }
        if (drawing)
        {
            drawing = false;
            if (onArcEditingChanged != nullptr) onArcEditingChanged(false);
            strokePts.push_back({ e.position, curPress(e) });
            commitStroke();
            strokePts.clear();
            // v1.33.4 (L15, part 2): the transaction ends after the stroke is
            // committed, so the app can see that something really changed.
            if (onEditEnd) onEditEnd();
            repaint();
        }
        lineAnchor.reset();
    }

bool ScoreEditor::keyPressed(const juce::KeyPress& k)
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
        // v1.33.4 (L15, part 2): the undo and redo keys. The editor has the
        // keyboard focus while the user draws, so it hands them to the app.
        if (cmd && (code == 'Z' || code == 'z'))
        {
            if (shift) { if (onRedo) onRedo(); }
            else       { if (onUndo) onUndo(); }
            return true;
        }
        if (cmd && (code == 'Y' || code == 'y')) { if (onRedo) onRedo(); return true; }

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

void ScoreEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
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

void ScoreEditor::setPlaying(bool p)
{ isPlaying = p; repaint(); }

double ScoreEditor::playheadNow() const
{
        if (playheadGetter != nullptr) return playheadGetter();
        return playheadSec != nullptr ? *playheadSec : 0.0;
    }

float ScoreEditor::pressureOf(const juce::MouseEvent& e)
{
        return (e.isPressureValid() && e.pressure > 0.0f)
                 ? juce::jlimit(0.02f, 1.0f, e.pressure) : 1.0f;
    }

float ScoreEditor::curPress(const juce::MouseEvent& e) const
{ return recordPressure ? pressureOf(e) : 1.0f; }

juce::Rectangle<float> ScoreEditor::plotArea() const
{
        auto b = getLocalBounds().toFloat();
        b.removeFromTop(4); b.removeFromBottom(6); b.removeFromLeft(6); b.removeFromRight(6);
        return b;
    }

juce::Rectangle<float> ScoreEditor::timeStripRect() const
{
        auto r = plotArea();
        const float cy = r.getCentreY();
        return { r.getX(), cy - 7.0f, r.getWidth(), 14.0f };
    }

juce::Rectangle<float> ScoreEditor::timeStripHitRect() const
{
        auto s = timeStripRect();
        const float h = touchSeen ? 6.0f : 4.0f;
        return { s.getX(), s.getY(), s.getWidth(), h };
    }

void ScoreEditor::upsertTouch(const juce::MouseEvent& e)
{
        for (auto& t : touches)
            if (t.id == e.source.getIndex()) { t.pos = e.position; return; }
        touches.push_back({ e.source.getIndex(), e.position });
    }

void ScoreEditor::removeTouch(int id)
{
        touches.erase(std::remove_if(touches.begin(), touches.end(),
                                     [&](const TouchPt& t){ return t.id == id; }),
                      touches.end());
    }

juce::Point<float> ScoreEditor::touchCentroid() const
{
        if (touches.empty()) return {};
        juce::Point<float> s;
        for (auto& t : touches) s += t.pos;
        return s / (float) touches.size();
    }

float ScoreEditor::touchSpread() const
{
        if (touches.empty()) return 1.0f;
        const auto c = touchCentroid();
        float sum = 0.0f;
        for (auto& t : touches) sum += (t.pos - c).getDistanceFromOrigin();
        return juce::jmax(1.0f, sum / (float) touches.size());
    }

void ScoreEditor::cancelInteraction()
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

void ScoreEditor::beginPinch()
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

void ScoreEditor::updatePinch()
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

void ScoreEditor::showTouchMenu(juce::Point<float> pos)
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

juce::Rectangle<float> ScoreEditor::zoneScreenRect() const
{
        return { juce::jmin(zoneStart.x, zoneCur.x), juce::jmin(zoneStart.y, zoneCur.y),
                 std::abs(zoneCur.x - zoneStart.x), std::abs(zoneCur.y - zoneStart.y) };
    }

juce::Rectangle<float> ScoreEditor::zoneRect(const upic::SieveZone& z) const
{
        const float x0 = timeToX(juce::jmin(z.t0, z.t1));
        const float x1 = timeToX(juce::jmax(z.t0, z.t1));
        const float yTop  = freqToY(juce::jmax(z.f0, z.f1)); // higher freq = higher on screen
        const float yBot  = freqToY(juce::jmin(z.f0, z.f1));
        return { x0, yTop, juce::jmax(1.0f, x1 - x0), juce::jmax(1.0f, yBot - yTop) };
    }

juce::String ScoreEditor::toolName() const
{
        switch (tool) { case Draw: return "Draw"; case Line: return "Line"; case SineStroke: return "Sine";
                        case Select: return "Select"; case Erase: return "Erase"; case Zone: return "Zone"; }
        return "?";
    }

double ScoreEditor::niceStep(double raw)
{
        double p = std::pow(10.0, std::floor(std::log10(raw)));
        double m = raw / p;
        double s = m < 1.5 ? 1 : (m < 3.5 ? 2 : (m < 7.5 ? 5 : 10));
        return s * p;
    }

bool ScoreEditor::arcInLayer(const upic::Arc& a) const
{ return a.mode == layer; }

bool ScoreEditor::arcInLayer(int id) const
{
        if (! page) return false;
        for (auto& a : page->arcs) if (a.id == id) return arcInLayer(a);
        return false;
    }

bool ScoreEditor::isSel(int id) const
{ return std::find(selArcs.begin(), selArcs.end(), id) != selArcs.end(); }

juce::Rectangle<float> ScoreEditor::marqueeRect() const
{
        const float x0 = juce::jmin(marqueeStart.x, marqueeCur.x);
        const float x1 = juce::jmax(marqueeStart.x, marqueeCur.x);
        const float y0 = juce::jmin(marqueeStart.y, marqueeCur.y);
        const float y1 = juce::jmax(marqueeStart.y, marqueeCur.y);
        return { x0, y0, juce::jmax(1.0f, x1 - x0), juce::jmax(1.0f, y1 - y0) };
    }

int ScoreEditor::curMode() const
{ return layer; }

upic::Arc* ScoreEditor::findArc(int id)
{
        if (!page) return nullptr;
        for (auto& a : page->arcs) if (a.id == id) return &a;
        return nullptr;
    }

std::vector<int> ScoreEditor::arcsNear(juce::Point<float> pos, float maxD) const
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

int ScoreEditor::pickArc(juce::Point<float> pos)
{
        auto v = arcsNear(pos);
        return v.empty() ? -1 : v.front();
    }

int ScoreEditor::pickZone(juce::Point<float> pos) const
{
        if (! page) return -1;
        const double t = xToTime(pos.x), f = yToFreq(pos.y);
        // topmost (last drawn) first. A few pixels of slack let the border
        // itself be grabbed for a move/resize (the exact edge can round to
        // just outside the time/frequency rectangle).
        for (auto it = page->zones.rbegin(); it != page->zones.rend(); ++it)
            if (it->contains(t, f) || zoneRect(*it).expanded (4.0f).contains (pos))
                return it->id;
        return -1;
    }

upic::SieveZone* ScoreEditor::findZone(int id)
{
        if (! page) return nullptr;
        for (auto& z : page->zones) if (z.id == id) return &z;
        return nullptr;
    }

int ScoreEditor::zoneHandleAt(const upic::SieveZone& z, juce::Point<float> pos) const
{
        const auto r = zoneRect (z);
        const float h = 7.0f;
        const bool onL = std::abs (pos.x - r.getX())      <= h;
        const bool onR = std::abs (pos.x - r.getRight())  <= h;
        const bool onT = std::abs (pos.y - r.getY())      <= h;
        const bool onB = std::abs (pos.y - r.getBottom()) <= h;
        if (onL && onT) return 5;
        if (onR && onT) return 6;
        if (onL && onB) return 7;
        if (onR && onB) return 8;
        if (onL) return 1;
        if (onR) return 2;
        if (onT) return 3;
        if (onB) return 4;
        return 0;
    }

void ScoreEditor::beginZoneDrag(juce::Point<float> pos)
{
        const int zid = pickZone (pos);
        if (zid < 0) return;
        auto* z = findZone (zid);
        if (z == nullptr) return;
        zoneHandle = zoneHandleAt (*z, pos);
        draggingZone = true;
        zoneDragId = zid;
        zoneDragStart = pos;
        zoneDragT0 = z->t0; zoneDragT1 = z->t1;
        zoneDragF0 = z->f0; zoneDragF1 = z->f1;
        // v1.33.4 (L15, part 2): the whole drag is one undo step
        if (onEditStart) onEditStart ("Move the sieve zone");
    }

void ScoreEditor::dragZoneTo(juce::Point<float> pos)
{
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        if (auto* z = findZone (zoneDragId))
        {
            const double t = xToTime (pos.x);
            const double f = yToFreq (pos.y);
            double t0 = zoneDragT0, t1 = zoneDragT1;
            double f0 = zoneDragF0, f1 = zoneDragF1;
            switch (zoneHandle)
            {
                case 1: t0 = t; break;
                case 2: t1 = t; break;
                case 3: f0 = f; break;
                case 4: f1 = f; break;
                case 5: t0 = t; f0 = f; break;
                case 6: t1 = t; f0 = f; break;
                case 7: t0 = t; f1 = f; break;
                case 8: t1 = t; f1 = f; break;
                default:
                {
                    // move: the whole zone follows the pointer
                    const double dt = t - xToTime (zoneDragStart.x);
                    const double f0p = juce::jmax (1.0, yToFreq (zoneDragStart.y));
                    const double f1p = juce::jmax (1.0, yToFreq (pos.y));
                    const double ratio = f1p / f0p;
                    t0 = zoneDragT0 + dt; t1 = zoneDragT1 + dt;
                    f0 = zoneDragF0 * ratio; f1 = zoneDragF1 * ratio;
                    break;
                }
            }
            // keep the zone inside the page and inside its interval
            const double lo = juce::jmin (t0, t1), hi = juce::jmax (t0, t1);
            const double flo = juce::jmin (f0, f1), fhi = juce::jmax (f0, f1);
            if (hi - lo > 0.02 && fhi > flo * 1.0001)
            {
                z->t0 = juce::jlimit (0.0, page->durationSec, lo);
                z->t1 = juce::jlimit (0.0, page->durationSec, hi);
                z->f0 = juce::jlimit (ambitusLo(), ambitusHi(), flo);
                z->f1 = juce::jlimit (ambitusLo(), ambitusHi(), fhi);
            }
        }
    }

void ScoreEditor::showArcMenu(int id)
{
        juce::PopupMenu m;
        m.addItem(1, "Duplicate");
        {
            bool muted = false;
            if (auto* a = findArc(id)) muted = a->muted;
            m.addItem(2, muted ? "Unmute" : "Mute");
        }
        m.addItem(3, "Remagnetize (re-apply sieve)");
#if GUERRERO_INTEGRATION
        // Only arcs that actually carry Guerrero provenance get the Guerrero
        // commands. Showing them on a hand-drawn arc - or in a build without the
        // engine - would offer actions that cannot run.
        if (const upic::Arc* menuArc = findArc(id))
            if (menuArc->hasGuerrero)
            {
                m.addItem(6, "Regenerate Guerrero arc (from recipe)");
                m.addItem(7, "Edit Guerrero recipe...");
            }
#endif
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
                case 6: if (sp->onRegenerateGuerrero) sp->onRegenerateGuerrero (id); break;
                case 7: if (sp->onEditGuerreroRecipe) sp->onEditGuerreroRecipe (id); break;
                case 4: sp->setSelectedArc(id); sp->deleteSelected(); break;
                case 5: sp->selectAll(); if (sp->onSelect) sp->onSelect(sp->getSelectedArc()); break;
                default: break;
            }
            sp->repaint();
        });
    }

juce::String ScoreEditor::remaindersText(const std::vector<int>& r)
{
        juce::String s;
        for (size_t i = 0; i < r.size(); ++i)
            s += (i > 0 ? "," : "") + juce::String (r[i]);
        return s;
    }

void ScoreEditor::showZoneMenu(int id)
{
        const upic::SieveZone* z = findZone (id);
        const auto presets = upic::sievePresets();
        auto pct = [](double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
        auto sameR = [](const upic::SieveZone* zz, int mm, const std::vector<int>& rr)
        {
            return zz != nullptr && zz->m == mm && zz->remainders == rr;
        };

        juce::PopupMenu m;
        m.addItem (zoneMenuDelete, "Delete zone");
        m.addItem (zoneMenuDeleteAll, "Delete all zones");
        m.addSeparator();

        // v1.33.2: the zone's sieve, time grid and stick are three submenus.
        // The tick shows what the zone actually uses now, so a change (or a
        // no-op) is visible instead of being a guess.
        juce::PopupMenu sieve;
        sieve.addItem (zoneMenuSieveCurrent,
                       "Use the current m" + juce::String (sieveM) + " {"
                           + remaindersText (sieveR) + "}",
                       true, sameR (z, sieveM, sieveR));
        sieve.addItem (zoneMenuSieveOff, "Off (free pitch)", true,
                       z != nullptr && z->remainders.empty());
        sieve.addSeparator();
        for (int i = 0; i < (int) presets.size(); ++i)
            sieve.addItem (zoneMenuSievePreset + i, presets[(size_t) i].name, true,
                           sameR (z, presets[(size_t) i].m, presets[(size_t) i].remainders));
        m.addSubMenu ("Sieve", sieve);

        juce::PopupMenu grid;
        grid.addItem (zoneMenuGridCurrent,
                      "Use the current (" + (timeGrid > 0.0
                          ? juce::String (timeGrid, 0) + "/s" : juce::String ("off")) + ")",
                      true, z != nullptr && std::abs (z->timeGrid - timeGrid) < 1e-9);
        grid.addItem (zoneMenuGridOff, "Off", true, z != nullptr && z->timeGrid <= 0.0);
        grid.addSeparator();
        static const double gridChoices[] = { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        for (int i = 0; i < 6; ++i)
            grid.addItem (zoneMenuGridChoice + i, juce::String (gridChoices[i], 0) + "/s",
                          true, z != nullptr && std::abs (z->timeGrid - gridChoices[i]) < 1e-9);
        m.addSubMenu ("Time grid", grid);

        juce::PopupMenu stick;
        stick.addItem (zoneMenuStickFollow, "Follow the global (" + pct (stickiness) + ")",
                       true, z != nullptr && z->stickiness < 0.0);
        stick.addItem (zoneMenuStickCurrent, "Use the current (" + pct (stickiness) + ")",
                       true, z != nullptr && z->stickiness >= 0.0
                               && std::abs (z->stickiness - stickiness) < 1e-9);
        stick.addSeparator();
        static const double stickChoices[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
        for (int i = 0; i < 5; ++i)
            stick.addItem (zoneMenuStickChoice + i, pct (stickChoices[i]), true,
                           z != nullptr && z->stickiness >= 0.0
                               && std::abs (z->stickiness - stickChoices[i]) < 1e-9);
        m.addSubMenu ("Stickiness", stick);

        juce::Component::SafePointer<ScoreEditor> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [sp, id](int result)
        {
            if (sp == nullptr || result == 0) return;
            sp->applyZoneMenu (id, result);
        });
    }

void ScoreEditor::applyZoneMenu(int id, int action)
{
        if (page == nullptr) return;
        if (action == zoneMenuDelete) { deleteZone (id); return; }
        if (action == zoneMenuDeleteAll)
        {
            std::vector<int> ids;
            for (auto& z : page->zones) ids.push_back (z.id);
            for (int zid : ids) deleteZone (zid);
            return;
        }

        bool changed = false;
        {
            const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
            auto* z = findZone (id);
            if (z == nullptr) return;

            if (action == zoneMenuSieveCurrent)
            {
                z->m = sieveM; z->remainders = sieveR; changed = true;
            }
            else if (action == zoneMenuSieveOff)
            {
                z->remainders.clear(); changed = true;
            }
            else if (action >= zoneMenuSievePreset
                     && action < zoneMenuSievePreset + (int) upic::sievePresets().size())
            {
                // copy: the vector returned by sievePresets() is a temporary,
                // so a reference into it would dangle (it freed m/R).
                const auto pr = upic::sievePresets()[(size_t) (action - zoneMenuSievePreset)];
                z->m = pr.m; z->remainders = pr.remainders; changed = true;
            }
            else if (action == zoneMenuGridCurrent)
            {
                z->timeGrid = timeGrid; changed = true;
            }
            else if (action == zoneMenuGridOff)
            {
                z->timeGrid = 0.0; changed = true;
            }
            else if (action >= zoneMenuGridChoice && action < zoneMenuGridChoice + 6)
            {
                static const double gridChoices[] = { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
                z->timeGrid = gridChoices[action - zoneMenuGridChoice]; changed = true;
            }
            else if (action == zoneMenuStickFollow)
            {
                z->stickiness = -1.0; changed = true;
            }
            else if (action == zoneMenuStickCurrent)
            {
                z->stickiness = juce::jlimit (0.0, 1.0, stickiness); changed = true;
            }
            else if (action >= zoneMenuStickChoice && action < zoneMenuStickChoice + 5)
            {
                static const double stickChoices[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
                z->stickiness = stickChoices[action - zoneMenuStickChoice]; changed = true;
            }
        }
        if (changed)
        {
            notify();
            repaint();
        }
    }

bool ScoreEditor::pitchMagnet(int& m, std::vector<int>& R, double t, double f) const
{
        if (! page) return false;
        if (const upic::SieveZone* z = upic::zoneAt(page->zones, t, f))
        {
            m = z->m; R = z->remainders;
            return ! R.empty(); // empty zone remainders = free inside the zone
        }
        if (sieveOn) { m = sieveM; R = sieveR; return ! R.empty(); }
        if (snap)
        {
            // M4: the chromatic magnet is the axis's own steps - the
            // semitones in 12-tone equal temperament (unchanged), the
            // scale's degrees in a tuning.
            m = tuning.degrees();
            R.clear();
            for (int i = 0; i < m; ++i) R.push_back (i);
            return true;
        }
        return false;
    }

double ScoreEditor::nextAllowedPitch(double f, int dir, double t) const
{
        int m = 12; std::vector<int> R;
        if (! pitchMagnet(m, R, t, f))
        {
            // no magnet: one step of the axis up or down - a semitone in
            // 12-tone equal temperament, a degree of the scale in a tuning.
            const double target = tuning.stepToFreq (tuning.freqToStep (f) + (double) dir);
            return juce::jlimit(ambitusLo(), ambitusHi(), target);
        }
        const double n = tuning.freqToStep(f);
        double best = dir > 0 ? 1.0e18 : -1.0e18;
        for (int r : R)
        {
            const double rr = (double) (((r % m) + m) % m);
            double q = std::floor((n - rr) / (double) m);
            double cand = q * m + rr;
            if (dir > 0) { if (cand <= n + 1e-9) cand += m; best = juce::jmin(best, cand); }
            else         { if (cand >= n - 1e-9) cand -= m; best = juce::jmax(best, cand); }
        }
        if (! tuning.stepInRange(best))
            return juce::jlimit(ambitusLo(), ambitusHi(), f); // edge: stay
        return juce::jlimit(ambitusLo(), ambitusHi(), tuning.stepToFreq(best));
    }

double ScoreEditor::nudgePitch(double t, double f, int dSemi) const
{
        if (std::abs(dSemi) == 1) return nextAllowedPitch(f, dSemi > 0 ? 1 : -1, t);
        int m = 12; std::vector<int> R;
        const double raw = f * std::pow(2.0, dSemi / 12.0);
        if (! pitchMagnet(m, R, t, f))
            return juce::jlimit(ambitusLo(), ambitusHi(), raw);
        return juce::jlimit(ambitusLo(), ambitusHi(), upic::sieveQuantizeHz(raw, m, R, tuning));
    }

double ScoreEditor::pageTimeResolution() const
{
        const double dur = page != nullptr ? page->durationSec : 30.0;
        return juce::jmax (0.0, dur / 500.0);   // 500 steps over the page
    }

double ScoreEditor::minPointSpacingSec() const
{ return juce::jmin (0.015, pageTimeResolution()); }

double ScoreEditor::minLineLengthSec() const
{ return juce::jmin (0.05, pageTimeResolution()); }

void ScoreEditor::quantiseArcPoints(upic::Arc& a, const std::vector<bool>& magnetised)
{
        for (auto& p : a.points) quantizeXY (p.t, p.freq);

        // re-sort: the grid can move a point's time
        std::vector<std::pair<double, size_t>> order;
        order.reserve (a.points.size());
        for (size_t i = 0; i < a.points.size(); ++i) order.emplace_back (a.points[i].t, i);
        std::stable_sort (order.begin(), order.end());
        std::vector<upic::ArcPoint> sorted;
        std::vector<bool> sortedMag;
        sorted.reserve (a.points.size());
        for (auto& o : order)
        {
            sorted.push_back (a.points[o.second]);
            sortedMag.push_back (magnetised[o.second]);
        }

        // merge the points that landed on the same time
        std::vector<upic::ArcPoint> merged;
        std::vector<bool> mergedMag;
        const double minSpacing = minPointSpacingSec();
        double lastT = -1.0e9;
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (sorted[i].t <= lastT + minSpacing) continue;
            merged.push_back (sorted[i]);
            mergedMag.push_back (sortedMag[i]);
            lastT = sorted[i].t;
        }

        // the staircase: a hold and a jump at every magnetised step. Only a
        // HARD snap makes real stairs - with a soft stick the drawing keeps
        // its intermediate pitches, and freezing them into steps would just
        // be a jagged copy of the same curve.
        const bool hard = stickiness > 0.999;
        std::vector<upic::ArcPoint> out;
        out.reserve (merged.size() * 2);
        for (size_t i = 0; i < merged.size(); ++i)
        {
            if (hard && i > 0 && mergedMag[i]
                && std::abs (merged[i].freq - merged[i - 1].freq) > 1e-9)
            {
                // the corner: the previous pitch holds until this moment
                out.push_back ({ merged[i].t, merged[i - 1].freq, merged[i - 1].i });
                // then the jump: the same time, the new pitch
                out.push_back (merged[i]);
            }
            else
            {
                out.push_back (merged[i]);
            }
        }
        a.points = std::move (out);
    }

bool ScoreEditor::pitchMagnetAt(double t, double f) const
{
        int m = 12; std::vector<int> R;
        return pitchMagnet (m, R, t, f);
    }

bool ScoreEditor::commitStroke()
{
        if (!page) return false;
        if (strokePts.size() < 2)
        {
            notice ("The stroke is too short to draw");
            return false;
        }
        // v1.33.4 (H13): the page's arc count is capped by the piece format.
        if (atArcLimit())
        {
            notice ("This page already holds " + juce::String (upic::limits::kMaxArcsPerPage)
                    + " arcs (the limit) - delete one before drawing another");
            return false;
        }
        bool added = false;
        {
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        // sort by x, convert to (t,f), drop duplicates in time
        std::sort(strokePts.begin(), strokePts.end(), [](auto& a, auto& b){ return a.pos.x < b.pos.x; });
        upic::Arc a;
        a.id = page->nextArcId;
        a.waveformId = curWave; a.envelopeId = curEnv; a.intensity = curInten;
        a.mode = curMode(); a.sampleId = curSampleId; a.refFreq = curRef;
        a.stepDivisions = curStep;   // v1.32: the discrete frequency space
        a.space = curSpace;           // v1.33: the frequency space
        if (a.mode == 3)
        {
            a.pmRatio = curPmRatio; a.pmIndex = curPmIndex;
            a.pmIndexEnv = curPmIndexEnv; a.pmFeedback = curPmFeedback;
            a.pmOps = curPmOps;
        }
        double lastT = -1;
        const double minSpacing = minPointSpacingSec();
        for (auto& q : strokePts)
        {
            double t = juce::jlimit(0.0, page->durationSec, xToTime(q.pos.x));
            if (t <= lastT + minSpacing) continue;   // the min spacing scales with the page
            double f = yToFreq(q.pos.y);
            a.points.push_back({ t, f, q.press });
            lastT = t;
        }
        if (a.points.size() < 2)
        {
            notice ("The stroke is too short to draw");
            return false;
        }
        // simplify the raw freehand curve to a manageable control-point count
        //
        // F6: the tolerance follows the view. At a higher zoom the same
        // normalized error is fewer pixels on the screen, so the curve has
        // to keep more of its points (the stroke is what gets saved, so this
        // is not only a drawing detail). Fine mode skips the simplification
        // altogether - every sampled point is kept.
        if (! fineDrawing)
        {
            const double zoom = juce::jmax (1.0, juce::jmax (timeZoom, freqZoom));
            upic::simplifyArcPoints (a.points,
                                     juce::jmax (kMinSimplifyEps, kSimplifyEps / zoom));
        }
        // v1.33.4 (H13): the piece format caps the points of an arc, so a
        // very detailed stroke is thinned out evenly (the shape survives)
        // instead of being cut off when the piece is loaded again.
        if ((int) a.points.size() > upic::limits::kMaxPointsPerArc)
        {
            std::vector<upic::ArcPoint> thinned;
            thinned.reserve ((size_t) upic::limits::kMaxPointsPerArc);
            const int n = (int) a.points.size();
            for (int i = 0; i < upic::limits::kMaxPointsPerArc; ++i)
                thinned.push_back (a.points[(size_t) ((int) ((double) i * (n - 1) / (upic::limits::kMaxPointsPerArc - 1)) + 0.5)]);
            a.points = std::move (thinned);
            notice ("The stroke had more than " + juce::String (upic::limits::kMaxPointsPerArc)
                    + " points (the limit) - it was thinned out");
        }
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
        // the time grid (global or zone) snaps t in the same call. The
        // pitch is stepped into a staircase where a magnet was active.
        {
            std::vector<bool> magnetised;
            magnetised.reserve (a.points.size());
            for (auto& p : a.points) magnetised.push_back (pitchMagnetAt (p.t, p.freq));
            quantiseArcPoints (a, magnetised);
        }
        if (a.points.size() >= 2)
        {
            ++page->nextArcId;
            page->arcs.push_back(a);
            selArcs = { a.id };
            added = true;
        }
        }
        if (added) notify();
        return added;
    }

bool ScoreEditor::commitLine(juce::Point<float> p0, juce::Point<float> p1, float press)
{
        if (!page) return false;
        // v1.33.4 (H13): the page's arc count is capped by the piece format.
        if (atArcLimit())
        {
            notice ("This page already holds " + juce::String (upic::limits::kMaxArcsPerPage)
                    + " arcs (the limit) - delete one before drawing another");
            return false;
        }
        bool added = false;
        {
        const juce::ScopedLock modelSl (modelLock != nullptr ? *modelLock : dummyLock);
        double t0 = juce::jlimit(0.0, page->durationSec, xToTime(p0.x));
        double t1 = juce::jlimit(0.0, page->durationSec, xToTime(p1.x));
        if (std::abs(t1 - t0) < minLineLengthSec())
        {
            notice ("The line is too short to draw - drag a little farther");
            return false;
        }
        if (t1 < t0) { std::swap(t0, t1); std::swap(p0, p1); }
        upic::Arc a; a.id = page->nextArcId;
        a.waveformId = curWave; a.envelopeId = curEnv; a.intensity = curInten;
        a.mode = curMode(); a.sampleId = curSampleId; a.refFreq = curRef;
        a.stepDivisions = curStep;   // v1.32: the discrete frequency space
        a.space = curSpace;           // v1.33: the frequency space
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
        // v1.33: the quantisers, and the staircase they make of the curve
        // (see quantiseArcPoints).
        {
            std::vector<bool> magnetised;
            magnetised.reserve (a.points.size());
            for (auto& p : a.points) magnetised.push_back (pitchMagnetAt (p.t, p.freq));
            quantiseArcPoints (a, magnetised);
        }
        if (a.points.size() >= 2)
        {
            ++page->nextArcId;
            page->arcs.push_back(a);
            selArcs = { a.id };
            added = true;
        }
        }
        if (added) notify();
        return added;
    }

void ScoreEditor::notify()
{ repaint(); if (onChange) onChange(); }

void ScoreEditor::notice(const juce::String& m)
{ if (onNotice) onNotice (m); }

bool ScoreEditor::atArcLimit() const
{
        return page != nullptr && (int) page->arcs.size() >= upic::limits::kMaxArcsPerPage;
    }

bool ScoreEditor::atZoneLimit() const
{
        return page != nullptr && (int) page->zones.size() >= upic::limits::kMaxSieveZones;
    }