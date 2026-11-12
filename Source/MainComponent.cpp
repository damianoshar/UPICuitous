#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
MainComponent::MainComponent(bool pluginMode)
    : pluginMode_(pluginMode)
    // v1.30 (5.3 refactor): the OSC receiver needs this component as its
    // listener, so it is constructed here rather than in place.
    , osc (*this)
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

    // v1.33.3: the app's own LookAndFeel - the toolbar's fonts follow the
    // layout's scale, so a control's text is never wrapped or cut off.
    setLookAndFeel (&appLaf);

    // v1.18 (wave 15): publish the first model snapshot before the audio
    // device starts, so the audio thread always has a model to render from
    // (it never takes dataLock any more).
    publishModelSnapshot();

    if (! pluginMode_)
        setAudioChannels(0, 2); // the VST3 wrapper gets its audio from the host
    startTimerHz(30);


    // v1.30 (5.3 refactor): the constructor is split into one method per
    //     group of controls, so each group can be read (and changed)
    //     on its own. The order is the order they appear in the window.
    buildTransportControls();
    buildViewToggles();
    buildAudioOverlay();
    buildSettingsOverlay();
    buildClearOverlay();
    buildLiveControls();
    buildCombos();
    buildToolbarControls();
    buildMaterialControls();
    buildSieveControls();
    buildStepControls();
    buildFrequencyControls();
    buildArcListPanel();
    buildMidiControls();
    buildMaterialTabs();
    buildPmControls();
    buildSampleControls();
    buildLiveInputControls();
    buildScoreArea();
    buildEditors();
    // v1.29: listen for the phone/MobMuPlat scrub. Message-loop callbacks
    // mean the playhead moves on the message thread exactly like a drag.
    // v1.30 (5.3 refactor): the receiver owns its own listeners; starting it
    // binds the endpoints (and rebinds them after the settings changed).
    startOscReceiver();

    refreshBoxes();
    refreshScoreBinding();
    updateStatus();
    setSize(1280, 800);
}

void MainComponent::buildTransportControls()
{
    // --- transport buttons ---
    addAndMakeVisible(playBtn); addAndMakeVisible(stopBtn); addAndMakeVisible(loopBtn); addAndMakeVisible(menuBtn);
    addAndMakeVisible(seqBtn); addAndMakeVisible(pressBtn); addAndMakeVisible(scrubBtn); addAndMakeVisible(revPlayBtn);
    addAndMakeVisible(saveBtn); addAndMakeVisible(loadBtn); addAndMakeVisible(exportBtn);
    addAndMakeVisible(stemsBtn);
    addAndMakeVisible(audioBtn); addAndMakeVisible(clearBtn);
    addAndMakeVisible(settingsBtn);
    settingsBtn.setName("settingsBtn");
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
}

void MainComponent::buildViewToggles()
{
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
}

void MainComponent::buildAudioOverlay()
{
    // v1.7 audio settings as an in-app overlay (Android has no closable
    // dialog windows). The selector is created on first use; OK or a tap
    // on the backdrop dismisses it.
    audioOverlay.setName("audioOverlay");
    audioOverlay.setVisible(false);
    audioOverlay.onDismiss = [this]{ closeAudioSettings(); };
    addChildComponent(audioOverlay); // stays hidden until the Audio button opens it
    audioOkBtn.onClick = [this]{ closeAudioSettings(); };
    audioOverlay.addAndMakeVisible(audioOkBtn);
}

void MainComponent::buildSettingsOverlay()
{
    // v1.30 (UPIC-54/56): MIDI and OSC get their own settings overlay; the
    // audio overlay is only the device selector now. Like the audio overlay,
    // this is in-app because Android has no closable dialog windows.
    settingsOverlay.title = "MIDI + OSC Settings";
    settingsOverlay.setName("settingsOverlay");
    settingsOverlay.setVisible(false);
    settingsOverlay.onDismiss = [this]{ closeSettings(); };
    addChildComponent(settingsOverlay);
    settingsOkBtn.setName("settingsOkBtn");
    settingsOkBtn.onClick = [this]{ closeSettings(); };
    settingsOverlay.addAndMakeVisible(settingsOkBtn);
    oscApplyBtn.setName("oscApplyBtn");
    oscApplyBtn.onClick = [this]{ applyOscSettings(); };
    settingsOverlay.addAndMakeVisible(oscApplyBtn);

    midiSectionLab.setName("midiSectionLab");
    midiSectionLab.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    midiSectionLab.setColour(juce::Label::textColourId, juce::Colour(0xffe0a040));
    oscSectionLab.setName("oscSectionLab");
    oscSectionLab.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    oscSectionLab.setColour(juce::Label::textColourId, juce::Colour(0xff64d2ff));
    settingsOverlay.addAndMakeVisible(midiSectionLab);
    settingsOverlay.addAndMakeVisible(oscSectionLab);

    oscStatusLab.setName("oscStatusLab");
    oscStatusLab.setFont(juce::Font(juce::FontOptions(12.0f)));
    oscStatusLab.setColour(juce::Label::textColourId, juce::Colour(0xff9a9aa6));
    oscStatusLab.setJustificationType(juce::Justification::centredLeft);
    settingsOverlay.addAndMakeVisible(oscStatusLab);

    oscPortLab.setName("oscPortLab");
    oscPortLab.setJustificationType(juce::Justification::centredRight);
    oscPortLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    oscPortLab.setColour(juce::Label::textColourId, juce::Colours::white);
    oscPortSlider.setName("oscPortSlider");
    oscPortSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    oscPortSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
    oscPortSlider.setNumDecimalPlacesToDisplay(0);
    oscPortSlider.setRange(1024.0, 65535.0, 1.0);
    oscPortSlider.setValue((double) osc.getPort(), juce::dontSendNotification);
    oscPortSlider.setTooltip("UDP port for /upic/scrub messages (default 9000).");
    settingsOverlay.addAndMakeVisible(oscPortLab);
    settingsOverlay.addAndMakeVisible(oscPortSlider);

    oscMcBtn.setName("oscMcBtn");
    oscMcBtn.setTooltip("Also listen on MobMuPlat's default multicast address.");
    oscMcBtn.setToggleState(osc.isMulticastEnabled(), juce::dontSendNotification);
    settingsOverlay.addAndMakeVisible(oscMcBtn);
    oscOnBtn.setName("oscOnBtn");
    oscOnBtn.setToggleState(osc.isEnabled(), juce::dontSendNotification);
    settingsOverlay.addAndMakeVisible(oscOnBtn);
    oscGroupLab.setName("oscGroupLab");
    oscGroupLab.setJustificationType(juce::Justification::centredRight);
    oscGroupLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    oscGroupLab.setColour(juce::Label::textColourId, juce::Colours::white);
    oscGroupEdit.setName("oscGroupEdit");
    oscGroupEdit.setText(osc.getMulticastGroup(), juce::dontSendNotification);
    oscGroupEdit.setInputRestrictions(32);
    oscGroupEdit.setTooltip("Multicast group (default 224.0.0.1).");
    settingsOverlay.addAndMakeVisible(oscGroupLab);
    settingsOverlay.addAndMakeVisible(oscGroupEdit);
    oscMcPortLab.setName("oscMcPortLab");
    oscMcPortLab.setJustificationType(juce::Justification::centredRight);
    oscMcPortLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    oscMcPortLab.setColour(juce::Label::textColourId, juce::Colours::white);
    oscMcPortSlider.setName("oscMcPortSlider");
    oscMcPortSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    oscMcPortSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
    oscMcPortSlider.setNumDecimalPlacesToDisplay(0);
    oscMcPortSlider.setRange(1024.0, 65535.0, 1.0);
    oscMcPortSlider.setValue((double) osc.getMulticastPort(), juce::dontSendNotification);
    oscMcPortSlider.setTooltip("Multicast port (default 54321).");
    settingsOverlay.addAndMakeVisible(oscMcPortLab);
    settingsOverlay.addAndMakeVisible(oscMcPortSlider);
}

void MainComponent::buildClearOverlay()
{
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
}

void MainComponent::buildLiveControls()
{
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
        engine.resetLiveVoices();
        scrubEngine.resetLiveVoices();
        flash(on ? "Live reverse: " + juce::String (revBufSec, 1) + " s buffer"
                 : "Live reverse off");
    };
    addAndMakeVisible(livePitchLab);
    addAndMakeVisible(livePitchBox);
    addAndMakeVisible(liveRevLab);
    addAndMakeVisible(liveRevLenBox);
    addAndMakeVisible(liveRevBtn);

    // v1.30: the live granular grain size. The SAMPLE tab has had one since
    // v1.19 (grainSlider); the LIVE tab's granular shifter used a fixed
    // 40 ms hop, so this control exposes the same 5-500 ms range.
    liveGrainLab.setName("liveGrainLab");
    liveGrainLab.setJustificationType(juce::Justification::centredRight);
    liveGrainLab.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    liveGrainLab.setColour(juce::Label::textColourId, juce::Colours::white);
    liveGrainLab.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a32));
    liveGrainSlider.setName("liveGrainSlider");
    liveGrainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    liveGrainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
    liveGrainSlider.setNumDecimalPlacesToDisplay(0);
    liveGrainSlider.setTextValueSuffix(" ms");
    liveGrainSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    liveGrainSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    liveGrainSlider.setRange(5.0, 500.0, 1.0);
    liveGrainSlider.setValue((double) liveGrainMs, juce::dontSendNotification);
    liveGrainSlider.setTooltip("Granular grain size for the LIVE tab.\n"
                               "Only affects the Granular live pitch mode.");
    liveGrainSlider.onValueChange = [this]{
        liveGrainMs = (int) liveGrainSlider.getValue();
        engine.setLiveGrainMs(liveGrainMs);
        scrubEngine.setLiveGrainMs(liveGrainMs);
    };
    addAndMakeVisible(liveGrainLab);
    addAndMakeVisible(liveGrainSlider);
    engine.setLiveGrainMs(liveGrainMs);
    scrubEngine.setLiveGrainMs(liveGrainMs);

    saveBtn.onClick = [this]{ saveFile(); };
    loadBtn.onClick = [this]{ loadFile(); };
    exportBtn.onClick = [this]{ renderWav(); };
    audioBtn.onClick = [this]{ showAudioSettings(); };
    settingsBtn.onClick = [this]{ showSettings(); };
    menuBtn.onClick = [this]{
        juce::PopupMenu m;
        m.addItem (1, "Save...");
        m.addItem (2, "Open...");
        m.addSeparator();
        m.addItem (3, "Render WAV...");
        m.addItem (4, "Render stems...");
        m.addSeparator();
        m.addItem (5, "Audio settings...");
        m.addItem (6, "MIDI + OSC settings...");
        m.addItem (7, "Clear page...");
        // v1.31 (U3): the UPIC could transform a composition. The reverse is
        // already here (the Rev button); these are the pitch transforms.
        juce::PopupMenu transform;
        transform.addItem (101, "Transpose up an octave");
        transform.addItem (102, "Transpose down an octave");
        transform.addItem (103, "Transpose up a semitone");
        transform.addItem (104, "Transpose down a semitone");
        transform.addSeparator();
        transform.addItem (105, "Invert the pitch (mirror the contour)");
        transform.addSeparator();
        transform.addItem (106, "Retrograde (mirror it in time)");
        transform.addItem (107, "Augment (stretch it in time x2)");
        transform.addItem (108, "Diminish (compress it in time x0.5)");
        m.addSubMenu ("Transform the page...", transform);
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
                    case 6: sp->showSettings(); break;
                    case 7: sp->showClearWarning(); break;
                    default: sp->transformPage (r); break;
                }
            });
    };
    clearBtn.setTooltip("Clear every arc on the current page (asks first)");
    clearBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff7a2a2a));
    clearBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9a3a3a));
    clearBtn.onClick = [this]{ showClearWarning(); };
}

void MainComponent::buildCombos()
{
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
}

void MainComponent::buildToolbarControls()
{
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
        updateWaveButtonColours();
    };
    envEditBox.onChange = [this]{
        int idx = envEditBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int)envelopes.size()) { selEnv = envelopes[(size_t)idx].id; envEd.bind(&envelopes, &selEnv); envBox.setSelectedId(idx + 1, juce::dontSendNotification); pushVoice(); }
    };

    // --- sliders ---
    addAndMakeVisible(durSlider); addAndMakeVisible(masterSlider); addAndMakeVisible(intenSlider);
    addAndMakeVisible(pageLab); addAndMakeVisible(durLab); addAndMakeVisible(masterLab); addAndMakeVisible(intenLab);
    addAndMakeVisible(timeLab); addAndMakeVisible(statusLab);

    // v1.31 (U2): the page duration runs from the UPIC's micro-time end
    // (a few milliseconds) to its macro-time end (12 minutes). A linear
    // slider over that range would be useless at the top, so the travel is
    // skewed: 30 s sits in the middle and the value box takes an exact
    // number (the UPIC's range was 6 ms to 12 min).
    durSlider.setRange (upic::kMinPageSeconds, upic::kMaxPageSeconds, 0.005);
    durSlider.setSkewFactorFromMidPoint (30.0);
    durSlider.setValue (30.0);
    durSlider.setNumDecimalPlacesToDisplay (3);
    durSlider.setTextValueSuffix (" s");
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
    // v1.31 (U2): the duration now reaches "720.000 s", so its value box
    // needs a little more room than the shared 72 px.
    durSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 92, 18);
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
}

void MainComponent::buildMaterialControls()
{
    // --- material controls: oscillator vs realtime sample pitch-shift ---
    addAndMakeVisible(sampleBox);
    addAndMakeVisible(loadSampleBtn); addAndMakeVisible(refSlider); addAndMakeVisible(tempoSlider); addAndMakeVisible(tempoLab); addAndMakeVisible(pmRatioSlider); addAndMakeVisible(pmIndexSlider);
    addAndMakeVisible(samplePitchBox); addAndMakeVisible(samplePitchLab);
    addAndMakeVisible(monBtn); addAndMakeVisible(capBtn);
    addAndMakeVisible(waveFromSlotBtn);
    addAndMakeVisible(inGainSlider); addAndMakeVisible(inGainLab);
    addAndMakeVisible(slotLab); addAndMakeVisible(refLab); addAndMakeVisible(matHintLab);
    matHintLab.setName("matHintLab");
    addAndMakeVisible (pmRatioLab); addAndMakeVisible (pmIndexLab);
    addAndMakeVisible (pmIndexEnvLab); addAndMakeVisible (pmIndexEnvBox);
    addAndMakeVisible (pmOpsLab); addAndMakeVisible (pmOpsBox);
    addAndMakeVisible (pmPresetLab); addAndMakeVisible (pmPresetBox);
    addAndMakeVisible (pmFeedbackLab); addAndMakeVisible (pmFeedbackSlider);
    addAndMakeVisible (pmImportBtn); addAndMakeVisible (pmExportBtn);
}

void MainComponent::buildSieveControls()
{
    // --- sieve quantizer (with the preset row) ---
    addAndMakeVisible(sieveBtn); addAndMakeVisible(sieveMEdit); addAndMakeVisible(sieveREdit);
    sieveMEdit.setName("sieveMEdit");
    sieveREdit.setName("sieveREdit");
    sieveBtn.setName("sieveBtn");
    sieveMEdit.setTextToShowWhenEmpty("m=12", juce::Colour(0xff6a6a76));
    sieveREdit.setTextToShowWhenEmpty("0,7", juce::Colour(0xff6a6a76));
    sieveMEdit.setFont(juce::Font(juce::FontOptions(12.0f)));
    sieveREdit.setFont(juce::Font(juce::FontOptions(12.0f)));
    sieveMEdit.setText("12", juce::dontSendNotification);
    sieveREdit.setText("0", juce::dontSendNotification);
    sieveMEdit.onTextChange = [this]{ pushSieve(); };
    sieveREdit.onTextChange = [this]{ pushSieve(); };
    sieveBtn.onClick = [this]{ pushSieve(); };

    // v1.33: the sieve row says what each control does. The stickiness in
    // particular was a mystery: it is the global knob that says how
    // strongly the drawing is pulled toward whatever magnet is active.
    // v1.33.2: the zones no longer follow it blindly - each zone carries
    // its own stick (the zone menu sets it) and only follows the global
    // value until one is chosen.
    sieveBtn.setTooltip ("The sieve quantises the drawing's PITCHES: only the\n"
                         "pitches n mod m in the remainder list are allowed,\n"
                         "so the curve becomes a staircase of steps.\n"
                         "The allowed pitches are drawn as guides on the page.\n"
                         "Inside a sieve zone the zone's own sieve wins.");
    sieveMEdit.setTooltip ("The sieve's modulus: the octave is divided into m\n"
                           "equal steps. 12 means semitones.");
    sieveREdit.setTooltip ("The sieve's remainders: which of the m divisions\n"
                           "are allowed. 0 leaves only the C's (an octave sieve),\n"
                           "0,4,7 is a major triad, 0,7 is C and G.");
    sievePresetBox.setTooltip ("Sieve presets: they set the m/R and switch the\n"
                               "sieve on.");
    stickSlider.setTooltip ("How strongly the drawing is pulled toward the\n"
                             "magnet: the sieve (the pitches), a zone's own\n"
                             "sieve, and the time grid (the times). 1 is a\n"
                             "hard snap, 0 leaves the drawing free.\n"
                             "GLOBAL: this value applies outside the zones.\n"
                             "A zone carries its own stick (right-click it);\n"
                             "until you set one it follows this value.");
    gridBox.setTooltip ("Quantises the drawing's TIMES (the sieve quantises\n"
                        "the pitches). The points are pulled toward the grid\n"
                        "by Stick, so the curve takes the grid's shape.\n"
                        "Off leaves the times free.\n"
                        "GLOBAL: a sieve zone can set its own time grid,\n"
                        "which applies inside the zone.");
    genBox.setTooltip ("Procedural arc generators.");

    // --- v1.31 (U1): the polyagogy. The UPIC's wave page had no fixed
    //     meaning: the same drawn curve could be the carrier's timbre AND a
    //     control signal. Here the arc's own wave can also be an amplitude
    //     LFO (envelope shaping) or a frequency LFO (a fixed pitch or a
    //     glissando), read at its own rate.
    addAndMakeVisible(modUseBox); addAndMakeVisible(modRateSlider);
    addAndMakeVisible(modDepthSlider); addAndMakeVisible(modUseLab);
    addAndMakeVisible(modRateLab); addAndMakeVisible(modDepthLab);
    modUseBox.setName("modUseBox");
    modRateSlider.setName("modRateSlider");
    modDepthSlider.setName("modDepthSlider");
    modUseBox.addItem("Oscillator", 1);
    modUseBox.addItem("+ Amplitude LFO", 2);
    modUseBox.addItem("+ Frequency LFO", 3);
    modUseBox.setSelectedId (1, juce::dontSendNotification);
    modUseBox.setTooltip ("What the arc's drawn wave does besides being\n"
                          "the timbre - the UPIC let the same curve be\n"
                          "the sound and a control signal at once.");
    modRateSlider.setRange (0.05, 20.0, 0.05);
    modRateSlider.setValue (1.0, juce::dontSendNotification);
    modRateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    modRateSlider.setNumDecimalPlacesToDisplay (2);
    modRateSlider.setTextValueSuffix (" Hz");
    modRateSlider.setTooltip ("The LFO's rate: 1 Hz is one sweep over a\n"
                              "second of score time.");
    modDepthSlider.setRange (0.0, 1.0, 0.01);
    modDepthSlider.setValue (0.0, juce::dontSendNotification);
    modDepthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    modDepthSlider.setNumDecimalPlacesToDisplay (2);
    modDepthSlider.setTooltip ("How deep the modulation goes: 1 is an octave\n"
                              "of pitch, or a full swing of the amplitude.");
    // the three controls edit the selected arcs, like the other live editors
    auto applyModToSelection = [this]{
        const auto ids = score.getSelectedArcs();
        if (ids.empty()) return;
        {
            const juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                {
                    a.waveUse = modUseBox.getSelectedId() - 1;
                    a.modRate = modRateSlider.getValue();
                    a.modDepth = modDepthSlider.getValue();
                }
        }
        publishModelSnapshot();
        markModelChanged();
        score.repaint();
    };
    modUseBox.onChange = [this, applyModToSelection]{
        applyModToSelection();
        flash ("Wave as: " + modUseBox.getText());
    };
    modRateSlider.onValueChange = [this, applyModToSelection]{ applyModToSelection(); };
    modDepthSlider.onValueChange = [this, applyModToSelection]{ applyModToSelection(); };

    // --- v1.2 magnetic-sieve row: preset library, stickiness, time grid,
    //     procedural arc generators + persistent shortcut/wheel hint ---
    addAndMakeVisible(sievePresetBox); addAndMakeVisible(stickSlider);
    addAndMakeVisible(gridBox); addAndMakeVisible(genBox);
    addAndMakeVisible(stickLab); addAndMakeVisible(gridLab); addAndMakeVisible(genLab);
    addAndMakeVisible(sieveMLab); addAndMakeVisible(sieveRLab);
    sieveMLab.setName("sieveMLab"); sieveRLab.setName("sieveRLab");
    addAndMakeVisible(kbdHintLab);
    sievePresetBox.setName("sievePresetBox");
    stickSlider.setName("stickSlider");
    gridBox.setName("gridBox");
    genBox.setName("genBox");
    kbdHintLab.setName("kbdHintLab");

    sievePresetBox.setTextWhenNothingSelected("Sieve preset");
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

    genBox.setTextWhenNothingSelected("Procedural arc generator");
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
    //
    // v1.33.3: the hints live in their own sidebar now, one item per line.
    kbdHintLab.setText("Wheel: pan pitch\n"
                       "Alt+wheel: zoom pitch\n"
                       "Ctrl+wheel: zoom time\n"
                       "Shift+wheel: pan time\n"
                       "Arrows: nudge selection\n"
                       "Shift+arrow: grid step / octave\n"
                       "Ctrl+A: select all\n"
                       "Ctrl+D: duplicate\n"
                       "Del: delete\n"
                       "R-click: arc menu\n"
                       "centre line: scrub playhead"
                       + juce::String(juce::Desktop::getInstance().getMainMouseSource().isTouch()
                            ? "\nTouch: 1 finger draws\n"
                              "pinch / 2-finger drag: zoom / pan\n"
                              "long-press: menu"
                            : ""),
                       juce::dontSendNotification);
    kbdHintLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    kbdHintLab.setJustificationType(juce::Justification::topLeft);
    kbdHintLab.setColour(juce::Label::textColourId, juce::Colour(0xff7a7a86));
}

// v1.32: the discrete frequency space - the UPIC's "discrete/non-discrete"
// function. The frequency space could be the continuum (a diagonal line is
// a glissando) or discrete, with equal temperaments of 1..99 divisions per
// octave: "the same diagonal arc would then sound like discrete steps in a
// defined temperament" (Robindoré, in "From Xenakis's UPIC to Graphic
// Notation Today", Hatje Cantz 2024).
//
// The sieve (the row below) snaps the DRAWN points; this steps the SOUND:
// the curve on the canvas stays smooth, but the pitch that plays moves
// through the temperament, so the glissando is heard as a staircase.
void MainComponent::buildStepControls()
{
    addAndMakeVisible(stepBtn); addAndMakeVisible(stepDivSlider);
    addAndMakeVisible(stepDivLab);
    stepBtn.setName("stepBtn");
    stepDivSlider.setName("stepDivSlider");
    stepBtn.setTooltip ("The UPIC's discrete frequency space: the arc's pitch\n"
                        "is moved through a temperament of equal divisions\n"
                        "per octave, so a glissando is heard as a staircase\n"
                        "of discrete steps. Off = the continuum.");
    stepDivSlider.setRange (1.0, 99.0, 1.0);
    stepDivSlider.setValue (12.0, juce::dontSendNotification);
    stepDivSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
    stepDivSlider.setTooltip ("Equal divisions per octave: 12 is the\n"
                              "chromatic scale, 19 and 31 the tempered\n"
                              "ones, 7 the diatonic one.");
    stepDivLab.setTooltip ("Equal divisions per octave.");
    stepBtn.onClick = [this]{
        applyStepToSelection();
        flash (stepBtn.getToggleState()
                 ? juce::String ("Stepped: ") + juce::String ((int) stepDivSlider.getValue())
                   + " divisions per octave"
                 : juce::String ("The continuum: the pitch glides again"));
    };
    stepDivSlider.onValueChange = [this]{ applyStepToSelection(); };
}

// The controls edit the selected arcs, like the other live editors.
void MainComponent::applyStepToSelection()
{
    const int divs = stepBtn.getToggleState()
                       ? juce::jlimit (1, 99, (int) stepDivSlider.getValue()) : 0;
    const auto ids = score.getSelectedArcs();
    if (! ids.empty())
    {
        {
            const juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                    a.stepDivisions = divs;
        }
        publishModelSnapshot();
        markModelChanged();
        score.repaint();
    }
    // a new arc is born with the frequency space on show, even when there
    // was no selection to edit
    score.setCurrentStep (divs);
    // the divisions only mean something when the arc is stepped
    stepDivSlider.setEnabled (divs > 0);
}

// v1.33: the frequency spaces - the UPIC's four frequency tables. "A
// composer could choose to operate within up to four different
// simultaneous tables or grids that could be superimposed on each page.
// These grids were invisible to the eye, however, and accessed by
// assigning one of four tables to whatever arc was being drawn, the same
// way as one would assign the waveform or envelope to it." (Robindoré)
//
// So: the PAGE owns the four spaces, and an arc names the one it is
// played in. A space is a range of the frequency axis - the position on
// the page's vertical axis is mapped into it, so the same drawing can be
// heard in another register, with its intervals scaled.
void MainComponent::buildFrequencyControls()
{
    // ---- the page's ambitus: the interval its vertical axis covers ----
    addAndMakeVisible(ambitusBox); addAndMakeVisible(ambitusLab);
    ambitusBox.setName("ambitusBox");
    ambitusBox.setTooltip ("The interval the page's vertical axis covers.\n"
                            "The UPIC's normal ambitus was six octaves; it\n"
                            "could be ten, or as small as a semitone.");
    {
        const auto& presets = upic::ambitusPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            ambitusBox.addItem (juce::String (presets[(size_t) i].name), i + 1);
        ambitusBox.addItem ("Custom", (int) presets.size() + 1);
    }
    ambitusBox.setSelectedId (1, juce::dontSendNotification);
    ambitusBox.onChange = [this]{ pushAmbitus(); };

    // ---- the page's acceleration: a tempo sweep across the page ----
    addAndMakeVisible(warpSlider); addAndMakeVisible(warpLab);
    warpSlider.setName("warpSlider");
    warpSlider.setRange (-0.9, 0.9, 0.05);
    warpSlider.setValue (0.0, juce::dontSendNotification);
    warpSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    warpSlider.setNumDecimalPlacesToDisplay (2);
    warpSlider.setTooltip ("The page's acceleration: the page's own time runs\n"
                            "at a varying rate. Positive starts slow and speeds\n"
                            "up, negative starts fast and slows down.");
    warpLab.setTooltip ("The page's acceleration.");
    warpSlider.onValueChange = [this]{ pushWarp(); };

    // ---- the page's four frequency spaces ----
    addAndMakeVisible(spaceBox); addAndMakeVisible(spaceLab);
    spaceBox.setName("spaceBox");
    spaceBox.setTooltip ("The frequency space this arc is played in.\n"
                         "The page has four (the UPIC's four tables);\n"
                         "the drawing is mapped into the one you pick.");
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible(spaceDefBox[i]); addAndMakeVisible(spaceDefLab[i]);
        spaceDefBox[i].setName ("spaceDefBox" + juce::String (i));
        spaceDefLab[i].setName ("spaceDefLab" + juce::String (i));
        spaceDefLab[i].setJustificationType (juce::Justification::centredRight);
        spaceDefLab[i].setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        spaceDefLab[i].setText ("S" + juce::String (i + 1), juce::dontSendNotification);
        spaceDefLab[i].setTooltip ("The page's frequency space " + juce::String (i + 1)
                                   + " - the range the page's vertical axis is\n"
                                     "mapped into for the arcs that use it.");
        const auto& presets = upic::freqSpacePresets();
        for (int p = 0; p < (int) presets.size(); ++p)
            spaceDefBox[i].addItem (juce::String (presets[(size_t) p].name), p + 1);
        spaceDefBox[i].setSelectedId (1, juce::dontSendNotification);
        spaceDefBox[i].onChange = [this]{ pushSpaceDefs(); };
    }

    spaceBox.onChange = [this]{
        applySpaceToSelection();
        const int s = juce::jlimit (0, 3, spaceBox.getSelectedId() - 1);
        flash ("Space " + juce::String (s + 1) + ": "
               + upic::freqSpacePresets()[(size_t) curPage().spaces[(size_t) s]].name);
    };
}

// The page's four spaces -> the model.
void MainComponent::pushSpaceDefs()
{
    {
        const juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        for (int i = 0; i < 4; ++i)
            pg.spaces[(size_t) i] = juce::jlimit (0,
                (int) upic::freqSpacePresets().size() - 1,
                spaceDefBox[i].getSelectedId() - 1);
    }
    publishModelSnapshot();
    markModelChanged();
    refreshSpaceBox();
    score.repaint();
    // say what changed, and whether anything is in that space - a space
    // with no arcs in it changes nothing you can hear
    int which = -1;
    for (int i = 0; i < 4; ++i)
        if (spaceDefBox[i].getSelectedId() != 1) { which = i; break; }
    if (which >= 0)
    {
        int n = 0;
        for (auto& a : curPage().arcs) if (a.space == which) ++n;
        flash ("Space " + juce::String (which + 1) + ": "
               + upic::freqSpacePresets()[(size_t) curPage().spaces[(size_t) which]].name
               + (n > 0 ? " - " + juce::String (n) + " arcs in it"
                        : " - no arcs in it yet (put one there with Space)"));
    }
}

// The arc's space: the assignment, the way a wave or an envelope is
// assigned.
void MainComponent::applySpaceToSelection()
{
    const int s = juce::jlimit (0, 3, spaceBox.getSelectedId() - 1);
    const auto ids = score.getSelectedArcs();
    if (! ids.empty())
    {
        {
            const juce::ScopedLock l(dataLock);
            for (auto& a : curPage().arcs)
                if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                    a.space = s;
        }
        publishModelSnapshot();
        markModelChanged();
        score.repaint();
    }
    // a new arc is born in the space on show
    score.setCurrentSpace (s);
}

// The arc's combo lists the page's four spaces by name.
void MainComponent::refreshSpaceBox()
{
    const auto ids = score.getSelectedArcs();
    int cur = -1;
    {
        const juce::ScopedLock l(dataLock);
        for (auto& a : curPage().arcs)
            if (! ids.empty() && std::find (ids.begin(), ids.end(), a.id) != ids.end())
            { cur = juce::jlimit (0, 3, a.space); break; }
        if (cur < 0) cur = score.getCurrentSpace();
    }
    spaceBox.clear (juce::dontSendNotification);
    const auto& presets = upic::freqSpacePresets();
    for (int i = 0; i < 4; ++i)
    {
        const int p = juce::jlimit (0, (int) presets.size() - 1,
                                    curPage().spaces[(size_t) i]);
        spaceBox.addItem (juce::String (i + 1) + ": " + juce::String (presets[(size_t) p].name),
                          i + 1);
    }
    spaceBox.setSelectedId (cur + 1, juce::dontSendNotification);

    // v1.33: the space labels carry the number of arcs in each space, so
    // it is visible which spaces are in use - changing a space that no arc
    // is in sounds like nothing happened, because nothing did.
    for (int i = 0; i < 4; ++i)
    {
        int n = 0;
        for (auto& a : curPage().arcs) if (a.space == i) ++n;
        spaceDefLab[i].setText ("S" + juce::String (i + 1)
                                + (n > 0 ? " (" + juce::String (n) + ")" : ""),
                                juce::dontSendNotification);
    }
}

// The page's ambitus -> the model.
void MainComponent::pushAmbitus()
{
    const auto& presets = upic::ambitusPresets();
    const int id = ambitusBox.getSelectedId() - 1;
    if (id < 0 || id >= (int) presets.size()) return;   // "Custom": nothing to do
    {
        const juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        pg.pitchLo = presets[(size_t) id].lo;
        pg.pitchHi = presets[(size_t) id].hi;
    }
    publishModelSnapshot();
    markModelChanged();
    score.repaint();
    flash (juce::String (presets[(size_t) id].name));
}

// The page's acceleration -> the model.
void MainComponent::pushWarp()
{
    {
        const juce::ScopedLock l(dataLock);
        curPage().warp = juce::jlimit (-0.9, 0.9, warpSlider.getValue());
    }
    publishModelSnapshot();
    markModelChanged();
}

void MainComponent::buildArcListPanel()
{
    // v1.21 (wave 19): the arc list panel - the score's inventory on the
    // right of the display, with the per-arc controls. It is a view (the
    // visibility is session state), like the Wave / Env / Hints toggles.
    arcListBtn.setName("arcListBtn");
    arcListBtn.setClickingTogglesState(true);
    arcListBtn.setTooltip("Show the list of arcs (id, pitch range, time span, page, space)");
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
}

void MainComponent::buildMidiControls()
{
    // v1.23 (wave 20): the MIDI input foundation. The device list, the
    // activity monitor and the learn live in the MIDI + OSC settings overlay
    // (v1.30: it is no longer part of the audio settings).
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
    // v1.30: the MIDI section belongs to the MIDI + OSC settings overlay.
    settingsOverlay.addAndMakeVisible (midiDevBox);
    settingsOverlay.addAndMakeVisible (midiDevLab);
    settingsOverlay.addAndMakeVisible (midiGainLab);
    settingsOverlay.addAndMakeVisible (midiIntLab);
    settingsOverlay.addAndMakeVisible (midiActLab);
    settingsOverlay.addAndMakeVisible (midiLearnGain);
    settingsOverlay.addAndMakeVisible (midiLearnInt);
    settingsOverlay.addAndMakeVisible (midiBindGainLab);
    settingsOverlay.addAndMakeVisible (midiBindIntLab);
    settingsOverlay.addAndMakeVisible (midiScrubLab);
    settingsOverlay.addAndMakeVisible (midiLearnScrub);
    settingsOverlay.addAndMakeVisible (midiBindScrubLab);
    settingsOverlay.addAndMakeVisible (midiTransportLab);
    settingsOverlay.addAndMakeVisible (midiLearnTransport);
    settingsOverlay.addAndMakeVisible (midiBindTransportLab);
    refreshMidiDevices();
    updateMidiLabels();
}

void MainComponent::buildMaterialTabs()
{
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
}

void MainComponent::buildPmControls()
{
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
            // SECURITY (S-03): std::stod accepts "nan" and "inf", and the
            // engine's jlimit() passes NaN through unchanged - so a crafted
            // preset could put a NaN into the PM parameters and from there
            // into the audio thread. The parsed value is checked here.
            auto parseFloat = [&] (const std::string& k, double d) {
                if (k.empty()) return d;
                try {
                    const double v = std::stod (k);
                    return std::isfinite (v) ? v : d;
                } catch (...) { return d; }
            };
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
                a.pmRatio = upic::limits::clampFinite (r, 0.25, 16.0);
                a.pmIndex = upic::limits::clampFinite (lvl * 5.0, 0.0, 100.0);
                a.pmFeedback = upic::limits::clampFinite (fb, -1.0, 1.0);
                a.pmIndexEnv = envId;
            }
            pmRatioSlider.setValue (upic::limits::clampFinite (r, 0.25, 16.0), juce::dontSendNotification);
            pmIndexSlider.setValue (upic::limits::clampFinite (lvl * 5.0, 0.0, 100.0), juce::dontSendNotification);
            { int ei = 1; for (size_t j = 0; j < envelopes.size(); ++j) if (envelopes[j].id == envId) { ei = (int) j + 2; break; } pmIndexEnvBox.setSelectedId (ei, juce::dontSendNotification); }
            pmFeedbackSlider.setValue (upic::limits::clampFinite (fb, -1.0, 1.0), juce::dontSendNotification);
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
}

void MainComponent::buildSampleControls()
{
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
}

void MainComponent::buildLiveInputControls()
{
    // --- v1.4 live input: monitor toggle, capture toggle, input gain ---
    monBtn.onClick = [this]{
        if (! monBtn.getToggleState()) { monitoring = false; return; }
        ensureLiveInputFor([this](bool ok){
            if (ok) monitoring = true;
            else monBtn.setToggleState(false, juce::dontSendNotification);
        });
    };
    capBtn.setClickingTogglesState(true);
    capBtn.setName("capBtn");
    // v1.33.3: the capture button is green, and red while it records - the
    // button's text turns into "Stop" and the toggle state is on.
    capBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2a6a3a));
    capBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6a2a2a));
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

    // v1.31 (U4): the digitised wave page. The real UPIC could fill the
    // wavetable from a recording (Xenakis sampled the bulls for
    // Taurhiphanie). Here the selected sample slot becomes a wavetable: the
    // whole take is resampled into one loop of the oscillator.
    waveFromSlotBtn.setName("waveFromSlotBtn");
    waveFromSlotBtn.setTooltip("Turn the selected slot into a wavetable\n"
                                "(the UPIC's digitised wave page: the take\n"
                                "becomes one loop of the oscillator, so its\n"
                                "pitch follows the arc).");
    waveFromSlotBtn.onClick = [this]{ waveFromSelectedSlot(); };

    slotLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    slotLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    pmRatioLab.setText ("Ratio", juce::dontSendNotification); pmIndexLab.setText ("Index", juce::dontSendNotification); tempoLab.setText ("Tempo", juce::dontSendNotification); tempoLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    refLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    samplePitchLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    samplePitchLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    inGainLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    inGainLab.setColour(juce::Label::textColourId, juce::Colour(0xffc9c9d4));
    matHintLab.setText("Osc: draw arcs on the wavetable\n"
                       "Wave = carrier, Env = amplitude",
                       juce::dontSendNotification);
    matHintLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    matHintLab.setJustificationType(juce::Justification::topLeft);
    matHintLab.setColour(juce::Label::textColourId, juce::Colour(0xff7a7a86));

    timeLab.setColour(juce::Label::textColourId, juce::Colours::white);
    timeLab.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    timeLab.setJustificationType(juce::Justification::centredRight);
    statusLab.setColour(juce::Label::textColourId, juce::Colour(0xff9a9aa5));
    statusLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    statusLab.setJustificationType(juce::Justification::centredRight);
}

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
}

void MainComponent::buildEditors()
{
    // --- wave/env editors ---
    addAndMakeVisible(waveEd); addAndMakeVisible(envEd);
    addAndMakeVisible(wSine); addAndMakeVisible(wSaw); addAndMakeVisible(wSqr); addAndMakeVisible(wTri);
    addAndMakeVisible(wNoise); addAndMakeVisible(wSmooth); addAndMakeVisible(wNorm);
    wSine.setName("wSine");
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
        mixMonitorInput (L, R, n);

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
            // v1.30: the live ring's read direction follows the transport
            // when reverse is on, so the live voices must re-anchor on the
            // other ring (forward <-> mirrored).
            engine.resetLiveVoices();
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
            // and the freeze sustain (the click without a drag)
            AudioBlock block { L, R, SL, SR, FL, FR, n, playingAtStart };
            renderScrubPreview (*snap, block, previewGain);
            renderScrubFreeze (*snap, block, freezeGain);

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
    applyScoreFade (SL, SR, n, scoreFadeN, rendered, modelChanged);

    // add the score to the output (the monitor mix is already in there)
    mixMasterGain (L, R, SL, SR, n);

    // a model edit was audible this block: drop the old voices now, on the
    // audio thread (the message thread never touches the engine any more)
    if (modelChanged)
        engine.reset();

    // Click-free manual stop: keep rendering the page for kStopFadeSec while
    // the output ramps to silence, then halt (playhead keeps its position).
    applyStopFade (L, R, n, stopping);
}

//==============================================================================
// v1.30 (5.3 refactor): the parts of the audio block that were inlined in
// getNextAudioBlock() are their own methods now. They all run on the audio
// thread, so they only touch the members - no message thread, no locks.
//==============================================================================
void MainComponent::mixMonitorInput (float* L, float* R, int n)
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

void MainComponent::applyScoreFade (float* SL, float* SR, int n, int scoreFadeN,
                                    bool rendered, bool modelChanged)
{
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
}

void MainComponent::mixMasterGain (float* L, float* R, const float* SL, const float* SR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        // v1.15: the master gain is smoothed per sample (a step = a click)
        masterSmoothed += (masterGain.load() - masterSmoothed) * kGainSmooth;
        L[i] += SL[i] * masterSmoothed;
        if (R != nullptr) R[i] += SR[i] * masterSmoothed;
    }
}

void MainComponent::renderScrubPreview (const ModelSnapshot& snap, AudioBlock& b, double previewGain)
{
    if (b.playingAtStart || ! scrubPreviewActive.load(std::memory_order_relaxed))
        return;

    int remaining = scrubPreviewRemaining.load(std::memory_order_relaxed);
    if (remaining <= 0 || snap.pages.empty())
    {
        scrubPreviewActive.store(false, std::memory_order_relaxed);
        return;
    }

    const int p = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
    const upic::Page& pg = snap.pages[(size_t) p];
    const int chunk = juce::jmin(b.numSamples, remaining);
    juce::FloatVectorOperations::clear (b.scoreL, chunk);
    if (b.scoreR != nullptr) juce::FloatVectorOperations::clear (b.scoreR, chunk);

    double ph = scrubPreviewPlayhead.load(std::memory_order_relaxed);
    const double rate = scrubRate.load(std::memory_order_relaxed);
    const int sps = scrubPreviewSerial.load(std::memory_order_relaxed);
    if (sps != seenScrubPreviewSerial)
    {
        seenScrubPreviewSerial = sps;
        scrubEngine.reset(); // re-anchor to the dragged playhead
    }

    scrubEngine.render (pg, ph, snap.waveforms, snap.envelopes, snap.slots,
                        b.scoreL, (b.scoreR != nullptr ? b.scoreR : b.scoreL),
                        chunk, 1.0f, rate);

    const int fade = juce::jmin (chunk / 4, (int) (audioSampleRate * 0.005));
    for (int i = 0; i < chunk; ++i)
    {
        float g = 1.0f;
        if (fade > 0)
        {
            if (i < fade) g = (float) i / (float) fade;
            else if (i >= chunk - fade) g = (float) (chunk - 1 - i) / (float) fade;
        }
        b.scoreL[i] *= g;
        if (b.scoreR != nullptr) b.scoreR[i] *= g;
    }

    const float mg = masterGain.load(std::memory_order_relaxed) * (float) previewGain;
    for (int i = 0; i < chunk; ++i)
    {
        b.outL[i] += b.scoreL[i] * mg;
        if (b.outR != nullptr)
            b.outR[i] += (b.scoreR != nullptr ? b.scoreR[i] : b.scoreL[i]) * mg;
    }

    scrubPreviewPlayhead.store (ph + (double) chunk * rate / audioSampleRate, std::memory_order_relaxed);
    const int left = remaining - chunk;
    scrubPreviewRemaining.store (left, std::memory_order_relaxed);
    if (left <= 0) scrubPreviewActive.store (false, std::memory_order_relaxed);
}

void MainComponent::renderScrubFreeze (const ModelSnapshot& snap, AudioBlock& b, double freezeGain)
{
    if (b.playingAtStart || ! scrubFreezeActive.load(std::memory_order_relaxed))
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double stopAt = scrubFreezeStopAtMs.load(std::memory_order_relaxed);
    if (stopAt > 0.0 && nowMs >= stopAt)
    {
        scrubFreezeActive.store (false, std::memory_order_relaxed);
        scrubFreezeStopAtMs.store (0.0, std::memory_order_relaxed);
        return;
    }

    if (snap.pages.empty())
        return;

    const int p = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
    const upic::Page& pg = snap.pages[(size_t) p];
    juce::FloatVectorOperations::clear (b.freezeL, b.numSamples);
    if (b.freezeR != nullptr) juce::FloatVectorOperations::clear (b.freezeR, b.numSamples);

    engine.render (pg, audioPlayhead, snap.waveforms, snap.envelopes, snap.slots,
                   b.freezeL, (b.freezeR != nullptr ? b.freezeR : b.freezeL),
                   b.numSamples, 1.0f, 0.0, true);

    float gain = 1.0f;
    if (stopAt > 0.0)
    {
        const double rem = stopAt - nowMs;
        gain = (float) juce::jlimit (0.0, 1.0, rem / juce::jmax (1, scrubTailMs));
    }

    const float mg = masterGain.load(std::memory_order_relaxed) * gain * (float) freezeGain;
    for (int i = 0; i < b.numSamples; ++i)
    {
        b.outL[i] += b.freezeL[i] * mg;
        if (b.outR != nullptr)
            b.outR[i] += (b.freezeR != nullptr ? b.freezeR[i] : b.freezeL[i]) * mg;
    }
}

void MainComponent::applyStopFade (float* L, float* R, int n, bool stopping){
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
    if (revFrames > 0 && revBuf[revPlayIdx].getNumSamples() >= revFrames)
    {
        // v1.20/1.21: the reverse - the shifter can read the frozen, mirrored
        // segment instead of the live ring. The arc decides which one (the
        // arc list's Rev box). v1.30: the capture runs continuously, so the
        // global transport reverse can force the live input backwards even
        // when the Live tab's Rev button is off.
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
        // v1.33: the page's acceleration - the page's own time runs at a
        // varying rate, so the transport's speed is the transport's rate
        // times the page's rate at the playhead.
        const double pageRate = pg.rateAt (audioPlayhead);
        int chunk = (int) std::floor(remain * sr / (std::abs (rate) * pageRate));
        if (chunk < 1) chunk = 1;
        chunk = juce::jlimit(1, n - offset, chunk);

        engine.render(pg, audioPlayhead, snap.waveforms, snap.envelopes, snap.slots,
                      SL + offset, (SR != nullptr ? SR + offset : nullptr), chunk, 1.0f,
                      rate * pageRate);

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

        audioPlayhead += (double) chunk * rate * pageRate / sr;
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

//==============================================================================
// v1.29: OSC scrub. A phone running MobMuPlat sends /upic/scrub with a float
// 0..1; the receiver calls back on the message thread, so the playhead moves
// through the same seek + preview path as a canvas drag. When the messages
// stop, timerCallback() ends the gesture (and never starts playback).
void MainComponent::oscScrubReceived (double normalised)
{
    handleOscScrub (normalised);
}

void MainComponent::handleOscScrub (double normalised)
{
    const double dur = juce::jmax (1.0e-6, curPage().durationSec);
    const double t = juce::jlimit (0.0, dur, normalised * dur);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (! externalScrubActive)
    {
        externalScrubActive = true;
        externalScrubFrozen = false;
        score.externalScrubStart (t);
        if (! oscAnnounced)
        {
            oscAnnounced = true;
            flash ("OSC scrub on UDP " + juce::String (osc.getPort()));
        }
    }
    else
    {
        if (externalScrubFrozen)
        {
            externalScrubFrozen = false;
            if (score.onScrubFreezeCancel != nullptr)
                score.onScrubFreezeCancel();
        }
        score.externalScrubTo (t);
    }

    externalScrubLastT = t;
    lastExternalScrubMs = nowMs;
}

void MainComponent::updateExternalScrub (double nowMs)
{
    if (! externalScrubActive) return;
    const double dt = nowMs - lastExternalScrubMs;
    if (! externalScrubFrozen && dt > 100.0)
    {
        externalScrubFrozen = true;
        if (score.onScrubFreezeStart != nullptr)
            score.onScrubFreezeStart (externalScrubLastT);
    }
    if (dt > 2000.0)
    {
        externalScrubFrozen = false;
        externalScrubActive = false;
        score.externalScrubEnd();
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
    // v1.30: capture continuously, so the global transport reverse can use
    // the mirrored segment immediately (the Live tab's Rev button only sets
    // the arcs' reverse flags; it no longer gates the capture).
    if (revBufFrames.load(std::memory_order_relaxed) > 0)
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
                    // only re-anchor if something actually reads the reverse
                    // ring; the capture itself now runs continuously.
                    if (revLiveOn.load(std::memory_order_relaxed)
                        || playRate.load(std::memory_order_relaxed) < 0.0)
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
    matHintLab.setText (drawMatMode == 0 ? "Osc: draw arcs on the wavetable\n"
                                          "Wave = carrier, Env = amplitude"
                      : drawMatMode == 1 ? "Sample: Pitch sets how arcs transpose the slot\n"
                                          "Ref = unison"
                      : drawMatMode == 2 ? "Live: arcs multiply the input pitch\n"
                                          "1x = input, 2x = octave up"
                      : "PM: Ratio x Index into the carrier phase\n"
                        "IdxEnv shapes the modulation index",
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
    // v1.29: the phone stopped sending scrub positions - end the gesture
    updateExternalScrub (juce::Time::getMillisecondCounterHiRes());
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
        if (osc.isListening()) s += " | OSC " + juce::String (osc.getPort());
        if (osc.isMulticastListening()) s += " | MCAST";
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
        juce::Font f;
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
                          FL(&samplePitchLab, 34, &samplePitchBox, 180),
                          FL(&grainLab, 40, &grainSlider, 100),
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
                           std::vector<juce::Rectangle<int>>* rects) -> int
    {
        const int rh2 = (int) std::lround (rowH36 * sc);
        const int g2  = juce::jmax (1, (int) std::lround (2.0f * sc));
        const int gap = juce::jmax (1, (int) std::lround ((float) groupGapDp * sc));
        const int right = area.getRight();
        int x = area.getX(), y = area.getY();

        for (auto& grp : groups)
        {
            const int gw = (int) std::lround ((float) groupWidth (grp) * sc);
            // does the group fit on the current line?
            if (x > area.getX() && x + gw + gap > right) { x = area.getX(); y += rh2; }
            const int gx = x, gy = y;

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
                if (x > area.getX() && x + lw + cw > right) { x = area.getX(); y += rh2; }
                if (place)
                {
                    if (it.label != nullptr)
                        it.label->setBounds (x + g2, y + g2, juce::jmax (1, lw - 2 * g2), rh2 - 2 * g2);
                    if (it.comp != nullptr)
                        it.comp->setBounds (x + lw + g2, y + g2, juce::jmax (1, cw - 2 * g2), rh2 - 2 * g2);
                }
                x += lw + cw;
            }
            if (rects != nullptr)
                rects->push_back (juce::Rectangle<int> (gx, gy, juce::jmax (1, x - gx),
                                                        juce::jmax (1, y + rh2 - gy)));
            x += gap;
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
            toolbarGroups.push_back (sidebar);
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
    o->setProperty("liveGrainMs", liveGrainMs);
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
        // SECURITY (S-03): the decoded sample data of all the slots together
        // is capped, so a piece with many huge slots cannot exhaust memory.
        size_t totalSampleBytes = 0;
        for (auto& sv : *arr)
        {
            auto slot = upic::SampleSlot::fromVar(sv, 1);
            if (slot.data != nullptr)
                totalSampleBytes += (size_t) slot.data->getNumSamples()
                                  * (size_t) slot.data->getNumChannels() * sizeof(float);
            if (totalSampleBytes > upic::limits::kMaxTotalSampleBytes) break;
            for (auto& s : sampleSlots)
                if (s.id == slot.id) { s = std::move(slot); break; }
        }
    }
    if (auto* arr = v.getProperty("waveforms", juce::var()).getArray())
    {
        int i = 0;
        for (auto& wv : *arr)
        {
            if ((int) waveforms.size() >= upic::limits::kMaxWaveforms) break;
            waveforms.push_back(upic::Waveform::fromVar(wv, i++));
        }
    }
    if (auto* arr = v.getProperty("envelopes", juce::var()).getArray())
    {
        int i = 0;
        for (auto& ev : *arr)
        {
            if ((int) envelopes.size() >= upic::limits::kMaxEnvelopes) break;
            envelopes.push_back(upic::Envelope::fromVar(ev, i++));
        }
    }
    if (auto* arr = v.getProperty("pages", juce::var()).getArray())
        for (auto& pv : *arr)
        {
            if ((int) pages.size() >= upic::limits::kMaxPages) break;
            pages.push_back(upic::Page::fromVar(pv));
        }
    if (waveforms.empty()) waveforms = upic::defaultWaveforms();
    if (envelopes.empty()) envelopes = upic::defaultEnvelopes();
    if (pages.empty()) pages.push_back(upic::Page());
    // clamp ids
    currentPage = juce::jlimit(0, (int)pages.size() - 1, (int) v.getProperty("currentPage", 0));
    // SECURITY (S-03): the master gain is clamped to the slider's range, so a
    // piece cannot push the mix into clipping or silence.
    masterGain.store(upic::limits::clampGain((float)(double) v.getProperty("master", 0.8)));
    markModelChanged(); // v1.15: the whole piece was replaced -> fade across it
    // v1.1 state (defaults keep old / Python-written files working unchanged)
    seqPages = (bool) v.getProperty("seqPages", false);
    recordPressure = (bool) v.getProperty("recordPressure", false);
    scrubAudition = (bool) v.getProperty("scrubAudition", true);
    muteWhileDragging.store((bool) v.getProperty("muteWhileDragging", true), std::memory_order_relaxed);
    dragSmoothMs.store(juce::jlimit(0, 1000, (int) v.getProperty("dragSmoothMs", 20)), std::memory_order_relaxed);
    scrubGrainMs = juce::jlimit(5, 500, (int) v.getProperty("scrubGrainMs", 40));
    sampleGrainMs = juce::jlimit(5, 500, (int) v.getProperty("sampleGrainMs", 40));
    liveGrainMs = juce::jlimit(5, 500, (int) v.getProperty("liveGrainMs", 40));
    sieveOn = (bool) v.getProperty("sieveOn", false);
    sieveM = juce::jlimit(2, 120, (int) v.getProperty("sieveM", 12));
    sieveR.clear();
    if (auto* arr = v.getProperty("sieveR", juce::var()).getArray())
        for (auto& rv : *arr)
        {
            if ((int) sieveR.size() >= upic::limits::kMaxSieveRemainders) break;
            sieveR.push_back((((int) rv % sieveM) + sieveM) % sieveM);
        }
    // v1.33.2: an old settings file can carry an empty sieveR (the field was
    // allowed to be empty, and the placeholder made it look like 0,7). That
    // restored a sieve with no remainders, which is inert - the Sieve
    // checkbox then did nothing. Fall back to the octave sieve.
    if (sieveR.empty()) sieveR = { 0 };
    seqBtn.setToggleState(seqPages, juce::dontSendNotification);
    pressBtn.setToggleState(recordPressure, juce::dontSendNotification);
    scrubBtn.setToggleState(scrubAudition, juce::dontSendNotification);
    muteDragBtn.setToggleState(muteWhileDragging.load(), juce::dontSendNotification);
    dragSmoothSlider.setValue((double) dragSmoothMs.load(), juce::dontSendNotification);
    scrubGrainSlider.setValue((double) scrubGrainMs, juce::dontSendNotification);
    grainSlider.setValue((double) sampleGrainMs, juce::dontSendNotification);
    liveGrainSlider.setValue((double) liveGrainMs, juce::dontSendNotification);
    engine.setScrubGrainMs(scrubGrainMs);
    engine.setSampleGrainMs(sampleGrainMs);
    engine.setLiveGrainMs(liveGrainMs);
    scrubEngine.setScrubGrainMs(scrubGrainMs);
    scrubEngine.setSampleGrainMs(sampleGrainMs);
    scrubEngine.setLiveGrainMs(liveGrainMs);
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
    stickiness = upic::limits::clampFinite((double) v.getProperty("stickiness", 1.0), 0.0, 1.0);
    timeGrid = upic::limits::clampFinite((double) v.getProperty("timeGrid", 0.0), 0.0, 64.0);
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
            // SECURITY (S-03): a piece is untrusted input. Refuse to read a
            // file that is far bigger than any real piece, so a huge or
            // hostile file cannot exhaust memory before it is even parsed.
            if (f.getSize() > (juce::int64) upic::limits::kMaxPieceBytes)
            {
                flash("Piece too large (max 10 MB)");
                return;
            }
            // SECURITY (S-03): the whole load is wrapped, so a piece that
            // exhausts the memory or trips an assertion in the parser takes
            // the app down with a message instead of taking the app down.
            try
            {
                auto txt = f.loadFileAsString();
                if (txt.getNumBytesAsUTF8() > upic::limits::kMaxPieceBytes) return;
                auto v = juce::JSON::parse(txt);
                if (v.isVoid()) return;
                loadFromVar(v);
            }
            catch (const std::exception& e)
            {
                flash (std::string ("Could not load the piece: ") + e.what());
            }
            catch (...)
            {
                flash ("Could not load the piece");
            }
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

            // SECURITY (S-03): the decode and the allocation are wrapped, so
            // a crafted file that exhausts the memory shows a message
            // instead of taking the app down.
            try
            {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
            if (reader == nullptr)
            {
                flash("Unsupported audio file: " + f.getFileName());
                return;
            }

            // SECURITY (S-03): the sample rate and the length come from the
            // file header, so they are not trusted: a crafted header could
            // ask for a huge allocation. The rate is clamped to the range
            // the piece format accepts, and the length to 60 seconds of it.
            const double srIn = upic::limits::clampFinite (reader->sampleRate, 1000.0, upic::limits::kMaxSampleRate);
            const auto maxFrames64 = juce::jmin ((juce::int64) upic::limits::kMaxFramesPerSample,
                                                 reader->lengthInSamples);
            const int maxFrames = (int) juce::jmax ((juce::int64) 0, maxFrames64);
            const int ch = juce::jmax(1, juce::jmin(2, (int) reader->numChannels));
            if (maxFrames <= 0)
            {
                flash("The audio file is empty");
                return;
            }
            juce::AudioBuffer<float> tmp(ch, maxFrames);
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
            }
            catch (const std::exception& e)
            {
                flash (std::string ("Could not load the audio file: ") + e.what());
            }
            catch (...)
            {
                flash ("Could not load the audio file");
            }
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
    closeSettings();
    if (audioSelector == nullptr)
        audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, false, false, true, false);
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

//==============================================================================
// v1.30 (UPIC-54/56): MIDI + OSC settings. The audio overlay no longer owns
// the MIDI section; this overlay is the one place for the controller setup.
void MainComponent::showSettings()
{
    closeAudioSettings();
    refreshMidiDevices();
    // show the applied values, not abandoned edits from a previous visit
    oscPortSlider.setValue((double) osc.getPort(), juce::dontSendNotification);
    oscMcPortSlider.setValue((double) osc.getMulticastPort(), juce::dontSendNotification);
    oscGroupEdit.setText(osc.getMulticastGroup(), juce::dontSendNotification);
    oscMcBtn.setToggleState(osc.isMulticastEnabled(), juce::dontSendNotification);
    oscOnBtn.setToggleState(osc.isEnabled(), juce::dontSendNotification);
    updateOscStatus();
    settingsOverlay.setBounds(getLocalBounds());
    settingsOverlay.setVisible(true);
    settingsOverlay.toFront(false);
    resized();
}

void MainComponent::closeSettings()
{
    settingsOverlay.setVisible(false);
}

void MainComponent::startOscReceiver()
{
    osc.start();
    oscAnnounced = false;
    updateOscStatus();
}

void MainComponent::stopOscReceiver()
{
    osc.stop();
}

void MainComponent::rebindOscReceiver()
{
    osc.start();
    oscAnnounced = false;
    updateOscStatus();
}

void MainComponent::applyOscSettings()
{
    osc.setEnabled (oscOnBtn.getToggleState());
    osc.setPort (juce::jlimit (1024, 65535, (int) oscPortSlider.getValue()));
    osc.setMulticastPort (juce::jlimit (1024, 65535, (int) oscMcPortSlider.getValue()));
    const auto group = oscGroupEdit.getText().trim();
    if (group.isNotEmpty())
        osc.setMulticastGroup (group);
    osc.setMulticastEnabled (oscMcBtn.getToggleState());

    rebindOscReceiver();

    if (osc.isListening())
        flash("OSC scrub on UDP " + juce::String (osc.getPort())
              + (osc.isMulticastListening() ? " + multicast " + osc.getMulticastGroup() + ":"
                                                   + juce::String (osc.getMulticastPort())
                                             : juce::String()));
    else
        flash("OSC unavailable: UDP port " + juce::String (osc.getPort()) + " busy");
}


void MainComponent::updateOscStatus()
{
    oscStatusLab.setText (osc.statusText(), juce::dontSendNotification);

    if (! osc.isEnabled())
    {
        statusLab.setTooltip ("OSC scrub disabled in MIDI + OSC settings");
        return;
    }

    statusLab.setTooltip ("OSC scrub: send /upic/scrub <0..1> to UDP port "
                          + juce::String (osc.getPort())
                          + " (or multicast " + osc.getMulticastGroup() + ":"
                          + juce::String (osc.getMulticastPort()) + ")");
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
    waveEd.bind (&waveforms, &selWave);
    pushVoice();
    publishModelSnapshot();
    markModelChanged();
    flash ("Wave page digitised: \"" + wave.name + "\" (one loop)");
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
//==============================================================================
void MainComponent::transformPage (int op)
{
    // 0 = transpose, 1 = invert (pitch), 2 = retrograde (time), 3 = scale (time)
    int kind = 0;
    double semitones = 0.0;
    double factor = 1.0;
    juce::String what;

    switch (op)
    {
        case 101: kind = 0; semitones =  12.0; what = "transposed up an octave"; break;
        case 102: kind = 0; semitones = -12.0; what = "transposed down an octave"; break;
        case 103: kind = 0; semitones =   1.0; what = "transposed up a semitone"; break;
        case 104: kind = 0; semitones =  -1.0; what = "transposed down a semitone"; break;
        case 105: kind = 1; what = "inverted (the pitch mirrored)"; break;
        case 106: kind = 2; what = "retrograded (the time mirrored)"; break;
        case 107: kind = 3; factor = 2.0; what = "augmented (twice as long)"; break;
        case 108: kind = 3; factor = 0.5; what = "diminished (half as long)"; break;
        default: return;
    }

    const auto ids = score.getSelectedArcs();
    const bool onlySelected = ! ids.empty();
    int touched = 0;

    {
        const juce::ScopedLock l(dataLock);
        for (auto& a : curPage().arcs)
        {
            if (onlySelected
                && std::find (ids.begin(), ids.end(), a.id) == ids.end())
                continue;

            switch (kind)
            {
                case 0: a.transpose (semitones); break;
                case 1: a.invertPitch(); break;
                case 2: a.retrograde(); break;
                case 3: a.scaleTime (factor); break;
                default: break;
            }
            ++touched;
        }
        if (touched > 0)
        {
            markModelChanged(); // the voices re-anchor -> fade across it
            publishModelSnapshot();
            arcListDirty = true;
        }
    }

    if (touched == 0)
    {
        flash ("Nothing to transform on this page");
        return;
    }

    flash (juce::String (touched) + (touched == 1 ? " arc " : " arcs ") + what
           + (onlySelected ? " (the selection)" : ""));
    score.repaint();
    repaint();
}
