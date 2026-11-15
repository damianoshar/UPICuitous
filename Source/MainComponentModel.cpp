#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The model: the controls are kept in step with the page, the generators fill
// it, and the arc list mirrors it.

//==============================================================================
upic::Page& MainComponent::curPage()
{
    if (pages.empty()) pages.push_back(upic::Page());
    currentPage = juce::jlimit(0, (int)pages.size() - 1, currentPage);
    return pages[(size_t)currentPage];
}
const upic::Page& MainComponent::curPage() const
{
    static upic::Page emptyFallback;
    if (pages.empty()) return emptyFallback;
    int idx = juce::jlimit(0, (int)pages.size() - 1, currentPage);
    return pages[(size_t)idx];
}

void MainComponent::refreshBoxes()
{
    arcListDirty = true; // v1.21: the panel's rows follow the model
    pageBox.setName("pageBox");
    // v1.33.4 (H13): the page buttons are named too, so the selftest can
    // press them (the page count is capped by the piece format).
    pageAddBtn.setName("pageAddBtn");
    pageDupBtn.setName("pageDupBtn");
    pageDelBtn.setName("pageDelBtn");
    pageBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < pages.size(); ++i)
        pageBox.addItem("Page " + juce::String((int)i + 1) + "  (" + juce::String(pages[i].arcs.size()) + " arcs)", (int)i + 1);
    pageBox.setSelectedId(currentPage + 1, juce::dontSendNotification);
    durSlider.setValue(curPage().durationSec, juce::dontSendNotification);

    // v1.33: the page's four frequency spaces
    for (int i = 0; i < 4; ++i)
        spaceDefBox[i].setSelectedId (juce::jlimit (0,
            (int) upic::freqSpacePresets().size() - 1,
            curPage().spaces[(size_t) i]) + 1, juce::dontSendNotification);
    refreshSpaceBox();

    // v1.33: the ambitus - the combo shows the preset that matches, or
    // "Custom" when the page carries an interval of its own
    {
        const auto& presets = upic::ambitusPresets();
        int match = 0;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (std::abs (curPage().pitchLo - presets[(size_t) i].lo) < 1e-6
                && std::abs (curPage().pitchHi - presets[(size_t) i].hi) < 1e-6)
            { match = i + 1; break; }
        ambitusBox.setSelectedId (match > 0 ? match : (int) presets.size() + 1,
                                  juce::dontSendNotification);
    }
    // v1.33: the acceleration
    warpSlider.setValue (curPage().warp, juce::dontSendNotification);

    waveBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < waveforms.size(); ++i)
        waveBox.addItem(juce::String(waveforms[i].id) + ": " + waveforms[i].name, (int)i + 1);
    envBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < envelopes.size(); ++i)
        envBox.addItem(juce::String(envelopes[i].id) + ": " + envelopes[i].name, (int)i + 1);
    waveEditBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < waveforms.size(); ++i)
        waveEditBox.addItem(juce::String(waveforms[i].id) + ": " + waveforms[i].name, (int)i + 1);
    envEditBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < envelopes.size(); ++i)
        envEditBox.addItem(juce::String(envelopes[i].id) + ": " + envelopes[i].name, (int)i + 1);

    pmIndexEnvBox.clear (juce::dontSendNotification);
    // ComboBox item ids must be > 0: id 1 is the plain amp envelope, the
    // envelope bank follows from id 2 (so the box never shows blank)
    pmIndexEnvBox.addItem ("(amp env)", 1);
    for (size_t i = 0; i < envelopes.size(); ++i)
        pmIndexEnvBox.addItem (juce::String (envelopes[i].id) + ": " + envelopes[i].name, (int) i + 2);
    pmIndexEnvBox.setSelectedId (1, juce::dontSendNotification);

    sampleBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < sampleSlots.size(); ++i)
        sampleBox.addItem(juce::String(sampleSlots[i].id) + ": "
                          + (sampleSlots[i].loaded() ? sampleSlots[i].name : juce::String("(empty)"))
                          + (sampleSlots[i].reversed ? " (rev)" : ""),
                          (int)i + 1);
    selSlot = juce::jlimit(0, (int) sampleSlots.size() - 1, selSlot);
    sampleBox.setSelectedId(selSlot + 1, juce::dontSendNotification);

    // keep selections valid
    auto findIdx = [](auto& vec, int id){ for (size_t i = 0; i < vec.size(); ++i) if (vec[i].id == id) return (int)i; return 0; };
    waveBox.setSelectedId(findIdx(waveforms, selWave) + 1, juce::dontSendNotification);
    envBox.setSelectedId(findIdx(envelopes, selEnv) + 1, juce::dontSendNotification);
    waveEditBox.setSelectedId(findIdx(waveforms, selWave) + 1, juce::dontSendNotification);
    envEditBox.setSelectedId(findIdx(envelopes, selEnv) + 1, juce::dontSendNotification);
    updateStatus();
}

void MainComponent::refreshScoreBinding()
{
    // v1.33.4 (M22): the editors change the model, so they mark it as
    // changed - the status timer publishes the snapshot only when the
    // revision moved (it used to copy the model on every tick, whatever
    // happened). The callback is passed to every bind() because bind()
    // replaces it.
    score.bind(&curPage(), &waveforms, &playheadSec, [this]{
        markModelChanged(); updateStatus(); refreshBoxes(); });
    pushVoice();
    score.setPlaying(isPlaying);
    waveEd.bind(&waveforms, &selWave, [this]{ markModelChanged(); });
    envEd.bind(&envelopes, &selEnv, [this]{ markModelChanged(); });
    // v1.33.4 (L15, part 2): the undo transaction for the envelope editor
    envEd.setOnEditStart([this]{ beginEdit ("Edit the envelope"); });
    envEd.setOnEditEnd([this]{ endEdit(); });
    updateSampleView();
    score.repaint(); waveEd.repaint(); envEd.repaint();
}

bool MainComponent::prepareWaveEdit()
{
    // v1.27: the wavetable editor is shared by every arc that names a wave.
    // Editing it with a selection must not rewrite the arcs that are not
    // selected, so the selection gets a private copy first. Without a
    // selection this is the old library/template edit.
    if (drawMatMode != 0 && drawMatMode != 3) return false;
    const auto ids = score.getSelectedArcs();
    if (ids.empty()) return true;

    bool forked = false;
    {
        const juce::ScopedLock l(dataLock);
        upic::Waveform* src = nullptr;
        for (auto& w : waveforms) if (w.id == selWave) { src = &w; break; }
        if (src == nullptr && ! waveforms.empty())
        {
            // match WaveformEditor::current(), which falls back to the first
            src = &waveforms.front();
            selWave = src->id;
        }
        if (src == nullptr) return false;

        // Arc ids repeat between pages, so only the current page can have
        // selected arcs; every arc on another page is unselected.
        bool shared = false;
        for (size_t p = 0; p < pages.size() && !shared; ++p)
            for (const auto& a : pages[p].arcs)
            {
                if (a.waveformId != selWave) continue;
                const bool selected = (p == (size_t) currentPage
                                       && std::find(ids.begin(), ids.end(), a.id) != ids.end());
                if (!selected) { shared = true; break; }
            }

        if (shared)
        {
            upic::Waveform copy = *src;
            int newId = 0;
            for (const auto& w : waveforms) newId = juce::jmax(newId, w.id + 1);
            copy.id = newId;
            const juce::String base = copy.name;
            juce::String name = base + " copy";
            int n = 2;
            for (;;)
            {
                bool used = false;
                for (const auto& w : waveforms) if (w.name == name) { used = true; break; }
                if (!used) break;
                name = base + " copy " + juce::String(n++);
            }
            copy.name = name;
            waveforms.push_back(copy);
            selWave = newId;
            forked = true;
        }

        // All selected arcs follow the wave that is about to change.
        for (auto& a : curPage().arcs)
            if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
                a.waveformId = selWave;
    }

    if (forked)
    {
        refreshBoxes();
        pushVoice();
        // v1.33.4 (M22): the fork changed the model - mark it, so the
        // status timer publishes the snapshot even if no edit follows
        markModelChanged();
    }
    score.repaint();
    return true;
}

void MainComponent::pushVoice()
{
    int w = waveBox.getSelectedId() - 1;
    int e = envBox.getSelectedId() - 1;
    if (w >= 0 && ! waveforms.empty()) selWave = waveforms[(size_t)(w % (int) waveforms.size())].id;
    if (e >= 0 && ! envelopes.empty()) selEnv = envelopes[(size_t)(e % (int) envelopes.size())].id;
    const int sid = (selSlot >= 0 && selSlot < (int) sampleSlots.size())
                        ? sampleSlots[(size_t) selSlot].id : 1;
    drawSampleId = sid; // remembered for procedural generation
    score.setCurrentVoice(selWave, selEnv, drawIntensity, drawMatMode, sid, drawRef);
    // v1.32: a new arc is born with the frequency space on show
    score.setCurrentStep (stepBtn.getToggleState()
                            ? juce::jlimit (1, 99, (int) stepDivSlider.getValue()) : 0);
    // v1.33: and in the frequency space on show
    score.setCurrentSpace (juce::jlimit (0, 3, spaceBox.getSelectedId() - 1));
    int pmEnvId = 0;
    const int pmIdx = pmIndexEnvBox.getSelectedId();
    if (pmIdx >= 2 && pmIdx - 2 < (int) envelopes.size())
        pmEnvId = envelopes[(size_t)(pmIdx - 2)].id;
    score.setCurrentPmVoice(pmRatioSlider.getValue(), pmIndexSlider.getValue(),
                            pmEnvId, pmFeedbackSlider.getValue(),
                            pmOpsBox.getSelectedId() == 2 ? 4 : 2);
}

// Load one arc into the toolbar. The canvas calls this when an arc is clicked;
// the arc list calls it too, so both selection paths show the same settings.
void MainComponent::syncControlsToArc(int arcId)
{
    if (arcId < 0)
    {
        selectedArcId = -1;
        lastArcSelection.clear();
        arcListPanel.setSelectedArcs(currentPage, {});
        return;
    }

    {
        juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        for (auto& a : pg.arcs) if (a.id == arcId)
        {
            // reflect voice in boxes
            for (size_t i = 0; i < waveforms.size(); ++i)
                if (waveforms[i].id == a.waveformId)
                {
                    waveBox.setSelectedId((int)i + 1, juce::dontSendNotification);
                    waveEditBox.setSelectedId((int)i + 1, juce::dontSendNotification);
                }
            for (size_t i = 0; i < envelopes.size(); ++i)
                if (envelopes[i].id == a.envelopeId)
                {
                    envBox.setSelectedId((int)i + 1, juce::dontSendNotification);
                    envEditBox.setSelectedId((int)i + 1, juce::dontSendNotification);
                }
            int pmIdx = 1;
            for (size_t j = 0; j < envelopes.size(); ++j)
                if (envelopes[j].id == a.pmIndexEnv) { pmIdx = (int) j + 2; break; }
            pmIndexEnvBox.setSelectedId (pmIdx, juce::dontSendNotification);
            pmRatioSlider.setValue (a.pmRatio, juce::dontSendNotification);
            pmIndexSlider.setValue (a.pmIndex, juce::dontSendNotification);
            pmFeedbackSlider.setValue (a.pmFeedback, juce::dontSendNotification);
            pmOpsBox.setSelectedId (a.pmOps == 4 ? 2 : 1, juce::dontSendNotification);
            intenSlider.setValue(a.intensity, juce::dontSendNotification);
            drawIntensity = a.intensity; // keep new strokes in sync with the slider
            // v1.31 (U1): the polyagogy controls follow the selected arc
            modUseBox.setSelectedId (juce::jlimit (0, 2, a.waveUse) + 1, juce::dontSendNotification);
            modRateSlider.setValue (a.modRate, juce::dontSendNotification);
            modDepthSlider.setValue (a.modDepth, juce::dontSendNotification);
            // v1.32: the discrete frequency space follows the selected arc
            stepBtn.setToggleState (a.stepDivisions > 0, juce::dontSendNotification);
            stepDivSlider.setValue ((double) juce::jlimit (1, 99,
                                       a.stepDivisions > 0 ? a.stepDivisions : 12),
                                    juce::dontSendNotification);
            stepDivSlider.setEnabled (a.stepDivisions > 0);
            // v1.33: the frequency space follows the selected arc
            score.setCurrentSpace (juce::jlimit (0, 3, a.space));
            refreshSpaceBox();
            // reflect material in the tabs (the arc is in this tab's layer)
            drawMatMode = juce::jlimit(0, 3, a.mode);
            syncMaterialTabs();
            for (size_t i = 0; i < sampleSlots.size(); ++i)
                if (sampleSlots[i].id == a.sampleId) { selSlot = (int) i; break; }
            sampleBox.setSelectedId(selSlot + 1, juce::dontSendNotification);
            updateRevButton();
            drawRef = a.refFreq;
            refSlider.setValue(drawRef, juce::dontSendNotification);
            updateSampleView();
            pushVoice();
            break;
        }
    }

    selectedArcId = score.getSelectedArc();
    lastArcSelection = score.getSelectedArcs();
    arcListPanel.setSelectedArcs(currentPage, lastArcSelection);
}

void MainComponent::updateSampleView()
{
    if (drawMatMode == 1 && selSlot >= 0 && selSlot < (int) sampleSlots.size())
        waveEd.setSampleView(&sampleSlots, selSlot);
    else
        waveEd.setSampleView(nullptr, -1);
}

// v1.19: the Reverse button follows the selected slot.
void MainComponent::updateRevButton()
{
    const bool on = selSlot >= 0 && selSlot < (int) sampleSlots.size()
                    && sampleSlots[(size_t) selSlot].reversed;
    revBtn.setToggleState(on, juce::dontSendNotification);
    revBtn.setButtonText(on ? "Rev ON" : "Rev");
    revBtn.setTooltip(on ? "The slot plays backwards (click to play it forward)"
                          : "Play the selected slot backwards");
}

void MainComponent::flash(const juce::String& m)
{
    flashMsg = m;
    flashUntilMs = juce::Time::getMillisecondCounter() + 4000;
}

//==============================================================================
// v1.33.4 (M28): the long jobs - the render, the stems, the decode, the
// save - run on a worker thread, so the window stays alive and the audio
// thread is not held up by them.
//
// The rules:
//   - the model is captured on the message thread (under the lock) and the
//     worker never touches the component again
//   - the work checks the cancel flag between the blocks, and the progress
//     is published as a percentage the status line shows
//   - the report comes back on the message thread (the worker posts it), so
//     the messages and the UI are only touched there
void MainComponent::startJob (const juce::String& label,
                              std::function<void (Job&)> work,
                              std::function<void (Job&)> report,
                              bool announce)
{
    // one job at a time - a second one would fight the first over the disk
    if (jobBusy.load (std::memory_order_relaxed))
    {
        if (announce)
            flash ("Another job is still running (" + jobLabel() + ") - wait for it, or cancel it");
        return;
    }

    // the previous job's thread is done (it cleared the flag), so it can go
    if (worker.joinable()) worker.join();

    auto job = std::make_shared<Job>();
    job->label = label;
    job->silent = ! announce;
    job->work = std::move (work);
    job->report = std::move (report);
    activeJob = job;
    jobBusy.store (true, std::memory_order_relaxed);
    if (announce)
        flash (label + "...");

    // the safe pointer: the window can be closed while the job runs
    juce::Component::SafePointer<MainComponent> sp (this);
    worker = std::thread ([sp, job]
    {
        if (job->work)
        {
            try { job->work (*job); }
            catch (...) { job->ok = false; }
        }
        // the job is done - the next one may start (the report is only a
        // message, it does not block anything)
        job->done.store (true, std::memory_order_relaxed);
        if (sp != nullptr) sp->jobBusy.store (false, std::memory_order_relaxed);
        // the report belongs to the message thread
        juce::MessageManager::callAsync ([sp, job]
        {
            if (sp == nullptr) return;
            if (sp->activeJob == job) sp->activeJob.reset();
            if (job->report) job->report (*job);
        });
    });
}

void MainComponent::cancelJob()
{
    if (! jobBusy.load (std::memory_order_relaxed)) return;
    if (activeJob) activeJob->cancelled.store (true, std::memory_order_relaxed);
    flash ("Cancelling " + jobLabel() + "...");
}

int MainComponent::jobProgress() const
{
    return activeJob ? activeJob->progress.load (std::memory_order_relaxed) : 0;
}

juce::String MainComponent::jobLabel() const
{
    return activeJob ? activeJob->label : juce::String();
}

void MainComponent::pushSieve()
{
    sieveOn = sieveBtn.getToggleState();
    const int m = sieveMEdit.getText().trim().getIntValue();
    if (m >= 2 && m <= 120) sieveM = m; // keep previous value while an invalid one is typed
    std::vector<int> rs;
    auto tokens = juce::StringArray::fromTokens(sieveREdit.getText(), ",; \t", "");
    for (auto& tok : tokens)
    {
        auto t = tok.trim();
        if (t.isEmpty() || ! t.containsOnly("-0123456789")) continue;
        rs.push_back(((t.getIntValue() % sieveM) + sieveM) % sieveM);
    }
    // v1.33.2: an empty/unparseable remainder field must not silently turn
    // the sieve off (the checkbox then looked like it did nothing). Keep the
    // last valid list while the field is being edited.
    if (! rs.empty()) sieveR = rs;
    score.setSieve(sieveOn, sieveM, sieveR);
    updateStatus();
}

// nUPIC-style procedural arc generation: the four generators from the reference
// (stochastic/convergent arborescence, echelon construction, ordinal spine).
// Generated arcs are stamped with the current voice and selected afterwards.
void MainComponent::runGenerator(int kind)
{
    juce::Random rng((int) juce::Time::getMillisecondCounter());
    upic::GenParams gp;
    std::vector<upic::Arc> gen;
    juce::String name = "generator";
    std::vector<int> created;
    int added = 0, skipped = 0;
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the arcs were generated.
    beginEdit ("Generate arcs");
    {
        juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        gp.t0 = 0.0;
        gp.t1 = pg.durationSec * 0.92;
        gp.f0 = 90.0;
        gp.f1 = 4000.0;
        switch (kind)
        {
            case 0: gen = upic::genStochasticArborescence(gp, rng); name = "stochastic arborescence"; break;
            case 1: gen = upic::genConvergentArborescence(gp, rng); name = "convergent arborescence"; break;
            case 2: gen = upic::genEchelon(gp, rng);                name = "echelon construction";   break;
            case 3: gen = upic::genOrdinalSpine(gp, rng);           name = "ordinal spine";          break;
            default: break;
        }
        for (auto& a : gen)
        {
            // v1.33.4 (H13): the page's arc count is capped by the piece
            // format, so the generator stops at the cap instead of writing
            // arcs that the loader would drop on the next open.
            if ((int) pg.arcs.size() >= upic::limits::kMaxArcsPerPage) { ++skipped; continue; }
            a.id = pg.nextArcId++;
            a.waveformId = selWave;
            a.envelopeId = selEnv;
            a.intensity = drawIntensity;
            a.mode = drawMatMode; // live arcs for the live tab, etc.
            a.sampleId = drawSampleId;
            a.refFreq = drawRef;
            if (a.mode == 3)
            {
                a.pmRatio = pmRatioSlider.getValue();
                a.pmIndex = pmIndexSlider.getValue();
                const int pmIdx = pmIndexEnvBox.getSelectedId();
                a.pmIndexEnv = (pmIdx >= 2 && pmIdx - 2 < (int) envelopes.size())
                                   ? envelopes[(size_t)(pmIdx - 2)].id : 0;
                a.pmFeedback = pmFeedbackSlider.getValue();
                a.pmOps = pmOpsBox.getSelectedId() == 2 ? 4 : 2;
            }
            created.push_back(a.id);
            pg.arcs.push_back(std::move(a));
            ++added;
        }
        score.setSelectedArcs(created);
    }
    refreshBoxes();
    updateStatus();
    score.repaint();
    // v1.33.4 (L15, part 2): the generated arcs are part of the model, so
    // they must be marked as changed (the audio thread sees them right away
    // instead of at the next safety-net publish) - and the undo keeps the
    // state from before them.
    markModelChanged();
    publishModelSnapshot();
    endEdit();
    juce::String msg = gen.empty() ? ("Generator produced no arcs (" + name + ")")
                                   : ("Generated " + juce::String(added) + " arcs -- " + name);
    if (skipped > 0)
        msg += " (" + juce::String(skipped) + " more did not fit: a page holds at most "
               + juce::String (upic::limits::kMaxArcsPerPage) + " arcs)";
    flash(msg);
}

//==============================================================================
// v1.21 (wave 19): the arc list - one row per arc, grouped by material,
// with the arc's page, pitch range and time span.
void MainComponent::rebuildArcList()
{
    arcListDirty = false;
    if (! showArcList) return;

    std::vector<ArcListPanel::Row> rows;

    struct Group { int kind; const char* name; juce::Colour colour; };
    const Group groups[] = { { 0, "OSC",    juce::Colour (0xff64d2ff) },
                              { 1, "SAMPLE", juce::Colour (0xffd2a664) },
                              { 2, "LIVE",   juce::Colour (0xffd26464) },
                              { 3, "PM",     juce::Colour (0xffa664d2) } };
    for (const auto& g : groups)
    {
        // the arcs of this kind, over every page
        std::vector<std::pair<int, const upic::Arc*>> found;
        for (size_t p = 0; p < pages.size(); ++p)
            for (const auto& a : pages[p].arcs)
                if (a.mode == g.kind) found.push_back ({ (int) p, &a });

        if (found.empty()) continue;

        ArcListPanel::Row head;
        head.kind = ArcListPanel::Row::group;
        head.title = juce::String (g.name) + "  (" + juce::String ((int) found.size()) + ")";
        head.colour = g.colour;
        rows.push_back (head);

        for (const auto& f : found)
        {
            const auto& a = *f.second;
            ArcListPanel::Row r;
            r.kind = ArcListPanel::Row::arc;
            r.arcId = a.id;
            r.page = f.first;
            r.mode = a.pitchMode;
            r.live = (a.mode == 2);
            r.title = "#" + juce::String (a.id);
            r.showMode = (a.mode == 1 || a.mode == 2); // only sample/live arcs
            r.showReverse = (a.mode == 2);   // the live arcs own the direction

            if (a.mode == 2)
            {
                r.reversed = a.reversed;
            }
            else if (a.mode == 1)
            {
                // a sample's direction belongs to the material (the slot)
                const int sid = juce::jlimit (1, (int) sampleSlots.size() - 1, a.sampleId);
                r.reversed = sampleSlots[(size_t) (sid - 1)].reversed;
            }

            // the pitch range (the arc's points) and the time span
            double lo = 0.0, hi = 0.0;
            if (! a.points.empty())
            {
                lo = hi = a.points.front().freq;
                for (const auto& pt : a.points)
                {
                    lo = juce::jmin (lo, pt.freq);
                    hi = juce::jmax (hi, pt.freq);
                }
            }
            r.hz = juce::String ((int) std::lround (lo)) + "-" + juce::String ((int) std::lround (hi));
            r.time = juce::String (a.startTime(), 1) + "-" + juce::String (a.endTime(), 1);
            r.pageText = "p" + juce::String (f.first + 1);
            r.spaceText = "S" + juce::String (a.space + 1);
            rows.push_back (r);
        }
    }

    arcListPanel.setRows (rows);
    arcListPanel.setSelectedArcs (currentPage, score.getSelectedArcs());
}

void MainComponent::showClearWarning()
{
    clearOverlay.pageNumber = currentPage + 1;
    clearOverlay.setBounds(getLocalBounds());
    clearOverlay.setVisible(true);
    clearOverlay.toFront(true);
    clearOverlay.grabKeyboardFocus();
    resized(); // position the confirm / cancel buttons
}

void MainComponent::closeClearWarning()
{
    clearOverlay.setVisible(false);
}

//==============================================================================
// v1.31 (U4): the digitised wave page.
//
// The real UPIC's wave page could be filled from a recording - the wavetable
// was digitised through an A/D converter, and Xenakis sampled the bellowing
// of bulls for Taurhiphanie (1988). Here the selected sample slot (a file
// or a mic take) becomes a wavetable: the whole take is resampled into one
// loop of the oscillator.
//==============================================================================
void MainComponent::waveFromSelectedSlot()
{
    const upic::SampleSlot* slot = nullptr;
    {
        const juce::ScopedLock l(dataLock);
        if (selSlot >= 0 && selSlot < (int) sampleSlots.size())
            slot = &sampleSlots[(size_t) selSlot];
    }

    if (slot == nullptr || slot->data == nullptr || slot->data->getNumSamples() <= 0)
    {
        flash ("No sample in this slot - load or capture one first");
        return;
    }

    upic::Waveform wave;
    {
        const juce::ScopedLock l(dataLock);
        int newId = 0;
        for (const auto& w : waveforms) newId = juce::jmax (newId, w.id + 1);

        juce::String name = slot->name.isNotEmpty() ? slot->name : juce::String ("Slot " + juce::String (slot->id));
        name += " wave";
        int n = 2;
        for (;;)
        {
            bool used = false;
            for (const auto& w : waveforms) if (w.name == name) { used = true; break; }
            if (! used) break;
            name = (slot->name.isNotEmpty() ? slot->name : juce::String ("Slot " + juce::String (slot->id)))
                 + " wave " + juce::String (n++);
        }

        wave = upic::waveFromTake (*slot->data, newId, name);
        waveforms.push_back (wave);
        selWave = newId;
    }

    refreshBoxes();
    waveEd.bind (&waveforms, &selWave, [this]{ markModelChanged(); });
    pushVoice();
    publishModelSnapshot();
    markModelChanged();
    flash ("Wave page digitised: \"" + wave.name + "\" (one loop)");
}

void MainComponent::clearCurrentPage()
{
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the page was cleared.
    beginEdit ("Clear the page");
    {
        const juce::ScopedLock l(dataLock);
        curPage().arcs.clear();
        score.setSelectedArc(-1);
        selectedArcId = -1;
        lastArcSelection.clear();
        arcListPanel.setSelectedArcs(currentPage, {});
        refreshBoxes();
        markModelChanged(); // clearing while playing fades instead of clicking
        publishModelSnapshot();
    }
    endEdit();
    repaint();
}

//==============================================================================
// v1.31 (U3): the UPIC's pitch transformations.
//
// The reverse is already here (the Rev box, the global Rev, the slot Rev).
// These are the other two: a transposition and a pitch inversion. They work
// on the drawn curve - where the music is - and they follow the same scope
// rule as the rest of the app: the selected arcs if there is a selection,
// otherwise every arc of the current page.
//
// The menu ids come from the "Transform the page..." submenu.
