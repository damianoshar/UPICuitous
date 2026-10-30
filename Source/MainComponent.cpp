#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
MainComponent::MainComponent(bool pluginMode)
    : pluginMode_(pluginMode)
{
    waveforms = upic::defaultWaveforms();
    envelopes = upic::defaultEnvelopes();
    pages.push_back(upic::demoPage());
    for (int i = 0; i < upic::kMaxSampleSlots; ++i)
    {
        upic::SampleSlot s;
        s.id = i + 1;
        sampleSlots.push_back(std::move(s));
    }

    // v1.18 (wave 15): publish the first model snapshot before the audio
    // device starts, so the audio thread always has a model to render from
    // (it never takes dataLock any more).
    publishModelSnapshot();

    if (! pluginMode_)
        setAudioChannels(0, 2); // the VST3 wrapper gets its audio from the host
    startTimerHz(30);

    // --- transport buttons ---
    addAndMakeVisible(playBtn); addAndMakeVisible(stopBtn); addAndMakeVisible(loopBtn); addAndMakeVisible(menuBtn);
    addAndMakeVisible(seqBtn); addAndMakeVisible(pressBtn); addAndMakeVisible(scrubBtn); addAndMakeVisible(revPlayBtn);
    addAndMakeVisible(saveBtn); addAndMakeVisible(loadBtn); addAndMakeVisible(exportBtn);
    addAndMakeVisible(stemsBtn);
    addAndMakeVisible(audioBtn); addAndMakeVisible(clearBtn);
    if (pluginMode_) audioBtn.setVisible(false);
    saveBtn.setVisible (false); loadBtn.setVisible (false); exportBtn.setVisible (false); stemsBtn.setVisible (false); audioBtn.setVisible (false);
    loopBtn.setToggleState(true, juce::dontSendNotification);
    snapBtn.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(snapBtn);

    playBtn.onClick = [this]{ startPlayback(); };
    playBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2a6a3a));
    playBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8a4a));
    stopBtn.setTooltip("Stop; double-click to return to the start");
    stopBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff6a2a2a));
    // ToggleButtons have no button background in LookAndFeel_V4 - colour the
    // tick so each transport toggle still reads as a coloured control.
    loopBtn.setColour (juce::ToggleButton::tickColourId,    juce::Colour (0xff4a7ac8));
    seqBtn.setColour  (juce::ToggleButton::tickColourId,    juce::Colour (0xff4a7ac8));
    revPlayBtn.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffc07830));
    pressBtn.setColour (juce::ToggleButton::tickColourId,   juce::Colour (0xff9a5ac8));
    scrubBtn.setColour (juce::ToggleButton::tickColourId,   juce::Colour (0xff5a9ac8));
    snapBtn.setColour (juce::ToggleButton::tickColourId,    juce::Colour (0xff3a9a5a));
    stopBtn.onClick = [this]{
        // v1.27: a double-click (or double-tap) rewinds, a single click stops.
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const bool doubleClick = (nowMs - lastStopClickMs) <= 400.0;
        lastStopClickMs = nowMs;
        stopPlayback();
        if (doubleClick)
        {
            seekPlayhead(0.0);
            score.repaint();
        }
    };
    loopBtn.onClick = [this]{ loopPage = loopBtn.getToggleState(); };
    seqBtn.onClick = [this]{ seqPages = seqBtn.getToggleState(); };
    revPlayBtn.onClick = [this]{
        const bool rev = revPlayBtn.getToggleState();
        const double now = playRate.load(std::memory_order_relaxed);
        const double nxt = rev ? -1.0 : 1.0;
        if (nxt != now)
        {
            prevPlayRate.store(now, std::memory_order_relaxed);
            playRate.store(nxt, std::memory_order_relaxed);
            if (isPlaying.load(std::memory_order_relaxed))
                dirChangeSerial.fetch_add(1, std::memory_order_relaxed);
        }
    };
    pressBtn.onClick = [this]{
        recordPressure = pressBtn.getToggleState();
        score.setRecordPressure(recordPressure);
    };
    scrubBtn.onClick = [this]{ scrubAudition = scrubBtn.getToggleState(); };
    scrubBtn.setToggleState(scrubAudition, juce::dontSendNotification);

    for (auto* s : { &scrubLenSlider, &scrubCrossSlider, &scrubTailSlider, &scrubGrainSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        s->setNumDecimalPlacesToDisplay(0);
        s->setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(s);
    }
    for (auto* l : { &scrubLenLab, &scrubCrossLab, &scrubTailLab, &scrubGrainLab })
    {
        l->setJustificationType(juce::Justification::centredRight);
        l->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, juce::Colours::white);
        l->setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a32));
        addAndMakeVisible(l);
    }
    scrubLenSlider.setTextValueSuffix(" ms");
    scrubCrossSlider.setTextValueSuffix(" ms");
    scrubTailSlider.setTextValueSuffix(" ms");
    scrubLenSlider.setRange(20.0, 1000.0, 1.0);
    scrubLenSlider.setValue(scrubPreviewLen * 1000.0 / audioSampleRate, juce::dontSendNotification);
    scrubLenSlider.onValueChange = [this]{ scrubPreviewLen = juce::jmax(64, (int)(audioSampleRate * scrubLenSlider.getValue() / 1000.0)); };
    scrubCrossSlider.setRange(0.0, 1000.0, 1.0);
    scrubCrossSlider.setValue((double) scrubCrossMs, juce::dontSendNotification);
    scrubCrossSlider.onValueChange = [this]{ scrubCrossMs = (int) scrubCrossSlider.getValue(); };
    scrubTailSlider.setRange(0.0, 1000.0, 1.0);
    scrubTailSlider.setValue((double) scrubTailMs, juce::dontSendNotification);
    scrubTailSlider.onValueChange = [this]{ scrubTailMs = (int) scrubTailSlider.getValue(); };
    scrubGrainSlider.setTextValueSuffix(" ms");
    scrubGrainSlider.setRange(5.0, 500.0, 1.0);
    scrubGrainSlider.setValue((double) scrubGrainMs, juce::dontSendNotification);
    scrubGrainSlider.onValueChange = [this]{
        scrubGrainMs = (int) scrubGrainSlider.getValue();
        engine.setScrubGrainMs(scrubGrainMs);
        scrubEngine.setScrubGrainMs(scrubGrainMs);
    };
    engine.setScrubGrainMs(scrubGrainMs);
    scrubEngine.setScrubGrainMs(scrubGrainMs);

    grainLab.setJustificationType(juce::Justification::centredRight);
    grainLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    grainLab.setColour(juce::Label::textColourId, juce::Colours::white);
    grainLab.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(grainLab);
    grainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    grainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    grainSlider.setNumDecimalPlacesToDisplay(0);
    grainSlider.setTextValueSuffix(" ms");
    grainSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    grainSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    grainSlider.setRange(5.0, 500.0, 1.0);
    grainSlider.setValue((double) sampleGrainMs, juce::dontSendNotification);
    grainSlider.onValueChange = [this]{
        sampleGrainMs = (int) grainSlider.getValue();
        engine.setSampleGrainMs(sampleGrainMs);
        scrubEngine.setSampleGrainMs(sampleGrainMs);
    };
    addAndMakeVisible(grainSlider);
    engine.setSampleGrainMs(sampleGrainMs);
    scrubEngine.setSampleGrainMs(sampleGrainMs);
    muteDragBtn.setClickingTogglesState(true);
    muteDragBtn.setToggleState(muteWhileDragging.load(), juce::dontSendNotification);
    muteDragBtn.onClick = [this]{ muteWhileDragging.store(muteDragBtn.getToggleState(), std::memory_order_relaxed); };
    muteDragBtn.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff9a5ac8));
    addAndMakeVisible(muteDragBtn);
    dragSmoothLab.setJustificationType(juce::Justification::centredRight);
    dragSmoothLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    dragSmoothLab.setColour(juce::Label::textColourId, juce::Colours::white);
    dragSmoothLab.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(dragSmoothLab);
    dragSmoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dragSmoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    dragSmoothSlider.setNumDecimalPlacesToDisplay(0);
    dragSmoothSlider.setTextValueSuffix(" ms");
    dragSmoothSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    dragSmoothSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    dragSmoothSlider.setRange(0.0, 1000.0, 1.0);
    dragSmoothSlider.setValue((double) dragSmoothMs.load(), juce::dontSendNotification);
    dragSmoothSlider.onValueChange = [this]{ dragSmoothMs.store((int) dragSmoothSlider.getValue(), std::memory_order_relaxed); };
    addAndMakeVisible(dragSmoothSlider);
    stemsBtn.onClick = [this]{ exportStems(); };
    snapBtn.onClick = [this]{ score.setSnap(snapBtn.getToggleState()); };

    // v1.7: view toggles β€” hide the waveform/envelope editors at the bottom
    // and the instruction hints, so the canvas gains the space. View state
    // only: nothing here changes the piece.
    for (auto* t : { &waveTog, &envTog, &hintsTog })
    {
        t->setClickingTogglesState(true);
        t->setToggleState(true, juce::dontSendNotification);
        t->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4a6a));
        addAndMakeVisible(t);
    }
    waveTog.setTooltip("Show/hide the waveform editor");
    envTog.setTooltip("Show/hide the envelope editor");
    hintsTog.setTooltip("Show/hide the instruction hints");
    waveTog.onClick  = [this]{ showWaveEd = waveTog.getToggleState();  resized(); };
    envTog.onClick   = [this]{ showEnvEd  = envTog.getToggleState();  resized(); };
    hintsTog.onClick = [this]{ showHints  = hintsTog.getToggleState();
                               score.setShowHints(showHints); resized(); };

    // v1.7 audio settings as an in-app overlay (Android has no closable
    // dialog windows). The selector is created on first use; OK or a tap
    // on the backdrop dismisses it.
    audioOverlay.setName("audioOverlay");
    audioOverlay.setVisible(false);
    audioOverlay.onDismiss = [this]{ closeAudioSettings(); };
    addChildComponent(audioOverlay); // stays hidden until the Audio button opens it
    audioOkBtn.onClick = [this]{ closeAudioSettings(); };
    audioOverlay.addAndMakeVisible(audioOkBtn);

    // v1.27: Clear is destructive, so it asks first (in-app, so it also works
    // on Android). The confirm button is red, like the Clear button itself.
    clearOverlay.setName("clearOverlay");
    clearOverlay.setWantsKeyboardFocus(true);
    clearOverlay.setVisible(false);
    clearOverlay.onCancel = [this]{ closeClearWarning(); };
    addChildComponent(clearOverlay);
    clearConfirmBtn.setName("clearConfirmBtn");
    clearCancelBtn.setName("clearCancelBtn");
    clearConfirmBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff8a2a2a));
    clearConfirmBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffaa3a3a));
    clearCancelBtn.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff3a3a46));
    clearConfirmBtn.onClick = [this]{ clearCurrentPage(); closeClearWarning(); };
    clearCancelBtn.onClick  = [this]{ closeClearWarning(); };
    clearOverlay.addAndMakeVisible(clearConfirmBtn);
    clearOverlay.addAndMakeVisible(clearCancelBtn);

    // v1.9: the live pitch algorithm is selectable - the v1.5 granular
    // shifter stays the default. Tape = varispeed with crossfaded re-anchors
    // (low latency, keeps transients), vocoder = FFT phase vocoder (clean on
    // sustained material). Session-only, like the rest of the audio settings.
    livePitchLab.setName("livePitchLab");
    livePitchBox.setName("livePitchBox");
    livePitchBox.addItem("Granular (smooth)", 1);
    livePitchBox.addItem("Tape (varispeed)", 2);
    livePitchBox.addItem("Phase vocoder", 3);
    livePitchBox.setSelectedId(1, juce::dontSendNotification);
    livePitchBox.setTooltip("Live input pitch shift for LIVE-tab arcs.\n"
                            "Granular and Phase vocoder cover the full 1/16x - 16x range "
                            "(like the Sample tab); Tape (varispeed) reads the mic faster "
                            "than it arrives, so it keeps an octave up/down.");
    livePitchBox.onChange = [this]{
        engine.setLivePitchMode(livePitchBox.getSelectedId() - 1);
        markModelChanged(); // v1.15: the live voices re-anchor -> fade across it
        flash("Live pitch: " + livePitchBox.getText());
    };
    // v1.21: the live pitch algorithm moved out of the audio settings
    // overlay into the material row (the tab IS the material - the overlay
    // is for the device). The label matches the Sample tab's "Pitch": the
    // tab says which material it is.
    livePitchLab.setText("Pitch", juce::dontSendNotification);
    addAndMakeVisible(livePitchLab);
    addAndMakeVisible(livePitchBox);

    // v1.20 (wave 18): the live reverse - the microphone's last N seconds
    // played backwards. N is the buffer duration the user picks; the effect
    // is a reverse delay: every N-second segment of the input is captured
    // and then played back mirrored (the pitch is the arc's, as usual).
    // The controls live in the material row (the tab IS the material), not
    // in the audio settings overlay - that one is for the device.
    liveRevLab.setName("liveRevLab");
    liveRevLenBox.setName("liveRevLenBox");
    liveRevLenBox.addItem("0.5 s", 1);
    liveRevLenBox.addItem("1 s", 2);
    liveRevLenBox.addItem("2 s", 3);
    liveRevLenBox.setSelectedId(2, juce::dontSendNotification);
    liveRevLenBox.setTooltip("How much of the microphone input the reverse plays back.\n"
                              "The longer the buffer, the longer the delay.");
    liveRevLenBox.onChange = [this]{
        revBufSec = liveRevLenBox.getSelectedId() == 1 ? 0.5
                    : liveRevLenBox.getSelectedId() == 3 ? 2.0 : 1.0;
        updateRevBuffers();
    };
    liveRevBtn.setName("liveRevBtn");
    liveRevBtn.setClickingTogglesState(true);
    liveRevBtn.setTooltip("Play the microphone's last seconds backwards");
    liveRevBtn.onClick = [this]{
        const bool on = liveRevBtn.getToggleState();
        revLiveOn.store(on, std::memory_order_relaxed);
        {
            // v1.21: the global Rev is a bulk edit - it sets the direction on
            // every live arc (the arc list's Rev box is the per-arc control)
            juce::ScopedLock l(dataLock);
            for (auto& pg : pages)
                for (auto& a : pg.arcs)
                    if (a.mode == 2) a.reversed = on;
            markModelChanged();
            publishModelSnapshot();
            arcListDirty = true;
        }
        if (on)
        {
            // start a fresh capture, so the first segment is not a mix of the
            // old content and the new input
            revCapIdx = 0; revPlayIdx = 1;
            revCapHead = 0; revCapCount = 0; revPlayHead = 0;
            updateRevBuffers();
        }
        markModelChanged(); // the live voices re-anchor -> fade across it
        flash(on ? "Live reverse: " + juce::String (revBufSec, 1) + " s buffer"
                 : "Live reverse off");
    };
    addAndMakeVisible(livePitchLab);
    addAndMakeVisible(livePitchBox);
    addAndMakeVisible(liveRevLab);
    addAndMakeVisible(liveRevLenBox);
    addAndMakeVisible(liveRevBtn);

    saveBtn.onClick = [this]{ saveFile(); };
    loadBtn.onClick = [this]{ loadFile(); };
    exportBtn.onClick = [this]{ renderWav(); };
    audioBtn.onClick = [this]{ showAudioSettings(); };
    menuBtn.onClick = [this]{
        juce::PopupMenu m;
        m.addItem (1, "Save...");
        m.addItem (2, "Open...");
        m.addSeparator();
        m.addItem (3, "Render WAV...");
        m.addItem (4, "Render stems...");
        m.addSeparator();
        m.addItem (5, "Audio settings...");
        m.addItem (6, "Clear page...");
        juce::Component::SafePointer<MainComponent> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn),
            [sp] (int r) {
                if (sp == nullptr || r == 0) return;
                switch (r)
                {
                    case 1: sp->saveFile(); break;
                    case 2: sp->loadFile(); break;
                    case 3: sp->renderWav(); break;
                    case 4: sp->exportStems(); break;
                    case 5: sp->showAudioSettings(); break;
                    case 6: sp->showClearWarning(); break;
                }
            });
    };
    clearBtn.setTooltip("Clear every arc on the current page (asks first)");
    clearBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff7a2a2a));
    clearBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9a3a3a));
    clearBtn.onClick = [this]{ showClearWarning(); };

    // --- combos ---
    addAndMakeVisible(pageBox); addAndMakeVisible(toolBox);
    addAndMakeVisible(waveBox); addAndMakeVisible(envBox);
    addAndMakeVisible(waveEditBox); addAndMakeVisible(envEditBox);
    addAndMakeVisible(pageAddBtn); addAndMakeVisible(pageDupBtn); addAndMakeVisible(pageDelBtn);

    toolBox.addItem("Draw", 1); toolBox.addItem("Line", 2); toolBox.addItem("Sine", 3);
    toolBox.addItem("Select/Move", 4); toolBox.addItem("Erase", 5);
    toolBox.addItem("Sieve zone", 6);
    toolBox.setSelectedId(1, juce::dontSendNotification);
    toolBox.onChange = [this]{
        score.setTool((ScoreEditor::Tool)(toolBox.getSelectedId() - 1));
    };

    pageAddBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        upic::Page p; p.durationSec = curPage().durationSec;
        pages.insert(pages.begin() + currentPage + 1, p);
        currentPage++;
        seekPlayhead(0.0);
        bumpPageEpoch();
        publishModelSnapshot(); // the new page must be audible right away
        refreshBoxes(); refreshScoreBinding();
    };
    pageDupBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        pages.insert(pages.begin() + currentPage + 1, curPage());
        currentPage++;
        seekPlayhead(0.0);
        bumpPageEpoch();
        publishModelSnapshot();
        refreshBoxes(); refreshScoreBinding();
    };
    pageDelBtn.onClick = [this]{
        if (pages.size() <= 1) return;
        juce::ScopedLock l(dataLock);
        pages.erase(pages.begin() + currentPage);
        currentPage = juce::jlimit(0, (int)pages.size() - 1, currentPage);
        seekPlayhead(0.0);
        bumpPageEpoch();
        publishModelSnapshot();
        refreshBoxes(); refreshScoreBinding();
    };
    pageBox.onChange = [this]{
        int id = pageBox.getSelectedId();
        juce::ScopedLock l(dataLock);
        if (id >= 1 && id <= (int)pages.size())
        {
            currentPage = id - 1;
            tempoSlider.setValue (pages[(size_t) currentPage].tempoBpm, juce::dontSendNotification);
            seekPlayhead(0.0);
            bumpPageEpoch();
            markModelChanged(); // v1.15: a page change replaces the sound -> fade across it
            publishModelSnapshot(); // the new page must be audible right away
            refreshScoreBinding();
        }
    };

    // v1.26: the toolbar is the current voice AND a live editor for the
    // selected arcs. Wave / env / intensity / Ref changes apply to every
    // selected arc, so the toolbar no longer silently edits only new strokes.
    auto applyWaveToSelection = [this]{
        pushVoice();
        const auto ids = score.getSelectedArcs();
        const int idx = waveBox.getSelectedId() - 1;
        if (ids.empty() || idx < 0 || idx >= (int) waveforms.size()) return;
        const int wid = waveforms[(size_t) idx].id;
        {
            juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
                    a.waveformId = wid;
        }
        publishModelSnapshot();
        score.repaint();
    };
    auto applyEnvToSelection = [this]{
        pushVoice();
        const auto ids = score.getSelectedArcs();
        const int idx = envBox.getSelectedId() - 1;
        if (ids.empty() || idx < 0 || idx >= (int) envelopes.size()) return;
        const int eid = envelopes[(size_t) idx].id;
        {
            juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
                    a.envelopeId = eid;
        }
        publishModelSnapshot();
        score.repaint();
    };
    waveBox.onChange = applyWaveToSelection;
    envBox.onChange = applyEnvToSelection;
    pmIndexEnvBox.onChange = [this]{
        const int id = pmIndexEnvBox.getSelectedId();
        const int envId = (id >= 2 && id - 2 < (int) envelopes.size())
                              ? envelopes[(size_t)(id - 2)].id : 0;
        const auto ids = score.getSelectedArcs();
        if (! ids.empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                    a.pmIndexEnv = envId;
            publishModelSnapshot();
        }
        pushVoice();
    };

    waveEditBox.onChange = [this]{
        int idx = waveEditBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int)waveforms.size()) { selWave = waveforms[(size_t)idx].id; waveEd.bind(&waveforms, &selWave); waveBox.setSelectedId(idx + 1, juce::dontSendNotification); pushVoice(); }
    };
    envEditBox.onChange = [this]{
        int idx = envEditBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int)envelopes.size()) { selEnv = envelopes[(size_t)idx].id; envEd.bind(&envelopes, &selEnv); envBox.setSelectedId(idx + 1, juce::dontSendNotification); pushVoice(); }
    };

    // --- sliders ---
    addAndMakeVisible(durSlider); addAndMakeVisible(masterSlider); addAndMakeVisible(intenSlider);
    addAndMakeVisible(pageLab); addAndMakeVisible(durLab); addAndMakeVisible(masterLab); addAndMakeVisible(intenLab);
    addAndMakeVisible(timeLab); addAndMakeVisible(statusLab);

    durSlider.setRange(5.0, 120.0, 0.5); durSlider.setValue(30.0);
    durSlider.onValueChange = [this]{
        juce::ScopedLock l(dataLock);
        curPage().durationSec = durSlider.getValue();
        // v1.18: the audio thread owns the playhead while playing, so clamp
        // the value the UI shows and ask the audio thread to move with it
        if (displayPlayhead() > curPage().durationSec)
            seekPlayhead(curPage().durationSec);
        // same page pointer: just repaint (a full rebind would drop the selection)
        score.repaint();
    };
    masterSlider.setRange(0.0, 1.5, 0.01); masterSlider.setValue(0.8);
    masterSlider.onValueChange = [this]{ masterGain.store((float) masterSlider.getValue()); };
    intenSlider.setRange(0.05, 1.0, 0.01); intenSlider.setValue(0.8);
    intenSlider.onValueChange = [this]{
        drawIntensity = (float) intenSlider.getValue();
        pushVoice();
        const auto ids = score.getSelectedArcs();
        if (ids.empty()) return;
        {
            juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
                    a.intensity = drawIntensity;
        }
        publishModelSnapshot();
        score.repaint();
    };
    // all numeric sliders share the same right-aligned value box, so the rows
    // read as one column instead of mixing left and right text boxes
    durSlider.setTextBoxStyle    (juce::Slider::TextBoxRight, false, 72, 18);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    intenSlider.setTextBoxStyle  (juce::Slider::TextBoxRight, false, 72, 18);
    // v1.25: brighter tracks and thumbs - the dark scheme put the unfilled
    // track almost on top of the toolbar card.
    auto styleSlider = [](juce::Slider& s)
    {
        s.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
        s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
        s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
        s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
    };
    for (auto* s : { &durSlider, &masterSlider, &intenSlider, &refSlider, &tempoSlider,
                     &pmRatioSlider, &pmIndexSlider, &pmFeedbackSlider, &stickSlider,
                     &inGainSlider })
        styleSlider (*s);

    // --- material controls: oscillator vs realtime sample pitch-shift ---
    addAndMakeVisible(sampleBox);
    addAndMakeVisible(loadSampleBtn); addAndMakeVisible(refSlider); addAndMakeVisible(tempoSlider); addAndMakeVisible(tempoLab); addAndMakeVisible(pmRatioSlider); addAndMakeVisible(pmIndexSlider);
    addAndMakeVisible(samplePitchBox); addAndMakeVisible(samplePitchLab);
    addAndMakeVisible(monBtn); addAndMakeVisible(capBtn);
    addAndMakeVisible(inGainSlider); addAndMakeVisible(inGainLab);
    addAndMakeVisible(slotLab); addAndMakeVisible(refLab); addAndMakeVisible(matHintLab);
    addAndMakeVisible (pmRatioLab); addAndMakeVisible (pmIndexLab);
    addAndMakeVisible (pmIndexEnvLab); addAndMakeVisible (pmIndexEnvBox);
    addAndMakeVisible (pmOpsLab); addAndMakeVisible (pmOpsBox);
    addAndMakeVisible (pmPresetLab); addAndMakeVisible (pmPresetBox);
    addAndMakeVisible (pmFeedbackLab); addAndMakeVisible (pmFeedbackSlider);
    addAndMakeVisible (pmImportBtn); addAndMakeVisible (pmExportBtn);

    // --- sieve quantizer (with the preset row) ---
    addAndMakeVisible(sieveBtn); addAndMakeVisible(sieveMEdit); addAndMakeVisible(sieveREdit);
    sieveMEdit.setName("sieveMEdit");
    sieveREdit.setName("sieveREdit");
    sieveMEdit.setTextToShowWhenEmpty("m=12", juce::Colour(0xff6a6a76));
    sieveREdit.setTextToShowWhenEmpty("0,7", juce::Colour(0xff6a6a76));
    sieveMEdit.setFont(juce::Font(juce::FontOptions(12.0f)));
    sieveREdit.setFont(juce::Font(juce::FontOptions(12.0f)));
    sieveMEdit.setText("12", juce::dontSendNotification);
    sieveREdit.setText("0", juce::dontSendNotification);
    sieveMEdit.onTextChange = [this]{ pushSieve(); };
    sieveREdit.onTextChange = [this]{ pushSieve(); };
    sieveBtn.onClick = [this]{ pushSieve(); };

    // --- v1.2 magnetic-sieve row: preset library, stickiness, time grid,
    //     procedural arc generators + persistent shortcut/wheel hint ---
    addAndMakeVisible(sievePresetBox); addAndMakeVisible(stickSlider);
    addAndMakeVisible(gridBox); addAndMakeVisible(genBox);
    addAndMakeVisible(stickLab); addAndMakeVisible(gridLab); addAndMakeVisible(genLab);
    addAndMakeVisible(kbdHintLab);
    sievePresetBox.setName("sievePresetBox");
    stickSlider.setName("stickSlider");
    gridBox.setName("gridBox");
    genBox.setName("genBox");
    kbdHintLab.setName("kbdHintLab");

    sievePresetBox.setTextWhenNothingSelected("Sieve preset...");
    {
        auto presets = upic::sievePresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            sievePresetBox.addItem(presets[(size_t) i].name, i + 1);
    }
    sievePresetBox.onChange = [this]{
        const int idx = sievePresetBox.getSelectedId() - 1;
        const auto presets = upic::sievePresets();
        if (idx < 0 || idx >= (int) presets.size()) return;
        const auto& pr = presets[(size_t) idx];
        sieveMEdit.setText(juce::String(pr.m), juce::dontSendNotification);
        juce::String rt;
        for (size_t i = 0; i < pr.remainders.size(); ++i)
            rt += (i > 0 ? "," : "") + juce::String(pr.remainders[i]);
        sieveREdit.setText(rt, juce::dontSendNotification);
        sieveBtn.setToggleState(true, juce::dontSendNotification);
        pushSieve();
        flash("Sieve preset: " + pr.name);
        sievePresetBox.setSelectedId(0, juce::dontSendNotification); // back to placeholder
    };

    stickSlider.setRange(0.0, 1.0, 0.05);
    stickSlider.setValue(1.0, juce::dontSendNotification);
    stickSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 18);
    stickSlider.onValueChange = [this]{
        stickiness = stickSlider.getValue();
        score.setStickiness(stickiness);
    };

    gridBox.setTextWhenNothingSelected("Time grid");
    gridBox.addItem("Off", 1);
    gridBox.addItem("0.5/s", 2); gridBox.addItem("1/s", 3); gridBox.addItem("2/s", 4);
    gridBox.addItem("4/s", 5);   gridBox.addItem("8/s", 6); gridBox.addItem("16/s", 7);
    gridBox.setSelectedId(1, juce::dontSendNotification);
    gridBox.onChange = [this]{
        static const double divs[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        const int i = juce::jlimit(0, 6, gridBox.getSelectedId() - 1);
        timeGrid = divs[i];
        score.setTimeGrid(timeGrid);
    };

    genBox.setTextWhenNothingSelected("Procedural arc gen...");
    genBox.addItem("Stochastic arborescence", 1);
    genBox.addItem("Convergent arborescence", 2);
    genBox.addItem("Echelon (Bourbaki)", 3);
    genBox.addItem("Ordinal spine + branches", 4);
    genBox.onChange = [this]{
        const int kind = genBox.getSelectedId() - 1;
        if (kind >= 0) runGenerator(kind);
        genBox.setSelectedId(0, juce::dontSendNotification);
    };

    // NOTE: keep UI string literals ASCII-only: juce::String(const char*) is an
    // ASCII conversion, and non-ASCII bytes trip a Debug-build assertion
    // (juce_String.cpp) and render as mojibake in Release.
    kbdHintLab.setText("Wheel: pan pitch  |  Alt+wheel: zoom pitch  |  Ctrl+wheel: zoom time  |  Shift+wheel: pan time"
                       "  |  Arrows: nudge selection (Shift: grid step / octave)  |  Ctrl+A: select all  |  Ctrl+D: duplicate"
                       "  |  Del: delete  |  R-click: arc menu  |  centre line: scrub playhead"
                       + juce::String(juce::Desktop::getInstance().getMainMouseSource().isTouch()
                            ? "   ||   Touch: 1 finger draws  |  pinch / 2-finger drag: zoom / pan  |  long-press: menu"
                            : ""),
                       juce::dontSendNotification);
    kbdHintLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    kbdHintLab.setJustificationType(juce::Justification::centredRight);
    kbdHintLab.setColour(juce::Label::textColourId, juce::Colour(0xff7a7a86));

    // v1.21 (wave 19): the arc list panel - the score's inventory on the
    // right of the display, with the per-arc controls. It is a view (the
    // visibility is session state), like the Wave / Env / Hints toggles.
    arcListBtn.setName("arcListBtn");
    arcListBtn.setClickingTogglesState(true);
    arcListBtn.setTooltip("Show the list of arcs (page, pitch range, time span)");
    arcListBtn.onClick = [this]{
        showArcList = arcListBtn.getToggleState();
        arcListDirty = true;
        resized();
    };
    addAndMakeVisible(arcListBtn);
    addAndMakeVisible(arcListPanel);
    arcListPanel.setVisible(false);

    // clicking a row selects the arc on the canvas. Ctrl/Cmd/Shift-click
    // toggles it in a multi-selection; a plain click selects just that arc.
    arcListPanel.onArcClicked = [this](int arcId, int page, bool additive){
        int arcMode = -1;
        bool pageChanged = false;
        {
            juce::ScopedLock l(dataLock);
            if (page < 0 || page >= (int) pages.size()) return;
            if (page != currentPage)
            {
                currentPage = page;
                pageChanged = true;
            }
            for (const auto& a : pages[(size_t) page].arcs)
                if (a.id == arcId) { arcMode = a.mode; break; }
        }
        if (arcMode < 0) return;

        // the arc has to be in the current layer to be selected, so the tab
        // follows the arc's material (this also updates the canvas layer)
        if (arcMode != drawMatMode)
            setMaterial(arcMode);
        if (pageChanged)
        {
            refreshBoxes();
            refreshScoreBinding();
        }

        std::vector<int> ids;
        if (additive)
            ids = score.getSelectedArcs();
        const auto it = std::find(ids.begin(), ids.end(), arcId);
        if (it != ids.end()) ids.erase(it);
        else ids.push_back(arcId);
        score.setSelectedArcs(ids);

        selectedArcId = score.getSelectedArc();
        lastArcSelection = score.getSelectedArcs();
        arcListPanel.setSelectedArcs(page, lastArcSelection);
        if (selectedArcId >= 0) syncControlsToArc(selectedArcId);
        updateStatus();
    };
    // the Rev box: a live arc plays the microphone backwards
    arcListPanel.onReverseToggled = [this](int page, int arcId, bool reversed){
        juce::ScopedLock l(dataLock);
        if (page < 0 || page >= (int) pages.size()) return;
        for (auto& a : pages[(size_t) page].arcs)
            if (a.id == arcId) a.reversed = reversed;
        markModelChanged();
        publishModelSnapshot();
        arcListDirty = true;
    };
    // the mode dropdown: this arc's algorithm (-1 = follow the tab)
    arcListPanel.onModeChosen = [this](int page, int arcId, int mode){
        juce::ScopedLock l(dataLock);
        if (page < 0 || page >= (int) pages.size()) return;
        for (auto& a : pages[(size_t) page].arcs)
            if (a.id == arcId) a.pitchMode = mode;
        markModelChanged();
        publishModelSnapshot();
        arcListDirty = true;
    };
    arcListPanel.onPageClicked = [this](int page){
        juce::ScopedLock l(dataLock);
        if (page >= 0 && page < (int) pages.size())
        {
            currentPage = page;
            refreshBoxes();
            refreshScoreBinding();
        }
    };

    // v1.23 (wave 20): the MIDI input foundation. The device list, the
    // activity monitor and the learn live in the audio settings overlay
    // (it is where the device lives).
    masterSlider.setName("masterSlider");
    intenSlider.setName("intenSlider");
    midiActLab.setName("midiActLab");
    midiBindGainLab.setName("midiBindGainLab");
    midiDevBox.setName("midiDevBox");
    midiDevBox.setTooltip ("The MIDI input the app listens to");
    midiDevBox.onChange = [this]
    {
        const int id = midiDevBox.getSelectedId();
        const bool ok = midi.open (id <= 1 ? juce::String() : midiDevBox.getItemText (id - 1));
        if (! ok)
        {
            flash ("Could not open the MIDI input");
            midiDevBox.setSelectedId (1, juce::dontSendNotification);
            midi.close();
        }
        else
        {
            flash (midi.isOpen() ? "MIDI in: " + midi.openDeviceName() : "MIDI in: none");
        }
        updateMidiLabels();
    };
    midiLearnGain.onClick = [this]
    {
        if (midi.isLearning() && midi.learningTarget() == MidiHub::Target::masterGain)
            midi.cancelLearn();
        else
            midi.startLearn (MidiHub::Target::masterGain);
        updateMidiLabels();
    };
    midiLearnTransport.onClick = [this]
    {
        if (midi.isLearning() && midi.learningTarget() == MidiHub::Target::transport)
            midi.cancelLearn();
        else
            midi.startLearn (MidiHub::Target::transport);
        updateMidiLabels();
    };
    audioOverlay.addAndMakeVisible (midiDevBox);
    audioOverlay.addAndMakeVisible (midiDevLab);
    audioOverlay.addAndMakeVisible (midiGainLab);
    audioOverlay.addAndMakeVisible (midiIntLab);
    audioOverlay.addAndMakeVisible (midiActLab);
    audioOverlay.addAndMakeVisible (midiLearnGain);
    audioOverlay.addAndMakeVisible (midiLearnInt);
    audioOverlay.addAndMakeVisible (midiBindGainLab);
    audioOverlay.addAndMakeVisible (midiBindIntLab);
    audioOverlay.addAndMakeVisible (midiScrubLab);
    audioOverlay.addAndMakeVisible (midiLearnScrub);
    audioOverlay.addAndMakeVisible (midiBindScrubLab);
    audioOverlay.addAndMakeVisible (midiTransportLab);
    audioOverlay.addAndMakeVisible (midiLearnTransport);
    audioOverlay.addAndMakeVisible (midiBindTransportLab);
    refreshMidiDevices();
    updateMidiLabels();

    // v1.8 the one tab strip above the canvas: each material is its own tab
    // and canvas layer. The tab is the tab of the page too - the tabs share
    // the timeline (zoom/scroll/playhead); Live labels the grid in Hz.
    for (auto* t : { &matOscTog, &matSampleTog, &matLiveTog, &matPmTog })
    {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(0x7402);
        t->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4a6a));
        addAndMakeVisible(t);
    }
    matLiveTog.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6a4a2a));
    matPmTog.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6a2a6a));
    matOscTog.setTooltip("Osc tab: oscillator arcs (wavetables)");
    matSampleTog.setTooltip("Sample tab: sample arcs - Slot, Load sample, In/Mon/Capture above");
    matLiveTog.setTooltip("Live tab: arcs transpose the mic input in realtime");
    matPmTog.setTooltip ("PM tab: 2-op phase-modulation arcs");
    matOscTog.setToggleState(true, juce::dontSendNotification);
    matOscTog.onClick    = [this]{ setMaterial(0); };
    matSampleTog.onClick = [this]{ setMaterial(1); };
    matLiveTog.onClick   = [this]{ setMaterial(2); };
    matPmTog.onClick    = [this]{ setMaterial(3); };
    refSlider.setName("refSlider");
    refSlider.setNormalisableRange(juce::NormalisableRange<double>(20.0, 4000.0, 0.01, 0.4));
    refSlider.setValue(upic::kDefaultRefFreq, juce::dontSendNotification);
    refSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 18);
    refSlider.setTextValueSuffix(" Hz");
    refSlider.setTooltip("Reference pitch: the arc's 1x pitch and the grid's 1x.\n"
                         "Dragging it retunes the selected arc(s) and sets the "
                         "reference for new strokes.");
    refSlider.onValueChange = [this]{
        drawRef = refSlider.getValue();
        const auto ids = score.getSelectedArcs();
        bool changed = false;
        if (! ids.empty())
        {
            juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
                {
                    a.refFreq = drawRef;
                    changed = true;
                }
        }
        if (changed)
        {
            publishModelSnapshot();
            score.repaint();
        }
        pushVoice();
    };
    tempoSlider.setNormalisableRange (juce::NormalisableRange<double> (40.0, 240.0, 1.0, 0.5));
    tempoSlider.setValue (120.0, juce::dontSendNotification);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    tempoSlider.onValueChange = [this]{ const juce::ScopedLock sl (dataLock); curPage().tempoBpm = (double) tempoSlider.getValue(); refreshBoxes(); };
    pmRatioSlider.setName ("pmRatioSlider");
    pmRatioSlider.setNormalisableRange (juce::NormalisableRange<double> (0.1, 32.0, 0.01, 0.4));
    pmRatioSlider.setValue (2.0, juce::dontSendNotification);
    pmRatioSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    pmIndexSlider.setName ("pmIndexSlider");
    pmIndexSlider.setNormalisableRange (juce::NormalisableRange<double> (0.0, 20.0, 0.01, 0.4));
    pmIndexSlider.setValue (1.5, juce::dontSendNotification);
    pmIndexSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    pmFeedbackSlider.setNormalisableRange (juce::NormalisableRange<double> (0.0, 1.0, 0.001, 0.4));
    pmFeedbackSlider.setValue (0.0, juce::dontSendNotification);
    pmFeedbackSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    pmFeedbackSlider.onValueChange = [this]{
        if (! score.getSelectedArcs().empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (score.getSelectedArcs().begin(), score.getSelectedArcs().end(), a.id) != score.getSelectedArcs().end())
                    a.pmFeedback = pmFeedbackSlider.getValue();
            publishModelSnapshot();
        }
        pushVoice();
    };
    pmPresetBox.addItem ("(manual)", 1);
    pmPresetBox.addItem ("Bell (2.0x, percussive)", 2);
    pmPresetBox.addItem ("Brass (1.0x, flat)", 3);
    pmPresetBox.addItem ("Pluck (3.0x, percussive)", 4);
    pmPresetBox.addItem ("Organ (1.0x, sustain, fb)", 5);
    pmPresetBox.addItem ("Metallic (1.414x, perc, fb)", 6);
    pmPresetBox.setSelectedId (1, juce::dontSendNotification);
    pmOpsBox.addItem ("2-op", 1); pmOpsBox.addItem ("4-op", 2); pmOpsBox.setSelectedId (1, juce::dontSendNotification);
    // names used by the selftest and UI automation
    waveBox.setName ("waveBox");
    sampleBox.setName ("sampleBox");
    pmOpsBox.setName ("pmOpsBox");
    pmPresetBox.setName ("pmPresetBox");
    pmFeedbackSlider.setName ("pmFeedbackSlider");
    pmOpsBox.onChange = [this]{
        const int ops = (pmOpsBox.getSelectedId() == 2) ? 4 : 2;
        const auto ids = score.getSelectedArcs();
        if (! ids.empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (ids.begin(), ids.end(), a.id) != ids.end() && a.mode == 3)
                    a.pmOps = ops;
            publishModelSnapshot();
        }
        pushVoice();
    };
    pmImportBtn.onClick = [this]{
        juce::FileChooser fc ("Import PM preset", juce::File(), "*.preset");
        fc.launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& c) {
            auto f = c.getResult();
            if (f == juce::File()) return;
            std::ifstream in (f.getFullPathName().toStdString());
            if (! in) { flash ("Could not read preset"); return; }
            std::string s ((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto getField = [&] (const std::string& key) -> std::string {
                auto pos = s.find (key + "=");
                if (pos == std::string::npos) return "";
                auto end = s.find ("\n", pos);
                return s.substr (pos + key.size() + 1, (end == std::string::npos ? s.size() : end) - (pos + key.size() + 1));
            };
            auto parseFloat = [&] (const std::string& k, double d) { try { return k.empty() ? d : std::stod (k); } catch (...) { return d; } };
            std::string op1ratio, op1level, op1fb;
            {
                auto pos = s.find ("[op1]");
                if (pos != std::string::npos) {
                    auto next = s.find ("[", pos + 1);
                    auto sec = s.substr (pos, next == std::string::npos ? s.size() - pos : next - pos);
                    auto gr = [&] (const std::string& key) {
                        auto p2 = sec.find (key + "=");
                        if (p2 == std::string::npos) return std::string();
                        auto e2 = sec.find ("\n", p2);
                        return sec.substr (p2 + key.size() + 1, (e2 == std::string::npos ? sec.size() : e2) - (p2 + key.size() + 1));
                    };
                    op1ratio = gr ("ratio"); op1level = gr ("level"); op1fb = gr ("fb");
                }
            }
            const double r   = parseFloat (op1ratio.empty() ? getField ("ratio") : op1ratio, 2.0);
            const double lvl = parseFloat (op1level.empty() ? "0.8" : op1level, 0.8);
            const double fb  = parseFloat (op1fb, 0.0);
            const int envId  = (int) parseFloat (getField ("pmIndexEnv"), 0.0);
            const juce::ScopedLock sl (dataLock);
            const auto ids = score.getSelectedArcs();
            for (auto& a : curPage().arcs)
            {
                if (! ids.empty() && std::find (ids.begin(), ids.end(), a.id) == ids.end()) continue;
                if (a.mode != 3) continue;
                a.pmRatio = r;
                a.pmIndex = lvl * 5.0;
                a.pmFeedback = juce::jlimit (0.0, 1.0, fb);
                a.pmIndexEnv = envId;
            }
            pmRatioSlider.setValue (r, juce::dontSendNotification);
            pmIndexSlider.setValue (lvl * 5.0, juce::dontSendNotification);
            { int ei = 1; for (size_t j = 0; j < envelopes.size(); ++j) if (envelopes[j].id == envId) { ei = (int) j + 2; break; } pmIndexEnvBox.setSelectedId (ei, juce::dontSendNotification); }
            pmFeedbackSlider.setValue (juce::jlimit (0.0, 1.0, fb), juce::dontSendNotification);
            pushVoice();
            publishModelSnapshot();
            flash ("Imported PM preset");
        });
    };
    pmExportBtn.onClick = [this]{
        if (score.getSelectedArcs().empty()) { flash ("Select a PM arc first"); return; }
        const int id = score.getSelectedArc();
        upic::Arc* aPtr = nullptr;
        { const juce::ScopedLock sl (dataLock); for (auto& a : curPage().arcs) if (a.id == id) { aPtr = &a; break; } }
        if (aPtr == nullptr || aPtr->mode != 3) { flash ("Selected arc is not PM"); return; }
        std::ostringstream o;
        o << "[preset]\nname=UPIC PM export\nmaster=0.8\ntune=440\n";
        o << "[op0]\nenabled=1\nwave=0\nratio=1\nlevel=0.9\nfb=0\nenv=" << aPtr->pmIndexEnv << "\nsustain=0\nrelease=0\n";
        o << "[op1]\nenabled=1\nwave=0\nratio=" << aPtr->pmRatio << "\nlevel=" << (aPtr->pmIndex / 5.0) << "\nfb=" << aPtr->pmFeedback << "\nenv=" << aPtr->pmIndexEnv << "\nsustain=0\nrelease=0\n";
        o << "[matrix]\nm1_0=0.8\nout0=1.0\n";
        o << "fb1=" << aPtr->pmFeedback << "\n";
        const std::string text = o.str();
        juce::FileChooser fc ("Export PM preset", juce::File(), "*.preset");
        fc.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, text] (const juce::FileChooser& c) {
            auto f = c.getResult();
            if (f == juce::File()) return;
            auto out = f.withFileExtension ("preset");
            if (! out.replaceWithText (juce::String (text))) flash ("Export failed");
            else flash ("Exported " + out.getFileName());
        });
    };

    pmPresetBox.onChange = [this]{
        const int sel = pmPresetBox.getSelectedId();
        if (sel <= 1) return;
        double ratio = 2.0, idx = 1.5, fb = 0.0; int envId = 0;
        if (sel == 2) { ratio = 2.0;   idx = 2.5; envId = 1; fb = 0.0; }
        else if (sel == 3) { ratio = 1.0; idx = 1.0; envId = 0;  fb = 0.1; }
        else if (sel == 4) { ratio = 3.0; idx = 4.0; envId = 1; fb = 0.0; }
        else if (sel == 5) { ratio = 1.0; idx = 0.5; envId = 0;  fb = 0.3; }
        else if (sel == 6) { ratio = 1.414; idx = 5.0; envId = 1; fb = 0.2; }
        // the preset also sets the current voice, so the next PM arc drawn
        // is born with it (whether or not an arc is selected)
        pmRatioSlider.setValue (ratio, juce::dontSendNotification);
        pmIndexSlider.setValue (idx, juce::dontSendNotification);
        { int ei = 1; for (size_t j = 0; j < envelopes.size(); ++j) if (envelopes[j].id == envId) { ei = (int) j + 2; break; } pmIndexEnvBox.setSelectedId (ei, juce::dontSendNotification); }
        pmFeedbackSlider.setValue (fb, juce::dontSendNotification);
        const auto ids = score.getSelectedArcs();
        if (! ids.empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
            {
                if (std::find (ids.begin(), ids.end(), a.id) == ids.end()) continue;
                if (a.mode != 3) continue;
                a.pmRatio = ratio; a.pmIndex = idx; a.pmIndexEnv = envId; a.pmFeedback = fb;
            }
            publishModelSnapshot();
        }
        pushVoice();
    };
    pmRatioSlider.onValueChange = [this]{
        if (! score.getSelectedArcs().empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (score.getSelectedArcs().begin(), score.getSelectedArcs().end(), a.id) != score.getSelectedArcs().end())
                    a.pmRatio = pmRatioSlider.getValue();
            publishModelSnapshot();
        }
        pushVoice();
    };
    pmIndexSlider.onValueChange = [this]{
        if (! score.getSelectedArcs().empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (score.getSelectedArcs().begin(), score.getSelectedArcs().end(), a.id) != score.getSelectedArcs().end())
                    a.pmIndex = pmIndexSlider.getValue();
            publishModelSnapshot();
        }
        pushVoice();
    };
    sampleBox.onChange = [this]{
        int i = sampleBox.getSelectedId() - 1;
        if (i >= 0 && i < (int) sampleSlots.size()) selSlot = i;
        // v1.26: the slot is an arc setting too - changing it with a
        // multi-selection assigns the slot to every selected sample arc.
        const auto ids = score.getSelectedArcs();
        if (! ids.empty() && selSlot >= 0 && selSlot < (int) sampleSlots.size())
        {
            const int sid = sampleSlots[(size_t) selSlot].id;
            {
                const juce::ScopedLock sl (dataLock);
                for (auto& a : curPage().arcs)
                    if (a.mode == 1
                        && std::find (ids.begin(), ids.end(), a.id) != ids.end())
                        a.sampleId = sid;
            }
            publishModelSnapshot();
        }
        updateSampleView(); pushVoice();
        updateRevButton(); // v1.19: the button shows the selected slot's state
    };
    loadSampleBtn.onClick = [this]{ loadSampleIntoSlot(selSlot); };

    // v1.19 (wave 17): the selected slot plays backwards. The engine mirrors
    // the material read, so the pitch modes all work unchanged. Session-only:
    // the piece format is untouched (FUTURE_IDEAS.md, UPIC-25).
    addAndMakeVisible(revBtn);
    revBtn.setName("revBtn");
    revBtn.setClickingTogglesState(true);
    revBtn.setTooltip("Play the selected slot backwards");
    revBtn.onClick = [this]{
        if (selSlot < 0 || selSlot >= (int) sampleSlots.size()) return;
        const bool on = revBtn.getToggleState();
        sampleSlots[(size_t) selSlot].reversed = on;
        // v1.26: the slot direction applies to the selected sample arcs too
        // (a multi-selection may use more than one slot).
        const auto ids = score.getSelectedArcs();
        if (! ids.empty())
        {
            const juce::ScopedLock sl (dataLock);
            for (auto& a : curPage().arcs)
            {
                if (a.mode != 1
                    || std::find (ids.begin(), ids.end(), a.id) == ids.end())
                    continue;
                for (auto& s : sampleSlots)
                    if (s.id == a.sampleId) { s.reversed = on; break; }
            }
        }
        updateRevButton();
        markModelChanged();  // v1.15: the material changes -> fade across it
        publishModelSnapshot(); // v1.18: the audio thread must see it now
        flash(on ? "Selected slot(s): reversed" : "Selected slot(s): forward");
    };
    updateRevButton();

    // v1.10: the sample pitch mode mirrors the live pitch selector - the
    // original varispeed stays the default; granular (stretch) maps the slot
    // onto the arc, granular (no stretch) changes the length with pitch, and
    // the phase vocoder keeps the slot's playback length intact.
    // Session-only, like the live pitch mode.
    samplePitchLab.setName("samplePitchLab");
    samplePitchBox.setName("samplePitchBox");
    samplePitchBox.addItem("Varispeed (tape)", 1);
    samplePitchBox.addItem("Granular (stretch)", 2);
    samplePitchBox.addItem("Phase vocoder", 3);
    samplePitchBox.addItem("Granular (no stretch)", 4);
    samplePitchBox.setSelectedId(1, juce::dontSendNotification);
    score.sampleGranularTab = (samplePitchBox.getSelectedId() == 2);
    samplePitchBox.setTooltip("Sample-tab arcs: how the slot is transposed.\n"
                              "At 1x (the arc sits on Ref) the pitch is unchanged; "
                              "draw above/below Ref to transpose.\n"
                              "Varispeed and Granular (no stretch) change the slot's "
                              "playback length with pitch; Granular (stretch) fits "
                              "the slot to the arc length; Phase vocoder keeps the "
                              "slot's length and shifts pitch only.");
    samplePitchBox.onChange = [this]{
        const int pm = samplePitchBox.getSelectedId() - 1;
        engine.setSamplePitchMode(pm);
        scrubEngine.setSamplePitchMode(pm);
        score.sampleGranularTab = (samplePitchBox.getSelectedId() == 2);
        markModelChanged(); // v1.15: the sample voices re-anchor -> fade across it
        flash("Sample pitch: " + samplePitchBox.getText());
    };

    // --- v1.4 live input: monitor toggle, capture toggle, input gain ---
    monBtn.onClick = [this]{
        if (! monBtn.getToggleState()) { monitoring = false; return; }
        ensureLiveInputFor([this](bool ok){
            if (ok) monitoring = true;
            else monBtn.setToggleState(false, juce::dontSendNotification);
        });
    };
    capBtn.setClickingTogglesState(true);
    capBtn.onClick = [this]{
        if (! capBtn.getToggleState())
        {
            if (capturing.load()) requestCaptureStop(1);
            capBtn.setButtonText("Capture");
            return;
        }
        ensureLiveInputFor([this](bool ok){
            if (ok) startCapture();
            else capBtn.setToggleState(false, juce::dontSendNotification);
        });
    };
    inGainSlider.setName("inGainSlider");
    inGainSlider.setRange(0.0, 1.0, 0.01);
    inGainSlider.setValue(0.8, juce::dontSendNotification);
    inGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 18);
    inGainSlider.onValueChange = [this]{ inputGain = (float) inGainSlider.getValue(); };

    slotLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    slotLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    pmRatioLab.setText ("Ratio", juce::dontSendNotification); pmIndexLab.setText ("Index", juce::dontSendNotification); tempoLab.setText ("Tempo", juce::dontSendNotification); tempoLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    refLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    samplePitchLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    samplePitchLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    inGainLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    inGainLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    matHintLab.setText("Osc: draw arcs on the wavetable | Wave = carrier, Env = amplitude",
                       juce::dontSendNotification);
    matHintLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    matHintLab.setJustificationType(juce::Justification::centredRight);
    matHintLab.setColour(juce::Label::textColourId, juce::Colour(0xff7a7a86));

    timeLab.setColour(juce::Label::textColourId, juce::Colours::white);
    timeLab.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    timeLab.setJustificationType(juce::Justification::centredRight);
    statusLab.setColour(juce::Label::textColourId, juce::Colour(0xff9a9aa5));
    statusLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    statusLab.setJustificationType(juce::Justification::centredRight);

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
    waveEd.setOnEditStart([this]{ return prepareWaveEdit(); });
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
                double sd = sampleSlots[(size_t) si].durationSec();
                if (sd <= 0.0) break;
                double ref = juce::jmax(1.0, a.refFreq);
                const int tabMode = samplePitchBox.getSelectedId() - 1;
                bool arcMapped = (a.pitchMode >= 0) ? (a.pitchMode == 1 || a.pitchMode == 2)
                                                    : (tabMode == 1 || tabMode == 2);
                double arcDur = juce::jmax(1.0e-6, a.endTime() - a.startTime());
                double progress = juce::jlimit(0.0, 1.0, (t - a.startTime()) / arcDur);
                double pos = arcMapped ? progress * sd
                                       : UpicEngine::scrubMaterialPos(a, t, ref, sd);
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
            scrubFreezeStopAtMs.store(juce::Time::getMillisecondCounterHiRes() + scrubTailMs,
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
    score.onArcEditingChanged = [this](bool editing){
        arcEditing.store(editing, std::memory_order_relaxed);
        if (! editing)
        {
            // apply the edited score on release
            markModelChanged();
            publishModelSnapshot();
        }
    };

    // --- wave/env editors ---
    addAndMakeVisible(waveEd); addAndMakeVisible(envEd);
    addAndMakeVisible(wSine); addAndMakeVisible(wSaw); addAndMakeVisible(wSqr); addAndMakeVisible(wTri);
    addAndMakeVisible(wNoise); addAndMakeVisible(wSmooth); addAndMakeVisible(wNorm);
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

    eFlat.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetFlat(); envEd.repaint(); } };
    ePerc.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetPercussive(); envEd.repaint(); } };
    eSwell.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetSwell(); envEd.repaint(); } };
    eGate.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetGate(); envEd.repaint(); } };
    eStr.onClick = [this, curE]{ const juce::ScopedLock l(dataLock); if (auto* e = curE()) { e->presetStrings(); envEd.repaint(); } };

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

    refreshBoxes();
    refreshScoreBinding();
    updateStatus();
    setSize(1280, 800);
}

MainComponent::~MainComponent()
{
    // v1.18: stop the audio thread first (no callback can be in flight), then
    // hand back the snapshots it owned so the handover can free them here.
    shutdownAudio();
    snapshots.retireReader(liveSnapshot);
    snapshots.clear();
}

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
    pageBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < pages.size(); ++i)
        pageBox.addItem("Page " + juce::String((int)i + 1) + "  (" + juce::String(pages[i].arcs.size()) + " arcs)", (int)i + 1);
    pageBox.setSelectedId(currentPage + 1, juce::dontSendNotification);
    durSlider.setValue(curPage().durationSec, juce::dontSendNotification);

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
    score.bind(&curPage(), &waveforms, &playheadSec, [this]{ updateStatus(); refreshBoxes(); });
    pushVoice();
    score.setPlaying(isPlaying);
    waveEd.bind(&waveforms, &selWave);
    envEd.bind(&envelopes, &selEnv);
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
    sieveR = rs; // empty = sieve inert (identity)
    score.setSieve(sieveOn, sieveM, sieveR);
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
        }
        score.setSelectedArcs(created);
    }
    refreshBoxes();
    updateStatus();
    score.repaint();
    flash(gen.empty() ? ("Generator produced no arcs (" + name + ")")
                      : ("Generated " + juce::String((int) gen.size()) + " arcs -- " + name));
}

void MainComponent::exportStems()
{
    auto fc = std::make_shared<juce::FileChooser>(
        "Export stems (one WAV per voice) -- choose a folder",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, fc](const juce::FileChooser& c){
            auto dir = c.getResult();
            if (dir == juce::File()) return;
            double sr = 44100.0;
            if (auto* dev = deviceManager.getCurrentAudioDevice())
                if (dev->getCurrentSampleRate() > 0) sr = dev->getCurrentSampleRate();
            std::vector<juce::File> written;
            {
                juce::ScopedLock l(dataLock);
                written = renderStems(curPage(), waveforms, envelopes, sampleSlots,
                                      dir, "stem", sr, masterGain.load(),
                                      engine.getSamplePitchMode());
            }
            if (written.empty())
                flash("Stems: nothing to render (no audible arcs / voices in this page)");
            else
                flash("Wrote " + juce::String((int) written.size()) + " stem file(s) to "
                      + dir.getFullPathName());
        });
}

void MainComponent::updateStatus() {}

//==============================================================================
// v1.18 (wave 15): snapshot publication + the small message <-> audio
// signals. All of these are called on the message thread.
void MainComponent::publishModelSnapshot()
{
    auto* snap = new ModelSnapshot();
    snap->waveforms = waveforms;
    snap->envelopes = envelopes;
    snap->pages = pages;
    snap->slots = sampleSlots; // shared buffers: only the pointers are copied
    snap->currentPage = currentPage;
    snap->pageEpoch = pageEpoch.load(std::memory_order_relaxed);
    snap->seqPages = seqPages;
    snap->loopPage = loopPage;
    snapshots.publish(snap); // frees the snapshots the reader retired
}

void MainComponent::seekPlayhead(double t)
{
    playheadSec = t;
    playheadAtEnd.store(false, std::memory_order_relaxed);
    seekTo.store(t, std::memory_order_relaxed);
    seekSerial.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::bumpPageEpoch()
{
    pageEpoch.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::requestEngineReset()
{
    engineResetSerial.fetch_add(1, std::memory_order_relaxed);
}

double MainComponent::displayPlayhead() const
{
    return isPlaying.load(std::memory_order_relaxed)
               ? playheadPublished.load(std::memory_order_relaxed)
               : playheadSec;
}

void MainComponent::startPlayback()
{
    // a Stop click after Play is a fresh single click, not a double-click
    lastStopClickMs = -1.0e9;
    bool hasLive = false;
    {
        juce::ScopedLock l(dataLock);
        // the audio thread owns the playhead: only seek when the piece ended
        const double rate = audioPlayRate;
        // direction-aware seek: playing backwards from the start wraps to the end
        if (rate < 0.0)
        {
            if (playheadAtEnd.load(std::memory_order_relaxed)
                || displayPlayhead() <= 0.05)
                seekPlayhead(curPage().durationSec);
        }
        else if (playheadAtEnd.load(std::memory_order_relaxed)
              || displayPlayhead() >= curPage().durationSec - 0.05)
        {
            seekPlayhead(0.0);
        }
        requestEngineReset(); // re-anchor every voice at the playhead
        isPlaying.store(true);
        wantsStop.store(false);   // cancel any pending fade-out
        for (auto& a : curPage().arcs)
            if (a.mode == 2 && ! a.muted) { hasLive = true; break; }
    }
    // Live arcs are silent without input: enable it on first playback.
    if (hasLive && ! inputEnabled)
        ensureLiveInputFor([](bool){});
    score.setPlaying(true);
    playBtn.setButtonText("Playing...");
}

void MainComponent::stopPlayback(bool fromAudio)
{
    {
        juce::ScopedLock l(dataLock);
        if (isPlaying.load())
        {
            // click-free stop (UPISketch "playback now fades out smoothly"):
            // keep rendering and ramp the output to silence over kStopFadeSec,
            // then the audio callback halts on its own.
            wantsStop.store(true);
        }
        else
        {
            wantsStop.store(false); // idle: clear any stale stop request
            // Only the stop posted by the audio thread knows where the fade /
            // page end landed - syncing on a plain Stop click while idle would
            // overwrite a scrub with the last playback position.
            if (fromAudio)
            {
                const double p = playheadPublished.load(std::memory_order_relaxed);
                if (p > 0.0) playheadSec = p;
            }
        }
    }
    score.setPlaying(isPlaying.load() || wantsStop.load());
    playBtn.setButtonText("Play"); // reusable at once (startPlayback cancels the fade)
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    audioSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    engine.prepare(audioSampleRate, samplesPerBlockExpected);
    scrubEngine.prepare(audioSampleRate, samplesPerBlockExpected);
    scrubEngine.setScrubPreview(true);
    scrubPreviewLen = juce::jmax(256, (int) (audioSampleRate * 0.05)); // ~50 ms
    liveRingSize = juce::jmax(1024, (int) (audioSampleRate * kLiveRingSec));
    liveRingA.assign((size_t) liveRingSize, 0.0f);
    liveRingB.assign((size_t) liveRingSize, 0.0f);
    liveWritePos = 0;
    liveTapActive = false;
    // v1.20 (wave 18): the live reverse buffers. They are allocated once, at
    // the maximum duration (2 s); the chosen duration only uses the first
    // N frames - so the message thread never reallocates them while the
    // audio thread may be reading.
    {
        const int maxFrames = juce::jmax(1024, (int) std::lround(2.0 * audioSampleRate));
        for (auto& b : revBuf)
        {
            b.setSize(2, maxFrames, false, false, true);
            b.clear();
        }
        revBufFrames.store(juce::jmax(1024, (int) std::lround(revBufSec * audioSampleRate)));
        revRestart.store(true, std::memory_order_relaxed);
    }
    stopFadeTotal = juce::jmax(1, (int) std::ceil(kStopFadeSec * audioSampleRate));
    stopFadePos = 0;
    wantsStop.store(false);
    // v1.15: the click-free state starts clean, and the score buffer is
    // sized for the host's block (it only grows if the host sends more)
    scoreBuf.setSize(2, juce::jmax(1024, samplesPerBlockExpected), false, false, true);
    scoreGain = 1.0f;
    scoreTail = 0.0f;
    masterSmoothed = masterGain.load();
    inGainSmoothed = inputGain.load();
    // Device restarted under a running take: the buffered rate is stale, so
    // the next audio block hands the partial take off for discard.
    if (capturing.load()) capAbortPending = true;
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    // Live input runs before the clear: the player buffer initially carries
    // the input channels, and clearActiveBufferRegion() would wipe them.
    const bool mon = tapLiveInput(info);
    info.clearActiveBufferRegion();

    const int n = info.numSamples;
    float* L = info.buffer->getWritePointer(0, info.startSample);
    float* R = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
    const int scoreFadeN = juce::jmax(1, (int) std::lround(audioSampleRate * (double) dragSmoothMs.load(std::memory_order_relaxed) / 1000.0));

    if (mon) // monitor mix: live passthrough, independent of the transport
    {
        const float* s0 = inScratch.getReadPointer(0);
        const float* s1 = inScratch.getNumChannels() > 1 ? inScratch.getReadPointer(1) : s0;
        for (int i = 0; i < n; ++i)
        {
            // v1.15: the monitor gain is smoothed per sample - a step in a
            // gain (a slider move, host automation) is a click
            inGainSmoothed += (inputGain.load() - inGainSmoothed) * kGainSmooth;
            L[i] += s0[i] * inGainSmoothed;
            if (R != nullptr) R[i] += s1[i] * inGainSmoothed;
        }
    }

    // v1.15: the score renders into its own buffer, so a block that cannot
    // be rendered (the UI thread holds the model lock) can be faded out
    // instead of cut. A hard cut from the previous block's last sample to
    // zero is a click - and it happens whatever the host's buffer size is.
    if (scoreBuf.getNumChannels() < 2 || scoreBuf.getNumSamples() < n)
        scoreBuf.setSize(2, n, false, false, true);
    scoreBuf.clear();
    float* SL = scoreBuf.getWritePointer(0);
    float* SR = scoreBuf.getWritePointer(1);

    if (freezeBuf.getNumChannels() < 2 || freezeBuf.getNumSamples() < n)
        freezeBuf.setSize(2, n, false, false, true);
    freezeBuf.clear();
    float* FL = freezeBuf.getWritePointer(0);
    float* FR = freezeBuf.getWritePointer(1);

    bool rendered = false;
    bool modelChanged = false;
    const bool playingAtStart = isPlaying.load(std::memory_order_relaxed);
    const bool stopping = wantsStop.load(std::memory_order_relaxed);
    {
        // v1.18 (wave 15): no lock on the audio thread. Take the newest
        // immutable model snapshot and render from it - the message thread
        // owns the live model and frees retired snapshots on its own thread.
        ModelSnapshot* snap = snapshots.take(liveSnapshot);

        // a seek / scrub from the UI moves the audio playhead
        const int seek = seekSerial.load(std::memory_order_relaxed);
        if (seek != seenSeekSerial)
        {
            seenSeekSerial = seek;
            audioPlayhead = seekTo.load(std::memory_order_relaxed);
            engine.reset();
        }

        const int fs = scrubFreezeStartSerial.load(std::memory_order_relaxed);
        if (fs != seenFreezeStartSerial)
        {
            seenFreezeStartSerial = fs;
            audioPlayhead = scrubFreezePlayhead.load(std::memory_order_relaxed);
            engine.reset();
        }

        // Play asks for fresh voices (no fade: the transport is starting)
        const int reset = engineResetSerial.load(std::memory_order_relaxed);
        if (reset != seenEngineResetSerial)
        {
            seenEngineResetSerial = reset;
            engine.reset();
        }

        // v1.25 (wave 22): a direction change while playing crossfades over
        // ~20 ms instead of re-anchoring mid-stream (which clicked).
        const int dcs = dirChangeSerial.load(std::memory_order_relaxed);
        if (dcs != seenDirChangeSerial)
        {
            seenDirChangeSerial = dcs;
            dirFadeOutRemaining = kDirCrossfadeSamples;
            audioPlayRate = prevPlayRate.load(std::memory_order_relaxed);
        }

        if (snap != nullptr)
        {
            // A page change must be adopted even while stopped, so the next
            // Play starts on the page the UI shows.
            if (snap->pageEpoch != seenPageEpoch)
            {
                seenPageEpoch = snap->pageEpoch;
                audioPage = snap->pages.empty() ? 0
                    : juce::jlimit(0, (int) snap->pages.size() - 1, snap->currentPage);
                audioPlayhead = 0.0;
                engine.reset();
            }

            // v1.28: gain crossfade between preview and freeze
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            double previewGain = scrubPreviewActive.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            double freezeGain = scrubFreezeActive.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            const double ts = scrubFreezeTransitionStartMs.load(std::memory_order_relaxed);
            const double te = scrubFreezeTransitionEndMs.load(std::memory_order_relaxed);
            if (scrubFreezeActive.load(std::memory_order_relaxed) && te > ts && nowMs < te)
            {
                const double prog = juce::jlimit(0.0, 1.0, (nowMs - ts) / (te - ts));
                freezeGain = prog;
                if (scrubPreviewActive.load(std::memory_order_relaxed))
                    previewGain = 1.0 - prog;
            }

            // v1.28: scrub audition (short preview while dragging the playhead)
            if (! playingAtStart && scrubPreviewActive.load(std::memory_order_relaxed))
            {
                int remaining = scrubPreviewRemaining.load(std::memory_order_relaxed);
                if (remaining > 0 && ! snap->pages.empty())
                {
                    int p = juce::jlimit(0, (int) snap->pages.size() - 1, audioPage);
                    const upic::Page& pg = snap->pages[(size_t) p];
                    int chunk = juce::jmin(n, remaining);
                    juce::FloatVectorOperations::clear(SL, chunk);
                    if (SR != nullptr) juce::FloatVectorOperations::clear(SR, chunk);
                    double ph = scrubPreviewPlayhead.load(std::memory_order_relaxed);
                    const double rate = scrubRate.load(std::memory_order_relaxed);
                    const int sps = scrubPreviewSerial.load(std::memory_order_relaxed);
                    if (sps != seenScrubPreviewSerial)
                    {
                        seenScrubPreviewSerial = sps;
                        scrubEngine.reset(); // re-anchor to the dragged playhead
                    }
                    scrubEngine.render(pg, ph, snap->waveforms, snap->envelopes, snap->slots,
                                      SL, (SR != nullptr ? SR : SL), chunk, 1.0f, rate);
                    const int fade = juce::jmin(chunk / 4, (int) (audioSampleRate * 0.005));
                    for (int i = 0; i < chunk; ++i)
                    {
                        float g = 1.0f;
                        if (fade > 0)
                        {
                            if (i < fade) g = (float) i / (float) fade;
                            else if (i >= chunk - fade) g = (float) (chunk - 1 - i) / (float) fade;
                        }
                        SL[i] *= g;
                        if (SR != nullptr) SR[i] *= g;
                    }
                    const float mg = masterGain.load(std::memory_order_relaxed) * (float) previewGain;
                    for (int i = 0; i < chunk; ++i)
                    {
                        L[i] += SL[i] * mg;
                        if (R != nullptr) R[i] += (SR != nullptr ? SR[i] : SL[i]) * mg;
                    }
                    scrubPreviewPlayhead.store(ph + (double) chunk * rate / audioSampleRate, std::memory_order_relaxed);
                    const int left = remaining - chunk;
                    scrubPreviewRemaining.store(left, std::memory_order_relaxed);
                    if (left <= 0) scrubPreviewActive.store(false, std::memory_order_relaxed);
                }
                else
                {
                    scrubPreviewActive.store(false, std::memory_order_relaxed);
                }
            }

            // v1.28: freeze sustain (click without drag)
            if (! playingAtStart && scrubFreezeActive.load(std::memory_order_relaxed))
            {
                const double nowMs2 = juce::Time::getMillisecondCounterHiRes();
                const double stopAt = scrubFreezeStopAtMs.load(std::memory_order_relaxed);
                if (stopAt > 0.0 && nowMs2 >= stopAt)
                {
                    scrubFreezeActive.store(false, std::memory_order_relaxed);
                    scrubFreezeStopAtMs.store(0.0, std::memory_order_relaxed);
                }
                else if (! snap->pages.empty())
                {
                    int p = juce::jlimit(0, (int) snap->pages.size() - 1, audioPage);
                    const upic::Page& pg = snap->pages[(size_t) p];
                    juce::FloatVectorOperations::clear(FL, n);
                    if (FR != nullptr) juce::FloatVectorOperations::clear(FR, n);
                    engine.render(pg, audioPlayhead, snap->waveforms, snap->envelopes, snap->slots,
                                  FL, (FR != nullptr ? FR : FL), n, 1.0f, 0.0, true);
                    float gain = 1.0f;
                    if (stopAt > 0.0)
                    {
                        const double rem = stopAt - nowMs2;
                        gain = (float) juce::jlimit(0.0, 1.0, rem / juce::jmax(1, scrubTailMs));
                    }
                    const float mg = masterGain.load(std::memory_order_relaxed) * gain * (float) freezeGain;
                    for (int i = 0; i < n; ++i)
                    {
                        L[i] += FL[i] * mg;
                        if (R != nullptr) R[i] += (FR != nullptr ? FR[i] : FL[i]) * mg;
                    }
                }
            }

            // Absorb model edits while stopped (no sound to fade).
            const int epoch = modelEpoch.load(std::memory_order_relaxed);
            modelChanged = (epoch != seenModelEpoch);
            seenModelEpoch = epoch;

            if (playingAtStart)
            {
                rendered = true;
                renderScoreBlock(*snap, SL, SR, n);
                // v1.25 (wave 22): direction-change crossfade. Applied on top of
                // the rendered score; the scoreGain path is forced to 1.0 so it
                // does not double-ramp.
                if (dirFadeOutRemaining > 0 || dirFadeInRemaining > 0)
                {
                    scoreGain = 1.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        float g = 1.0f;
                        if (dirFadeOutRemaining > 0)
                        {
                            int done = kDirCrossfadeSamples - dirFadeOutRemaining;
                            int pos  = done + i;
                            if (pos > kDirCrossfadeSamples) pos = kDirCrossfadeSamples;
                            g = 1.0f - (float) pos / (float) kDirCrossfadeSamples;
                        }
                        else
                        {
                            int done = kDirCrossfadeSamples - dirFadeInRemaining;
                            int pos  = done + i;
                            if (pos > kDirCrossfadeSamples) pos = kDirCrossfadeSamples;
                            g = (float) pos / (float) kDirCrossfadeSamples;
                        }
                        SL[i] *= g;
                        if (SR != nullptr) SR[i] *= g;
                    }
                    if (dirFadeOutRemaining > 0)
                    {
                        dirFadeOutRemaining -= n;
                        if (dirFadeOutRemaining <= 0)
                        {
                            dirFadeOutRemaining = 0;
                            audioPlayRate = playRate.load(std::memory_order_relaxed);
                            engine.reset();
                            dirFadeInRemaining = kDirCrossfadeSamples;
                        }
                    }
                    else
                    {
                        dirFadeInRemaining -= n;
                        if (dirFadeInRemaining < 0) dirFadeInRemaining = 0;
                    }
                }
            }
        }
    }

    // v1.15: fade the score in/out, so that no block boundary can click
    if (rendered && ! modelChanged)
    {
        if (scoreGain < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                // v1.18: clamp the ramp - without it a block longer than
                // kScoreFadeSamples would keep raising the gain (a 2048-block
                // fade-in peaked at 21x, an audible blast after a model change)
                const float k = juce::jmin(1.0f, (float) i / (float) scoreFadeN);
                const float g = scoreGain + (1.0f - scoreGain) * k;
                SL[i] *= g; SR[i] *= g;
            }
            scoreGain = 1.0f;
        }
        scoreTail = (n > 0) ? SL[n - 1] : 0.0f;
    }
    else
    {
        // no score this block (or the model changed under it): ramp the last
        // sample down to zero instead of cutting it (the step from the
        // previous block would be a click)
        for (int i = 0; i < n; ++i)
        {
            const float k = juce::jmin(1.0f, (float) i / (float) scoreFadeN);
            const float v = scoreTail * scoreGain * (1.0f - k);
            SL[i] = v; SR[i] = v;
        }
        scoreGain = 0.0f;
        scoreTail = 0.0f;
    }

    // add the score to the output (the monitor mix is already in there)
    for (int i = 0; i < n; ++i)
    {
        // v1.15: the master gain is smoothed per sample (a step = a click)
        masterSmoothed += (masterGain.load() - masterSmoothed) * kGainSmooth;
        L[i] += SL[i] * masterSmoothed;
        if (R != nullptr) R[i] += SR[i] * masterSmoothed;
    }

    // a model edit was audible this block: drop the old voices now, on the
    // audio thread (the message thread never touches the engine any more)
    if (modelChanged)
        engine.reset();

    // Click-free manual stop: keep rendering the page for kStopFadeSec while
    // the output ramps to silence, then halt (playhead keeps its position).
    if (stopping && isPlaying.load(std::memory_order_relaxed))
    {
        for (int i = 0; i < n; ++i)
        {
            const double rSec = (double) juce::jmax(0, stopFadeTotal - stopFadePos) / audioSampleRate;
            const float g = (float) upic::stopFadeGain(rSec, (double) stopFadeTotal / audioSampleRate);
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
            ++stopFadePos;
        }
        if (stopFadePos >= stopFadeTotal)
        {
            isPlaying.store(false);
            wantsStop.store(false);
            stopFadePos = 0;
            engine.reset();
            // UI must not be touched from the audio thread -> SafePointer.
            juce::Component::SafePointer<MainComponent> sp(this);
            juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
        }
    }
    else
    {
        // no fade pending: the audio thread owns the counter, so it is the
        // one that resets it (the message thread only asks for the stop)
        stopFadePos = 0;
    }
}

//==============================================================================
// v1.15: the score render for one block, into its own buffer - the caller
// fades it, so a block that cannot be rendered is a dip instead of a click.
// v1.18 (wave 15): renders from an immutable snapshot; the audio thread never
// takes dataLock and never touches the message thread's model.
void MainComponent::renderScoreBlock(const ModelSnapshot& snap, float* SL, float* SR, int n)
{
    const double sr = audioSampleRate;

    // v1.5: publish the live input ring view for mode-2 (live input) arcs
    const int revFrames = revBufFrames.load(std::memory_order_relaxed);
    if (revLiveOn.load(std::memory_order_relaxed) && revFrames > 0
        && revBuf[revPlayIdx].getNumSamples() >= revFrames)
    {
        // v1.20/1.21: the reverse - the shifter can read the frozen, mirrored
        // segment instead of the live ring. The arc decides which one (the
        // arc list's Rev box).
        const auto& play = revBuf[revPlayIdx];
        engine.setLiveReverse(play.getReadPointer(0),
                              play.getNumChannels() > 1 ? play.getReadPointer(1) : nullptr,
                              revFrames, (long long) revPlayHead, liveTapActive);
    }
    else
    {
        engine.setLiveReverse(nullptr, nullptr, 0, 0, false);
    }
    engine.setLiveInput(liveRingA.data(), liveRingB.data(), liveRingSize,
                        liveWritePos, liveTapActive);

    const auto publishPlayhead = [this]
    { playheadPublished.store(audioPlayhead, std::memory_order_relaxed); };

    // UI must not be touched from the audio thread, and `this` may be gone by
    // the time the callback runs -> SafePointer. The epoch guard drops a late
    // notification if the user picked another page meanwhile.
    const auto notifyPage = [this](int next)
    {
        const int epoch = pageEpoch.load(std::memory_order_relaxed);
        juce::Component::SafePointer<MainComponent> sp(this);
        juce::MessageManager::callAsync([sp, next, epoch]{
            if (sp == nullptr) return;
            if (sp->pageEpoch.load(std::memory_order_relaxed) != epoch) return;
            sp->currentPage = juce::jlimit(0, (int) sp->pages.size() - 1, next);
            sp->refreshBoxes();
            sp->refreshScoreBinding();
        });
    };

    // Render in sub-chunks split at the page boundary so looping never
    // leaves an unrendered (silent) tail at the seam.
    int offset = 0;
    while (offset < n)
    {
        if (snap.pages.empty())
        {
            isPlaying.store(false); wantsStop.store(false);
            engine.reset(); publishPlayhead(); return;
        }
        // the message thread may have deleted a page since the last snapshot
        if (audioPage < 0 || audioPage >= (int) snap.pages.size())
            audioPage = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
        const auto& pg = snap.pages[(size_t) audioPage];

        if (pg.durationSec < 0.001)
        {
            isPlaying.store(false); wantsStop.store(false);
            engine.reset(); publishPlayhead(); return;
        }

        const double rate = playRate.load(std::memory_order_relaxed);
        const bool reverse = rate < 0.0;

        if (reverse ? (audioPlayhead <= 0.0) : (audioPlayhead >= pg.durationSec))
        {
            if (reverse)
            {
                if (snap.seqPages)
                {
                    if (audioPage > 0)
                    {
                        --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                    else
                    {
                        audioPlayhead = 0.0;
                        isPlaying.store(false); wantsStop.store(false);
                        playheadAtEnd.store(false, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
                        juce::Component::SafePointer<MainComponent> sp(this);
                        juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
                        return;
                    }
                }
                else if (snap.loopPage)
                {
                    if (audioPage > 0)
                    {
                        --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                    else
                    {
                        const int last = (int) snap.pages.size() - 1;
                        audioPage = last; audioPlayhead = snap.pages[(size_t) last].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                }
                else if (audioPage > 0)
                {
                    --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                    engine.reset(); notifyPage(audioPage);
                }
                else
                {
                    audioPlayhead = 0.0;
                    isPlaying.store(false); wantsStop.store(false);
                    playheadAtEnd.store(false, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
                    juce::Component::SafePointer<MainComponent> sp(this);
                    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
                    return;
                }
            }
            else
            {
                if (snap.seqPages)
                {
                    const int next = upic::seqNextPage(audioPage, (int) snap.pages.size(), snap.loopPage);
                    if (next >= 0)
                    {
                        audioPage = next; audioPlayhead = 0.0; engine.reset(); notifyPage(next);
                    }
                    else
                    {
                        audioPlayhead = pg.durationSec;
                        isPlaying.store(false); wantsStop.store(false);
                        playheadAtEnd.store(true, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
                        juce::Component::SafePointer<MainComponent> sp(this);
                        juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
                        return;
                    }
                }
                else if (snap.loopPage)
                {
                    audioPlayhead = 0.0; engine.reset();
                }
                else if (audioPage + 1 < (int) snap.pages.size())
                {
                    ++audioPage; audioPlayhead = 0.0; engine.reset(); notifyPage(audioPage);
                }
                else
                {
                    audioPlayhead = pg.durationSec;
                    isPlaying.store(false); wantsStop.store(false);
                    playheadAtEnd.store(true, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
                    juce::Component::SafePointer<MainComponent> sp(this);
                    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
                    return;
                }
            }
            continue;
        }

        const double remain = reverse ? audioPlayhead : (pg.durationSec - audioPlayhead);
        int chunk = (int) std::floor(remain * sr / std::abs(rate));
        if (chunk < 1) chunk = 1;
        chunk = juce::jlimit(1, n - offset, chunk);

        engine.render(pg, audioPlayhead, snap.waveforms, snap.envelopes, snap.slots,
                      SL + offset, (SR != nullptr ? SR + offset : nullptr), chunk, 1.0f, rate);

        if (! snap.loopPage && ((reverse && audioPage == 0) || (! reverse && audioPage + 1 >= (int) snap.pages.size())))
        {
            for (int k = 0; k < chunk; ++k)
            {
                const double tEnd = reverse
                    ? (audioPlayhead + (double) k / sr)
                    : (pg.durationSec - (audioPlayhead + (double) k / sr));
                const float g = (float) upic::stopFadeGain(tEnd, kStopFadeSec);
                SL[offset + k] *= g;
                SR[offset + k] *= g;
            }
        }

        audioPlayhead += (double) chunk * rate / sr;
        offset += chunk;
    }
    publishPlayhead();
}

void MainComponent::releaseResources()
{
    // v1.18: keep the live snapshot across a device/host restart - it is
    // immutable and valid; only the destructor hands it back (after the audio
    // thread has been shut down). Retiring it here would leave a restarted
    // host with no model until the 30 Hz timer ticks.
}

//==============================================================================
// v1.11 VST3 plugin mode: the host's input bus count replaces the device input
// count. The process buffer itself follows the same in-place convention as a
// device callback buffer (channel k carries input k on entry), so the live tap
// below needs no special case.
void MainComponent::setPluginInputChannelCount(int numChannels)
{
    if (! pluginMode_) return;
    numLiveInputs = juce::jlimit(0, 2, numChannels);
}

//==============================================================================
// v1.4 live input: input tap (audio thread, lock-free) + capture lifecycle.
// The tap copies the player buffer's input channels to inScratch BEFORE the
// clear, and advances the capture ring. Slot install happens on the message
// thread via handoffCapture -> finalizeCapture (same SafePointer pattern as
// the transport callbacks above).
void MainComponent::updateRevBuffers()
{
    // The message thread only *asks*: the duration becomes a frame count the
    // audio thread reads, and the restart flag makes it reset its state. The
    // buffers themselves are allocated once (at the maximum duration), so
    // nothing is reallocated while the audio thread may be reading them.
    revBufFrames.store(juce::jmax(1024, (int) std::lround(revBufSec * audioSampleRate)));
    revRestart.store(true, std::memory_order_relaxed);
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
            r.info = juce::String ((int) std::lround (lo)) + "-" + juce::String ((int) std::lround (hi)) + " Hz   "
                   + juce::String (a.startTime(), 1) + "-" + juce::String (a.endTime(), 1) + "s   p"
                   + juce::String (f.first + 1);
            rows.push_back (r);
        }
    }

    arcListPanel.setRows (rows);
    arcListPanel.setSelectedArcs (currentPage, score.getSelectedArcs());
}

//==============================================================================
// v1.23 (wave 20): the MIDI input foundation - the learn and the parameters
// the MIDI drives. Everything here runs on the message thread (the MIDI
// callback only publishes the message).
void MainComponent::handleMidi()
{
    const auto a = midi.pollActivity();
    if (! a.valid) return;

    // the activity monitor
    juce::String what;
    if (a.type == MidiHub::kTypeCc)        what = "CC " + juce::String (a.number);
    else if (a.type == MidiHub::kTypeNoteOn)  what = "note " + juce::String (a.number) + " on";
    else if (a.type == MidiHub::kTypeNoteOff) what = "note " + juce::String (a.number) + " off";
    else                                   what = "MIDI";
    midiActLab.setText (what + " = " + juce::String (a.value)
                        + "   ch " + juce::String (a.channel + 1)
                        + (midi.isOpen() ? "   " + midi.openDeviceName() : juce::String()),
                        juce::dontSendNotification);

    // the learn: the next control message binds to the target
    if (midi.isLearning())
    {
        const bool isCc = (a.type == MidiHub::kTypeCc);
        const bool isNote = (a.type == MidiHub::kTypeNoteOn);
        if (isCc || isNote)
        {
            const auto target = midi.learningTarget();
            MidiHub::Binding b;
            b.bound = true;
            b.note = isNote;
            b.number = a.number;
            b.channel = a.channel;
            midi.setBinding (target, b);
            midi.cancelLearn();
            flash ("Learned " + juce::String (isNote ? "note " : "CC ")
                   + juce::String (a.number) + " -> "
                   + (target == MidiHub::Target::masterGain ? "Master gain" : target == MidiHub::Target::drawIntensity ? "Draw intensity" : target == MidiHub::Target::scrub ? "Scrub" : "Transport"));
        }
        // anything else (clock, pitch bend, ...) is ignored: the learn waits
        // for a CC or a note, and the button cancels it
    }
    updateMidiLabels();

    // the bound parameters follow the MIDI (the MIDI wins while it is bound)
    const auto bg = midi.binding (MidiHub::Target::masterGain);
    if (midi.matches (bg, a))
    {
        const float v = MidiHub::normalised (a);
        masterGain.store (v);
        masterSlider.setValue (v, juce::dontSendNotification);
    }
    const auto bi = midi.binding (MidiHub::Target::drawIntensity);
    if (midi.matches (bi, a))
    {
        const float v = MidiHub::normalised (a);
        drawIntensity = v;
        intenSlider.setValue (v, juce::dontSendNotification);
        pushVoice();
    }
    const auto bs = midi.binding (MidiHub::Target::scrub);
    if (midi.matches (bs, a))
    {
        // relative jog: centre (64) = no motion; values above = positive,
        // below = negative. Scale to +/- 4x.
        int v = a.value - 64;
        double vel = (double) v / 16.0; // 64 -> 4.0
        if (vel > 4.0) vel = 4.0;
        if (vel < -4.0) vel = -4.0;
        scrubTarget = vel;
        scrubActive = true;
    }
    // transport binding: a learned note/CC toggles play/stop
    const auto bt = midi.binding (MidiHub::Target::transport);
    if (midi.matches (bt, a))
    {
        if (a.type == MidiHub::kTypeNoteOn || a.value > 63)
        {
            if (isPlaying.load (std::memory_order_relaxed)) stopPlayback();
            else startPlayback();
        }
    }
    else if (! bt.bound)
    {
        // no transport binding: fall back to any note (the original behaviour)
        if (a.type == MidiHub::kTypeNoteOn)
        {
            if (! isPlaying.load (std::memory_order_relaxed)) startPlayback();
        }
        else if (a.type == MidiHub::kTypeNoteOff)
        {
            if (isPlaying.load (std::memory_order_relaxed)) stopPlayback();
        }
    }
}

void MainComponent::injectMidiMessage (const juce::MidiMessage& m)
{
    int type = MidiHub::kTypeOther, number = 0, value = 0;
    if (m.isController()) { type = MidiHub::kTypeCc; number = m.getControllerNumber(); value = m.getControllerValue(); }
    else if (m.isNoteOn())  { type = MidiHub::kTypeNoteOn; number = m.getNoteNumber(); value = (int) m.getVelocity(); }
    else if (m.isNoteOff()) { type = MidiHub::kTypeNoteOff; number = m.getNoteNumber(); value = 0; }
    midi.injectForTest (type, m.getChannel(), number, value);
}

void MainComponent::updateMidiLabels()
{
    auto describe = [this] (MidiHub::Target t) -> juce::String
    {
        if (midi.isLearning() && midi.learningTarget() == t)
            return "learning...";
        const auto b = midi.binding (t);
        if (! b.bound) return "-";
        return (b.note ? "note " : "CC ") + juce::String (b.number)
               + " ch " + juce::String (b.channel + 1);
    };
    midiBindGainLab.setText (describe (MidiHub::Target::masterGain), juce::dontSendNotification);
    midiBindIntLab.setText (describe (MidiHub::Target::drawIntensity), juce::dontSendNotification);
    midiBindScrubLab.setText (describe (MidiHub::Target::scrub), juce::dontSendNotification);
    midiBindTransportLab.setText (describe (MidiHub::Target::transport), juce::dontSendNotification);
    midiLearnGain.setButtonText (midi.isLearning() && midi.learningTarget() == MidiHub::Target::masterGain
                                   ? "Cancel" : "Learn");
    midiLearnInt.setButtonText (midi.isLearning() && midi.learningTarget() == MidiHub::Target::drawIntensity ? "Cancel" : "Learn");
    midiLearnScrub.setButtonText (midi.isLearning() && midi.learningTarget() == MidiHub::Target::scrub ? "Cancel" : "Learn");
    midiLearnTransport.setButtonText (midi.isLearning() && midi.learningTarget() == MidiHub::Target::transport ? "Cancel" : "Learn");
}

void MainComponent::refreshMidiDevices()
{
    // the list is rebuilt when the overlay opens, so a device plugged in
    // after the app started shows up; the open device stays selected
    const juce::String current = midi.openDeviceName();
    midiDevBox.clear (juce::dontSendNotification);
    midiDevBox.addItem ("None", 1);
    const auto names = MidiHub::deviceNames();
    int selected = 1;
    for (int i = 0; i < names.size(); ++i)
    {
        midiDevBox.addItem (names[i], i + 2);
        if (names[i] == current) selected = i + 2;
    }
    midiDevBox.setSelectedId (selected, juce::dontSendNotification);
    if (midi.isOpen() && selected == 1)
        midi.close();   // the device disappeared
}

bool MainComponent::tapLiveInput(const juce::AudioSourceChannelInfo& info)
{
    if (capAbortPending.exchange(false)) // device restarted mid-take: drop it
    {
        capDiscard = true;
        capStopReason = 4;
        capStopRequested = true;
    }

    const int nIn = juce::jmin(2, numLiveInputs.load());
    if (nIn <= 0)
    {
        liveTapActive = false; // mode-2 live arcs go silent without input
        if (capturing.load()) { capStopReason = 3; capStopRequested = true; }
        if (capStopRequested.load()) handoffCapture();
        return false;
    }

    auto* buf = info.buffer;
    if (buf->getNumChannels() < 1 || info.numSamples <= 0)
    {
        liveTapActive = false;
        return monitoring.load();
    }

    const int n = info.numSamples;
    if (inScratch.getNumChannels() < 2 || inScratch.getNumSamples() < n)
        inScratch.setSize(2, n, false, false, true);
    const float* in0 = buf->getReadPointer(0, info.startSample);
    const float* in1 = (nIn > 1 && buf->getNumChannels() > 1)
                           ? buf->getReadPointer(1, info.startSample) : in0;
    inScratch.copyFrom(0, 0, in0, n);
    inScratch.copyFrom(1, 0, in1, n);

    // v1.5: feed the live ring (mode-2 arcs read it behind this write head)
    if (liveRingSize > 0)
    {
        int at = (int) (liveWritePos % (long long) liveRingSize);
        int off = 0, rem = n;
        const float* s0 = inScratch.getReadPointer(0);
        const float* s1 = inScratch.getReadPointer(1);
        while (rem > 0)
        {
            const int w = juce::jmin(rem, liveRingSize - at);
            std::copy(s0 + off, s0 + off + w, liveRingA.begin() + at);
            std::copy(s1 + off, s1 + off + w, liveRingB.begin() + at);
            off += w; rem -= w; at += w;
            if (at >= liveRingSize) at = 0;
        }
        liveWritePos += n;
        liveTapActive = true;
    }
    else
    {
        liveTapActive = false;
    }

    // v1.20 (wave 18): the live reverse capture. The write head moves
    // BACKWARDS, so the buffer holds the last D seconds mirrored in time:
    // reading it forward plays the input backwards - the same trick as the
    // sample path, and the shifter needs no change at all.
    if (revLiveOn.load(std::memory_order_relaxed))
    {
        const int D = revBufFrames.load(std::memory_order_relaxed);
        if (revRestart.exchange(false, std::memory_order_relaxed))
        {
            // the message thread asked for a fresh start (or a new duration)
            revCapIdx = 0; revPlayIdx = 1;
            revCapCount = 0;
            revCapHead = D - 1;
            // the playback starts after the shifter's lag, so the first read
            // is already inside the segment (every live shifter reads behind
            // the head)
            revPlayHead = juce::jmin(D - 1, (int) std::lround(0.25 * audioSampleRate));
        }
        if (D > 0 && revBuf[revCapIdx].getNumSamples() >= D)
        {
            auto& cap = revBuf[revCapIdx];
            const float* s0 = inScratch.getReadPointer(0);
            const float* s1 = inScratch.getReadPointer(1);
            const int fadeN = juce::jmax(1, (int) std::lround(0.005 * audioSampleRate));
            for (int i = 0; i < n; ++i)
            {
                revCapHead = (revCapHead - 1 + D) % D;
                float g = 1.0f;
                if (revCapCount < fadeN)
                    g = (float) revCapCount / (float) fadeN;              // oldest end: in
                else if (revCapCount >= D - fadeN)
                    g = (float) (D - revCapCount) / (float) fadeN;        // newest end: out
                cap.setSample(0, revCapHead, s0[i] * g);
                cap.setSample(1, revCapHead, s1[i] * g);
                ++revCapCount;
                if (revCapCount >= D)
                {
                    // the segment is complete: it becomes the playback and
                    // the buffer that was playing becomes the new capture
                    // (nothing is copied, so there is no spike in the audio
                    // thread)
                    revPlayIdx = revCapIdx;
                    revPlayHead = juce::jmin(D - 1, (int) std::lround(0.25 * audioSampleRate));
                    revCapIdx = 1 - revCapIdx;
                    revCapHead = D - 1;
                    revCapCount = 0;
                    engine.resetLiveVoices(); // the live voices re-anchor
                }
            }
            // the playback head runs at 1x and stops at the segment's end
            revPlayHead = juce::jmin(D, revPlayHead + n);
        }
    }

    if (capturing.load() && capMax > 0)
    {
        const int at = capPos.load();
        const int w = juce::jmin(n, juce::jmax(0, capMax - at));
        if (w > 0)
        {
            capBuf.copyFrom(0, at, inScratch, 0, 0, w);
            capBuf.copyFrom(1, at, inScratch, 1, 0, w);
            capPos.store(at + w);
        }
        if (capPos.load() >= capMax) { capStopReason = 2; capStopRequested = true; }
    }

    if (capStopRequested.load()) handoffCapture();
    return monitoring.load();
}

void MainComponent::handoffCapture()
{
    capturing = false;
    capStopRequested = false;
    capFinalCount = capPos.load();
    capFinalReason = capStopReason.exchange(0);
    capReady = true;
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->finalizeCapture(); });
}

void MainComponent::requestCaptureStop(int reason)
{
    if (! capturing.load()) return;
    capStopReason = reason;
    capStopRequested = true;
}

void MainComponent::startCapture()
{
    if (capturing.load()) return;
    const double rate = audioSampleRate > 0.0 ? audioSampleRate : 44100.0;
    const int maxFrames = (int) juce::jmin((juce::int64) (1LL << 28),
                                           (juce::int64) (kMaxCaptureSec * rate));
    if (maxFrames < 64)
    {
        flash("Cannot capture at this sample rate");
        capBtn.setToggleState(false, juce::dontSendNotification);
        return;
    }
    capBuf.setSize(2, maxFrames);
    capBuf.clear();
    capRate = rate;
    capMax = maxFrames;
    capPos = 0;
    capFinalCount = 0;
    capDiscard = false;
    capStopReason = 0;
    capFinalReason = 0;
    capturing = true;
    capBtn.setButtonText("Stop");
}

void MainComponent::finalizeCapture()
{
    if (! capReady.exchange(false)) return;
    capBtn.setToggleState(false, juce::dontSendNotification);
    capBtn.setButtonText("Capture");
    repaint();

    const int reason = capFinalReason;
    if (capDiscard.exchange(false) || capFinalCount < 64)
    {
        flash(reason == 4 ? "Take discarded (audio device changed)"
                          : "Take too short - discarded");
        return;
    }

    juce::AudioBuffer<float> take(2, capFinalCount);
    take.copyFrom(0, 0, capBuf, 0, 0, capFinalCount);
    take.copyFrom(1, 0, capBuf, 1, 0, capFinalCount);

    const int slotIndex = juce::jlimit(0, (int) sampleSlots.size() - 1, selSlot);
    const int sid = sampleSlots[(size_t) slotIndex].id;
    auto slot = upic::slotFromCapture(sid, takeName(), take, capRate);

    {
        // model is read by the audio thread -> mutate under the lock
        juce::ScopedLock l(dataLock);
        // the capture replaces the material, not the slot: the reverse flag
        // (v1.19) belongs to the slot, so it survives
        const bool wasReversed = sampleSlots[(size_t) slotIndex].reversed;
        sampleSlots[(size_t) slotIndex] = slot;
        sampleSlots[(size_t) slotIndex].reversed = wasReversed;
        markModelChanged(); // v1.15: new material -> fade across it
        publishModelSnapshot(); // v1.18: the next block renders the take
    }
    refreshBoxes();
    updateSampleView();
    pushVoice();
    repaint();

    const double secs = ((double) capFinalCount) / juce::jmax(1.0, capRate);
    juce::String msg = "Captured " + juce::String(secs, 1) + "s into slot " + juce::String(sid);
    if (reason == 2) msg += " (take full)";
    else if (reason == 3) msg += " (input lost)";
    flash(msg);
}

juce::String MainComponent::takeName()
{
    return "Take " + juce::Time::getCurrentTime().formatted("%H%M%S");
}

void MainComponent::ensureLiveInputFor(std::function<void(bool)> done)
{
    if (inputEnabled && numLiveInputs.load() > 0) { done(true); return; }
    requestLiveInput(std::move(done));
}

// v1.8: the one tab strip above the canvas - each material (Osc / Sample /
// Live) is a tab and its own canvas layer on the same page + timeline, so
// the tabs double as the old Score / Live pitch layer switch.
void MainComponent::setMaterial(int m)
{
    drawMatMode = juce::jlimit(0, 3, m);
    score.setLayer(drawMatMode);
    syncMaterialTabs();
    matHintLab.setText (drawMatMode == 0 ? "Osc: draw arcs on the wavetable | Wave = carrier, Env = amplitude"
                      : drawMatMode == 1 ? "Sample: Pitch sets how arcs transpose the slot | Ref = unison"
                      : drawMatMode == 2 ? "Live: arcs multiply the input pitch | 1x = input, 2x = octave up"
                      : "PM: Ratio x Index into the carrier phase | IdxEnv shapes the modulation index",
                      juce::dontSendNotification);
    resized();
    updateSampleView();
    pushVoice();
    // The mic is opened lazily, when it is actually needed: Mon/Capture on
    // the Sample tab, or Play with live arcs on the page. Opening the device
    // here blocked the message thread for seconds on the first Live-tab
    // click (the input is a device restart), which read as "the UI lags
    // when I click Live".
}

void MainComponent::syncMaterialTabs()
{
    matOscTog.setToggleState(drawMatMode == 0, juce::dontSendNotification);
    matSampleTog.setToggleState(drawMatMode == 1, juce::dontSendNotification);
    matLiveTog.setToggleState(drawMatMode == 2, juce::dontSendNotification);
    matPmTog.setToggleState (drawMatMode == 3, juce::dontSendNotification);
}

void MainComponent::requestLiveInput(std::function<void(bool)> done)
{
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::RuntimePermissions::Callback cont = [sp, done](bool granted){
        if (sp == nullptr) return;
        if (! granted) { done(false); return; }
        done(sp->enableDeviceInputs());
    };
#if JUCE_ANDROID
    // RECORD_AUDIO is dangerous: the system dialog is async.
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, cont);
#else
    cont(true);
#endif
}

bool MainComponent::enableDeviceInputs()
{
    if (pluginMode_)
    {
        // no device to open here: the host's input bus is the live input
        const int nIn = numLiveInputs.load();
        inputEnabled = nIn > 0;
        if (! inputEnabled)
            flash("No host input: live arcs will be silent");
        return inputEnabled;
    }
    setAudioChannels(2, 2);
    int nIn = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        nIn = dev->getActiveInputChannels().countNumberOfSetBits();
    numLiveInputs = nIn;
    if (nIn > 0) { inputEnabled = true; return true; }
    setAudioChannels(0, 2); // restore output-only
    inputEnabled = false;
    flash("No audio input channels on this device");
    return false;
}

//==============================================================================
// v1.16: the latency the host has to compensate for the current page.
//
// The score paths (osc, sample varispeed, sample granular) have no delay at
// all, while the live paths do: the granular reads 220 ms behind the write
// head, the tape shifter 40-200 ms (its re-anchor window) and the phase
// vocoder a 1024-sample prime. The host can only compensate ONE number for
// the whole plugin, so:
//   - if the page mixes a zero-delay path with a delayed one, nothing is
//     reported (0) - the score must stay sample-accurate against the
//     project, and a single number could not satisfy both paths anyway;
//   - if every audible path is delayed, the largest delay is reported, so
//     the host can align the plugin with the input it came from.
int MainComponent::liveLatencySamples() const
{
    const auto& pg = curPage();
    int maxDelayed = 0;
    bool anyZeroDelay = false;
    bool anyArc = false;

    for (const auto& a : pg.arcs)
    {
        if (a.muted || a.points.size() < 2) continue;
        anyArc = true;

        double sec = 0.0;
        if (a.mode == 2) // live input
        {
            switch (engine.getLivePitchMode())
            {
                case 0:  sec = upic::GrainShifter::kLagSec; break;   // 220 ms
                case 1:  sec = 0.5 * (upic::TapeShifter::kLagMinSec
                                    + upic::TapeShifter::kLagMaxSec); break; // 120 ms (the window's middle)
                default: sec = (double) upic::VocoderCore::kPrime / audioSampleRate; break; // ~21 ms
            }
        }
        else if (a.mode == 1 && engine.getSamplePitchMode() == 2)
        {
            sec = (double) upic::VocoderCore::kPrime / audioSampleRate; // the sample vocoder's prime
        }

        if (sec <= 0.0) anyZeroDelay = true;
        else maxDelayed = juce::jmax(maxDelayed, (int) std::ceil(sec * audioSampleRate));
    }

    if (! anyArc || anyZeroDelay) return 0;
    return maxDelayed;
}

void MainComponent::timerCallback()
{
    ++timerTicks;
    // Refresh the cached input count (device/add-remove happens here on the
    // message thread; the audio tap only reads the atomic). In plugin mode the
    // processor keeps it fresh from the host's input bus instead.
    if (! pluginMode_)
    {
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            numLiveInputs = dev->getActiveInputChannels().countNumberOfSetBits();
        else
            numLiveInputs = 0;
    }

    juce::ScopedLock l(dataLock);
    // v1.21: the arc list follows the model (the rows are rebuilt only when
    // something changed - not on every timer tick)
    if (arcListDirty) rebuildArcList();
    // the panel's selection follows the canvas's (multi-selection included)
    {
        const auto sel = score.getSelectedArcs();
        if (sel != lastArcSelection)
        {
            lastArcSelection = sel;
            selectedArcId = sel.empty() ? -1 : sel.front();
            arcListPanel.setSelectedArcs (currentPage, sel);
        }
    }
    // v1.23: the MIDI input - the message thread picks up what arrived
    handleMidi();
    // v1.25 (wave 22): scrub smoothing. The jog target decays; the smoothed
    // velocity drives the playhead. Hysteresis keeps a resting jog at zero.
    {
        scrubVelocity = scrubVelocity * kScrubSmoothOld + scrubTarget * kScrubSmoothNew;
        scrubTarget *= kScrubTargetDecay;
        if (std::abs(scrubVelocity) < kScrubHysteresis) scrubVelocity = 0.0;
        if (std::abs(scrubTarget)  < (kScrubHysteresis * 0.4)) scrubTarget  = 0.0;
        if (scrubActive)
        {
            const double v = juce::jlimit(-4.0, 4.0, scrubVelocity);
            if (std::abs(v) > 0.05)
            {
                if (! isPlaying.load(std::memory_order_relaxed))
                {
                    prevPlayRate.store(playRate.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    startPlayback();
                }
                if (v != playRate.load(std::memory_order_relaxed))
                {
                    prevPlayRate.store(playRate.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    playRate.store(v, std::memory_order_relaxed);
                    dirChangeSerial.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else if (isPlaying.load(std::memory_order_relaxed))
            {
                scrubActive = false;
                stopPlayback();
            }
        }
    }
    // v1.18 (wave 15): publish the immutable model the audio thread renders
    // from. Unconditional on every tick - there is no "publish on change"
    // path that could be forgotten.
    if (!(muteWhileDragging && arcEditing.load(std::memory_order_relaxed)))
        publishModelSnapshot();
    const double dur = curPage().durationSec;
    const bool revNow = playRate.load(std::memory_order_relaxed) < 0.0;
    timeLab.setText((revNow ? juce::String("REV ") : juce::String()) + juce::String(displayPlayhead(), 1) + " / " + juce::String(dur, 1) + " s",
                    juce::dontSendNotification);

    // v1.16: the latency the host has to compensate for this page (0 = the
    // score is sample-accurate). Shown in the status line so the delay is
    // never a surprise.
    const int latSamples = liveLatencySamples();

    juce::String s;
    if (juce::Time::getMillisecondCounter() < flashUntilMs)
    {
        s = flashMsg; // transient feedback (file dialogs overwrite status otherwise)
    }
    else
    {
        s = "Page " + juce::String(currentPage + 1) + "/" + juce::String((int) pages.size())
          + " | " + juce::String((int) curPage().arcs.size()) + " arcs"
          + (curPage().zones.empty() ? juce::String()
             : " | " + juce::String((int) curPage().zones.size()) + " zone(s)");
        if (seqPages) s += " | SEQ";
        if (recordPressure) s += " | PRESS";
        if (timeGrid > 0) s += " | GRID " + juce::String(timeGrid, 0) + "/s";
        if (stickiness < 0.999) s += " | STICK " + juce::String((int) std::round(stickiness * 100)) + "%";
        if (numLiveInputs.load() > 0) s += " | IN";
        if (latSamples > 0)
            s += " | LAT " + juce::String((int) std::round(latSamples / audioSampleRate * 1000.0)) + "ms";
        if (monitoring.load()) s += " | MON";
        if (capturing.load())
            s += " | CAP " + juce::String((double) capPos.load() / juce::jmax(1.0, capRate), 1) + "s";
        if (sieveOn)
        {
            s += " | SIEVE m" + juce::String(sieveM) + " {";
            for (size_t i = 0; i < sieveR.size(); ++i)
                s += (i > 0 ? "," : "") + juce::String(sieveR[i]);
            s += "}";
        }
        s += "  --  ";
        int sel = score.getSelectedArc();
        const int nsel = (int) score.getSelectedArcs().size();
        if (sel >= 0)
        {
            for (auto& a : curPage().arcs) if (a.id == sel)
            {
                s += "Sel #" + juce::String(a.id);
                if (nsel > 1) s += " (+" + juce::String(nsel - 1) + " more)";
                if (a.mode == 1)
                    s += "  [SAMPLE s" + juce::String(a.sampleId) + " ref " + juce::String(a.refFreq, 0) + "Hz]";
                else
                    s += "  v" + juce::String(a.waveformId);
                s += " e" + juce::String(a.envelopeId) + "  int " + juce::String(a.intensity, 2)
                   + (a.muted ? " MUTED" : "")
                   + " [" + juce::String(a.startTime(), 1) + "-" + juce::String(a.endTime(), 1) + "s]";
                break;
            }
        }
        else s += "draw curves; assign voice per arc.";
    }
    statusLab.setText(s, juce::dontSendNotification);
    score.repaint();

    // v1.16: publish the latency the host has to compensate. The host can
    // only take one number, and it must not change per block - so it is
    // published from here (the message thread) when the page or the pitch
    // mode changed.
    publishLatency();
}

void MainComponent::publishLatency()
{
    if (! onLatencyChanged) return;
    const int lat = liveLatencySamples();
    if (lat != publishedLatency)
    {
        publishedLatency = lat;
        onLatencyChanged(lat);
    }
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0b0b0e));
    // one subtle card per toolbar row, so the transport / voice / material /
    // groove groups read as separate blocks (each with a faint tint)
    static const juce::Colour fills[]   { juce::Colour (0xff1a1f2a), juce::Colour (0xff1a1a22),
                                          juce::Colour (0xff1a241d), juce::Colour (0xff241a28) };
    static const juce::Colour borders[] { juce::Colour (0xff39455c), juce::Colour (0xff3a3a4a),
                                          juce::Colour (0xff3a5a44), juce::Colour (0xff5a3a5a) };
    for (int i = 0; i < (int) toolbarGroups.size(); ++i)
    {
        auto& r = toolbarGroups[(size_t) i];
        if (r.isEmpty()) continue;
        auto rf = r.toFloat().reduced (1.0f, 2.0f);
        g.setColour (fills[i & 3]);
        g.fillRoundedRectangle (rf, 5.0f);
        g.setColour (borders[i & 3]);
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
        juce::Component* comp  = nullptr; int w = 0;      // w = 0 -> takes the row's rest
        int minSlice = 24;   // trailing flex labels wrap below this width
    };
    auto F  = [](juce::Component* c, int w)              { return FlowItem{ nullptr, 0, c, w }; };
    auto FL = [](juce::Component* l, int lw, juce::Component* c, int w)
                                                          { return FlowItem{ l, lw, c, w }; };
    // long explanation labels: wrap to their own row unless they get a real slice
    auto FH = [](juce::Component* c, int minSlice)        { return FlowItem{ nullptr, 0, c, 0, minSlice }; };

    const std::vector<std::vector<FlowItem>> rowsInit {
        // transport / pages / files
        { F(&playBtn, 90), F(&stopBtn, 70), F(&loopBtn, 64), F(&seqBtn, 60), F(&revPlayBtn, 64), F(&pressBtn, 96), F(&scrubBtn, 72),
          FL(&pageLab, 40, &pageBox, 150),
          F(&pageAddBtn, 30), F(&pageDupBtn, 46), F(&pageDelBtn, 30),
          F(&menuBtn, 74), F(&clearBtn, 64), F(&timeLab, 0) },
        // voice / view (built below: the tab decides which controls apply)
        {},
        // material (filled in below: the tab decides which material)
        {},
        // presets / sieve / stickiness / time grid / generators / hints
        { FL(&tempoLab, 40, &tempoSlider, 140),
          F(&sieveBtn, 64), F(&sievePresetBox, 150), F(&sieveMEdit, 52), F(&sieveREdit, 84),
          FL(&stickLab, 38, &stickSlider, 160),
          FL(&gridLab, 44, &gridBox, 84),
          FL(&genLab, 36, &genBox, 220),
          FH(&kbdHintLab, 220) },
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
                                           &inGainLab, &inGainSlider, &monBtn, &capBtn };
    for (auto* c : sampleCtl) c->setVisible(showSampleCtl);
    juce::Component* const liveCtl[] = { &livePitchLab, &livePitchBox,
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

    // Each tab shows only the draw/voice controls that apply to its material.
    std::vector<FlowItem> voiceRow;
    voiceRow.push_back (FL(&toolLab, 36, &toolBox, 120));
    if (showWaveCtl)
        voiceRow.push_back (FL(&waveLab, 42, &waveBox, 140));
    voiceRow.push_back (FL(&envLab, 34, &envBox, 140));
    voiceRow.push_back (FL(&intenLab, 40, &intenSlider, 180));
    voiceRow.push_back (FL(&durLab, 30, &durSlider, 160));
    voiceRow.push_back (FL(&masterLab, 52, &masterSlider, 180));
    voiceRow.push_back (F(&snapBtn, 68));
    voiceRow.push_back (F(&statusLab, 0));

    std::vector<FlowItem> materialRow;
    if (showSampleCtl)
        materialRow = { FL(&slotLab, 34, &sampleBox, 180),
                        F(&loadSampleBtn, 110), F(&revBtn, 64),
                        FL(&refLab, 34, &refSlider, 210),
                        FL(&samplePitchLab, 34, &samplePitchBox, 180),
                        FL(&grainLab, 40, &grainSlider, 100),
                        FL(&inGainLab, 30, &inGainSlider, 140), F(&monBtn, 64), F(&capBtn, 84) };
    else if (showLiveCtl)
        materialRow = { FL(&refLab, 34, &refSlider, 210),
                        FL(&livePitchLab, 42, &livePitchBox, 180),
                        FL(&liveRevLab, 52, &liveRevLenBox, 90),
                        F(&liveRevBtn, 64) };
    else if (showPmCtl)
        materialRow = { FL(&pmOpsLab, 34, &pmOpsBox, 80),
                        FL(&pmPresetLab, 44, &pmPresetBox, 180),
                        FL(&pmRatioLab, 40, &pmRatioSlider, 150),
                        FL(&pmIndexLab, 40, &pmIndexSlider, 150),
                        FL(&pmIndexEnvLab, 46, &pmIndexEnvBox, 120),
                        FL(&pmFeedbackLab, 42, &pmFeedbackSlider, 130),
                        F(&pmImportBtn, 64), F(&pmExportBtn, 64) };
    // the material hint is always there; the sieve group sits with the
    // sieve preset on the next row (v1.25)
    materialRow.push_back(FH(&matHintLab, 220));
    std::vector<std::vector<FlowItem>> rows = rowsInit;
    rows[1] = voiceRow;
    rows[2] = materialRow;
    rows.push_back({ FL(&scrubLenLab, 60, &scrubLenSlider, 130), FL(&scrubCrossLab, 70, &scrubCrossSlider, 130), FL(&scrubTailLab, 60, &scrubTailSlider, 130), FL(&scrubGrainLab, 70, &scrubGrainSlider, 110) });
    rows.push_back({ F(&muteDragBtn, 90), FL(&dragSmoothLab, 70, &dragSmoothSlider, 130) });
    // the Hints view toggle drops the explanation labels (last of their rows)
    if (! showHints)
    {
        rows[2].pop_back();
        rows[3].pop_back();
    }
    if (pluginMode_) // host audio: no device selector button in the flow
        rows[0].erase(std::remove_if(rows[0].begin(), rows[0].end(),
                                     [this](const FlowItem& it){ return it.comp == &audioBtn; }),
                      rows[0].end());
    matHintLab.setVisible(showHints);
    kbdHintLab.setVisible(showHints);

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

    auto rowsUsed = [&](const std::vector<FlowItem>& items, int availW, float s)
    {
        int n = 1, x = 0;
        for (auto& it : items)
        {
            const int w = (it.label != nullptr ? (int) std::lround(it.labelW * s) : 0)
                        + (int) std::lround(it.w * s);
            if (it.label == nullptr && it.w == 0)
            {
                // trailing flex label: it wraps unless it can keep at least
                // its minimum slice of this row (must agree with placeFlow)
                if (availW - x < it.minSlice) ++n;
                continue;
            }
            if (x > 0 && x + w > availW) { ++n; x = 0; }
            x += w;
        }
        return n;
    };

    // fit-shrink: wrapping is scale-dependent, so settle by iteration
    float s = 1.0f;
    int topRowsTotal = 0;
    for (int iter = 0; ; ++iter)
    {
        topRowsTotal = 0;
        for (auto& r : rows) topRowsTotal += rowsUsed(r, b.getWidth(), s);
        const int minH = topRowsTotal * (int) std::lround(rowH36 * s) + 6
                       + (int) std::lround(minScoreH * s)
                       + (int) std::lround(26.0f * s) // Osc|Sample|Live tab strip
                       + (anyEditors ? (int) std::lround(minEdH * s) + 6 : 0)
                       + (stripMode ? (int) std::lround(34.0f * s) : 0);
        if (minH <= b.getHeight() || iter >= 8) break;
        s *= 0.92f;
        if (s < 0.55f) { s = 0.55f; }
    }

    auto placeFlow = [&](juce::Rectangle<int> area, const std::vector<FlowItem>& items, float s)
    {
        const int rh2 = (int) std::lround(rowH36 * s);
        const int g2  = juce::jmax(1, (int) std::lround(2.0f * s));
        int x = area.getX(), y = area.getY();
        const int right = area.getRight();
        for (auto& it : items)
        {
            const int lw = it.label != nullptr ? (int) std::lround(it.labelW * s) : 0;
            const int cw = it.w > 0 ? (int) std::lround(it.w * s) : 0;
            if (x > area.getX() && x + lw + cw > right) { x = area.getX(); y += rh2; }
            if (it.label != nullptr)
            {
                it.label->setBounds(x + g2, y + g2, juce::jmax(1, lw - 2 * g2), rh2 - 2 * g2);
                x += lw;
            }
            if (cw > 0)
            {
                it.comp->setBounds(x + g2, y + g2, juce::jmax(1, cw - 2 * g2), rh2 - 2 * g2);
            }
            else
            {
                // trailing flex label (time/status/hints): wrap unless it
                // keeps a decent slice of this row β€” see rowsUsed
                if (right - x - g2 < it.minSlice) { x = area.getX(); y += rh2; }
                it.comp->setBounds(x + g2, y + g2, juce::jmax(8, right - x - g2), rh2 - 2 * g2);
            }
            x += cw;
        }
    };

    // top toolbar block: one wrapped flow per logical row
    {
        auto top = b.removeFromTop (topRowsTotal * (int) std::lround (rowH36 * s) + 6);
        toolbarGroups.clear();
        for (auto& r : rows)
        {
            const int n = rowsUsed(r, top.getWidth(), s);
            auto rowRect = top.removeFromTop(n * (int) std::lround(rowH36 * s));
            toolbarGroups.push_back(rowRect);
            placeFlow(rowRect, r, s);
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

    // v1.7 audio settings overlay: full-window backdrop + centred card
    audioOverlay.setBounds(getLocalBounds());
    if (audioSelector != nullptr && audioOverlay.isVisible())
    {
        auto card = audioOverlay.cardRect().reduced(12);
        card.removeFromTop(AudioOverlay::titleH);
        // v1.23: the MIDI section (the device, the monitor, the learn)
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
            card.removeFromTop (6);
        }
        // v1.21: the live pitch algorithm and the live reverse moved to the
        // material row - the overlay is for the device only now
        card.removeFromTop(6);
        auto btnRow = card.removeFromBottom(40);
        card.removeFromBottom(6);
        audioOkBtn.setBounds(btnRow.removeFromRight(120).reduced(2));
        audioSelector->setBounds(card);
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

//==============================================================================
juce::var MainComponent::pieceToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "UPICuitous");
    o->setProperty("version", 1);
    o->setProperty("master", masterGain.load());
    o->setProperty("currentPage", currentPage);
    // v1.1 (absent in older / Python-written files -> defaults applied)
    o->setProperty("seqPages", seqPages);
    o->setProperty("recordPressure", recordPressure);
    o->setProperty("scrubAudition", scrubAudition);
    o->setProperty("muteWhileDragging", muteWhileDragging.load());
    o->setProperty("dragSmoothMs", dragSmoothMs.load());
    o->setProperty("scrubGrainMs", scrubGrainMs);
    o->setProperty("sampleGrainMs", sampleGrainMs);
    o->setProperty("sieveOn", sieveOn);
    o->setProperty("sieveM", sieveM);
    juce::Array<juce::var> srArr;
    for (int r : sieveR) srArr.add(r);
    o->setProperty("sieveR", srArr);
    // v1.2 (same rule: defaults keep old files working unchanged)
    o->setProperty("stickiness", stickiness);
    o->setProperty("timeGrid", timeGrid);
    juce::Array<juce::var> wa, ea, pa, sa;
    for (auto& w : waveforms) wa.add(w.toVar());
    for (auto& e : envelopes) ea.add(e.toVar());
    for (auto& s : sampleSlots) if (s.loaded()) sa.add(s.toVar());
    for (auto& p : pages) pa.add(p.toVar());
    o->setProperty("waveforms", wa);
    o->setProperty("envelopes", ea);
    o->setProperty("samples", sa);
    o->setProperty("pages", pa);
    return juce::var(o);
}

void MainComponent::loadFromVar(const juce::var& v)
{
    juce::ScopedLock l(dataLock);
    stopPlayback();
    // stopPlayback only starts a fade while playing: make the stop immediate
    // here, since the piece (and possibly its pages) is about to be replaced
    isPlaying.store(false);
    wantsStop.store(false);
    waveforms.clear(); envelopes.clear(); pages.clear();
    for (auto& s : sampleSlots) { s.name = juce::String(); s.data.reset(); s.sampleRate = 44100.0; }
    if (auto* arr = v.getProperty("samples", juce::var()).getArray())
    {
        for (auto& sv : *arr)
        {
            auto slot = upic::SampleSlot::fromVar(sv, 1);
            for (auto& s : sampleSlots)
                if (s.id == slot.id) { s = std::move(slot); break; }
        }
    }
    if (auto* arr = v.getProperty("waveforms", juce::var()).getArray())
    {
        int i = 0;
        for (auto& wv : *arr) waveforms.push_back(upic::Waveform::fromVar(wv, i++));
    }
    if (auto* arr = v.getProperty("envelopes", juce::var()).getArray())
    {
        int i = 0;
        for (auto& ev : *arr) envelopes.push_back(upic::Envelope::fromVar(ev, i++));
    }
    if (auto* arr = v.getProperty("pages", juce::var()).getArray())
        for (auto& pv : *arr) pages.push_back(upic::Page::fromVar(pv));
    if (waveforms.empty()) waveforms = upic::defaultWaveforms();
    if (envelopes.empty()) envelopes = upic::defaultEnvelopes();
    if (pages.empty()) pages.push_back(upic::Page());
    // clamp ids
    currentPage = juce::jlimit(0, (int)pages.size() - 1, (int) v.getProperty("currentPage", 0));
    masterGain.store((float)(double) v.getProperty("master", 0.8));
    markModelChanged(); // v1.15: the whole piece was replaced -> fade across it
    // v1.1 state (defaults keep old / Python-written files working unchanged)
    seqPages = (bool) v.getProperty("seqPages", false);
    recordPressure = (bool) v.getProperty("recordPressure", false);
    scrubAudition = (bool) v.getProperty("scrubAudition", true);
    muteWhileDragging.store((bool) v.getProperty("muteWhileDragging", true), std::memory_order_relaxed);
    dragSmoothMs.store(juce::jlimit(0, 1000, (int) v.getProperty("dragSmoothMs", 20)), std::memory_order_relaxed);
    scrubGrainMs = juce::jlimit(5, 500, (int) v.getProperty("scrubGrainMs", 40));
    sampleGrainMs = juce::jlimit(5, 500, (int) v.getProperty("sampleGrainMs", 40));
    sieveOn = (bool) v.getProperty("sieveOn", false);
    sieveM = juce::jlimit(2, 120, (int) v.getProperty("sieveM", 12));
    sieveR.clear();
    if (auto* arr = v.getProperty("sieveR", juce::var()).getArray())
        for (auto& rv : *arr)
            sieveR.push_back((((int) rv % sieveM) + sieveM) % sieveM);
    seqBtn.setToggleState(seqPages, juce::dontSendNotification);
    pressBtn.setToggleState(recordPressure, juce::dontSendNotification);
    scrubBtn.setToggleState(scrubAudition, juce::dontSendNotification);
    muteDragBtn.setToggleState(muteWhileDragging.load(), juce::dontSendNotification);
    dragSmoothSlider.setValue((double) dragSmoothMs.load(), juce::dontSendNotification);
    scrubGrainSlider.setValue((double) scrubGrainMs, juce::dontSendNotification);
    grainSlider.setValue((double) sampleGrainMs, juce::dontSendNotification);
    engine.setScrubGrainMs(scrubGrainMs);
    engine.setSampleGrainMs(sampleGrainMs);
    scrubEngine.setScrubGrainMs(scrubGrainMs);
    scrubEngine.setSampleGrainMs(sampleGrainMs);
    sieveBtn.setToggleState(sieveOn, juce::dontSendNotification);
    sieveMEdit.setText(juce::String(sieveM), juce::dontSendNotification);
    {
        juce::String rt;
        for (size_t i = 0; i < sieveR.size(); ++i)
            rt += (i > 0 ? "," : "") + juce::String(sieveR[i]);
        sieveREdit.setText(rt, juce::dontSendNotification);
    }
    score.setRecordPressure(recordPressure);
    score.setSieve(sieveOn, sieveM, sieveR);
    // v1.2 state (defaults: full stickiness, no time grid = v1.1 behaviour)
    stickiness = juce::jlimit(0.0, 1.0, (double) v.getProperty("stickiness", 1.0));
    timeGrid = juce::jlimit(0.0, 64.0, (double) v.getProperty("timeGrid", 0.0));
    stickSlider.setValue(stickiness, juce::dontSendNotification);
    score.setStickiness(stickiness);
    {
        static const double divs[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        int gi = 0;
        for (int i = 0; i < 7; ++i) if (std::abs(divs[i] - timeGrid) < 1e-9) gi = i;
        gridBox.setSelectedId(gi + 1, juce::dontSendNotification);
        score.setTimeGrid(timeGrid);
    }
    selWave = waveforms.front().id; selEnv = envelopes.front().id;
    selSlot = 0;
    drawMatMode = score.getLayer(); // the tab stays where it is across a load
    syncMaterialTabs();
    drawRef = upic::kDefaultRefFreq;
    refSlider.setValue(drawRef, juce::dontSendNotification);
    seekPlayhead(0.0);
    bumpPageEpoch();
    masterSlider.setValue(masterGain.load(), juce::dontSendNotification);
    refreshBoxes(); refreshScoreBinding();
    // v1.18: the next audio block must already render the loaded piece (the
    // timer alone would be up to 33 ms late - and a host may not run it).
    publishModelSnapshot();
}

juce::String MainComponent::pieceStateText()
{
    juce::ScopedLock l(dataLock);
    return juce::JSON::toString(pieceToVar(), false);
}

void MainComponent::saveFile()
{
    auto fc = std::make_shared<juce::FileChooser>("Save UPIC piece", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("piece.upic.json"), "*.json");
    fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc](const juce::FileChooser& c){
            auto f = c.getResult();
            if (f == juce::File()) return;
            juce::ScopedLock l(dataLock);
            f.replaceWithText(juce::JSON::toString(pieceToVar(), true));
        });
}

void MainComponent::loadFile()
{
    auto fc = std::make_shared<juce::FileChooser>("Load UPIC piece", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc](const juce::FileChooser& c){
            auto f = c.getResult();
            if (!f.existsAsFile()) return;
            auto txt = f.loadFileAsString();
            auto v = juce::JSON::parse(txt);
            if (v.isVoid()) return;
            loadFromVar(v);
        });
}

void MainComponent::loadSampleIntoSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= (int) sampleSlots.size()) return;

    auto fc = std::make_shared<juce::FileChooser>(
        "Load audio material (played + pitch-shifted along arcs)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.flac;*.ogg;*.aiff;*.aif");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc, slotIndex](const juce::FileChooser& c){
            auto f = c.getResult();
            if (! f.existsAsFile()) return;

            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
            if (reader == nullptr)
            {
                flash("Unsupported audio file: " + f.getFileName());
                return;
            }

            // cap at 60s per slot to keep memory / .upic.json size sane
            const double srIn = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            const int maxFrames = (int) juce::jmin((juce::int64) (60.0 * srIn), reader->lengthInSamples);
            const int ch = juce::jmax(1, juce::jmin(2, (int) reader->numChannels));
            juce::AudioBuffer<float> tmp(ch, juce::jmax(1, maxFrames));
            tmp.clear();
            reader->read(&tmp, 0, maxFrames, 0, true, ch > 1);
            reader.reset();

            // make the material loop seamlessly for the grain reader
            upic::makeSeamlessLoop(tmp, srIn);

            {
                // model is read by the audio thread -> mutate under the lock.
                // v1.18: the buffer is immutable + shared, so the pointer is
                // replaced instead of written; snapshots that still hold the
                // old pointer keep it alive until they are retired.
                juce::ScopedLock l(dataLock);
                auto& slot = sampleSlots[(size_t) slotIndex];
                slot.name = f.getFileNameWithoutExtension();
                slot.sampleRate = srIn;
                slot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(tmp));
                markModelChanged(); // v1.15: new material -> fade across it
                publishModelSnapshot(); // the next block must see it (no timer needed)
            }
            flash("Loaded \"" + f.getFileNameWithoutExtension() + "\" into slot "
                  + juce::String(sampleSlots[(size_t) slotIndex].id));
             score.sampleDurations[sampleSlots[(size_t) slotIndex].id] = sampleSlots[(size_t) slotIndex].durationSec();
             score.sampleDurations[sampleSlots[(size_t) slotIndex].id] = sampleSlots[(size_t) slotIndex].durationSec();
            selSlot = slotIndex;
            refreshBoxes();
            updateSampleView();
            pushVoice();
            repaint();
        });
}

void MainComponent::renderWav()
{
    auto fc = std::make_shared<juce::FileChooser>("Render current page to WAV", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("upic-page.wav"), "*.wav");
    fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc](const juce::FileChooser& c){
            auto f = c.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension("wav");
            double sr = 44100.0;
            if (auto* dev = deviceManager.getCurrentAudioDevice())
                if (dev->getCurrentSampleRate() > 0) sr = dev->getCurrentSampleRate();
            juce::ScopedLock l(dataLock);
            int total = (int) std::ceil(curPage().durationSec * sr);
            juce::AudioBuffer<float> buf(2, total);
            buf.clear();
            // v1.18: render with a local engine - the member engine belongs to
            // the audio thread now (dataLock no longer serialises it).
            UpicEngine offline;
            offline.prepare(sr, 8192);
            offline.renderPageOffline(curPage(), waveforms, envelopes, sampleSlots, buf, sr, masterGain.load());
            juce::WavAudioFormat fmt;
            // createOutputStream() appends on Windows: delete first (fresh file)
            f.deleteFile();
            if (auto out = f.createOutputStream())
            {
                if (auto* w = fmt.createWriterFor(out.release(), sr, (unsigned int)2, 16, {}, 0))
                {
                    w->writeFromAudioSampleBuffer(buf, 0, total);
                    delete w;
                }
            }
        });
}

void MainComponent::showAudioSettings()
{
    // v1.7: an in-app overlay instead of a DialogWindow - on Android JUCE
    // has no floating windows, so the dialog had no title bar and no close
    // button there and could never be dismissed.
    // Inputs are allowed: enabling them requests RECORD_AUDIO at runtime
    // (manifest) and falls back to output-only when denied or absent.
    if (audioSelector == nullptr)
        audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, false, false, true, false);
    refreshMidiDevices();
    audioOverlay.addAndMakeVisible(*audioSelector);
    // v1.26: keep the OK button above the selector. On some layouts the
    // selector's viewport can otherwise sit over the button and eat its click.
    audioOkBtn.toFront(false);
    audioOverlay.setBounds(getLocalBounds());
    audioOverlay.setVisible(true);
    audioOverlay.toFront(false);
    resized(); // position the selector + OK button inside the card
}

void MainComponent::closeAudioSettings()
{
    // device changes already applied live in the selector; OK / backdrop
    // tap / Esc just dismiss the overlay
    audioOverlay.setVisible(false);
    if (audioSelector != nullptr && audioSelector->getParentComponent() == &audioOverlay)
        audioOverlay.removeChildComponent(audioSelector.get());
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

void MainComponent::clearCurrentPage()
{
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
    repaint();
}
