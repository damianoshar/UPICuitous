#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The layout: where every control sits, and how the window is painted.

void MainComponent::buildScoreArea()
{
    // --- score ---
    addAndMakeVisible(score);
    score.setName("score");
    timeLab.setName("timeLab");
    waveEd.setName("waveEd");
    // every editor mutates the same model the timer copies into the audio
    // snapshot, so they all share the message-thread model lock
    score.setModelLock(&dataLock);
    waveEd.setModelLock(&dataLock);
    envEd.setModelLock(&dataLock);
    waveEd.setOnEditStart([this]{
        // v1.33.4 (L15, part 2): the undo transaction - the state from
        // before the wavetable is edited (the fork of the selection
        // included).
        beginEdit ("Edit the wavetable");
        return prepareWaveEdit();
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
        [this]{ return isPlaying.load(std::memory_order_relaxed)
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
        if (!isPlaying.load(std::memory_order_relaxed) && scrubAudition)
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
    score.onSelect = [this](int id){ syncControlsToArc(id); updateStatus(); };
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
        publishModelSnapshot();
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
    eFlat.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetFlat(); envEd.repaint(); markModelChanged(); } };
    ePerc.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetPercussive(); envEd.repaint(); markModelChanged(); } };
    eSwell.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetSwell(); envEd.repaint(); markModelChanged(); } };
    eGate.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetGate(); envEd.repaint(); markModelChanged(); } };
    eStr.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetStrings(); envEd.repaint(); markModelChanged(); } };

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
    static const juce::Colour fills[]   { juce::Colour (0xff1a1f2a), juce::Colour (0xff1a1a22),
                                          juce::Colour (0xff1a241d), juce::Colour (0xff241a28) };
    static const juce::Colour borders[] { juce::Colour (0xff39455c), juce::Colour (0xff3a3a4a),
                                          juce::Colour (0xff3a5a44), juce::Colour (0xff5a3a5a) };
    for (auto& card : toolbarGroups)
    {
        auto r = card.area;
        if (r.isEmpty()) continue;
        auto rf = r.toFloat().reduced (1.0f, 2.0f);
        g.setColour (fills[(size_t) (card.colour & 3)]);
        g.fillRoundedRectangle (rf, 5.0f);
        g.setColour (borders[(size_t) (card.colour & 3)]);
        g.drawRoundedRectangle (rf, 5.0f, 1.0f);
    }
}

void MainComponent::resized()
{
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

    // transport
    groups.push_back ({ "transport", { F(&playBtn, 90), F(&stopBtn, 70), F(&loopBtn, 64),
                                        F(&seqBtn, 60), F(&revPlayBtn, 64), F(&pressBtn, 96),
                                        F(&scrubBtn, 72) } });

    // the pages and the files
    groups.push_back ({ "pages", { FL(&pageLab, 40, &pageBox, 150),
                                   F(&pageAddBtn, 30), F(&pageDupBtn, 46), F(&pageDelBtn, 30) } });
    groups.push_back ({ "file", { F(&menuBtn, 74), F(&settingsBtn, 78), F(&clearBtn, 64) } });

    // the tempo and the clock
    groups.push_back ({ "tempo", { FL(&tempoLab, 40, &tempoSlider, 140) } });
    groups.push_back ({ "clock", { F(&timeLab, 150) } });

    // the voice: what is drawn (the tool, the carrier wave, the envelope)
    {
        FlowGroup voice { "voice", {} };
        voice.items.push_back (FL (&toolLab, 36, &toolBox, 120));
        if (showWaveCtl)
            voice.items.push_back (FL (&waveLab, 42, &waveBox, 140));
        voice.items.push_back (FL (&envLab, 34, &envBox, 140));
        groups.push_back (voice);
    }
    // ... and how it sounds: the intensity, the duration and the master
    // gain are one theme, so they stay together in their own group.
    groups.push_back ({ "levels", { FL (&intenLab, 40, &intenSlider, 180),
                                    FL (&durLab, 30, &durSlider, 160),
                                    FL (&masterLab, 52, &masterSlider, 180) } });
    // the pitch: the snap, the discrete step (the arc's pitch moves
    // through a temperament) and the arc's frequency space (the UPIC's
    // four tables). The step belongs to the arc's voice, not to one
    // material, so it shows in every tab.
    groups.push_back ({ "pitch", { F(&snapBtn, 68), F(&stepBtn, 76),
                                   FL (&stepDivLab, 52, &stepDivSlider, 110),
                                   FL (&spaceLab, 44, &spaceBox, 170) } });

    // the material: the current tab's own controls
    {
        FlowGroup mat { "material", {} };
        if (showSampleCtl)
            mat.items = { FL(&slotLab, 34, &sampleBox, 180),
                          F(&loadSampleBtn, 110), F(&revBtn, 64),
                          FL(&refLab, 34, &refSlider, 210),
                          FL(&levelLab, 34, &levelSlider, 110),
                          FL(&samplePitchLab, 34, &samplePitchBox, 180),
                          FL(&grainLab, 40, &grainSlider, 100),
                          // M5: the material's own edits (the normalise, the
                          // fades and the reset; the trim is dragged in the
                          // material view)
                          F(&normMatBtn, 64), F(&fadeMatBtn, 64), F(&resetMatBtn, 64),
                          FL(&inGainLab, 30, &inGainSlider, 140), F(&monBtn, 64), F(&capBtn, 84),
                          F(&waveFromSlotBtn, 110) };
        else if (showLiveCtl)
            mat.items = { FL(&refLab, 34, &refSlider, 210),
                          FL(&livePitchLab, 42, &livePitchBox, 180),
                          FL(&liveGrainLab, 40, &liveGrainSlider, 120),
                          FL(&liveRevLab, 52, &liveRevLenBox, 90),
                          F(&liveRevBtn, 64) };
        else if (showPmCtl)
            mat.items = { FL(&pmOpsLab, 34, &pmOpsBox, 80),
                          FL(&pmPresetLab, 44, &pmPresetBox, 180),
                          FL(&pmRatioLab, 40, &pmRatioSlider, 150),
                          FL(&pmIndexLab, 40, &pmIndexSlider, 150),
                          FL(&pmIndexEnvLab, 46, &pmIndexEnvBox, 120),
                          FL(&pmFeedbackLab, 42, &pmFeedbackSlider, 130),
                          F(&pmImportBtn, 64), F(&pmExportBtn, 64) };
        else
            // v1.31 (U1): the Osc tab's material group carries the polyagogy
            // controls - what the arc's drawn wave does besides being the
            // timbre.
            mat.items = { FL(&modUseLab, 56, &modUseBox, 150),
                          FL(&modRateLab, 34, &modRateSlider, 120),
                          FL(&modDepthLab, 40, &modDepthSlider, 110) };
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
    groups.push_back ({ "magnets", { FL(&stickLab, 84, &stickSlider, 160),
                                     FL(&gridLab, 86, &gridBox, 84) } });

    // v1.33: the page's frequency settings - its ambitus, its
    // acceleration and its four frequency spaces (the UPIC's tables)
    groups.push_back ({ "page", { FL(&ambitusLab, 58, &ambitusBox, 170),
                                  FL(&warpLab, 40, &warpSlider, 120),
                                  FL(&spaceDefLab[0], 46, &spaceDefBox[0], 120),
                                  FL(&spaceDefLab[1], 46, &spaceDefBox[1], 120),
                                  FL(&spaceDefLab[2], 46, &spaceDefBox[2], 120),
                                  FL(&spaceDefLab[3], 46, &spaceDefBox[3], 120) } });

    // v1.33: the tools (procedural generators)
    groups.push_back ({ "tools", { FL(&genLab, 36, &genBox, 220) } });

    // the scrub's parameters and the arc dragging
    groups.push_back ({ "scrub", { FL(&scrubLenLab, 60, &scrubLenSlider, 130),
                                   FL(&scrubCrossLab, 70, &scrubCrossSlider, 130),
                                   FL(&scrubTailLab, 60, &scrubTailSlider, 130),
                                   FL(&scrubGrainLab, 70, &scrubGrainSlider, 110) } });
    groups.push_back ({ "drag", { F(&muteDragBtn, 90),
                                  FL(&dragSmoothLab, 70, &dragSmoothSlider, 130) } });

    // the status line: it takes the rest of its line
    groups.push_back ({ "status", { FX (&statusLab, 220) } });

    // the Hints view toggle owns the sidebar (below): the two explanation
    // labels are no longer part of the flow.
    if (pluginMode_) // host audio: no device selector button in the flow
        for (auto& grp : groups)
            grp.items.erase (std::remove_if (grp.items.begin(), grp.items.end(),
                                             [this] (const FlowItem& it){ return it.comp == &audioBtn; }),
                             grp.items.end());

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

    auto groupWidth = [&] (const FlowGroup& g) -> int
    {
        int w = 0;
        for (auto& it : g.items)
            w += it.flex ? it.minSlice : (it.labelW + it.w);
        return w;
    };

    auto flowGroups = [&] (juce::Rectangle<int> area, float sc, bool place,
                           std::vector<ToolbarCard>* rects) -> int
    {
        const int rh2 = (int) std::lround (rowH36 * sc);
        const int g2  = juce::jmax (1, (int) std::lround (2.0f * sc));
        const int gap = juce::jmax (1, (int) std::lround ((float) groupGapDp * sc));
        const int right = area.getRight();
        int x = area.getX(), y = area.getY();

        int groupColour = 0;
        for (auto& grp : groups)
        {
            const int gw = (int) std::lround ((float) groupWidth (grp) * sc);
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
                const int cw = (int) std::lround ((float) it.w * sc);
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
    // on a large screen. The "Hints" toggle shows and hides it.
    const int sidebarW = (showHints && getWidth() >= 820) ? 260 : 0;

    // fit-shrink: wrapping is scale-dependent, so settle by iteration
    float s = 1.0f;
    int topRowsTotal = 0;
    for (int iter = 0; ; ++iter)
    {
        auto area = b;
        if (sidebarW > 0) area.removeFromRight (sidebarW + 8);
        topRowsTotal = flowGroups (area, s, false, nullptr);
        const int minH = topRowsTotal * (int) std::lround (rowH36 * s) + 6
                       + (int) std::lround (minScoreH * s)
                       + (int) std::lround (26.0f * s) // Osc|Sample|Live tab strip
                       + (anyEditors ? (int) std::lround (minEdH * s) + 6 : 0)
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

        flowGroups (area, s, true, &toolbarGroups);

        // the two explanation labels live in the sidebar now
        const bool sidebarOn = sidebarW > 0;
        matHintLab.setVisible (sidebarOn);
        kbdHintLab.setVisible (sidebarOn);
        if (sidebarOn)
        {
            toolbarGroups.push_back ({ sidebar, (int) groups.size() });
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
    const int stripH = stripMode ? (int) std::lround(34.0f * s) : 0;
    // a single visible editor gets a little less height than two side by side
    const float wantEdHNow = (showWaveEd && showEnvEd) ? wantEdH : 240.0f;
    int edH = anyEditors ? (int) std::lround(wantEdHNow * s) : 0;
    const int budget = b.getHeight() - (int) std::lround(minScoreH * s) - 6 - stripH;
    if (anyEditors)
        edH = juce::jmin(edH, juce::jmax((int) std::lround(minEdH * s), budget));
    auto bottom = b.removeFromBottom(edH + stripH);

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

    auto placeEditor = [&](juce::Rectangle<int> col, juce::Component& editor,
                           juce::Component& box, juce::Component* const* btns,
                           const int* ideal, int n)
    {
        auto t = col.removeFromTop((int) std::lround(26.0f * s));
        int idealTotal = 0, idealSum = 0;
        for (int i = 0; i < n; ++i)
        {
            idealTotal += (int) std::lround(ideal[i] * s);
            idealSum   += ideal[i];
        }
        const int boxMin = (int) std::lround(100.0f * s);
        // dropdown stays compact so the preset buttons get the row's spare
        // width; on narrow layouts the box steps aside so the buttons stay
        // visible (never below 40dp)
        const int boxMax = (int) std::lround(150.0f * s);
        const bool fits = t.getWidth() - idealTotal >= boxMin;
        const int boxW = fits ? juce::jlimit(40, juce::jmax(40, t.getWidth() - n),
                                             juce::jlimit(boxMin, boxMax, t.getWidth() * 40 / 100))
                              : juce::jlimit(40, juce::jmax(40, t.getWidth() - n),
                                             juce::jmin(boxMin, t.getWidth() / 3));
        box.setBounds(t.removeFromLeft(boxW).reduced(2));
        // buttons share the row's spare width in their ideal proportions
        const int avail = t.getWidth();
        const int weights = fits ? juce::jmax(1, idealSum) : n;
        for (int i = 0; i < n; ++i)
        {
            const int rem = t.getWidth();
            const int w = fits ? juce::jlimit(1, rem, (int) std::lround((double) avail * (double) ideal[i] / (double) weights))
                               : juce::jmax(1, rem / (n - i));
            btns[i]->setBounds(t.removeFromLeft(w).reduced(1));
        }
        editor.setBounds(col.reduced(2));
    };

    juce::Component* wbtns[7] = { &wSine, &wSaw, &wSqr, &wTri, &wNoise, &wSmooth, &wNorm };
    const int wideal[] = { 30, 30, 30, 30, 44, 48, 34 };
    waveEd.setVisible(showWaveEd);
    waveEditBox.setVisible(showWaveEd);
    for (auto* w : wbtns) w->setVisible(showWaveEd);
    if (showWaveEd) placeEditor(waveCol, waveEd, waveEditBox, wbtns, wideal, 7);

    juce::Component* ebtns[5] = { &eFlat, &ePerc, &eSwell, &eGate, &eStr };
    const int eideal[] = { 40, 40, 44, 40, 34 };
    envEd.setVisible(showEnvEd);
    envEditBox.setVisible(showEnvEd);
    for (auto* eb : ebtns) eb->setVisible(showEnvEd);
    if (showEnvEd) placeEditor(envCol, envEd, envEditBox, ebtns, eideal, 5);
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
    {
        auto tabs = b.removeFromTop((int) std::lround(26.0f * s));
        matPmTog.setVisible (true);
        arcListBtn.setVisible (true);
        matOscTog.setBounds(tabs.removeFromLeft((int) std::lround(56.0f * s)).reduced(1));
        matSampleTog.setBounds(tabs.removeFromLeft((int) std::lround(76.0f * s)).reduced(1));
        matLiveTog.setBounds(tabs.removeFromLeft((int) std::lround(56.0f * s)).reduced(1));
        if (tabs.getWidth() >= (int) std::lround (40.0f * s)) matPmTog.setBounds (tabs.removeFromLeft ((int) std::lround (40.0f * s)).reduced (1));
        else matPmTog.setVisible (false);
        hintsTog.setBounds(tabs.removeFromRight((int) std::lround(56.0f * s)).reduced(1));
        envTog.setBounds(tabs.removeFromRight((int) std::lround(46.0f * s)).reduced(1));
        waveTog.setBounds(tabs.removeFromRight((int) std::lround(52.0f * s)).reduced(1));
        // v1.21: the arc list toggle sits with the other view toggles
        if (tabs.getWidth() >= (int) std::lround (52.0f * s)) arcListBtn.setBounds (tabs.removeFromRight ((int) std::lround (52.0f * s)).reduced (1));
        else arcListBtn.setVisible (false);
    }

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
