#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The component itself: the constructor, the transport and the toolbar, the
// timer and the page transformations.
//
// The controls of the material tabs live in MainComponentControls.cpp, the
// layout in MainComponentLayout.cpp, the model in MainComponentModel.cpp, the
// audio in MainComponentAudio.cpp, the MIDI in MainComponentMidi.cpp and the
// files in MainComponentFiles.cpp.

// Keep every numeric slider in the same visual family as Master Volume.
// Deliberately do not change range, behaviour, format or text-box dimensions.
namespace
{
void applyMasterVolumeSliderTheme (juce::Slider& s)
{
    s.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
    s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
    s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
    s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
}
}

//==============================================================================
MainComponent::MainComponent(bool pluginMode)
    : pluginMode_(pluginMode)
    // v1.30 (5.3 refactor): the OSC receiver needs this component as its
    // listener, so it is constructed here rather than in place.
    , osc (*this)
{
    waveforms = upic::defaultWaveforms();
    envelopes = upic::defaultEnvelopes();
    ampTables = upic::defaultAmplitudeTables();   // WP2: the identity table
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

    // v1.33.4 (M28): the job runner reports through the status line (the
    // flash message) - the runner itself is in JobRunner.h.
    jobs.announce = [this] (const juce::String& m) { flash (m); };

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
    // Theme all slider groups after their builders run. Previously Scrub,
    // Drag smooth, Sample grain and Live grain retained the transparent
    // JUCE value boxes instead of the Master Volume treatment.
    // F11: the toolbar's amounts are knobs now, but the palette is the
    // slider's own - the knob draws with the same colours.
    const std::vector<juce::Slider*> themedSliders = {
        &durSlider, &masterSlider, &intenSlider,
        &refSlider, &tempoSlider, &pmRatioSlider,
        &pmIndexSlider, &pmFeedbackSlider, &stickSlider,
        &inGainSlider, &scrubLenSlider, &scrubCrossSlider,
        &scrubTailSlider, &scrubGrainSlider, &grainSlider,
        &dragSmoothSlider, &liveGrainSlider, &oscPortSlider,
        &oscMcPortSlider, &modRateSlider, &modDepthSlider,
        &stepDivSlider, &warpSlider, &levelSlider, &arcModSlider };
    for (auto* slider : themedSliders)
        applyMasterVolumeSliderTheme (*slider);
    groupPanel.themeSliders();   // WP3
    // Icon-only controls remain discoverable with mouse/pen hover. Preserve
    // any more specific tooltips that the individual controls already set.
    pageAddBtn.setTooltip ("Add a page");
    pageDupBtn.setTooltip ("Duplicate the current page");
    pageDelBtn.setTooltip ("Delete the current page");
    for (auto* b : { &playBtn, &stopBtn, &menuBtn, &settingsBtn, &clearBtn,
                     &saveBtn, &loadBtn, &exportBtn, &stemsBtn, &audioBtn,
                     &waveTog, &envTog, &hintsTog, &arcListBtn,
                     &matOscTog, &matSampleTog, &matLiveTog, &matPmTog,
                     &assignBtn, &delArcBtn, &muteArcBtn,
                     &loadSampleBtn, &pmImportBtn, &pmExportBtn,
                     &normMatBtn, &fadeMatBtn, &resetMatBtn,
                     &liveRevBtn, &revBtn, &waveFromSlotBtn })
        if (b->getTooltip().isEmpty()) b->setTooltip (b->getButtonText());
    // v1.29: listen for the phone/MobMuPlat scrub. Message-loop callbacks
    // mean the playhead moves on the message thread exactly like a drag.
    // v1.30 (5.3 refactor): the receiver owns its own listeners; starting it
    // binds the endpoints (and rebinds them after the settings changed).
    startOscReceiver();

    refreshBoxes();
    refreshScoreBinding();
    updateStatus();
    // F5: the session's fold levels and view toggles, before the first
    // layout pass (the app persists them; the selftest does not).
    loadSessionSettings();
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

    // F5 (2026-11-17): the permanent strip's Restore button. The strip keeps
    // Menu, Play/Stop, the tool selector and this button reachable at every
    // fold level, so a hidden group is never lost.
    addAndMakeVisible (restoreGroupsBtn);
    restoreGroupsBtn.setName ("restoreGroupsBtn");
    restoreGroupsBtn.setTooltip ("Expand every toolbar group again");
    restoreGroupsBtn.onClick = [this]{ restoreAllGroups(); };
    loopBtn.setName("loopBtn");
    seqBtn.setName("seqBtn");
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
    // v1.33.4 (M22): the loop and the page sequencer are part of the model
    // the audio thread renders from, so they have to mark it as changed -
    // the status timer publishes the snapshot only when the revision moved.
    loopBtn.onClick = [this]{ loopPage = loopBtn.getToggleState(); markModelChanged (false); };
    seqBtn.onClick = [this]{ seqPages = seqBtn.getToggleState(); markModelChanged(); };
    revPlayBtn.onClick = [this]{
        const bool rev = revPlayBtn.getToggleState();
        const double now = playRate.load(std::memory_order_relaxed);
        const double nxt = rev ? -1.0 : 1.0;
        if (nxt != now)
        {
            prevPlayRate.store(now, std::memory_order_relaxed);
            playRate.store(nxt, std::memory_order_relaxed);
            if (transportActive())
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
        // F11: the knob draws its own value (and takes a typed value on a
        // double click), so it has no text box.
        s->setNumDecimalPlacesToDisplay(0);
        addAndMakeVisible(s);
    }
    scrubLenSlider.setName ("scrubLenSlider");
    scrubCrossSlider.setName ("scrubCrossSlider");
    scrubGrainSlider.setName ("scrubGrainSlider");
    for (auto* l : { &scrubLenLab, &scrubCrossLab, &scrubTailLab, &scrubGrainLab })
    {
        l->setJustificationType(juce::Justification::centredRight);
        l->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, juce::Colours::white);
        // the label is plain text on the toolbar card (the reference's
        // labels carry no plate of their own)
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(l);
    }
    scrubLenSlider.setTextValueSuffix(" ms");
    scrubCrossSlider.setTextValueSuffix(" ms");
    scrubTailSlider.setTextValueSuffix(" ms");
    scrubLenSlider.setRange(20.0, 1000.0, 1.0);
    scrubLenSlider.setValue(scrubPreviewLen * 1000.0 / audioSampleRate(), juce::dontSendNotification);
    scrubLenSlider.onValueChange = [this]{ scrubPreviewLen = juce::jmax(64, (int)(audioSampleRate() * scrubLenSlider.getValue() / 1000.0)); };
    scrubCrossSlider.setRange(0.0, 1000.0, 1.0);
    scrubCrossSlider.setValue((double) scrubCrossMs, juce::dontSendNotification);
    scrubCrossSlider.onValueChange = [this]{ scrubCrossMs = (int) scrubCrossSlider.getValue(); };
    scrubTailSlider.setRange(0.0, 1000.0, 1.0);
    // v1.33.4 (L14): the tail length is written here (the message thread)
    // and read by the audio thread (the freeze fade) - it is atomic.
    scrubTailSlider.setName ("scrubTailSlider");
    scrubTailSlider.setValue((double) scrubTailMs.load (std::memory_order_relaxed), juce::dontSendNotification);
    scrubTailSlider.onValueChange = [this]{ scrubTailMs.store ((int) scrubTailSlider.getValue(), std::memory_order_relaxed); };
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
    grainLab.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(grainLab);
    grainSlider.setNumDecimalPlacesToDisplay(0);
    grainSlider.setName ("grainSlider");
    grainSlider.setTextValueSuffix(" ms");
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
    dragSmoothLab.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(dragSmoothLab);
    dragSmoothSlider.setNumDecimalPlacesToDisplay(0);
    dragSmoothSlider.setName ("dragSmoothSlider");
    dragSmoothSlider.setTextValueSuffix(" ms");
    dragSmoothSlider.setRange(0.0, 1000.0, 1.0);
    dragSmoothSlider.setValue((double) dragSmoothMs.load(), juce::dontSendNotification);
    dragSmoothSlider.onValueChange = [this]{ dragSmoothMs.store((int) dragSmoothSlider.getValue(), std::memory_order_relaxed); };
    addAndMakeVisible(dragSmoothSlider);
    stemsBtn.onClick = [this]{ exportStems(); };
    snapBtn.onClick = [this]{ score.setSnap(snapBtn.getToggleState()); };
}

void MainComponent::buildViewToggles()
{
    // v1.7: view toggles ΞΒ²Ξ²β€Β¬Ξ²β‚¬Β hide the waveform/envelope editors at the bottom
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
        const int pm = livePitchBox.getSelectedId() - 1;
        // v1.33.4 (M21): the vocoder's working memory is prepared BEFORE the
        // audio thread can see the new mode
        prepareVocodersIfNeeded (engine.getSamplePitchMode(), pm);
        engine.setLivePitchMode(pm);
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
    liveGrainLab.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    liveGrainSlider.setName("liveGrainSlider");
    liveGrainSlider.setNumDecimalPlacesToDisplay(0);
    liveGrainSlider.setTextValueSuffix(" ms");
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
        // M4: the tuning - the scale the drawing snaps to. The arcs hold
        // frequencies, so a tuning does not retune what is already drawn.
        m.addSeparator();
        {
            juce::String label = "Import a Scala tuning...";
            if (tuning.custom)
            {
                juce::String now = tuning.describe();
                if (now.length() > 28) now = now.substring (0, 27) + "...";
                label += " (now: " + now + ")";
            }
            m.addItem (12, label);
        }
        if (tuning.custom)
            m.addItem (13, "Use 12-tone equal temperament");
        // v1.33.4 (L15, part 2): the undo. The menu is built when it opens,
        // so the steps are the ones that are really there.
        if (canUndo())
        {
            m.addItem (9, "Undo " + undoLabel());
            if (canRedo()) m.addItem (10, "Redo " + redoLabel());
        }
        // v1.33.4 (H3): the host's clock. The choice is the user's: the app's
        // own transport, or the DAW's (the plugin only - the stand-alone app
        // has no host to follow).
        if (pluginMode_)
            m.addItem (11, isFollowingHostTransport()
                              ? "Follow the host transport: on"
                              : "Follow the host transport: off");
        // v1.33.4 (M28): the long jobs (the render, the stems, the decode,
        // the save) run on a worker thread, so they can be stopped.
        if (jobRunning())
        {
            m.addSeparator();
            m.addItem (8, "Cancel the job (" + jobLabel() + ")");
        }
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
                    case 8: sp->cancelJob(); break;   // v1.33.4 (M28)
                    case 9: sp->undo(); break;        // v1.33.4 (L15)
                    case 10: sp->redo(); break;       // v1.33.4 (L15)
                    case 11: sp->setFollowHostTransport (! sp->isFollowingHostTransport());
                             break;                    // v1.33.4 (H3)
                    case 12: sp->importScalaTuning(); break;  // M4
                    case 13: sp->applyTuning (upic::Tuning{}); break;  // M4
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
    // F2/F3: the editor selectors live in the LibraryManager rows (see
    // buildEditors), which places them; they are not added here.
    addAndMakeVisible(pageAddBtn); addAndMakeVisible(pageDupBtn); addAndMakeVisible(pageDelBtn);

    toolBox.setName ("toolBox"); // the tool selector displays its SVG beside the selected tool
    toolBox.addItem("Draw", 1); toolBox.addItem("Line", 2); toolBox.addItem("Sine", 3);
    toolBox.addItem("Select/Move", 4); toolBox.addItem("Erase", 5);
    toolBox.addItem("Sieve zone", 6);
    toolBox.setSelectedId(1, juce::dontSendNotification);
    toolBox.onChange = [this]{
        score.setTool((ScoreEditor::Tool)(toolBox.getSelectedId() - 1));
    };

    pageAddBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        // v1.33.4 (H13): the piece format caps the page count, so the page
        // that does not fit is refused here instead of being dropped when
        // the piece is loaded again.
        if ((int) pages.size() >= upic::limits::kMaxPages)
        {
            flash ("A piece holds at most " + juce::String (upic::limits::kMaxPages)
                   + " pages (the limit) - delete one before adding another");
            return;
        }
        upic::Page p; p.durationSec = curPage().durationSec;
        pages.insert(pages.begin() + currentPage + 1, p);
        currentPage++;
        seekPlayhead(0.0);
        bumpPageEpoch();
    publishModelSnapshot();
        refreshBoxes(); refreshScoreBinding();
    };
    pageDupBtn.onClick = [this]{
        juce::ScopedLock l(dataLock);
        // v1.33.4 (H13): the same page cap (see pageAddBtn).
        if ((int) pages.size() >= upic::limits::kMaxPages)
        {
            flash ("A piece holds at most " + juce::String (upic::limits::kMaxPages)
                   + " pages (the limit) - delete one before duplicating one");
            return;
        }
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
    publishModelSnapshot();
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
        refreshLibraryBoxes(); // F2: the use counts moved
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
        refreshLibraryBoxes(); // F3: the use counts moved
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
        refreshLibraryBoxes(); // F3: a PM-index reference is a use too
    };

    waveEditBox.onChange = [this]{
        int idx = waveEditBox.getSelectedId() - 1;
        // v1.33.4 (M22): bind() replaces the change callback, so the mark
        // has to be passed here too
        if (idx >= 0 && idx < (int)waveforms.size()) { selWave = waveforms[(size_t)idx].id; waveEd.bind(&waveforms, &selWave, [this]{ markModelChanged(); }); waveBox.setSelectedId(idx + 1, juce::dontSendNotification); pushVoice(); }
        updateWaveButtonColours();
    };
    envEditBox.onChange = [this]{
        int idx = envEditBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int)envelopes.size()) { selEnv = envelopes[(size_t)idx].id; envEd.bind(&envelopes, &selEnv, [this]{ markModelChanged(); }); envBox.setSelectedId(idx + 1, juce::dontSendNotification); pushVoice(); }
    };

    // --- sliders ---
    addAndMakeVisible(durSlider); addAndMakeVisible(masterSlider); addAndMakeVisible(intenSlider);
    durSlider.setName ("durSlider");
    addAndMakeVisible(durLab); addAndMakeVisible(masterLab); addAndMakeVisible(intenLab);
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
    // F11 (2026-11-18): the levels are knobs now. The knob draws its own
    // value on its face (and takes a typed value on a double click), so the
    // old right-aligned value boxes are gone.
    // The full numeric-slider palette is applied once in the constructor,
    // after every control group (including Scrub and overlays) is built.
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
            // v1.33.4 (M24): the arc's own algorithm wins over the tab's
            // setting - the same rule the DSP uses (UpicEngine::effectiveMode).
            // The old code consulted only the global setting, so a page with
            // a per-arc override reported the wrong latency to the host.
            switch (engine.effectiveMode (a, engine.getLivePitchMode()))
            {
                case 0:  sec = upic::GrainShifter::kLagSec; break;   // 220 ms
                case 1:  sec = 0.5 * (upic::TapeShifter::kLagMinSec
                                    + upic::TapeShifter::kLagMaxSec); break; // 120 ms (the window's middle)
                default: sec = (double) upic::VocoderCore::kPrime / audioSampleRate(); break; // ~21 ms
            }
        }
        else if (a.mode == 1 && engine.effectiveMode (a, engine.getSamplePitchMode()) == 2)
        {
            sec = (double) upic::VocoderCore::kPrime / audioSampleRate(); // the sample vocoder's prime
        }

        if (sec <= 0.0) anyZeroDelay = true;
        else maxDelayed = juce::jmax(maxDelayed, (int) std::ceil(sec * audioSampleRate()));
    }

    if (! anyArc || anyZeroDelay) return 0;
    return maxDelayed;
}

void MainComponent::timerCallback()
{
    ++timerTicks;
    if (seqRecording) sequenceRecordTick();   // WP4
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
    updateMidiScrub();
    // v1.18 (wave 15): publish the immutable model the audio thread renders
    // from.
    //
    // v1.33.4 (M22): only when the model actually changed. The timer used
    // to deep-copy every page, arc, waveform and envelope on EVERY tick -
    // 30 times a second, even while nothing changed and even with the
    // plugin editor closed. The revision moves when the model changes
    // (markModelChanged), so the copy happens when there is something new
    // to copy.
    //
    // The safety net: a publish every 2 s even when the revision did not
    // move. A change path that forgets to mark itself would then be silent
    // for at most 2 s instead of forever.
    if (!(muteWhileDragging && arcEditing.load(std::memory_order_relaxed)))
    {
        const auto rev = modelRevision.load(std::memory_order_relaxed);
        const auto nowMs = (uint64_t) juce::Time::getMillisecondCounter();
        const bool changed = (rev != lastPublishedRevision);
        const bool safetyNet = (nowMs - lastModelPublishMs) >= 2000;
        if (changed || safetyNet)
    publishModelSnapshot();
    }
    const double dur = curPage().durationSec;
    const bool revNow = playRate.load(std::memory_order_relaxed) < 0.0;
    timeLab.setText((revNow ? juce::String("REV ") : juce::String()) + juce::String(displayPlayhead(), 1) + " / " + juce::String(dur, 1) + " s",
                    juce::dontSendNotification);

    // v1.16: the latency the host has to compensate for this page (0 = the
    // score is sample-accurate). Shown in the status line so the delay is
    // never a surprise.
    const int latSamples = liveLatencySamples();

    juce::String s;
    // v1.33.4 (M28): a running job has the first word - the user must see
    // that the app is working and how far it is. A silent job (the recovery
    // snapshot, L15) keeps out of the way: it runs every half minute and
    // must not keep interrupting the user.
    const auto job = jobs.current();
    if (jobRunning() && ! (job != nullptr && job->silent))
    {
        s = jobLabel() + " " + juce::String (jobProgress()) + "%"
            + (job != nullptr && job->cancelled.load (std::memory_order_relaxed)
                   ? " (cancelling...)" : " (File -> Cancel the job)");
    }
    else if (juce::Time::getMillisecondCounter() < flashUntilMs)
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
        // v1.33.4 (H3): the host's clock - the user has to see which clock
        // the transport runs on. v1.33.4 (H4): the MIDI jog has the
        // transport while it runs, so it says so instead.
        if (scrubActive) s += " | SCRUB";
        else if (followHostTransport.load (std::memory_order_relaxed)) s += " | HOST";
        if (recordPressure) s += " | PRESS";
        if (timeGrid > 0) s += " | GRID " + juce::String(timeGrid, 0) + "/s";
        if (stickiness < 0.999) s += " | STICK " + juce::String((int) std::round(stickiness * 100)) + "%";
        if (numLiveInputs.load() > 0) s += " | IN";
        if (latSamples > 0)
            s += " | LAT " + juce::String((int) std::round(latSamples / audioSampleRate() * 1000.0)) + "ms";
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
                {
                    // M5: the material's Ref is the slot's (the material's
                    // base pitch), so that is what the line shows.
                    double matRef = a.refFreq;
                    for (const auto& sl : sampleSlots)
                        if (sl.id == a.sampleId && sl.refFreq > 0.0) { matRef = sl.refFreq; break; }
                    s += "  [SAMPLE s" + juce::String(a.sampleId) + " ref "
                       + juce::String(matRef, 0) + "Hz]";
                }
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
    // v1.34.5: the status card is measured to its text. Re-flow the cards
    // when the string needs materially more or less room (a selection, a
    // job, an indicator); the band keeps the per-tick drag updates - which
    // only move a digit - from re-laying out. The re-flow is async: the
    // timer holds the model lock, and layout reads the UI only.
    if (! statusLayoutPending && statusLab.getWidth() > 0)
    {
        const float sc = appLaf.uiScale;
        const int textW = juce::GlyphArrangement::getStringWidthInt (
            appLaf.getLabelFont (statusLab), s);
        const int need = juce::jmax (
            (int) std::lround ((float) kStatusMinWidthDp * sc),
            textW + (int) std::lround (22.0f * sc));
        const int have = statusLab.getWidth();
        if (need > have + juce::jmax (8, have / 6)
            || need < have - juce::jmax (16, have / 4))
        {
            statusLayoutPending = true;
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<MainComponent> (this)]
                {
                    if (safe != nullptr)
                    {
                        safe->statusLayoutPending = false;
                        safe->resized();
                    }
                });
        }
    }
    score.repaint();

    // v1.16: publish the latency the host has to compensate. The host can
    // only take one number, and it must not change per block - so it is
    // published from here (the message thread) when the page or the pitch
    // mode changed.
    publishLatency();

    // v1.33.4 (L15, part 3): the crash-recovery snapshot. It writes itself
    // at most every 30 s, and only when the piece has unsaved changes and
    // the model moved since the last snapshot.
    maybeWriteRecoverySnapshot();

    // v1.33.4 (H3): the host's transport drives the app when the mode is on
    applyHostTransport();
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

    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the transform.
    beginEdit ("Transform the page");

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
        endEdit();
        flash ("Nothing to transform on this page");
        return;
    }

    endEdit();
    flash (juce::String (touched) + (touched == 1 ? " arc " : " arcs ") + what
           + (onlySelected ? " (the selection)" : ""));
    score.repaint();
    repaint();
}