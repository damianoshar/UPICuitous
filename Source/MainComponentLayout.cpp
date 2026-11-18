#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The layout: where every control sits, and how the window is painted.

bool MainComponent::sessionSettingsPersistent = false;

namespace
{
    // F5: the session settings live beside the app's other preferences. The
    // selftest never enables this, so the checks do not touch the user's
    // file; Main.cpp turns it on for the app.
    juce::ApplicationProperties& sessionProperties()
    {
        static juce::ApplicationProperties props;
        static bool initialised = false;
        if (! initialised)
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "UPICuitous";
            o.folderName = "UPICuitous";
            o.filenameSuffix = "settings";
            o.osxLibrarySubFolder = "Application Support";
            props.setStorageParameters (o);
            initialised = true;
        }
        return props;
    }
}

void MainComponent::setSessionSettingsPersistent (bool on)
{
    sessionSettingsPersistent = on;
}

void MainComponent::saveSessionSettings() const
{
    if (! sessionSettingsPersistent) return;
    if (auto* s = sessionProperties().getUserSettings())
    {
        s->setValue ("session", juce::JSON::toString (sessionSettingsToVar()));
        sessionProperties().saveIfNeeded();
    }
}

void MainComponent::loadSessionSettings()
{
    if (! sessionSettingsPersistent) return;
    if (auto* s = sessionProperties().getUserSettings())
    {
        const auto text = s->getValue ("session");
        if (text.isNotEmpty())
            sessionSettingsFromVar (juce::JSON::parse (text));
    }
}

juce::var MainComponent::sessionSettingsToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("showWaveEd", showWaveEd);
    o->setProperty ("showEnvEd", showEnvEd);
    o->setProperty ("showHints", showHints);
    // F4: the focus view is session state too (never piece state).
    o->setProperty ("focus", (int) focusedPanel);
    auto* g = new juce::DynamicObject();
    for (const auto& kv : groupLevels)
        g->setProperty (kv.first, (int) kv.second);
    o->setProperty ("groups", juce::var (g));
    return juce::var (o);
}

void MainComponent::sessionSettingsFromVar (const juce::var& v)
{
    if (! v.isObject()) return;
    showWaveEd = (bool) v.getProperty ("showWaveEd", true);
    showEnvEd  = (bool) v.getProperty ("showEnvEd", true);
    showHints  = (bool) v.getProperty ("showHints", true);
    waveTog.setToggleState (showWaveEd, juce::dontSendNotification);
    envTog.setToggleState (showEnvEd, juce::dontSendNotification);
    hintsTog.setToggleState (showHints, juce::dontSendNotification);
    score.setShowHints (showHints);
    focusedPanel = (FocusPanel) juce::jlimit (0, 3,
                                (int) v.getProperty ("focus", 0));
    updateFocusButtons();
    groupLevels.clear();
    if (auto* g = v.getProperty ("groups", juce::var()).getDynamicObject())
        for (const auto& p : g->getProperties())
        {
            // The old three-level fold (expanded 0, compact 1, hidden 2)
            // is read as the two-level disclosure: the compact and the
            // hidden levels both mean "collapsed".
            const int raw = (int) p.value;
            groupLevels[p.name.toString()] =
                (raw == (int) GroupLevel::expanded) ? GroupLevel::expanded
                                                    : GroupLevel::collapsed;
        }
    resized();
}

//==============================================================================
// F4 (2026-11-17): the focus views.
//
// A focus view maximises one panel. The permanent strip (Menu, Play/Stop,
// the tool and Restore) stays, every other group and the other panels go.
// Nothing about the model's view state is touched: the panel sizes, the
// scroll positions and the zoom levels are the same when the focus is left,
// because the layout is computed from the same inputs again.
void MainComponent::updateFocusButtons()
{
    auto set = [this] (juce::TextButton& b, bool on, const juce::String& what)
    {
        b.setButtonText (on ? "Exit" : "Focus");
        b.setTooltip (on ? "Leave the " + what + " focus view"
                          : "Maximise the " + what + " (and restore it)");
    };
    set (scoreFocusBtn, focusedPanel == FocusPanel::score, "score");
    set (waveFocusBtn,  focusedPanel == FocusPanel::wave,  "wavetable");
    set (envFocusBtn,   focusedPanel == FocusPanel::env,   "envelope");
}

void MainComponent::setFocusedPanel (FocusPanel p)
{
    if (focusedPanel == p) return;
    focusedPanel = p;
    updateFocusButtons();
    resized();
}

//==============================================================================
// F6: the numeric edit of the selected point.
//
// The fields move the selection through the same mutation path as a drag,
// and the whole edit is one undo transaction - so the undo stack and the
// model revision see it exactly as they see a drag.
void MainComponent::editSelectionTime (double tSec)
{
    beginEdit ("Move the point");
    const bool ok = score.setSelectionTime (tSec);
    if (! ok) { endEdit(); return; }
    endEdit();
    refreshPointFields();
}

void MainComponent::editSelectionFreq (double hz)
{
    beginEdit ("Tune the point");
    const bool ok = score.setSelectionFreq (hz);
    if (! ok) { endEdit(); return; }
    endEdit();
    refreshPointFields();
}

void MainComponent::refreshPointFields()
{
    double t = 0.0, f = 0.0;
    const bool has = score.selectionAnchor (t, f);
    viewBar.setPointFields (has, t, f);
}

MainComponent::GroupLevel MainComponent::groupLevelForTest (const juce::String& group) const
{
    const auto it = groupLevels.find (group);
    return it != groupLevels.end() ? it->second : GroupLevel::expanded;
}

void MainComponent::setGroupLevel (const juce::String& group, GroupLevel level)
{
    groupLevels[group] = level;
    resized();
}

void MainComponent::restoreAllGroups()
{
    groupLevels.clear();
    resized();
}

juce::ToggleButton* MainComponent::ensureGroupToggle (const juce::String& group)
{
    const auto it = groupToggles.find (group);
    if (it != groupToggles.end()) return it->second.get();

    auto b = std::make_unique<juce::ToggleButton>();
    b->setName (group + "FoldBtn");
    b->setButtonText (groupCaption (group));
    b->setTooltip ("Show or hide the " + groupCaption (group).toLowerCase() + " controls");
    b->onClick = [this, group]
    {
        const auto cur = groupLevelForTest (group);
        setGroupLevel (group, cur == GroupLevel::expanded ? GroupLevel::collapsed
                                                          : GroupLevel::expanded);
    };
    auto* raw = b.get();
    groupToggles[group] = std::move (b);
    addAndMakeVisible (*raw);
    return raw;
}

juce::String MainComponent::groupCaption (const juce::String& group)
{
    // F5: the caption of a group's disclosure toggle. The scrub section
    // keeps the reference archive's wording ("Scrub settings").
    if (group == "scrub")  return "Scrub settings";
    if (group == "transport") return "Transport";
    if (group == "pages")  return "Pages";
    if (group == "file")   return "File";
    if (group == "tempo")  return "Tempo";
    if (group == "clock")  return "Clock";
    if (group == "voice")  return "Voice";
    if (group == "levels") return "Levels";
    if (group == "pitch")  return "Pitch";
    if (group == "material") return "Material";
    if (group == "sieve")  return "Sieve";
    if (group == "magnets") return "Magnets";
    if (group == "page")   return "Page";
    if (group == "tools")  return "Tools";
    if (group == "drag")   return "Drag";
    if (group == "status") return "Status";
    return group;
}

void MainComponent::buildScoreArea()
{
    // --- score ---
    addAndMakeVisible(score);
    score.setName("score");
    timeLab.setName("timeLab");
    waveEd.setName("waveEd");
    envEd.setName("envEd");

    // --- F6 (2026-11-17): the score's view bar ---
    // The view commands, the Fine drawing toggle, the live readout and the
    // numeric edit of the selected point.
    addAndMakeVisible (viewBar);
    viewBar.zoomInBtn.setName ("zoomInBtn");
    viewBar.zoomOutBtn.setName ("zoomOutBtn");
    viewBar.fitBtn.setName ("fitBtn");
    viewBar.fitSelBtn.setName ("fitSelBtn");
    viewBar.resetBtn.setName ("resetZoomBtn");
    viewBar.fineBtn.setName ("fineBtn");
    viewBar.readoutLab.setName ("readoutLab");
    viewBar.timeEdit.setName ("pointTimeEdit");
    viewBar.freqEdit.setName ("pointFreqEdit");
    viewBar.onZoomIn  = [this]{ score.zoomIn(); };
    viewBar.onZoomOut = [this]{ score.zoomOut(); };
    viewBar.onFit     = [this]{ score.fitPage(); };
    viewBar.onFitSel  = [this]{ score.fitSelection(); };
    viewBar.onReset   = [this]{ score.resetZoom(); };
    viewBar.onFineToggle = [this]{
        score.setFineDrawing (! score.getFineDrawing());
        viewBar.setFineState (score.getFineDrawing());
    };
    viewBar.onTimeEdit = [this] (double seconds){ editSelectionTime (seconds); };
    viewBar.onFreqEdit = [this] (double hz){ editSelectionFreq (hz); };
    score.onCursorMove = [this] (double tSec, double fHz){ viewBar.setReadout (tSec, fHz); };
    score.onViewChanged = [this]{ refreshPointFields(); };
    viewBar.setFineState (score.getFineDrawing());

    // every editor mutates the same model the timer copies into the audio
    // snapshot, so they all share the message-thread model lock
    score.setModelLock(&dataLock);
    waveEd.setModelLock(&dataLock);
    envEd.setModelLock(&dataLock);
    waveEd.setOnEditStart([this]{
        // v1.33.4 (L15, part 2): the undo transaction - the state from
        // before the wavetable is edited (the fork of the selection
        // included). A refusal (a read-only tab) drops the transaction.
        beginEdit ("Edit the wavetable");
        if (! prepareWaveEdit())
        {
            endEdit();
            return false;
        }
        return true;
    });
    waveEd.setOnEditEnd([this]{ endEdit(); });
    // M5: the material's trim (the loop region) is dragged in the material
    // view. It is an edit of its own: the undo transaction, the end of it,
    // and the model's publication.
    waveEd.setOnSampleEditStart([this]{
        beginEdit ("Trim the material");
        return true;
    });
    waveEd.setOnSampleEditEnd([this]{
        endEdit();
        // the trimmed material is a different length
        if (selSlot >= 0 && selSlot < (int) sampleSlots.size())
        {
            const auto& slot = sampleSlots[(size_t) selSlot];
            score.sampleDurations[slot.id] = slot.durationSec();
        }
        publishModelSnapshot();
        updateSampleView();
    });
    waveEd.setOnSampleChanged([this]{ markModelChanged(); });
    // v1.18: the audio thread owns the playhead while playing; the editor
    // draws the published value and asks the audio thread to move on scrub
    score.setPlayheadSource(
        [this]{ return transportActive()
                        ? playheadPublished.load(std::memory_order_relaxed)
                        : playheadSec; },
        [this](double t){ seekPlayhead(t); });
    score.onScrubPreview = [this](double t){
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        double rate = 1.0;
        if (lastScrubMs > 0.0 && std::abs(t - lastScrubT) > 1e-9)
        {
            const double dt = t - lastScrubT;
            const double dms = nowMs - lastScrubMs;
            if (dms > 1.0)
                rate = juce::jlimit(-8.0, 8.0, dt / (dms / 1000.0));
        }
        if (std::abs(rate) < 0.25)
            rate = (t >= lastScrubT) ? 1.0 : -1.0;
        scrubRate.store(rate, std::memory_order_relaxed);
        lastScrubT = t;
        lastScrubMs = nowMs;
        if (!transportActive() && scrubAudition)
        {
            scrubPreviewPlayhead.store(t, std::memory_order_relaxed);
            scrubPreviewRemaining.store(scrubPreviewLen, std::memory_order_relaxed);
            scrubPreviewActive.store(true, std::memory_order_relaxed);
            scrubPreviewSerial.fetch_add(1, std::memory_order_relaxed);
        }
        // reflect scrub position in the material view
        double frac = -1.0;
        if (drawMatMode == 1)
        {
            const juce::ScopedLock sl(dataLock);
            for (auto& a : curPage().arcs)
            {
                if (a.mode != 1) continue;
                if (t < a.startTime() || t > a.endTime()) continue;
                int si = -1;
                for (size_t i = 0; i < sampleSlots.size(); ++i)
                    if (sampleSlots[i].id == a.sampleId && sampleSlots[i].loaded()) { si = (int) i; break; }
                if (si < 0) break;
                const auto& slot = sampleSlots[(size_t) si];
                double sd = slot.durationSec();
                if (sd <= 0.0) break;
                // M5: the material's Ref is the slot's (the material's base
                // pitch) - the same one the engine uses, so the marker in
                // the material view sits where the sound is.
                double ref = juce::jmax(1.0, slot.refFreq > 0.0 ? slot.refFreq : a.refFreq);
                const int tabMode = samplePitchBox.getSelectedId() - 1;
                bool arcMapped = (a.pitchMode >= 0) ? (a.pitchMode == 1 || a.pitchMode == 2)
                                                    : (tabMode == 1 || tabMode == 2);
                double arcDur = juce::jmax(1.0e-6, a.endTime() - a.startTime());
                double progress = juce::jlimit(0.0, 1.0, (t - a.startTime()) / arcDur);
                double pos = arcMapped ? progress * sd
                                       : UpicEngine::scrubMaterialPos(a, t, ref, sd,
                                                 curPage().spaceMap (a.space));
                frac = std::fmod(pos, sd) / sd;
                break;
            }
        }
        waveEd.setPlayhead(frac);
    };
    score.onScrubFreezeStart = [this](double t){
        if (scrubAudition)
        {
            scrubPreviewActive.store(false, std::memory_order_relaxed);
            // start a short preview slice for the crossfade
            scrubPreviewPlayhead.store(t, std::memory_order_relaxed);
            scrubPreviewRemaining.store(scrubPreviewLen, std::memory_order_relaxed);
            scrubPreviewActive.store(true, std::memory_order_relaxed);
            scrubFreezePlayhead.store(t, std::memory_order_relaxed);
            scrubFreezeStopAtMs.store(0.0, std::memory_order_relaxed);
            engine.copyOscillatorPhasesFrom(scrubEngine);
            scrubFreezeTransitionStartMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
            scrubFreezeTransitionEndMs.store(juce::Time::getMillisecondCounterHiRes() + scrubCrossMs, std::memory_order_relaxed);
            scrubFreezeActive.store(true, std::memory_order_relaxed);
            scrubFreezeStartSerial.fetch_add(1, std::memory_order_relaxed);
        }
    };
    score.onScrubFreezeStop = [this](){
        if (scrubFreezeActive.load(std::memory_order_relaxed))
            scrubFreezeStopAtMs.store(juce::Time::getMillisecondCounterHiRes()
                                     + (double) scrubTailMs.load (std::memory_order_relaxed),
                                     std::memory_order_relaxed);
    };
    score.onScrubFreezeCancel = [this](){
        scrubFreezeActive.store(false, std::memory_order_relaxed);
        scrubFreezeStopAtMs.store(0.0, std::memory_order_relaxed);
        scrubFreezeTransitionEndMs.store(0.0, std::memory_order_relaxed);
        scrubPreviewActive.store(false, std::memory_order_relaxed);
        waveEd.setPlayhead(-1.0);
    };
    score.onScrubDragRelease = [this](double /*t*/){
        // intentionally no-op: releasing a drag does not start playback
    };
    score.onSelect = [this](int id){ syncControlsToArc(id); updateStatus(); refreshPointFields(); };
    // v1.33.4 (H13): the editor refuses an edit that the piece format
    // cannot hold and says why.
    score.onNotice = [this](const juce::String& m){ flash (m); };
    // v1.33.4 (M22): a drag edits the model in place. The light hook marks
    // the model as changed, so the status timer publishes it - the full
    // change callback (which refreshes every combo box) is too heavy to run
    // on every mouse move.
    score.onModelEdited = [this]{ markModelChanged(); };
    score.onArcEditingChanged = [this](bool editing){
        arcEditing.store(editing, std::memory_order_relaxed);
        if (! editing)
        {
            // apply the edited score on release
            markModelChanged();
            publishModelSnapshot();
        }
    };
    // v1.33.4 (L15, part 2): the undo transaction. The editor says when a
    // change starts and when it is over; the app keeps the state from
    // before it (one snapshot per gesture - a drag is one undo step).
    score.onEditStart = [this](const juce::String& what){ beginEdit (what); };
    score.onEditEnd   = [this]{ endEdit(); };
    score.onUndo = [this]{ undo(); };
    score.onRedo = [this]{ redo(); };
}

void MainComponent::buildEditors()
{
    // --- wave/env editors ---
    addAndMakeVisible(waveEd); addAndMakeVisible(envEd);
    // F2/F3 (2026-11-17): the bank manager rows own the editor selectors.
    waveLib.setSelector(waveEditBox);
    envLib.setSelector(envEditBox);
    addAndMakeVisible(waveLib); addAndMakeVisible(envLib);
    addAndMakeVisible(wSine); addAndMakeVisible(wSaw); addAndMakeVisible(wSqr); addAndMakeVisible(wTri);
    addAndMakeVisible(wNoise); addAndMakeVisible(wSmooth); addAndMakeVisible(wNorm);
    wSine.setName("wSine");
    eFlat.setName("envFlatBtn");
    // Miniature wave/envelope presets show both their icons and their
    // original text; tooltips describe the action in more detail.
    for (auto* b : { &wSine, &wSaw, &wSqr, &wTri, &wNoise, &wSmooth, &wNorm,
                     &eFlat, &ePerc, &eSwell, &eGate, &eStr })
        b->setTooltip ("Apply " + b->getButtonText() + " shape");
    addAndMakeVisible(eFlat); addAndMakeVisible(ePerc); addAndMakeVisible(eSwell); addAndMakeVisible(eGate); addAndMakeVisible(eStr);
    addAndMakeVisible(assignBtn); addAndMakeVisible(delArcBtn); addAndMakeVisible(muteArcBtn);
    addAndMakeVisible(voiceLab); addAndMakeVisible(waveLab); addAndMakeVisible(envLab); addAndMakeVisible(toolLab);

    // --- F4 (2026-11-17): the focus views -------------------------------
    // One button per panel: it maximises the panel (the permanent strip
    // stays, the other panels and the groups go) and the same button leaves
    // the focus again. The focus is a layout state - the panel sizes, the
    // scroll positions and the zoom levels are not touched by it.
    for (auto* b : { &scoreFocusBtn, &waveFocusBtn, &envFocusBtn })
    {
        addAndMakeVisible (b);
        b->setTooltip ("Maximise this panel (and restore it)");
    }
    scoreFocusBtn.setName ("scoreFocusBtn");
    waveFocusBtn.setName ("waveFocusBtn");
    envFocusBtn.setName ("envFocusBtn");
    scoreFocusBtn.onClick = [this]{
        setFocusedPanel (focusedPanel == FocusPanel::score ? FocusPanel::none
                                                           : FocusPanel::score);
    };
    waveFocusBtn.onClick = [this]{
        setFocusedPanel (focusedPanel == FocusPanel::wave ? FocusPanel::none
                                                          : FocusPanel::wave);
    };
    envFocusBtn.onClick = [this]{
        setFocusedPanel (focusedPanel == FocusPanel::env ? FocusPanel::none
                                                         : FocusPanel::env);
    };
    updateFocusButtons();

    auto curW = [this]() -> upic::Waveform* {
        for (auto& w : waveforms) if (w.id == selWave) return &w;
        return waveforms.empty() ? nullptr : &waveforms.front();
    };
    auto curE = [this]() -> upic::Envelope* {
        for (auto& e : envelopes) if (e.id == selEnv) return &e;
        return envelopes.empty() ? nullptr : &envelopes.front();
    };

    // v1.27: the bottom editor edits a wavetable shared by every arc that
    // names it. With a selection, beginEdit() forks a private copy first, so
    // the shape buttons (and drawing) cannot rewrite unrelated arcs.
    auto editWave = [this, curW](std::function<void(upic::Waveform&)> fn) {
        if (! waveEd.beginEdit()) return;
        {
            const juce::ScopedLock l(dataLock);
            if (auto* w = curW()) fn(*w);
        }
        waveEd.repaint();
        score.repaint();
        markModelChanged();
        publishModelSnapshot();
        waveEd.endEdit();
    };
    wSine.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.makeSine(); }); };
    wSaw.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.makeSaw(); w.smooth(1); }); };
    wSqr.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.makeSquare(); w.smooth(2); }); };
    wTri.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.makeTriangle(); }); };
    wNoise.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.makeNoise((unsigned)juce::Random::getSystemRandom().nextInt()); }); };
    wSmooth.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.smooth(3); }); };
    wNorm.onClick = [editWave]{ editWave([](upic::Waveform& w){ w.normalize(); }); };
    updateWaveButtonColours();

    // v1.33.4 (M22): the envelope presets change the model, so they mark it
    // as changed - the status timer publishes the snapshot only then.
    // F3 (2026-11-17): a shared envelope forks before the preset changes it.
    auto editEnv = [this, curE](std::function<void(upic::Envelope&)> fn) {
        if (! envEd.beginEdit()) return;
        {
            const juce::ScopedLock l(dataLock);
            if (auto* e = curE()) fn(*e);
        }
        envEd.repaint();
        markModelChanged();
        publishModelSnapshot();
        envEd.endEdit();
    };
    eFlat.onClick = [editEnv]{ editEnv([](upic::Envelope& e){ e.presetFlat(); }); };
    ePerc.onClick = [editEnv]{ editEnv([](upic::Envelope& e){ e.presetPercussive(); }); };
    eSwell.onClick = [editEnv]{ editEnv([](upic::Envelope& e){ e.presetSwell(); }); };
    eGate.onClick = [editEnv]{ editEnv([](upic::Envelope& e){ e.presetGate(); }); };
    eStr.onClick = [editEnv]{ editEnv([](upic::Envelope& e){ e.presetStrings(); }); };

    assignBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        const auto ids = score.getSelectedArcs();
        for (auto& a : curPage().arcs)
        {
            if (std::find(ids.begin(), ids.end(), a.id) == ids.end()) continue;
            int w = waveBox.getSelectedId() - 1;
            int e = envBox.getSelectedId() - 1;
            if (w >= 0) a.waveformId = waveforms[(size_t)w].id;
            if (e >= 0) a.envelopeId = envelopes[(size_t)e].id;
            a.intensity = (float) intenSlider.getValue();
            a.mode = drawMatMode; // the current tab's material
            if (drawMatMode == 3) { int _ei = pmIndexEnvBox.getSelectedId(); a.pmIndexEnv = (_ei >= 2 && _ei - 2 < (int) envelopes.size()) ? envelopes[(size_t)(_ei - 2)].id : 0; }
            int si = sampleBox.getSelectedId() - 1;
            if (si >= 0 && si < (int) sampleSlots.size()) a.sampleId = sampleSlots[(size_t) si].id;
            a.refFreq = refSlider.getValue();
        }
        score.repaint(); updateStatus(); refreshBoxes();
    };
    delArcBtn.onClick = [this]{
        score.deleteSelected(); // multi-selection aware
        updateStatus(); refreshBoxes();
    };
    muteArcBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        const auto ids = score.getSelectedArcs();
        for (auto& a : curPage().arcs)
            if (std::find(ids.begin(), ids.end(), a.id) != ids.end()) a.muted = ! a.muted;
        score.repaint(); updateStatus();
    };

    // F2/F3 (2026-11-17): one undo transaction per bank command. A refusal
    // returns false (the manager stays put; the model has flashed why), and
    // endEdit drops the empty snapshot.
    auto bankEdit = [this](const juce::String& label, const std::function<bool()>& fn) -> bool {
        beginEdit(label);
        const bool ok = fn();
        endEdit();
        return ok;
    };

    waveLib.onNew        = [this, bankEdit]{ return bankEdit("New wavetable",
                                        [this]{ return libraryNewWave(); }); };
    waveLib.onDuplicate  = [this, bankEdit]{ return bankEdit("Duplicate the wavetable",
                                        [this]{ return libraryDuplicateWave(); }); };
    waveLib.onRename     = [this, bankEdit](const juce::String& name){ return bankEdit("Rename the wavetable",
                                        [this, name]{ return libraryRenameWave(name); }); };
    waveLib.onDelete     = [this, bankEdit](int replacementId){ return bankEdit("Delete the wavetable",
                                        [this, replacementId]{ return libraryDeleteWave(replacementId); }); };
    waveLib.onMakeUnique = [this, bankEdit]{ return bankEdit("Make the wavetable unique",
                                        [this]{ return libraryMakeWaveUnique(); }); };

    envLib.onNew        = [this, bankEdit]{ return bankEdit("New envelope",
                                       [this]{ return libraryNewEnvelope(); }); };
    envLib.onDuplicate  = [this, bankEdit]{ return bankEdit("Duplicate the envelope",
                                       [this]{ return libraryDuplicateEnvelope(); }); };
    envLib.onRename     = [this, bankEdit](const juce::String& name){ return bankEdit("Rename the envelope",
                                       [this, name]{ return libraryRenameEnvelope(name); }); };
    envLib.onDelete     = [this, bankEdit](int replacementId){ return bankEdit("Delete the envelope",
                                       [this, replacementId]{ return libraryDeleteEnvelope(replacementId); }); };
    envLib.onMakeUnique = [this, bankEdit]{ return bankEdit("Make the envelope unique",
                                       [this]{ return libraryMakeEnvelopeUnique(); }); };

    voiceLab.setText("VOICE (new + assign)", juce::dontSendNotification);
    voiceLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    voiceLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));

}

// v1.33.3: the wavetable's shape buttons wear the colour of the wave they
// make - the same palette the arcs and the wave table use, so the button
// and the arcs it colours read as one thing. The two operations (Smooth,
// Norm) follow the wave that is being edited.
void MainComponent::updateWaveButtonColours()
{
    juce::TextButton* const shape[] = { &wSine, &wSaw, &wSqr, &wTri, &wNoise };
    for (int i = 0; i < 5; ++i)
    {
        const auto c = upic::paletteColour (i);
        shape[i]->setColour (juce::TextButton::buttonColourId,   c.withAlpha (0.55f));
        shape[i]->setColour (juce::TextButton::buttonOnColourId, c);
        shape[i]->setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        shape[i]->setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    }
    juce::Colour cur = upic::paletteColour (0);
    for (auto& w : waveforms)
        if (w.id == selWave) { cur = w.colour; break; }
    for (auto* b : { &wSmooth, &wNorm })
    {
        b->setColour (juce::TextButton::buttonColourId,   cur.withAlpha (0.40f));
        b->setColour (juce::TextButton::buttonOnColourId, cur.withAlpha (0.75f));
        b->setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        b->setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    }
}

MainComponent::~MainComponent()
{
    // F5: remember the fold levels and the view toggles (the app only; the
    // selftest never enables the file).
    saveSessionSettings();
    stopOscReceiver();
    // v1.33.4 (M28): the worker must be gone before the members it could
    // touch are. It is asked to stop first - a render checks the flag
    // between the blocks, so it ends at once.
    jobs.shutdown();
    // v1.18: stop the audio thread first (no callback can be in flight), then
    // hand back the snapshots it owned so the handover can free them here.
    shutdownAudio();
    snapshots.retireReader(liveSnapshot);
    snapshots.clear();
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll (upic::uiBackground());
    // one subtle card per toolbar row, so the transport / voice / material /
    // groove groups read as separate blocks (each with a faint tint)
    static const juce::Colour fills[]   { juce::Colour (0xff161a24), juce::Colour (0xff16161e),
                                          juce::Colour (0xff161f19), juce::Colour (0xff1e1622) };
    static const juce::Colour borders[] { juce::Colour (0xff2f3a4e), juce::Colour (0xff30303e),
                                          juce::Colour (0xff30493a), juce::Colour (0xff483048) };
    for (auto& card : toolbarGroups)
    {
        auto r = card.area;
        if (r.isEmpty()) continue;
        auto rf = r.toFloat().reduced (1.0f, 2.0f);
        g.setColour (fills[(size_t) (card.colour & 3)]);
        g.fillRoundedRectangle (rf, 7.0f);
        g.setColour (borders[(size_t) (card.colour & 3)]);
        g.drawRoundedRectangle (rf, 7.0f, 1.0f);
    }
}

void MainComponent::resized()
{
    // F5: a previous fold pass hid some controls. Put them back before the
    // tab visibility rules below run again - they decide which controls the
    // current material actually shows, and the fold is applied on top.
    for (auto* c : foldHidden) if (c != nullptr) c->setVisible (true);
    foldHidden.clear();

    // All sizes below are *design units* (dp). On high-density displays the
    // whole component tree is placed in dp space (Main.cpp sets the JUCE
    // global scale factor to the display density), so controls keep a
    // constant physical size. Narrow windows wrap toolbar rows; if the
    // height budget still doesn't fit, the grid shrinks (down to 55%) β€”
    // tablets stay at 1x, small screens degrade gracefully.
    struct FlowItem
    {
        juce::Component* label = nullptr; int labelW = 0;
        juce::Component* comp  = nullptr; int w = 0;
        bool flex = false;       // takes the rest of the line (the status)
        int minSlice = 0;
        // F11: a knob's width depends on the narrow layout's fallback (the
        // horizontal slider needs more room than the rotary one).
        bool knob = false;
    };
    struct FlowGroup
    {
        juce::String name;
        std::vector<FlowItem> items;
    };

    // --- the width a control needs for its WHOLE text ---
    // v1.33.3: JUCE draws a button's text fitted into two lines, so a
    // narrow button wrapped its label, and a dropdown cut its text off.
    // Every control is measured here (in design units) and the flow gives
    // it at least that width, so the text is always whole.
    appLaf.uiScale = 1.0f;   // the measurement is in design units
    auto neededWidth = [&] (juce::Component* c) -> int
    {
        if (c == nullptr) return 0;
        juce::Font f (juce::FontOptions{});
        juce::String text;
        int pad = 6;
        if (auto* t = dynamic_cast<juce::ToggleButton*> (c))
        { text = t->getButtonText(); f = appLaf.toggleFont(); pad = 34; }
        else if (auto* b = dynamic_cast<juce::TextButton*> (c))
        { text = b->getButtonText(); f = appLaf.buttonFont(); pad = 20; }
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
        { text = cb->getText(); f = appLaf.buttonFont(); pad = 38; }
        else if (auto* l = dynamic_cast<juce::Label*> (c))
        { text = l->getText(); f = appLaf.getLabelFont (*l); pad = 6; }
        else return 0;
        if (text.isEmpty()) return 0;
        return juce::GlyphArrangement::getStringWidthInt (f, text) + pad;
    };

    // the number of lines of a hint text (the hints are one item per line)
    auto lineCount = [] (const juce::String& t) -> int
    {
        int n = 1;
        for (int i = 0; i < t.length(); ++i)
            if (t[i] == static_cast<juce_wchar> ('\n')) ++n;
        return n;
    };

    auto fitItem = [&] (juce::Component* l, int lw, juce::Component* c, int w) -> FlowItem
    {
        FlowItem it;
        it.label = l;
        it.labelW = (l != nullptr) ? juce::jmax (lw, neededWidth (l)) : 0;
        it.comp = c;
        it.w = (c != nullptr) ? juce::jmax (w, neededWidth (c)) : 0;
        return it;
    };
    auto F  = [&] (juce::Component* c, int w) { return fitItem (nullptr, 0, c, w); };
    auto FL = [&] (juce::Component* l, int lw, juce::Component* c, int w)
              { return fitItem (l, lw, c, w); };
    auto FX = [&] (juce::Component* c, int minSlice)
    { FlowItem it; it.comp = c; it.flex = true; it.minSlice = minSlice; return it; };
    // F11: a knob needs the knob's square plus the room for its value box -
    // not the long track the horizontal slider needed.
    auto FK = [&] (juce::Component* l, int lw, upic::Knob* c)
    {
        auto it = fitItem (l, lw, c, upic::Knob::preferredWidth());
        it.knob = true;
        return it;
    };

    // v1.21: the material row shows the CURRENT tab's material - the tab IS
    // the material (v1.8). The other tab's controls are hidden, not left in
    // place, so the row does not get crowded and the two pitch selectors
    // (the Sample one and the Live one) cannot be confused.
    const bool showSampleCtl = (drawMatMode == 1);
    const bool showLiveCtl   = (drawMatMode == 2);
    const bool showPmCtl     = (drawMatMode == 3);
    // v1.25: the input/capture group (gain, monitor, Capture) records into a
    // sample slot, so it belongs to the Sample tab only.
    juce::Component* const sampleCtl[] = { &slotLab, &sampleBox, &loadSampleBtn, &revBtn,
                                           &samplePitchLab, &samplePitchBox,
                                           &grainLab, &grainSlider,
                                           // M5: the slot's level and the material's edits
                                           &levelLab, &levelSlider,
                                           &normMatBtn, &fadeMatBtn, &resetMatBtn,
                                           &inGainLab, &inGainSlider, &monBtn, &capBtn,
                                           &waveFromSlotBtn };
    for (auto* c : sampleCtl) c->setVisible(showSampleCtl);
    juce::Component* const liveCtl[] = { &livePitchLab, &livePitchBox,
                                         &liveGrainLab, &liveGrainSlider,
                                         &liveRevLab, &liveRevLenBox, &liveRevBtn };
    for (auto* c : liveCtl) c->setVisible(showLiveCtl);
    // Ref belongs to both pitched materials (Sample and Live); it is not a
    // wavetable or PM control.
    const bool showRefCtl = (showSampleCtl || showLiveCtl);
    refLab.setVisible (showRefCtl);
    refSlider.setVisible (showRefCtl);
    // The wavetable selector only applies to Osc and PM arcs. Sample arcs play
    // the slot and Live arcs transpose the mic input, so showing a carrier
    // wave there was misleading - and left a dead gap in the row.
    const bool showWaveCtl = (drawMatMode == 0 || drawMatMode == 3);
    waveLab.setVisible (showWaveCtl); waveBox.setVisible (showWaveCtl);
    juce::Component* const pmCtl[] = { &pmOpsLab, &pmOpsBox, &pmPresetLab, &pmPresetBox,
                                       &pmRatioLab, &pmRatioSlider, &pmIndexLab, &pmIndexSlider,
                                       &pmIndexEnvLab, &pmIndexEnvBox, &pmFeedbackLab,
                                       &pmFeedbackSlider, &pmImportBtn, &pmExportBtn };
    for (auto* c : pmCtl) c->setVisible (showPmCtl);
    // v1.31 (U1): the polyagogy controls belong to the Osc tab (the wave
    // page is the Osc material's).
    juce::Component* const oscModCtl[] = { &modUseLab, &modUseBox, &modRateLab,
                                          &modRateSlider, &modDepthLab, &modDepthSlider };
    for (auto* c : oscModCtl) c->setVisible (! showSampleCtl && ! showLiveCtl && ! showPmCtl);

    // --- the toolbar's groups ---
    // v1.33.3: the groups keep their order and their internal grouping, but
    // a group no longer needs a row of its own: the next group continues on
    // the same line when there is room for it.
    std::vector<FlowGroup> groups;

    // transport: Play and Stop live in the permanent strip (F5), so they stay
    // reachable at every fold level.
    groups.push_back ({ "transport", { F(&loopBtn, 64),
                                        F(&seqBtn, 60), F(&revPlayBtn, 64), F(&pressBtn, 96),
                                        F(&scrubBtn, 72) } });

    // the pages and the files
    groups.push_back ({ "pages", { FL(&pageLab, 40, &pageBox, 150),
                                   F(&pageAddBtn, 30), F(&pageDupBtn, 46), F(&pageDelBtn, 30) } });
    // F5: Menu is in the permanent strip; the file group keeps the rest.
    groups.push_back ({ "file", { F(&settingsBtn, 78), F(&clearBtn, 64) } });

    // the tempo and the clock
    groups.push_back ({ "tempo", { FK(&tempoLab, 40, &tempoSlider) } });
    groups.push_back ({ "clock", { F(&timeLab, 150) } });

    // the voice: what is drawn (the carrier wave, the envelope). The tool
    // selector lives in the permanent strip (F5).
    {
        FlowGroup voice { "voice", {} };
        if (showWaveCtl)
            voice.items.push_back (FL (&waveLab, 42, &waveBox, 140));
        voice.items.push_back (FL (&envLab, 34, &envBox, 140));
        groups.push_back (voice);
    }
    // ... and how it sounds: the intensity, the duration and the master
    // gain are one theme, so they stay together in their own group.
    groups.push_back ({ "levels", { FK (&intenLab, 40, &intenSlider),
                                    FK (&durLab, 30, &durSlider),
                                    FK (&masterLab, 52, &masterSlider) } });
    // the pitch: the snap, the discrete step (the arc's pitch moves
    // through a temperament) and the arc's frequency space (the UPIC's
    // four tables). The step belongs to the arc's voice, not to one
    // material, so it shows in every tab.
    groups.push_back ({ "pitch", { F(&snapBtn, 68), F(&stepBtn, 76),
                                   FK (&stepDivLab, 52, &stepDivSlider),
                                   FL (&spaceLab, 44, &spaceBox, 170) } });

    // the material: the current tab's own controls
    {
        FlowGroup mat { "material", {} };
        if (showSampleCtl)
            mat.items = { FL(&slotLab, 34, &sampleBox, 180),
                          F(&loadSampleBtn, 110), F(&revBtn, 64),
                          FK(&refLab, 34, &refSlider),
                          FK(&levelLab, 34, &levelSlider),
                          FL(&samplePitchLab, 34, &samplePitchBox, 180),
                          FK(&grainLab, 40, &grainSlider),
                          // M5: the material's own edits (the normalise, the
                          // fades and the reset; the trim is dragged in the
                          // material view)
                          F(&normMatBtn, 64), F(&fadeMatBtn, 64), F(&resetMatBtn, 64),
                          FK(&inGainLab, 30, &inGainSlider), F(&monBtn, 64), F(&capBtn, 84),
                          F(&waveFromSlotBtn, 110) };
        else if (showLiveCtl)
            mat.items = { FK(&refLab, 34, &refSlider),
                          FL(&livePitchLab, 42, &livePitchBox, 180),
                          FK(&liveGrainLab, 40, &liveGrainSlider),
                          FL(&liveRevLab, 52, &liveRevLenBox, 90),
                          F(&liveRevBtn, 64) };
        else if (showPmCtl)
            mat.items = { FL(&pmOpsLab, 34, &pmOpsBox, 80),
                          FL(&pmPresetLab, 44, &pmPresetBox, 180),
                          FK(&pmRatioLab, 40, &pmRatioSlider),
                          FK(&pmIndexLab, 40, &pmIndexSlider),
                          FL(&pmIndexEnvLab, 46, &pmIndexEnvBox, 120),
                          FK(&pmFeedbackLab, 42, &pmFeedbackSlider),
                          F(&pmImportBtn, 64), F(&pmExportBtn, 64) };
        else
            // v1.31 (U1): the Osc tab's material group carries the polyagogy
            // controls - what the arc's drawn wave does besides being the
            // timbre.
            mat.items = { FL(&modUseLab, 56, &modUseBox, 150),
                          FK(&modRateLab, 34, &modRateSlider),
                          FK(&modDepthLab, 40, &modDepthSlider) };
        groups.push_back (mat);
    }

    // v1.33: the sieve is its own section. It quantises the pitch; it
    // is not a tool and it is not a page setting.
    groups.push_back ({ "sieve", { F(&sieveBtn, 64), F(&sievePresetBox, 150),
                                   FL(&sieveMLab, 18, &sieveMEdit, 52),
                                   FL(&sieveRLab, 18, &sieveREdit, 84) } });

    // v1.33: the GLOBAL magnets - the stick and the time grid. Every
    // page and every sieve zone follows these two (a zone can set its
    // own m/R and its own time grid, but not its own stick).
    groups.push_back ({ "magnets", { FK(&stickLab, 84, &stickSlider),
                                     FL(&gridLab, 86, &gridBox, 84) } });

    // v1.33: the page's frequency settings - its ambitus, its
    // acceleration and its four frequency spaces (the UPIC's tables)
    groups.push_back ({ "page", { FL(&ambitusLab, 58, &ambitusBox, 170),
                                  FK(&warpLab, 40, &warpSlider),
                                  FL(&spaceDefLab[0], 46, &spaceDefBox[0], 120),
                                  FL(&spaceDefLab[1], 46, &spaceDefBox[1], 120),
                                  FL(&spaceDefLab[2], 46, &spaceDefBox[2], 120),
                                  FL(&spaceDefLab[3], 46, &spaceDefBox[3], 120) } });

    // v1.33: the tools (procedural generators)
    groups.push_back ({ "tools", { FL(&genLab, 36, &genBox, 220) } });

    // the scrub's parameters and the arc dragging
    groups.push_back ({ "scrub", { FK(&scrubLenLab, 60, &scrubLenSlider),
                                   FK(&scrubCrossLab, 70, &scrubCrossSlider),
                                   FK(&scrubTailLab, 60, &scrubTailSlider),
                                   FK(&scrubGrainLab, 70, &scrubGrainSlider) } });
    groups.push_back ({ "drag", { F(&muteDragBtn, 90),
                                  FK(&dragSmoothLab, 70, &dragSmoothSlider) } });

    // the status line: it takes the rest of its line
    groups.push_back ({ "status", { FX (&statusLab, 220) } });

    // the Hints view toggle owns the sidebar (below): the two explanation
    // labels are no longer part of the flow.
    if (pluginMode_) // host audio: no device selector button in the flow
        for (auto& grp : groups)
            grp.items.erase (std::remove_if (grp.items.begin(), grp.items.end(),
                                             [this] (const FlowItem& it){ return it.comp == &audioBtn; }),
                             grp.items.end());

    // F5 (2026-11-17): the permanent strip. These controls stay reachable
    // whatever the groups do: the menu, the transport, the active tool and
    // the Restore button. It is a flow group of its own, so it wraps inside
    // itself on a narrow window instead of pushing controls out.
    std::vector<FlowGroup> stripGroups;
    stripGroups.push_back ({ "permanent", { F(&menuBtn, 74), F(&playBtn, 90),
                                            F(&stopBtn, 70),
                                            FL(&toolLab, 36, &toolBox, 120),
                                            F(&restoreGroupsBtn, 84) } });

    // F5: apply the session's fold level to every group. The disclosure
    // toggle (captioned with the group's name) is the group's first item,
    // so it is always visible - a collapsed group can be brought back
    // without the Restore button. A collapsed group's controls and its card
    // go, and the controls are hidden so they cannot float over the bare
    // canvas.
    //
    // F4: a focus view hides every group - the permanent strip is what
    // stays. The session's own fold levels are not touched, so leaving the
    // focus brings the groups back exactly as they were.
    const bool anyFocus = focusedPanel != FocusPanel::none;
    for (auto& t : groupToggles)
        t.second->setVisible (false);
    std::vector<FlowGroup> shownGroups;
    for (auto& grp : groups)
    {
        const auto level = anyFocus ? GroupLevel::collapsed : groupLevelForTest (grp.name);

        auto* fold = ensureGroupToggle (grp.name);
        fold->setVisible (true);
        fold->setToggleState (level == GroupLevel::expanded, juce::dontSendNotification);

        FlowGroup shown { grp.name, {} };
        // the disclosure is measured like any other control (the caption's
        // own width plus the tick)
        shown.items.push_back (F (fold, 60));

        if (level == GroupLevel::expanded)
        {
            for (auto& it : grp.items)
                shown.items.push_back (it);
        }
        else
        {
            for (auto& it : grp.items)
            {
                if (it.comp != nullptr) { it.comp->setVisible (false); foldHidden.push_back (it.comp); }
                if (it.label != nullptr) { it.label->setVisible (false); foldHidden.push_back (it.label); }
            }
        }
        shownGroups.push_back (std::move (shown));
    }

    auto b = getLocalBounds().reduced(8);
    const int rowH36 = 36, minScoreH = 150, minEdH = 110, wantEdH = 300;
    // Landscape (wider than tall): voice controls become a slim strip under
    // the editors - a tall side column would sit mostly empty on short
    // screens. Portrait keeps the side column.
    const bool landscape = getWidth() > getHeight();
    // v1.7 view state: the editors can be hidden individually; with both
    // hidden the voice controls fall back to a slim strip so the canvas
    // takes over the whole bottom area.
    const bool anyEditors = showWaveEd || showEnvEd;
    const bool stripMode  = landscape || ! anyEditors;

    // --- the flow ---
    // The groups are placed in order. A group moves to the next line only
    // when the current one has no room for it; a group that is wider than
    // the whole line wraps inside itself. The counting pass and the placing
    // pass must agree exactly, or a line lands on top of the next one.
    const int groupGapDp = 6;

    auto groupWidth = [&] (const FlowGroup& g, float sc) -> int
    {
        int w = 0;
        for (auto& it : g.items)
        {
            if (it.flex)
            {
                w += it.minSlice;
                continue;
            }
            // F11: the knob's fallback is wider than the knob.
            const int compW = (it.knob && upic::Knob::shouldFallBack (sc))
                                ? upic::Knob::kFallbackDesignWidth : it.w;
            w += it.labelW + compW;
        }
        return w;
    };

    auto flowGroups = [&] (const std::vector<FlowGroup>& list, juce::Rectangle<int> area,
                           float sc, bool place, std::vector<ToolbarCard>* rects,
                           int& groupColour) -> int
    {
        const int rh2 = (int) std::lround (rowH36 * sc);
        const int g2  = juce::jmax (1, (int) std::lround (2.0f * sc));
        const int gap = juce::jmax (1, (int) std::lround ((float) groupGapDp * sc));
        const int right = area.getRight();
        int x = area.getX(), y = area.getY();

        for (auto& grp : list)
        {
            const int gw = (int) std::lround ((float) groupWidth (grp, sc) * sc);
            // does the group fit on the current line?
            if (x > area.getX() && x + gw + gap > right) { x = area.getX(); y += rh2; }
            const int gx = x, gy = y;
            // a wide group wraps inside itself: every line it touches gets
            // its own card, or the middle of the group would sit on the bare
            // background and a bounding box would paint over the line gaps
            int segX = gx, segY = gy;
            auto closeSegment = [&]()
            {
                if (rects != nullptr && x > segX)
                    rects->push_back ({ juce::Rectangle<int> (segX, segY,
                                                              juce::jmax (1, x - segX), rh2),
                                        groupColour });
            };

            for (auto& it : grp.items)
            {
                if (it.flex)
                {
                    // the status line takes the rest of its line
                    const int minPx = (int) std::lround ((float) it.minSlice * sc);
                    const int fw = juce::jmax (minPx, right - x - g2);
                    if (place && it.comp != nullptr)
                        it.comp->setBounds (x + g2, y + g2, juce::jmax (8, fw - 2 * g2), rh2 - 2 * g2);
                    x += fw;
                    continue;
                }
                const int lw = (int) std::lround ((float) it.labelW * sc);
                // F11: a knob that is too small to read falls back to the
                // horizontal slider (decision 14), and the fallback needs
                // more room. The rule depends on the scale alone, so the
                // counting pass and the placing pass agree.
                bool knobFallback = false;
                if (it.knob && it.comp != nullptr)
                {
                    knobFallback = upic::Knob::shouldFallBack (sc);
                    if (auto* knob = dynamic_cast<upic::Knob*> (it.comp))
                        knob->setFallback (knobFallback);
                }
                const int cwDp = (it.knob && knobFallback) ? upic::Knob::kFallbackDesignWidth
                                                            : it.w;
                const int cw = (int) std::lround ((float) cwDp * sc);
                // a group that is wider than the line wraps inside itself
                if (x > area.getX() && x + lw + cw > right)
                {
                    closeSegment();
                    x = area.getX(); y += rh2;
                    segX = x; segY = y;
                }
                if (place)
                {
                    if (it.label != nullptr)
                        it.label->setBounds (x + g2, y + g2, juce::jmax (1, lw - 2 * g2), rh2 - 2 * g2);
                    if (it.comp != nullptr)
                        it.comp->setBounds (x + lw + g2, y + g2, juce::jmax (1, cw - 2 * g2), rh2 - 2 * g2);
                }
                x += lw + cw;
            }
            closeSegment();
            x += gap;
            ++groupColour;
        }
        return (y - area.getY()) / rh2 + 1;
    };

    // the hints sidebar: the long explanations that used to sit in the
    // toolbar's rows get their own column on the right - the spare space
    // on a large screen. The "Hints" toggle shows and hides it. A focus
    // view has no room for it (F4).
    const int sidebarW = (! anyFocus && showHints && getWidth() >= 820) ? 260 : 0;

    // fit-shrink: wrapping is scale-dependent, so settle by iteration
    float s = 1.0f;
    int topRowsTotal = 0, stripRows = 0;
    for (int iter = 0; ; ++iter)
    {
        auto area = b;
        if (sidebarW > 0) area.removeFromRight (sidebarW + 8);
        int colour = 0;
        stripRows = flowGroups (stripGroups, area, s, false, nullptr, colour);
        topRowsTotal = stripRows
                     + flowGroups (shownGroups, area, s, false, nullptr, colour);
        const int minH = topRowsTotal * (int) std::lround (rowH36 * s) + 6
                       + (int) std::lround (minScoreH * s)
                       + (int) std::lround (26.0f * s) // Osc|Sample|Live tab strip
                       // F6: the view bar's row comes out of the editors'
                       // height, so the canvas keeps its space
                       + (anyEditors ? (int) std::lround (minEdH * s) + 6
                                        + (int) std::lround (26.0f * s) : 0)
                       + (stripMode ? (int) std::lround (34.0f * s) : 0);
        if (minH <= b.getHeight() || iter >= 8) break;
        s *= 0.92f;
        if (s < 0.55f) { s = 0.55f; }
    }
    appLaf.uiScale = s;   // the fonts follow the layout's scale

    // top toolbar block: the groups share the lines, the hints get the
    // spare space on the right
    {
        auto top = b.removeFromTop (topRowsTotal * (int) std::lround (rowH36 * s) + 6);
        toolbarBounds = top;
        toolbarGroups.clear();
        juce::Rectangle<int> sidebar;
        if (sidebarW > 0)
            sidebar = top.removeFromRight (sidebarW);
        auto area = top;
        if (sidebarW > 0)
            area.removeFromRight (8);   // the gap between the groups and the sidebar

        // F5: the permanent strip is its own card row (or rows, when the
        // window is narrow), above the folding groups.
        permanentStripBounds = area.removeFromTop (stripRows * (int) std::lround (rowH36 * s));
        int colour = 0;
        flowGroups (stripGroups, permanentStripBounds, s, true, &toolbarGroups, colour);
        flowGroups (shownGroups, area, s, true, &toolbarGroups, colour);
        toolbarRows = topRowsTotal;

        // the two explanation labels live in the sidebar now
        const bool sidebarOn = sidebarW > 0;
        matHintLab.setVisible (sidebarOn);
        kbdHintLab.setVisible (sidebarOn);
        if (sidebarOn)
        {
            toolbarGroups.push_back ({ sidebar, colour });
            auto sb = sidebar.reduced (6, 4);
            const int matLines = lineCount (matHintLab.getText());
            const int kbdLines = lineCount (kbdHintLab.getText());
            const float safeS = juce::jmax (0.2f, s);
            const int matH = juce::jmin (sb.getHeight(), juce::jmax (18, matLines * 16 + 2));
            auto matRect = sb.removeFromTop (matH);
            // the text is scaled to the room the sidebar has
            matHintLab.setFont (juce::Font (juce::FontOptions (
                juce::jlimit (8.0f, 12.5f, (float) matRect.getHeight() / (float) juce::jmax (1, matLines) / safeS),
                juce::Font::bold)));
            matHintLab.setBounds (matRect);
            kbdHintLab.setFont (juce::Font (juce::FontOptions (
                juce::jlimit (8.0f, 12.5f, (float) sb.getHeight() / (float) juce::jmax (1, kbdLines) / safeS),
                juce::Font::bold)));
            kbdHintLab.setBounds (sb);
        }
    }

    // bottom: waveform editor | envelope editor | voice buttons
    //
    // F4: a focus view short-circuits all of this - the focused panel takes
    // the whole area below the toolbar, the other panels and the groups go.
    // The branch is placed after the helpers (the editor placement and the
    // tab strip), so both paths use the same code.
    auto placeEditor = [&] (juce::Rectangle<int> col, juce::Component& editor,
                           LibraryManager& manager, juce::Component* const* btns,
                           const int* ideal, int n, juce::Component* focusBtn)
    {
        // F2/F3: two header rows - the bank manager (selector, swatch and
        // commands) and the preset buttons. The editor takes the rest.
        manager.setBounds(col.removeFromTop((int) std::lround(26.0f * s)).reduced(2));
        auto t = col.removeFromTop((int) std::lround(26.0f * s));
        // F4: the focus button sits at the right end of the preset row.
        if (focusBtn != nullptr)
            focusBtn->setBounds (t.removeFromRight ((int) std::lround (64.0f * s)).reduced (1));
        int idealSum = 0;
        for (int i = 0; i < n; ++i)
            idealSum += ideal[i];
        const int avail = t.getWidth();
        for (int i = 0; i < n; ++i)
        {
            const int rem = t.getWidth();
            if (rem <= 0) break;
            const int w = juce::jlimit(1, rem, (int) std::lround((double)avail
                                        * (double)ideal[i]
                                        / (double)juce::jmax(1, idealSum)));
            btns[i]->setBounds(t.removeFromLeft(w).reduced(1));
        }
        editor.setBounds(col.reduced(2));
    };

    // v1.8 one strip directly above the canvas: each material is a tab and
    // its own canvas layer - Osc | Sample | Live - with the v1.7 view
    // toggles at the right end of the same strip
    auto placeTabStrip = [&] (juce::Rectangle<int> tabs)
    {
        matPmTog.setVisible (true);
        arcListBtn.setVisible (true);
        matOscTog.setBounds(tabs.removeFromLeft((int) std::lround(56.0f * s)).reduced(1));
        matSampleTog.setBounds(tabs.removeFromLeft((int) std::lround(76.0f * s)).reduced(1));
        matLiveTog.setBounds(tabs.removeFromLeft((int) std::lround(56.0f * s)).reduced(1));
        if (tabs.getWidth() >= (int) std::lround (40.0f * s)) matPmTog.setBounds (tabs.removeFromLeft ((int) std::lround (40.0f * s)).reduced (1));
        else matPmTog.setVisible (false);
        // F4: the score's focus button sits at the right end of the score's
        // own row (the canvas may not be covered by a control - the layout
        // is a pure tiling).
        if (tabs.getWidth() >= (int) std::lround (60.0f * s))
            scoreFocusBtn.setBounds (tabs.removeFromRight ((int) std::lround (60.0f * s)).reduced (1));
        else scoreFocusBtn.setVisible (false);
        hintsTog.setBounds(tabs.removeFromRight((int) std::lround(56.0f * s)).reduced(1));
        envTog.setBounds(tabs.removeFromRight((int) std::lround(46.0f * s)).reduced(1));
        waveTog.setBounds(tabs.removeFromRight((int) std::lround(52.0f * s)).reduced(1));
        // v1.21: the arc list toggle sits with the other view toggles
        if (tabs.getWidth() >= (int) std::lround (52.0f * s)) arcListBtn.setBounds (tabs.removeFromRight ((int) std::lround (52.0f * s)).reduced (1));
        else arcListBtn.setVisible (false);
    };

    juce::Component* wbtns[7] = { &wSine, &wSaw, &wSqr, &wTri, &wNoise, &wSmooth, &wNorm };
    const int wideal[] = { 30, 30, 30, 30, 44, 48, 34 };
    juce::Component* ebtns[5] = { &eFlat, &ePerc, &eSwell, &eGate, &eStr };
    const int eideal[] = { 40, 40, 44, 40, 34 };

    // --- F4: the focus view ---------------------------------------------
    if (focusedPanel != FocusPanel::none)
    {
        const bool focusScore = focusedPanel == FocusPanel::score;
        const bool focusWave  = focusedPanel == FocusPanel::wave;
        const bool focusEnv   = focusedPanel == FocusPanel::env;

        // the panels that are not focused go; the focus buttons of the
        // hidden panels go with them (the focused one says Exit).
        waveEd.setVisible (false); envEd.setVisible (false);
        waveLib.setVisible (false); envLib.setVisible (false);
        for (auto* w : wbtns) w->setVisible (false);
        for (auto* eb : ebtns) eb->setVisible (false);
        scoreFocusBtn.setVisible (focusScore);
        waveFocusBtn.setVisible (focusWave);
        envFocusBtn.setVisible (focusEnv);
        score.setVisible (focusScore);
        arcListPanel.setVisible (false);
        voiceLab.setVisible (false);
        assignBtn.setVisible (false);
        delArcBtn.setVisible (false);
        muteArcBtn.setVisible (false);
        matHintLab.setVisible (false);
        kbdHintLab.setVisible (false);

        if (focusScore)
        {
            // the material tabs belong to the score: they stay with it
            b.removeFromBottom(6);
            placeTabStrip (b.removeFromTop ((int) std::lround (26.0f * s)));
            // F6: the view bar keeps its own row, under the canvas
            viewBar.setVisible (true);
            viewBar.setLayoutScale (s);
            viewBar.setBounds (b.removeFromBottom ((int) std::lround (26.0f * s)));
            score.setBounds (b);
        }
        else
        {
            // the editors have no material tabs - the whole area is theirs
            matOscTog.setVisible (false); matSampleTog.setVisible (false);
            matLiveTog.setVisible (false); matPmTog.setVisible (false);
            hintsTog.setVisible (false); envTog.setVisible (false);
            waveTog.setVisible (false); arcListBtn.setVisible (false);
            scoreFocusBtn.setVisible (false);
            viewBar.setVisible (false);
            if (focusWave)
            {
                waveEd.setVisible (true); waveLib.setVisible (true);
                for (auto* w : wbtns) w->setVisible (true);
                placeEditor (b, waveEd, waveLib, wbtns, wideal, 7, &waveFocusBtn);
            }
            else
            {
                envEd.setVisible (true); envLib.setVisible (true);
                for (auto* eb : ebtns) eb->setVisible (true);
                placeEditor (b, envEd, envLib, ebtns, eideal, 5, &envFocusBtn);
            }
        }

        layoutOverlays (s);
        repaint();
        return;
    }

    const int stripH = stripMode ? (int) std::lround(34.0f * s) : 0;
    // F6: the score's view bar has its own row, directly under the canvas.
    // The row comes out of the editors' height, so the canvas keeps the
    // space it had (the canvas is the app's main work area).
    const int viewBarH = (int) std::lround (26.0f * s);
    // a single visible editor gets a little less height than two side by side
    const float wantEdHNow = (showWaveEd && showEnvEd) ? wantEdH : 240.0f;
    int edH = anyEditors ? (int) std::lround(wantEdHNow * s) : 0;
    const int budget = b.getHeight() - (int) std::lround(minScoreH * s) - 6 - stripH;
    if (anyEditors)
        edH = juce::jmin(edH, juce::jmax((int) std::lround(minEdH * s), budget));
    auto bottom = b.removeFromBottom(edH + stripH);

    // F6: the view bar sits between the canvas and the editors. When the
    // editors are there, its row comes out of their height, so the canvas
    // keeps the space it had; when they are hidden, the row comes out of
    // the canvas's own area (there is nothing else to give).
    viewBar.setVisible (true);
    viewBar.setLayoutScale (s);
    if (anyEditors)
        viewBar.setBounds (bottom.removeFromTop (viewBarH));
    else
        viewBar.setBounds (b.removeFromBottom (viewBarH));

    juce::Rectangle<int> vcol, vstrip;
    if (stripMode)
        vstrip = bottom.removeFromBottom(stripH);
    else
        vcol = bottom.removeFromRight(jmax((int) std::lround(150.0f * s),
                                           bottom.getWidth() * 12 / 100));

    // Editors side-by-side when there's room; on narrow layouts (portrait
    // phones) stack them so each keeps a usable width. A single visible
    // editor takes the whole width.
    juce::Rectangle<int> waveCol, envCol;
    if (showWaveEd && showEnvEd)
    {
        if (bottom.getWidth() >= (int) std::lround(460.0f * s))
        {
            waveCol = bottom.removeFromLeft(bottom.getWidth() * 42 / 100);
            envCol  = bottom;
        }
        else
        {
            waveCol = bottom.removeFromTop(bottom.getHeight() / 2);
            envCol  = bottom;
        }
    }
    else if (showWaveEd) waveCol = bottom;
    else if (showEnvEd)  envCol  = bottom;

    waveEd.setVisible(showWaveEd);
    waveLib.setVisible(showWaveEd);
    for (auto* w : wbtns) w->setVisible(showWaveEd);
    if (showWaveEd) placeEditor(waveCol, waveEd, waveLib, wbtns, wideal, 7, &waveFocusBtn);

    envEd.setVisible(showEnvEd);
    envLib.setVisible(showEnvEd);
    for (auto* eb : ebtns) eb->setVisible(showEnvEd);
    if (showEnvEd) placeEditor(envCol, envEd, envLib, ebtns, eideal, 5, &envFocusBtn);
    // F4: leaving a focus view restores everything the focus hid.
    score.setVisible (true);
    voiceLab.setVisible (true);
    assignBtn.setVisible (true);
    delArcBtn.setVisible (true);
    muteArcBtn.setVisible (true);
    matOscTog.setVisible (true); matSampleTog.setVisible (true);
    matLiveTog.setVisible (true); hintsTog.setVisible (true);
    envTog.setVisible (true); waveTog.setVisible (true);
    // the focus buttons: the panels carry their own
    scoreFocusBtn.setVisible (true);
    waveFocusBtn.setVisible (showWaveEd);
    envFocusBtn.setVisible (showEnvEd);
    // voice controls: side column (portrait with editors) or slim strip
    if (stripMode)
    {
        auto v = vstrip.reduced(4);
        voiceLab.setBounds(v.removeFromLeft((int) std::lround(140.0f * s)).reduced(2));
        const int bw = juce::jmax(1, v.getWidth() / 3);
        assignBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        delArcBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        muteArcBtn.setBounds(v.reduced(2));
    }
    else
    {
        auto v = vcol.reduced(4);
        voiceLab.setBounds(v.removeFromTop((int) std::lround(20.0f * s)));
        auto row = v.removeFromTop((int) std::lround(26.0f * s));
        assignBtn.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        delArcBtn.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        muteArcBtn.setBounds(row.reduced(2));
    }

    b.removeFromBottom(6);
    // v1.8 one strip directly above the canvas: each material is a tab and
    // its own canvas layer - Osc | Sample | Live - with the v1.7 view
    // toggles at the right end of the same strip
    placeTabStrip (b.removeFromTop ((int) std::lround (26.0f * s)));

    // v1.21: the arc list takes the right side of the canvas
    if (showArcList)
    {
        // at most 300 dp, and never so much that the canvas gets too narrow
        const int panelW = juce::jmin ((int) std::lround (300.0f * s),
                                        juce::jmax (0, b.getWidth() - (int) std::lround (240.0f * s)));
        if (panelW > 60)
        {
            arcListPanel.setBounds (b.removeFromRight (panelW).reduced (2));
            arcListPanel.setVisible (true);
        }
        else
        {
            arcListPanel.setVisible (false);
        }
    }
    else
    {
        arcListPanel.setVisible (false);
    }
    score.setBounds(b);


    // v1.30 (5.3 refactor): the overlay cards have their own method.
    layoutOverlays (s);

    // v1.33.3: the toolbar cards are painted by this component, not by the
    // controls. A layout change triggered from a toggle (Hints, Wave, Env)
    // moves the controls but does not invalidate the card rectangles, so the
    // old cards stayed behind until the window was resized. Repaint the whole
    // component after every layout pass.
    repaint();
}

void MainComponent::layoutOverlays (float s)
{
    // v1.7 audio settings overlay: full-window backdrop + centred card
    audioOverlay.setBounds(getLocalBounds());
    if (audioSelector != nullptr && audioOverlay.isVisible())
    {
        auto card = audioOverlay.cardRect().reduced(12);
        card.removeFromTop(AudioOverlay::titleH);
        // v1.30: the MIDI section moved to the settings overlay. The audio
        // card is the device selector plus OK.
        card.removeFromTop(6);
        auto btnRow = card.removeFromBottom(40);
        card.removeFromBottom(6);
        audioOkBtn.setBounds(btnRow.removeFromRight(120).reduced(2));
        audioSelector->setBounds(card);
    }

    // v1.30 (UPIC-54): MIDI + OSC settings overlay.
    settingsOverlay.setBounds(getLocalBounds());
    if (settingsOverlay.isVisible())
    {
        auto card = settingsOverlay.cardRect().reduced(12);
        card.removeFromTop(AudioOverlay::titleH);
        auto btnRow = card.removeFromBottom(40);
        card.removeFromBottom(6);
        settingsOkBtn.setBounds(btnRow.removeFromRight(100).reduced(2));
        btnRow.removeFromRight(8);
        oscApplyBtn.setBounds(btnRow.removeFromRight(110).reduced(2));

        // --- MIDI ---
        midiSectionLab.setBounds(card.removeFromTop((int) std::lround(22.0f * s)).reduced(2));
        {
            auto devRow = card.removeFromTop((int) std::lround (28.0f * s));
            midiDevLab.setBounds (devRow.removeFromLeft ((int) std::lround (76.0f * s)).reduced (2));
            midiDevBox.setBounds (devRow.removeFromLeft ((int) std::lround (220.0f * s)).reduced (2));
            card.removeFromTop (2);
            auto actRow = card.removeFromTop ((int) std::lround (22.0f * s));
            midiActLab.setBounds (actRow.removeFromLeft ((int) std::lround (300.0f * s)).reduced (2));
            card.removeFromTop (2);
            auto gainRow = card.removeFromTop ((int) std::lround (26.0f * s));
            midiGainLab.setBounds (gainRow.removeFromLeft ((int) std::lround (76.0f * s)).reduced (2));
            midiLearnGain.setBounds (gainRow.removeFromLeft ((int) std::lround (64.0f * s)).reduced (2));
            midiBindGainLab.setBounds (gainRow.removeFromLeft ((int) std::lround (90.0f * s)).reduced (2));
            card.removeFromTop (2);
            auto intRow = card.removeFromTop ((int) std::lround (26.0f * s));
            midiIntLab.setBounds (intRow.removeFromLeft ((int) std::lround (76.0f * s)).reduced (2));
            midiLearnInt.setBounds (intRow.removeFromLeft ((int) std::lround (64.0f * s)).reduced (2));
            midiBindIntLab.setBounds (intRow.removeFromLeft ((int) std::lround (90.0f * s)).reduced (2));
            card.removeFromTop (2);
            auto scrubRow = card.removeFromTop ((int) std::lround (26.0f * s));
            midiScrubLab.setBounds (scrubRow.removeFromLeft ((int) std::lround (76.0f * s)).reduced (2));
            midiLearnScrub.setBounds (scrubRow.removeFromLeft ((int) std::lround (64.0f * s)).reduced (2));
            midiBindScrubLab.setBounds (scrubRow.removeFromLeft ((int) std::lround (90.0f * s)).reduced (2));
            card.removeFromTop (2);
            auto transportRow = card.removeFromTop ((int) std::lround (26.0f * s));
            midiTransportLab.setBounds (transportRow.removeFromLeft ((int) std::lround (76.0f * s)).reduced (2));
            midiLearnTransport.setBounds (transportRow.removeFromLeft ((int) std::lround (64.0f * s)).reduced (2));
            midiBindTransportLab.setBounds (transportRow.removeFromLeft ((int) std::lround (90.0f * s)).reduced (2));
        }

        card.removeFromTop(8);
        // --- OSC ---
        oscSectionLab.setBounds(card.removeFromTop((int) std::lround(22.0f * s)).reduced(2));
        oscStatusLab.setBounds(card.removeFromTop((int) std::lround(20.0f * s)).reduced(2));
        auto onRow = card.removeFromTop((int) std::lround(28.0f * s));
        oscOnBtn.setBounds(onRow.removeFromLeft((int) std::lround(100.0f * s)).reduced(2));
        auto portRow = card.removeFromTop((int) std::lround(28.0f * s));
        oscPortLab.setBounds(portRow.removeFromLeft((int) std::lround(76.0f * s)).reduced(2));
        oscPortSlider.setBounds(portRow.removeFromLeft((int) std::lround(180.0f * s)).reduced(2));
        auto mcRow = card.removeFromTop((int) std::lround(28.0f * s));
        oscMcBtn.setBounds(mcRow.removeFromLeft((int) std::lround(100.0f * s)).reduced(2));
        oscGroupLab.setBounds(mcRow.removeFromLeft((int) std::lround(46.0f * s)).reduced(2));
        oscGroupEdit.setBounds(mcRow.removeFromLeft((int) std::lround(150.0f * s)).reduced(2));
        oscMcPortLab.setBounds(mcRow.removeFromLeft((int) std::lround(50.0f * s)).reduced(2));
        oscMcPortSlider.setBounds(mcRow.reduced(2));
    }

    // v1.27: the clear warning overlay (full-window backdrop + card)
    clearOverlay.setBounds(getLocalBounds());
    if (clearOverlay.isVisible())
    {
        auto card = clearOverlay.cardRect().reduced(16);
        card.removeFromTop(ClearOverlay::titleH);
        auto btnRow = card.removeFromBottom(36);
        card.removeFromBottom(6);
        clearCancelBtn.setBounds(btnRow.removeFromRight(110).reduced(2));
        btnRow.removeFromRight(8);
        clearConfirmBtn.setBounds(btnRow.removeFromRight(130).reduced(2));
    }
}
