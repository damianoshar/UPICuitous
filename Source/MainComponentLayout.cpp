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
    if (group == "accel")  return "Acceleration";
    if (group == "tools")  return "Generator";
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
        // WP7: a trim changes the material that was analysed. Close the
        // captured extraction source before the new snapshot is published,
        // so no preview or save can use the old region.
        invalidateSampleExtractSource();
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
        lastScrubT = t;
        lastScrubMs = nowMs;
        if (!transportActive() && scrubAudition)
            claimScrubPreviewRoute (t, rate);
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
            claimScrubFreezeRoute (t);
    };
    score.onScrubFreezeStop = [this](){
        updateScrubFreezeStop (juce::Time::getMillisecondCounterHiRes()
                               + (double) scrubTailMs.load (std::memory_order_relaxed));
    };
    score.onScrubFreezeCancel = [this](){
        cancelScrubFreezeRoute();
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
        // A successful stroke or line calls notify() after it has added the
        // arc, and that callback marks the revision. Marking it merely
        // because the gesture ended would turn a refused stroke (the page is
        // full, or the line is too short) into an undo step.
    };
    // v1.33.4 (L15, part 2): the undo transaction. The editor says when a
    // change starts and when it is over; the app keeps the state from
    // before it (one snapshot per gesture - a drag is one undo step).
    score.onEditStart = [this](const juce::String& what){ beginEdit (what); };
    score.onEditEnd   = [this]{
        endEdit();
        // v1.36: publish after the commit, not before it.
        publishModelSnapshot();
    };
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
    // v1.34.4: the arc commands the canvas context menu had (Duplicate,
    // Select all) are buttons in the Voice section as well.
    addAndMakeVisible(dupArcBtn); addAndMakeVisible(selectAllBtn);
    dupArcBtn.setName ("dupArcBtn");
    selectAllBtn.setName ("selectAllBtn");
    dupArcBtn.setTooltip ("Duplicate the selected arc(s) (Ctrl+D)");
    selectAllBtn.setTooltip ("Select every arc on this page (Ctrl+A)");
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
        {
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
    // v1.34.4: the context menu's Duplicate and Select all are buttons now.
    dupArcBtn.onClick = [this]{
        score.duplicateSelected();
        updateStatus(); refreshBoxes(); refreshPointFields();
    };
    selectAllBtn.onClick = [this]{
        score.selectAll();
        syncControlsToArc (score.getSelectedArc());
        updateStatus(); refreshPointFields();
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

    // WP7 (Wave F, 2026-11-21): the user library. Publishing changes no
    // piece state, so it is not an undo transaction. Importing is one bank
    // command and therefore exactly one transaction. Reveal opens the folder.
    waveLib.onPublish = [this]{ return libraryPublishWave(); };
    waveLib.onImport  = [this, bankEdit]{ return bankEdit("Import the wavetable",
                                       [this]{ return libraryImportWave(); }); };
    waveLib.onReveal  = [this]{ return libraryRevealWave(); };
    envLib.onPublish  = [this]{ return libraryPublishEnvelope(); };
    envLib.onImport   = [this, bankEdit]{ return bankEdit("Import the envelope",
                                       [this]{ return libraryImportEnvelope(); }); };
    envLib.onReveal   = [this]{ return libraryRevealEnvelope(); };

    waveLib.onSourceChanged = [this] (LibraryManager::Source source)
    {
        if (source == LibraryManager::Source::User)
            refreshUserLibraryBoxes (upic::UserAssetKind::Wave);
        else
            refreshLibraryBoxes();
    };
    envLib.onSourceChanged = [this] (LibraryManager::Source source)
    {
        if (source == LibraryManager::Source::User)
            refreshUserLibraryBoxes (upic::UserAssetKind::Envelope);
        else
            refreshLibraryBoxes();
    };

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
    // Close the deferred lifecycle-callback gate before any member can be
    // destroyed. A callback queued by an off-thread release then observes
    // the closed gate and never dereferences this partially torn-down object.
    closeAudioPreviewCallbackGate();

    // appLaf is a member, so it is destroyed before the Component base. Drop
    // the base class's weak reference first; otherwise the LookAndFeel
    // destructor sees a live owner and trips its debug assertion.
    setLookAndFeel (nullptr);

    // A plugin host can destroy the processor away from the message thread.
    // Stop the timer first, then wait for an already-running timer callback
    // to leave the message manager. No new callback can start after stopTimer.
    stopTimer();
    std::unique_ptr<juce::MessageManager::Lock> messageLock;
    if (auto* const messages = juce::MessageManager::getInstanceWithoutCreating();
        messages != nullptr && ! messages->isThisTheMessageThread())
        messageLock = std::make_unique<juce::MessageManager::Lock>();

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
    ordinaryPreviewSnapshots.retireReader (liveOrdinaryPreviewSnapshot);
    ordinaryPreviewSnapshots.clear();
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
    // height budget still doesn't fit, the grid shrinks (down to 55%) —
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
        // v1.34.3: the caption sits beside the control on the control row
        // (not above it) - the compact "Wave: [box]" in the Voice card.
        bool inlineLabel = false;
        // v1.34.3: this cell starts a new line in its card (the Pitch
        // buttons stay above the frequency-space dropdown).
        bool newLine = false;
        // v1.34.3: sit at the top of the line instead of its bottom (the
        // Pitch dropdown under the buttons would otherwise drop to the
        // knob's baseline, leaving a tall gap above it).
        bool topAlign = false;
        // v1.34.4: this knob spans the card's rows - it takes the right
        // column and its disc is cut in half by the row boundary.
        bool spansRows = false;
    };
    struct FlowGroup
    {
        juce::String name;
        std::vector<FlowItem> items;
        // v1.34: the card's title row - the group's disclosure toggle.
        juce::ToggleButton* header = nullptr;
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
    // v1.34.3: a label on the control row, to the left of its control.
    auto FI = [&] (juce::Component* l, int lw, juce::Component* c, int w)
    {
        auto it = fitItem (l, lw, c, w);
        it.inlineLabel = true;
        return it;
    };
    auto FX = [&] (juce::Component* c, int minSlice)
    { FlowItem it; it.comp = c; it.flex = true; it.minSlice = minSlice; return it; };
    // F11: a knob is a vertical card cell now - the caption above, the
    // large disc, the value below. The cell's default width is the knob's.
    auto FK = [&] (juce::Component* l, int lw, upic::Knob* c)
    {
        auto it = fitItem (l, lw, c, upic::Knob::kCellWidth);
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
                                           &waveFromSlotBtn, &sampleExtractBtn };  // WP7
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
    // v1.33.3: the groups keep their internal grouping and wrap onto the
    // next line when there is no room. v1.34.2: the card order is set by
    // the rank table at the end (the user's order).
    std::vector<FlowGroup> groups;

    // transport: Play and Stop live in the permanent strip (F5), so they stay
    // reachable at every fold level.
    groups.push_back ({ "transport", { F(&loopBtn, 64),
                                        F(&seqBtn, 60), F(&revPlayBtn, 64), F(&pressBtn, 96),
                                        F(&scrubBtn, 72) } });

    // the pages and the files
    // v1.34.2: the page selector's caption went; the group header already
    // says "Pages", so the box is the card's first cell.
    groups.push_back ({ "pages", { F(&pageBox, 150),
                                   F(&pageAddBtn, 30), F(&pageDupBtn, 46), F(&pageDelBtn, 30) } });
    // F5: Menu is in the permanent strip; the file group keeps the rest.
    groups.push_back ({ "file", { F(&settingsBtn, 78), F(&clearBtn, 64) } });

    // the tempo and the clock
    groups.push_back ({ "tempo", { FK(&tempoLab, 40, &tempoSlider) } });
    // v1.34.3: acceleration gets a card of its own (the Page card keeps
    // only its ambitus and its four frequency spaces).
    groups.push_back ({ "accel", { FK (&warpLab, 40, &warpSlider) } });
    groups.push_back ({ "clock", { F(&timeLab, 150) } });

    // the voice: what is drawn (the carrier wave, the envelope). The tool
    // selector lives in the permanent strip (F5).
    {
        FlowGroup voice { "voice", {} };
        if (showWaveCtl)
            voice.items.push_back (FI (&waveLab, 42, &waveBox, 140));
        voice.items.push_back (FI (&envLab, 34, &envBox, 140));
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
    // material, so it shows in every tab. v1.34.4: the card is two
    // columns - the buttons over the frequency-space dropdown on the
    // left, the division knob spanning both rows on the right.
    {
        FlowGroup pitch { "pitch", {} };
        pitch.items.push_back (F (&snapBtn, 68));
        pitch.items.push_back (F (&stepBtn, 76));
        auto stepDiv = FK (&stepDivLab, 52, &stepDivSlider);
        stepDiv.spansRows = true;
        pitch.items.push_back (stepDiv);
        auto pitchSpace = FL (&spaceLab, 44, &spaceBox, 170);
        pitchSpace.newLine = true;
        pitch.items.push_back (pitchSpace);
        groups.push_back (pitch);
    }

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
                          F(&waveFromSlotBtn, 110), F(&sampleExtractBtn, 82) };  // WP7
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
                                   FI(&sieveMLab, 18, &sieveMEdit, 52),
                                   FI(&sieveRLab, 18, &sieveREdit, 84) } });

    // v1.33: the GLOBAL magnets - the stick and the time grid. Every
    // page and every sieve zone follows these two (a zone can set its
    // own m/R and its own time grid, but not its own stick).
    groups.push_back ({ "magnets", { FK(&stickLab, 84, &stickSlider),
                                     FL(&gridLab, 86, &gridBox, 84) } });

    // v1.33: the page's frequency settings - its ambitus and its four
    // frequency spaces (the UPIC's tables). v1.34.3: the acceleration
    // moved to its own card.
    groups.push_back ({ "page", { FI(&ambitusLab, 58, &ambitusBox, 170),
                                  FI(&spaceDefLab[0], 46, &spaceDefBox[0], 120),
                                  FI(&spaceDefLab[1], 46, &spaceDefBox[1], 120),
                                  FI(&spaceDefLab[2], 46, &spaceDefBox[2], 120),
                                  FI(&spaceDefLab[3], 46, &spaceDefBox[3], 120),
                                  F(&spaceTableBtn, 66) } });

    // WP2/WP3/WP5: the amplitude table, the groups and the modulator.
    groups.push_back ({ "amp", { FI(&ampTableLab, 34, &ampTableBox, 150),
                                  F(&ampTableBtn, 76),
                                  F(&groupBtn, 86) } });
    groups.push_back ({ "mod", { FI(&arcModLab, 30, &arcModBox, 100),
                                  FI(&arcModTgtLab, 26, &arcModTgtBox, 66),
                                  FI(&arcModDepthLab, 36, &arcModSlider, 110) } });
    // --- WP6.4: the mixer (WP7 adds its own card in its marked block) ---
    groups.push_back ({ "ch", { FI(&channelLab, 26, &channelBox, 100),
                                F(&channelBtn, 62) } });
    // --- end WP6.4 ---

    // --- WP6.4: the mixer (WP7 adds its own card in its marked block) ---
    groups.push_back ({ "ch", { FI(&channelLab, 26, &channelBox, 100),
                                F(&channelBtn, 62) } });
    // --- end WP6.4 ---

    // WP4: the performance sequences.
    groups.push_back ({ "seq", { FI(&seqLab, 30, &seqBox, 110),
                                  F(&seqRecBtn, 52), F(&seqPlayBtn, 54),
                                  F(&seqLoopBtn, 52), F(&seqEditBtn, 54) } });

    // v1.34.3: the tools card is the Generator; its caption is the title,
    // so the "Gen" label above the dropdown is gone.
    groups.push_back ({ "tools", { F(&genBox, 220) } });

    // the scrub's parameters and the arc dragging
    groups.push_back ({ "scrub", { FK(&scrubLenLab, 60, &scrubLenSlider),
                                   FK(&scrubCrossLab, 70, &scrubCrossSlider),
                                   FK(&scrubTailLab, 60, &scrubTailSlider),
                                   FK(&scrubGrainLab, 70, &scrubGrainSlider) } });
    groups.push_back ({ "drag", { F(&muteDragBtn, 90),
                                  FK(&dragSmoothLab, 70, &dragSmoothSlider) } });

    // the status line: its card is measured to the one-line text
    groups.push_back ({ "status", { FX (&statusLab, kStatusMinWidthDp) } });

    // v1.34.2: the user's card order. A rank table keeps the order in one
    // place - the groups above are built where their controls are easiest
    // to describe, and the material card depends on the active tab. The
    // status line stays last: it is the flexible card that fills a row.
    static const char* const cardOrder[] = {
        "transport", "pages", "file", "voice", "sieve", "tools", "tempo",
        "accel", "levels", "material", "magnets", "scrub", "drag", "pitch",
        "page", "clock", "status"
    };
    auto cardRank = [] (const juce::String& name) -> int
    {
        for (int i = 0; i < (int) juce::numElementsInArray (cardOrder); ++i)
            if (name == cardOrder[i])
                return i;
        return (int) juce::numElementsInArray (cardOrder);
    };
    std::stable_sort (groups.begin(), groups.end(),
                      [&] (const FlowGroup& a, const FlowGroup& b)
                      { return cardRank (a.name) < cardRank (b.name); });

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

        // v1.34: the disclosure is the card's header, not an item in the
        // card's flow.
        FlowGroup shown { grp.name, {}, fold };

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

    // --- the permanent strip's flow ---------------------------------------
    // The strip stays a compact row of controls above the cards (it is the
    // menu + transport + tool + Restore, not a tool group).
    const int groupGapDp = 6;

    auto groupWidth = [&] (const FlowGroup& g) -> int
    {
        int w = 0;
        for (auto& it : g.items)
            w += it.flex ? it.minSlice : it.labelW + it.w;
        return w;
    };

    auto flowStrip = [&] (const std::vector<FlowGroup>& list, juce::Rectangle<int> area,
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
            const int gw = (int) std::lround ((float) groupWidth (grp) * sc);
            // does the group fit on the current line?
            if (x > area.getX() && x + gw + gap > right) { x = area.getX(); y += rh2; }
            const int gx = x, gy = y;
            int segX = gx, segY = gy;
            auto closeSegment = [&]()
            {
                if (rects != nullptr && x > segX)
                    rects->push_back ({ juce::Rectangle<int> (segX, segY,
                                                              juce::jmax (1, x - segX), rh2),
                                        groupColour, grp.name });
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
                const int cw = (int) std::lround ((float) it.w * sc);
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

    // --- the cards ---------------------------------------------------------
    // v1.34 (2026-11-18): every group is a card, like the EQ reference. The
    // card's header is the group's disclosure toggle; its cells are the
    // group's controls. A knob's cell is vertical (caption above, large
    // disc, value below); a labelled control keeps its label as the
    // caption; a bare control sits on the card's control row. The cards
    // wrap into rows - there is no scrolling - and a card that is wider
    // than the window wraps its own cells inside itself.
    const int cardPadX = 4, cardPadY = 3, cardGap = 6, cardHeaderH = 20;
    const int cardCellGap = 4, cardRowGap = 4;
    const int cardControlH = 26;   // the row a button / dropdown occupies
    const int kInlineGapDp = 4;    // label-to-control gap on an inline cell

    // v1.34.1: a card is only as tall as its tallest cell. A knob's cell is
    // 90dp (caption, disc, pill); a plain control is a caption row over one
    // control row. Cards in one row keep their own height - the layout does
    // not pad them to a common height. (The cell's pixel height is the sum
    // of its parts, not the scaled sum: 40dp at 0.56 is 22px, but the 8px
    // caption and the 15px control are 23px together.)
    struct CardCell { FlowItem item; int widthDp = 0; };

    auto captionWidthDp = [] (juce::Component* l) -> int
    {
        if (auto* lab = dynamic_cast<juce::Label*> (l))
        {
            juce::Font f (juce::FontOptions (10.5f, juce::Font::bold));
            return juce::GlyphArrangement::getStringWidthInt (f, lab->getText()) + 8;
        }
        return 0;
    };
    auto headerWidthDp = [] (const FlowGroup& grp) -> int
    {
        if (grp.header == nullptr)
            return 0;
        juce::Font f (juce::FontOptions (15.0f));
        return juce::GlyphArrangement::getStringWidthInt (f, grp.header->getButtonText()) + 44;
    };
    auto makeCells = [&] (const FlowGroup& grp) -> std::vector<CardCell>
    {
        std::vector<CardCell> cells;
        for (auto& it : grp.items)
        {
            CardCell c { it, 0 };
            // v1.34.5: the status card is measured to its one-line text
            // instead of stretching to the row's end. neededWidth (in
            // design units, like widthDp) carries the label's own text,
            // so the label's 5+5 px border and a little air are added.
            if (it.flex)
                c.widthDp = juce::jmax (it.minSlice, neededWidth (it.comp) + 16);
            else if (it.knob)
                c.widthDp = juce::jmax (upic::Knob::kCellWidth, captionWidthDp (it.label));
            else if (it.inlineLabel)
                c.widthDp = it.labelW + kInlineGapDp + it.w;
            else
                c.widthDp = juce::jmax (it.w, it.labelW + 4);
            cells.push_back (c);
        }
        // v1.34.3: a card's knobs sit at its right end - the plain
        // controls keep their order, then the knobs keep theirs.
        std::stable_partition (cells.begin(), cells.end(),
                               [] (const CardCell& c) { return ! c.item.knob; });
        return cells;
    };

    // v1.34.4: the height a cell needs - the same rule the card's rows use.
    auto cardCellHeight = [&] (const CardCell& c, float sc) -> int
    {
        const int captionH = juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellCaptionH * sc));
        const int ctrlH = juce::jmax (14, (int) std::lround ((float) cardControlH * sc));
        if (c.item.flex) return ctrlH;
        if (c.item.knob) return juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellHeight * sc));
        return (c.item.label != nullptr && ! c.item.inlineLabel ? captionH : 0) + ctrlH;
    };

    // v1.34.4: a knob that spans the card's rows (the Pitch card) takes the
    // right column; the other cells share the left one and the knob's disc
    // is cut in half by the boundary between their rows.
    struct CardLine { int width = 0, height = 0; std::vector<size_t> cells; };
    struct SpannerLayout
    {
        int index = -1;
        int knobW = 0, knobH = 0, half = 0, bodyH = 0, leftW = 0;
        std::vector<CardLine> lines;
    };
    auto layoutSpanner = [&] (const std::vector<CardCell>& cells, float sc, int maxW,
                               int gap) -> SpannerLayout
    {
        SpannerLayout out;
        for (size_t i = 0; i < cells.size(); ++i)
            if (cells[i].item.spansRows) { out.index = (int) i; break; }
        if (out.index < 0)
            return out;

        const auto& sp = cells[(size_t) out.index];
        out.knobW = juce::jmax (8, (int) std::lround ((float) sp.widthDp * sc));
        out.knobH = juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellHeight * sc));
        const int capH = juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellCaptionH * sc));
        const int pillH = juce::jmax (6, (int) std::lround ((float) upic::Knob::kCellPillH * sc));
        const int knobGap = juce::jmax (1, (int) std::lround ((float) upic::Knob::kCellGap * sc));
        const int side = juce::jmax (0, juce::jmin (out.knobW,
                                                    out.knobH - capH - pillH - 2 * knobGap));
        out.half = capH + knobGap + side / 2;   // the disc's middle

        const int leftMax = juce::jmax (8, maxW - out.knobW - gap);
        CardLine cur;
        int curW = 0, curH = 0;
        auto closeLine = [&]
        {
            if (! cur.cells.empty())
            {
                cur.width = curW;
                cur.height = curH;
                out.lines.push_back (cur);
            }
            cur = CardLine();
            curW = 0;
            curH = 0;
        };
        for (size_t i = 0; i < cells.size(); ++i)
        {
            if ((int) i == out.index) continue;
            const auto& c = cells[i];
            const int cw = juce::jmax (8, (int) std::lround ((float) c.widthDp * sc));
            const int ch = cardCellHeight (c, sc);
            if (c.item.newLine && curW > 0) closeLine();
            if (curW > 0 && curW + gap + cw > leftMax) closeLine();
            curW += (curW > 0 ? gap : 0) + cw;
            curH = juce::jmax (curH, ch);
            cur.cells.push_back (i);
        }
        closeLine();

        // the first row reaches the knob's disc middle; the rows below share
        // what is left, so the knob bridges the boundary
        int used = 0;
        for (size_t li = 0; li < out.lines.size(); ++li)
        {
            auto& line = out.lines[li];
            int h = line.height;
            if (li == 0)
                h = juce::jmax (h, out.half);
            else
                h = juce::jmax (h, (out.knobH - used) / (int) (out.lines.size() - li));
            line.height = h;
            used += h;
            out.leftW = juce::jmax (out.leftW, line.width);
        }
        out.bodyH = juce::jmax (out.knobH, used);
        return out;
    };

    struct CardMetrics { int width = 0, height = 0; };

    auto cardMetrics = [&] (const FlowGroup& grp, float sc, int maxW) -> CardMetrics
    {
        const int padX = juce::jmax (2, (int) std::lround ((float) cardPadX * sc));
        const int padY = juce::jmax (1, (int) std::lround ((float) cardPadY * sc));
        const int headerH = juce::jmax (8, (int) std::lround ((float) cardHeaderH * sc));
        const int gap = juce::jmax (2, (int) std::lround ((float) cardCellGap * sc));
        const int rowGap = juce::jmax (2, (int) std::lround ((float) cardRowGap * sc));
        const int innerMax = juce::jmax (1, maxW - 2 * padX);
        const int captionH = juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellCaptionH * sc));
        const int ctrlH = juce::jmax (14, (int) std::lround ((float) cardControlH * sc));
        auto cellH = [&] (const CardCell& c) -> int
        {
            if (c.item.flex) return ctrlH;
            if (c.item.knob) return juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellHeight * sc));
            return (c.item.label != nullptr && ! c.item.inlineLabel ? captionH : 0) + ctrlH;
        };

        const auto cells = makeCells (grp);
        int lineW = 0, widest = 0, bodyH = 0, lineH = 0;

        // v1.34.4: the Pitch card's knob spans the card's rows - the card
        // is two columns: the controls on the left, the knob on the right.
        const auto spanner = layoutSpanner (cells, sc, innerMax, gap);
        if (spanner.index >= 0)
        {
            const int headerPx = (int) std::lround ((float) headerWidthDp (grp) * sc);
            CardMetrics m;
            m.width = juce::jmin (maxW, juce::jmax (spanner.leftW + gap + spanner.knobW,
                                                    headerPx) + 2 * padX);
            m.height = headerH + 2 * padY + spanner.bodyH;
            return m;
        }

        for (auto& c : cells)
        {
            const int cw = juce::jmax (8, (int) std::lround ((float) c.widthDp * sc));
            const int ch = cellH (c);
            if (c.item.newLine && lineW > 0)
            {
                widest = juce::jmax (widest, lineW);
                bodyH += lineH + rowGap;
                lineW = 0;
                lineH = 0;
            }
            if (lineW > 0 && lineW + gap + cw > innerMax)
            {
                widest = juce::jmax (widest, lineW);
                bodyH += lineH + rowGap;
                lineW = 0;
                lineH = 0;
            }
            lineW += (lineW > 0 ? gap : 0) + cw;
            lineH = juce::jmax (lineH, ch);
        }
        widest = juce::jmax (widest, lineW);
        bodyH += lineH;

        const int headerPx = (int) std::lround ((float) headerWidthDp (grp) * sc);
        CardMetrics m;
        m.width = juce::jmin (maxW, juce::jmax (widest, headerPx) + 2 * padX);
        m.height = headerH + 2 * padY + bodyH;
        return m;
    };

    auto placeCardCells = [&] (const FlowGroup& grp, juce::Rectangle<int> card, float sc)
    {
        const int padX = juce::jmax (2, (int) std::lround ((float) cardPadX * sc));
        const int padY = juce::jmax (1, (int) std::lround ((float) cardPadY * sc));
        const int headerH = juce::jmax (8, (int) std::lround ((float) cardHeaderH * sc));
        const int gap = juce::jmax (2, (int) std::lround ((float) cardCellGap * sc));
        const int rowGap = juce::jmax (2, (int) std::lround ((float) cardRowGap * sc));
        const int captionH = juce::jmax (8, (int) std::lround ((float) upic::Knob::kCellCaptionH * sc));
        const int ctrlH = juce::jmax (14, (int) std::lround ((float) cardControlH * sc));
        const int knobH = (int) std::lround ((float) upic::Knob::kCellHeight * sc);
        auto cellH = [&] (const CardCell& c) -> int
        {
            if (c.item.flex) return ctrlH;
            if (c.item.knob) return juce::jmax (8, knobH);
            return (c.item.label != nullptr && ! c.item.inlineLabel ? captionH : 0) + ctrlH;
        };

        if (grp.header != nullptr)
            grp.header->setBounds (card.getX() + padX, card.getY() + padY,
                                   juce::jmax (8, card.getWidth() - 2 * padX),
                                   juce::jmax (8, headerH));

        auto body = card.withTrimmedTop (headerH).reduced (padX, 0);
        body.removeFromTop (padY);
        body.removeFromBottom (padY);

        // first, pack the cells into lines and learn each line's height
        const auto cells = makeCells (grp);

        // v1.34.4: the Pitch card's knob spans the card's rows - it takes
        // the right column and the other cells share the left one.
        const auto spanner = layoutSpanner (cells, sc, body.getWidth(), gap);
        if (spanner.index >= 0)
        {
            const auto& knobItem = cells[(size_t) spanner.index].item;
            juce::String caption;
            if (auto* lab = dynamic_cast<juce::Label*> (knobItem.label))
                caption = lab->getText();
            if (auto* knob = dynamic_cast<upic::Knob*> (knobItem.comp))
            {
                knob->setDesignScale (sc);
                knob->setCellCaption (caption);
                knob->setFallback (upic::Knob::shouldFallBack (sc));
            }
            if (knobItem.comp != nullptr)
                knobItem.comp->setBounds (body.getRight() - spanner.knobW, body.getY(),
                                          spanner.knobW,
                                          juce::jmin (spanner.knobH, body.getHeight()));
            if (knobItem.label != nullptr)
                knobItem.label->setVisible (false);

            // the left column: the rows are flush, so the knob bridges them
            int rowY = body.getY();
            for (const auto& line : spanner.lines)
            {
                int x = body.getX();
                for (auto ci : line.cells)
                {
                    const auto& c = cells[ci];
                    const auto& item = c.item;
                    const int cw = juce::jmin (
                        juce::jmax (8, (int) std::lround ((float) c.widthDp * sc)),
                        juce::jmax (8, body.getWidth() - spanner.knobW - gap));

                    if (item.flex)
                    {
                        if (item.comp != nullptr)
                            item.comp->setBounds (x, rowY, cw, line.height);
                    }
                    else if (item.inlineLabel)
                    {
                        const int gapPx = juce::jmax (2, (int) std::lround ((float) kInlineGapDp * sc));
                        const int lw = juce::jmin ((int) std::lround ((float) item.labelW * sc),
                                                   juce::jmax (0, cw - gapPx));
                        const int cy = rowY + (line.height - ctrlH) / 2;
                        if (item.label != nullptr)
                            item.label->setBounds (x, cy, lw, ctrlH);
                        if (item.comp != nullptr)
                            item.comp->setBounds (x + lw + gapPx, cy,
                                                  juce::jmax (1, cw - lw - gapPx), ctrlH);
                    }
                    else
                    {
                        const int capH = (item.label != nullptr ? captionH : 0);
                        const int cy = rowY + (line.height - (capH + ctrlH)) / 2;
                        if (item.label != nullptr)
                            item.label->setBounds (x, cy, cw, capH);
                        if (item.comp != nullptr)
                            item.comp->setBounds (x, cy + capH, cw, ctrlH);
                    }
                    x += cw + gap;
                }
                rowY += line.height;
            }
            return;
        }

        struct PlacedCell { const CardCell* cell; int x, y, w, lineH; };
        std::vector<PlacedCell> placed;
        size_t lineStart = 0;
        int x = body.getX(), y = body.getY(), lineH = 0;
        auto closeLine = [&]()
        {
            for (size_t k = lineStart; k < placed.size(); ++k)
                placed[k].lineH = lineH;
            y += lineH + rowGap;
            lineStart = placed.size();
            lineH = 0;
            x = body.getX();
        };
        for (auto& c : cells)
        {
            // v1.34.1: a narrowed card (the flex status) must not let its
            // cell hang over the card's edge; the cell follows the body.
            const int cw = juce::jmin (juce::jmax (8, (int) std::lround ((float) c.widthDp * sc)),
                                       juce::jmax (8, body.getWidth()));
            const int ch = cellH (c);
            if (c.item.newLine && x > body.getX())
                closeLine();
            if (x > body.getX() && x + cw > body.getRight())
                closeLine();
            placed.push_back ({ &c, x, y, cw, ch });
            x += cw + gap;
            lineH = juce::jmax (lineH, ch);
        }
        closeLine();

        // v1.34.3: a card whose only cell is a knob centres it under the
        // header instead of hugging the left edge (Tempo, Acceleration).
        if (placed.size() == 1 && placed[0].cell->item.knob)
            placed[0].x = body.getX() + (body.getWidth() - placed[0].w) / 2;

        // then place everything against its line's bottom, so the controls
        // and value pills line up however tall the neighbour is
        for (auto& p : placed)
        {
            const int bottom = p.y + p.lineH;
            auto& item = p.cell->item;

            if (item.flex)
            {
                // v1.34.5: the status label owns the card's whole body -
                // the measured cell can be narrower than the header.
                if (item.comp != nullptr)
                    item.comp->setBounds (body.getX(), p.y, body.getWidth(), p.lineH);
            }
            else if (item.knob)
            {
                juce::String caption;
                if (auto* lab = dynamic_cast<juce::Label*> (item.label))
                    caption = lab->getText();
                if (auto* knob = dynamic_cast<upic::Knob*> (item.comp))
                {
                    knob->setDesignScale (sc);
                    knob->setCellCaption (caption);
                    knob->setFallback (upic::Knob::shouldFallBack (sc));
                }
                if (item.comp != nullptr)
                    item.comp->setBounds (p.x, bottom - knobH, p.w, knobH);
                if (item.label != nullptr)
                    item.label->setVisible (false);
            }
            else if (item.inlineLabel)
            {
                const int gapPx = juce::jmax (2, (int) std::lround ((float) kInlineGapDp * sc));
                const int lw = juce::jmin ((int) std::lround ((float) item.labelW * sc),
                                           juce::jmax (0, p.w - gapPx));
                const int cellY = item.topAlign ? p.y : bottom - ctrlH;
                if (item.label != nullptr)
                    item.label->setBounds (p.x, cellY, lw, ctrlH);
                if (item.comp != nullptr)
                    item.comp->setBounds (p.x + lw + gapPx, cellY,
                                          juce::jmax (1, p.w - lw - gapPx), ctrlH);
            }
            else
            {
                const int labelY = item.topAlign ? p.y : bottom - ctrlH - captionH;
                const int compY  = item.topAlign ? p.y + captionH : bottom - ctrlH;
                if (item.label != nullptr)
                    item.label->setBounds (p.x, labelY, p.w, captionH);
                if (item.comp != nullptr)
                    item.comp->setBounds (p.x, compY, p.w, ctrlH);
            }
        }
    };

    auto layoutCards = [&] (const std::vector<FlowGroup>& list, juce::Rectangle<int> area,
                            float sc, bool place, std::vector<ToolbarCard>* rects,
                            int& groupColour, int* rowsOut = nullptr) -> int
    {
        const int gap = juce::jmax (2, (int) std::lround ((float) cardGap * sc));
        int rows = 0;

        struct Card { const FlowGroup* grp; int w, h; };
        std::vector<Card> cards;
        cards.reserve (list.size());
        for (auto& grp : list)
        {
            const auto m = cardMetrics (grp, sc, area.getWidth());
            cards.push_back ({ &grp, m.width, m.height });
        }

        // v1.34.5: every card keeps its own width - the status card is
        // measured to its text, so it packs into a row like the others
        // instead of being stretched over the rest of the line.
        std::vector<std::vector<size_t>> rowCards;
        std::vector<int> rowW, rowH;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            if (rowCards.empty() || rowW.back() + gap + cards[i].w > area.getWidth())
            {
                rowCards.push_back ({});
                rowW.push_back (0);
                rowH.push_back (0);
            }
            rowCards.back().push_back (i);
            rowW.back() += (rowCards.back().size() > 1 ? gap : 0) + cards[i].w;
            rowH.back() = juce::jmax (rowH.back(), cards[i].h);
        }

        int y = area.getY();
        for (size_t r = 0; r < rowCards.size(); ++r)
        {
            int x = area.getX();
            for (auto k : rowCards[r])
            {
                const int cw = cards[k].w;
                const juce::Rectangle<int> card (x, y, cw, cards[k].h);
                if (rects != nullptr)
                    rects->push_back ({ card, groupColour, cards[k].grp->name });
                if (place)
                    placeCardCells (*cards[k].grp, card, sc);
                ++groupColour;
                x += cw + gap;
            }
            y += rowH[r] + gap;
            ++rows;
        }

        if (rowsOut != nullptr) *rowsOut = rows;
        return juce::jmax (0, y - gap - area.getY());
    };

    // the hints sidebar: the long explanations that used to sit in the
    // toolbar's rows get their own column on the right - the spare space
    // on a large screen. The Hints toggle shows and hides it. A focus view
    // has no room for it (F4), and neither has a short window: the cards
    // need the width more than the hints do. v1.34.1: the width follows the
    // window (20%, 190-260dp), so a narrower sidebar can buy a whole card.
    const int sidebarW = (! anyFocus && showHints && getWidth() >= 820
                          && getHeight() >= 700)
                           ? juce::jlimit (190, 260, (int) std::lround (getWidth() * 0.2))
                           : 0;

    // fit-shrink: wrapping is scale-dependent, so settle by iteration.
    // v1.34: the cards are taller than the old rows. When the minimum
    // scale is still not enough, the cards that would not fit fold to
    // their headers - from the end, so the important groups stay open.
    // The user's own fold levels are never touched; the fold is recomputed
    // on every layout, so a larger window brings the cards back.
    float s = 1.0f;
    int stripRows = 0, topH = 0;
    std::vector<char> autoCollapsed (shownGroups.size(), 0);

    auto buildEffective = [&] (bool hide) -> std::vector<FlowGroup>
    {
        std::vector<FlowGroup> out;
        out.reserve (shownGroups.size());
        for (size_t i = 0; i < shownGroups.size(); ++i)
        {
            if (autoCollapsed[i] != 0 && shownGroups[i].name != "status")
            {
                if (hide)
                    for (auto& it : shownGroups[i].items)
                    {
                        if (it.comp != nullptr)
                        {
                            it.comp->setVisible (false);
                            foldHidden.push_back (it.comp);
                        }
                        if (it.label != nullptr)
                        {
                            it.label->setVisible (false);
                            foldHidden.push_back (it.label);
                        }
                    }
                out.push_back ({ shownGroups[i].name, {}, shownGroups[i].header });
            }
            else
            {
                out.push_back (shownGroups[i]);
            }
        }
        return out;
    };

    auto toolbarHeightAt = [&] (float sc) -> int
    {
        auto area = b;
        if (sidebarW > 0) area.removeFromRight (sidebarW + 8);
        int colour = 0;
        const int sr = flowStrip (stripGroups, area, sc, false, nullptr, colour);
        const auto eff = buildEffective (false);
        const int cards = layoutCards (eff, area, sc, false, nullptr, colour);
        return sr * (int) std::lround (rowH36 * sc) + (sr > 0 ? 4 : 0) + cards;
    };

    for (int iter = 0; ; ++iter)
    {
        topH = toolbarHeightAt (s);
        const int minH = topH + 6
                       + (int) std::lround (minScoreH * s)
                       + (int) std::lround (26.0f * s) // Osc|Sample|Live tab strip
                       // F6: the view bar's row comes out of the editors'
                       // height, so the canvas keeps its space
                       + (anyEditors ? (int) std::lround (minEdH * s) + 6
                                        + (int) std::lround (26.0f * s) : 0)
                       + (stripMode ? (int) std::lround (34.0f * s) : 0);
        if (minH <= b.getHeight() || iter >= 40) break;
        if (s > 0.5501f)
        {
            s = juce::jmax (0.55f, s * 0.92f);
            continue;
        }
        // at the minimum scale: fold a group from the end into its header
        int target = -1;
        for (int gi = (int) shownGroups.size() - 1; gi >= 0; --gi)
            if (autoCollapsed[(size_t) gi] == 0 && shownGroups[(size_t) gi].name != "status")
            {
                target = gi;
                break;
            }
        if (target < 0) break;
        autoCollapsed[(size_t) target] = 1;
    }
    appLaf.uiScale = s;   // the fonts follow the layout's scale

    // top toolbar block: the permanent strip and the cards; the hints get
    // the spare space on the right
    {
        auto top = b.removeFromTop (juce::jmin (b.getHeight(), topH + 6));
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
        int colour = 0;
        stripRows = flowStrip (stripGroups, area, s, false, nullptr, colour);
        const int stripH = stripRows * (int) std::lround (rowH36 * s);
        permanentStripBounds = area.removeFromTop (stripH);
        colour = 0;
        flowStrip (stripGroups, permanentStripBounds, s, true, &toolbarGroups, colour);
        if (stripH > 0) area.removeFromTop (4);

        // v1.34: one card per group, wrapped into rows
        int cardRows = 0;
        layoutCards (buildEffective (true), area, s, true, &toolbarGroups, colour, &cardRows);
        toolbarRows = stripRows + cardRows;

        // the two explanation labels live in the sidebar now
        const bool sidebarOn = sidebarW > 0;
        matHintLab.setVisible (sidebarOn);
        kbdHintLab.setVisible (sidebarOn);
        if (sidebarOn)
        {
            toolbarGroups.push_back ({ sidebar, colour, "hints" });
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
        dupArcBtn.setVisible (false);
        selectAllBtn.setVisible (false);
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
    // v1.34.1: the score is the composition canvas, so the editors keep at
    // most ~42% of the height below the toolbar even when they would like
    // 300dp. The recovered toolbar height goes to the score, not to empty
    // space at the bottom of a fixed-height editor.
    const int edCap = juce::jmax ((int) std::lround (minEdH * s),
                                  (int) std::lround ((float) juce::jmax (0, b.getHeight() - stripH) * 0.42f));
    if (anyEditors)
        edH = juce::jmin(edH, juce::jmax((int) std::lround(minEdH * s),
                                         juce::jmin (budget, edCap)));
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
    dupArcBtn.setVisible (true);
    selectAllBtn.setVisible (true);
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
        const int bw = juce::jmax(1, v.getWidth() / 5);
        assignBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        delArcBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        muteArcBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        dupArcBtn.setBounds(v.removeFromLeft(bw).reduced(2));
        selectAllBtn.setBounds(v.reduced(2));
    }
    else
    {
        auto v = vcol.reduced(4);
        voiceLab.setBounds(v.removeFromTop((int) std::lround(20.0f * s)));
        auto row = v.removeFromTop((int) std::lround(26.0f * s));
        assignBtn.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        delArcBtn.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        muteArcBtn.setBounds(row.reduced(2));
        // v1.34.4: the second row carries Duplicate and Select all.
        auto row2 = v.removeFromTop((int) std::lround (26.0f * s));
        const int bw2 = juce::jmax (1, row2.getWidth() / 2);
        dupArcBtn.setBounds(row2.removeFromLeft(bw2).reduced(2));
        selectAllBtn.setBounds(row2.reduced(2));
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
    freqTableEd.setBounds (getLocalBounds());
    ampTableEd.setBounds (getLocalBounds());
    groupPanel.setBounds (getLocalBounds());
    seqPanel.setBounds (getLocalBounds());
    if (channelPanel != nullptr) channelPanel->setBounds (getLocalBounds()); // WP6.4
    // WP7: the sample extraction panel is lazy, so it has no bounds until
    // it has been built once.
    if (sampleExtractPanel) sampleExtractPanel->setBounds (getLocalBounds());

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