#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The controls of the material tabs: the sieve, the step sequencer, the
// frequency spaces, the ambitus, the warp, the PM voice, the samples, the
// live input and the MIDI controls.

void MainComponent::buildMaterialControls()
{
    // --- material controls: oscillator vs realtime sample pitch-shift ---
    addAndMakeVisible(sampleBox);
    addAndMakeVisible(loadSampleBtn); addAndMakeVisible(refSlider); addAndMakeVisible(tempoSlider); addAndMakeVisible(tempoLab); addAndMakeVisible(pmRatioSlider); addAndMakeVisible(pmIndexSlider);
    addAndMakeVisible(samplePitchBox); addAndMakeVisible(samplePitchLab);
    addAndMakeVisible(monBtn); addAndMakeVisible(capBtn);
    addAndMakeVisible(waveFromSlotBtn);
    addAndMakeVisible(sampleExtractBtn);   // WP7
    addAndMakeVisible(inGainSlider); addAndMakeVisible(inGainLab);
    addAndMakeVisible(slotLab); addAndMakeVisible(refLab); addAndMakeVisible(matHintLab);
    // M5: the slot's level and the material's own edits
    addAndMakeVisible(levelSlider); addAndMakeVisible(levelLab);
    addAndMakeVisible(normMatBtn); addAndMakeVisible(fadeMatBtn); addAndMakeVisible(resetMatBtn);
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
    sieveMEdit.setText("12");
    sieveREdit.setText("0");
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
    modUseBox.setSelectedId (1);
    modUseBox.setTooltip ("What the arc's drawn wave does besides being\n"
                          "the timbre - the UPIC let the same curve be\n"
                          "the sound and a control signal at once.");
    modRateSlider.setRange (0.05, 20.0, 0.05);
    modRateSlider.setValue (1.0);
    modRateSlider.setNumDecimalPlacesToDisplay (2);
    modRateSlider.setTextValueSuffix (" Hz");
    modRateSlider.setTooltip ("The LFO's rate: 1 Hz is one sweep over a\n"
                              "second of score time.");
    modDepthSlider.setRange (0.0, 1.0, 0.01);
    modDepthSlider.setValue (0.0);
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
    addAndMakeVisible(stickLab); addAndMakeVisible(gridLab);
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
        sieveMEdit.setText(juce::String(pr.m));
        juce::String rt;
        for (size_t i = 0; i < pr.remainders.size(); ++i)
            rt += (i > 0 ? "," : "") + juce::String(pr.remainders[i]);
        sieveREdit.setText(rt);
        sieveBtn.setToggleState (true, juce::dontSendNotification);
        pushSieve();
        flash("Sieve preset: " + pr.name);
        sievePresetBox.setSelectedId(0); // back to placeholder
    };

    stickSlider.setRange(0.0, 1.0, 0.05);
    stickSlider.setValue(1.0);
    stickSlider.onValueChange = [this]{
        stickiness = stickSlider.getValue();
        score.setStickiness(stickiness);
    };

    gridBox.setTextWhenNothingSelected("Time grid");
    gridBox.addItem("Off", 1);
    gridBox.addItem("0.5/s", 2); gridBox.addItem("1/s", 3); gridBox.addItem("2/s", 4);
    gridBox.addItem("4/s", 5);   gridBox.addItem("8/s", 6); gridBox.addItem("16/s", 7);
    gridBox.setSelectedId(1);
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
#if GUERRERO_INTEGRATION
    genBox.addItem("Guerrero: envelope + seed", 5);
#endif
    genBox.onChange = [this]{
        const int id = genBox.getSelectedId();
#if GUERRERO_INTEGRATION
        if (id == 5)
        {
            // Open the parameter dialog rather than generating with defaults.
            openGuerreroDialog(0);
            genBox.setSelectedId(0, juce::dontSendNotification);
            return;
        }
#endif
        const int kind = id - 1;
        if (kind >= 0) runGenerator(kind);
        genBox.setSelectedId(0);
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
    stepDivSlider.setValue (12.0);
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
    ambitusBox.setSelectedId (1);
    ambitusBox.onChange = [this]{ pushAmbitus(); };

    // ---- the page's acceleration: a tempo sweep across the page ----
    addAndMakeVisible(warpSlider); addAndMakeVisible(warpLab);
    warpSlider.setName("warpSlider");
    warpSlider.setRange (-0.9, 0.9, 0.05);
    warpSlider.setValue (0.0);
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
        spaceDefBox[i].setSelectedId (1);
        spaceDefBox[i].onChange = [this]{ pushSpaceDefs(); };
    }

    spaceBox.onChange = [this]{
        if (refreshingControls) return; // a refresh is not a command
        applySpaceToSelection();
        const int s = juce::jlimit (0, 3, spaceBox.getSelectedId() - 1);
        flash ("Space " + juce::String (s + 1) + ": "
               + upic::freqSpacePresets()[(size_t) curPage().spaces[(size_t) s]].name);
    };

    // WP1: draw the conversion table of the space on show. The button edits
    // the space the arc's combo points at, so "pick space 2, press Table"
    // is the whole gesture.
    addAndMakeVisible (spaceTableBtn);
    spaceTableBtn.setName ("spaceTableBtn");
    spaceTableBtn.setTooltip ("Draw the frequency table of the space\n"
                              "selected in the Space menu (WP1)");
    spaceTableBtn.onClick = [this]{
        showFreqTableEditor (juce::jlimit (0, 3, spaceBox.getSelectedId() - 1));
    };

    freqTableEd.setName ("freqTableEd");
    addChildComponent (freqTableEd);
    freqTableEd.onClose = [this]{ closeFreqTableEditor(); };

    // WP7: the frequency table's source-aware library bridge. The model
    // command is wrapped in exactly one undo transaction.
    freqTableEd.library.onPublish = [this] { return libraryPublishFreqTable(); };
    freqTableEd.library.onImport = [this]
    {
        beginEdit ("Import the frequency table");
        const bool ok = libraryImportFreqTable();
        endEdit();
        return ok;
    };
    freqTableEd.library.onReveal = [this] { return libraryRevealFreqTable(); };
    freqTableEd.library.onSourceChanged = [this] (LibraryManager::Source source)
    {
        if (source == LibraryManager::Source::User)
            refreshUserLibraryBoxes (upic::UserAssetKind::FreqTable);
        else
            refreshLibraryBoxes();
    };
    freqTableEd.library.onSelectedId = [this] (int) {};

    // WP2: the amplitude conversion tables.
    addAndMakeVisible (ampTableLab);
    addAndMakeVisible (ampTableBox);
    addAndMakeVisible (ampTableBtn);
    ampTableLab.setName ("ampTableLab");
    ampTableBox.setName ("ampTableBox");
    ampTableBtn.setName ("ampTableBtn");
    ampTableLab.setJustificationType (juce::Justification::centredRight);
    ampTableLab.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    ampTableBox.setTooltip ("The amplitude conversion table this arc plays through.\n"
                            "Envelopes set the shape; the table maps it to gain.");
    ampTableBtn.setTooltip ("The amplitude table bank: draw, duplicate, rename,\n"
                            "delete and assign tables");
    ampTableBox.onChange = [this]{
        if (refreshingControls) return; // a refresh is not a command
        const int item = ampTableBox.getSelectedId();
        if (item <= 1) { selAmpTableId = -1; applyAmpTableToSelection (-1); return; }
        const int idx = juce::jlimit (0, (int) ampTables.size() - 1, item - 2);
        if (idx < 0 || idx >= (int) ampTables.size()) return;
        selAmpTableId = ampTables[(size_t) idx].id;
        applyAmpTableToSelection (selAmpTableId);
    };
    ampTableBtn.onClick = [this]{ showAmpTableEditor(); };
    refreshAmpTableBox();
    addChildComponent (ampTableEd);
    ampTableEd.onClose = [this]{ closeAmpTableEditor(); };

    // WP7: the panel's own New/Dup/Rename/Delete commands stay in the
    // panel. The shared manager is only the Piece/User bridge.
    ampTableEd.library.onPublish = [this] { return libraryPublishAmpTable(); };
    ampTableEd.library.onImport = [this]
    {
        beginEdit ("Import the amplitude table");
        const bool ok = libraryImportAmpTable();
        endEdit();
        return ok;
    };
    ampTableEd.library.onReveal = [this] { return libraryRevealAmpTable(); };
    ampTableEd.library.onSourceChanged = [this] (LibraryManager::Source source)
    {
        if (source == LibraryManager::Source::User)
            refreshUserLibraryBoxes (upic::UserAssetKind::AmpTable);
        else
            refreshLibraryBoxes();
    };
    ampTableEd.library.onSelectedId = [this] (int id)
    {
        selAmpTableId = id;
        refreshAmpTableEditor();
        refreshAmpTableBox();
    };

    // WP3: the arc-group panel.
    addAndMakeVisible (groupBtn);
    groupBtn.setName ("groupBtn");
    groupBtn.setTooltip ("Groups: named sets of arcs with shared mute, solo,\n"
                        "gain, transpose and timbre overrides");
    groupBtn.onClick = [this]{ showGroupPanel(); };
    addChildComponent (groupPanel);
    groupPanel.onClose = [this]{ closeGroupPanel(); };

    // WP4: the performance sequences.
    addAndMakeVisible (seqLab);
    addAndMakeVisible (seqBox);
    for (auto* b : { &seqRecBtn, &seqPlayBtn, &seqLoopBtn, &seqEditBtn })
        addAndMakeVisible (b);
    seqLab.setName ("seqLab");
    seqLab.setJustificationType (juce::Justification::centredRight);
    seqLab.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    seqBox.setName ("seqBox");
    seqRecBtn.setName ("seqRecBtn");   seqPlayBtn.setName ("seqPlayBtn");
    seqLoopBtn.setName ("seqLoopBtn"); seqEditBtn.setName ("seqEditBtn");
    seqLoopBtn.setClickingTogglesState (true);
    seqBox.setTooltip ("The performance sequence");
    seqRecBtn.setTooltip ("Record the movement through the score (page + playhead)");
    seqPlayBtn.setTooltip ("Replay the recorded performance");
    seqLoopBtn.setTooltip ("Loop the performance");
    seqEditBtn.setTooltip ("Open the sequence panel");
    seqBox.onChange = [this]
    {
        if (refreshingControls) return; // a refresh is not a command
        const int i = seqBox.getSelectedId() - 1;
        if (i >= 0 && i < (int) sequences.size()) selSeqId = sequences[(size_t) i].id;
        refreshSequencePanel();
    };
    seqRecBtn.onClick = [this]
    {
        if (seqRecording) stopSequenceRecording(); else startSequenceRecording();
    };
    seqPlayBtn.onClick = [this]
    {
        if (seqPlayActive.load()) stopSequencePlayback(); else startSequencePlayback();
    };
    seqLoopBtn.onClick = [this]{ toggleSequenceLoop(); };
    seqEditBtn.onClick = [this]{ showSequencePanel(); };
    addChildComponent (seqPanel);
    seqPanel.onClose = [this]{ closeSequencePanel(); };

    // WP7: the panel's own recorder and editing commands stay in the panel.
    // The shared manager is only the Piece/User bridge.
    seqPanel.library.onPublish = [this] { return libraryPublishSequence(); };
    seqPanel.library.onImport = [this]
    {
        beginEdit ("Import the performance sequence");
        const bool ok = libraryImportSequence();
        endEdit();
        return ok;
    };
    seqPanel.library.onReveal = [this] { return libraryRevealSequence(); };
    seqPanel.library.onSourceChanged = [this] (LibraryManager::Source source)
    {
        if (source == LibraryManager::Source::User)
            refreshUserLibraryBoxes (upic::UserAssetKind::Sequence);
        else
            refreshLibraryBoxes();
    };
    seqPanel.library.onSelectedId = [this] (int id)
    {
        selSeqId = id;
        refreshSequencePanel();
    };
    refreshSequencePanel();


    // WP5: the independent modulating arc.
    addAndMakeVisible (arcModLab);
    addAndMakeVisible (arcModBox);
    addAndMakeVisible (arcModTgtLab);
    addAndMakeVisible (arcModTgtBox);
    addAndMakeVisible (arcModDepthLab);
    addAndMakeVisible (arcModSlider);
    arcModLab.setName ("arcModLab");   arcModTgtLab.setName ("arcModTgtLab");
    arcModDepthLab.setName ("arcModDepthLab");
    arcModBox.setName ("arcModBox");   arcModTgtBox.setName ("arcModTgtBox");
    arcModSlider.setName ("arcModSlider");
    for (auto* l : { &arcModLab, &arcModTgtLab, &arcModDepthLab })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    }
    arcModBox.setTooltip ("The arc that modulates the selection (its contour bends the pitch or its level)");
    arcModTgtBox.setTooltip ("What the modulator drives: pitch or amplitude");
    arcModSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    arcModSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    arcModSlider.setRange (0.0, 1.0, 0.001);
    arcModSlider.setTooltip ("The modulation depth (1 = +/- one octave at the contour extremes)");
    arcModTgtBox.addItem ("Pitch", 1);
    arcModTgtBox.addItem ("Amp", 2);
    arcModBox.onChange = [this]
    {
        if (refreshingControls) return; // a refresh is not a command
        const int i = arcModBox.getSelectedId();
        setModOnSelection (i <= 1 ? -1 : i - 1,
                           arcModTgtBox.getSelectedId() == 2 ? 1 : 0,
                           arcModSlider.getValue());
    };
    arcModTgtBox.onChange = [this]
    {
        if (refreshingControls) return; // a refresh is not a command
        const int i = arcModBox.getSelectedId();
        setModOnSelection (i <= 1 ? -1 : i - 1,
                           arcModTgtBox.getSelectedId() == 2 ? 1 : 0,
                           arcModSlider.getValue());
    };
    arcModSlider.onDragStart = [this]
    {
        if (! modLevelsEditing) { beginEdit ("Modulation depth"); modLevelsEditing = true; }
    };
    arcModSlider.onValueChange = [this]
    {
        if (refreshingControls) return; // a refresh is not a command
        const int i = arcModBox.getSelectedId();
        if (i <= 1) return;
        const bool standalone = ! modLevelsEditing;
        if (standalone) beginEdit ("Modulation depth");
        { const juce::ScopedLock l (dataLock);
          for (auto& a : curPage().arcs)
              if (std::find (score.getSelectedArcs().begin(), score.getSelectedArcs().end(), a.id)
                  != score.getSelectedArcs().end())
                  a.modArcDepth = arcModSlider.getValue(); }
        markModelChanged();
        if (standalone) endEdit();
        publishModelSnapshot();
    };
    arcModSlider.onDragEnd = [this]
    {
        if (modLevelsEditing) { modLevelsEditing = false; endEdit(); }
    };
    refreshModBox();


    // --- WP6.4: the mixer UI ---
    // NOTE FOR WP7: this block only adds the Ch combo / Mix button. WP7's
    // library and extraction controls belong in their own marked block so a
    // parallel merge keeps both toolbars intact.
    addAndMakeVisible (channelLab);
    addAndMakeVisible (channelBox);
    addAndMakeVisible (channelBtn);
    channelLab.setName ("channelLab");
    channelBox.setName ("channelBox");
    channelBtn.setName ("channelBtn");
    channelLab.setJustificationType (juce::Justification::centredRight);
    channelLab.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    channelBox.setTooltip ("The mixer channel the selected arcs play on");
    channelBtn.setTooltip ("The mixer: channel levels, mute/solo and routing");
    channelBtn.onClick = [this]{ showChannelPanel(); };
    refreshChannelBox();
    channelBox.onChange = [this]
    {
        if (! channelBoxReady || channelBoxRefreshing || refreshingControls) return;
        const int item = channelBox.getSelectedId();
        if (item <= 1) { setChannelOnSelection (0); return; }
        const int idx2 = juce::jlimit (0, (int) channels.size() - 1, item - 2);
        if (idx2 >= 0 && idx2 < (int) channels.size())
            setChannelOnSelection (channels[(size_t) idx2].id);
    };
    channelBoxReady = true;
    // --- end WP6.4 ---
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

// WP1: the drawn frequency-table editor. It edits one page space; every
// commit is one undo transaction and republishes the snapshot so the change
// is audible immediately.
bool MainComponent::freqTableDestinationIsValid() const
{
    const juce::ScopedLock l (dataLock);
    return freqTablePage >= 0 && freqTablePage < (int) pages.size()
        && freqTableSpace >= 0 && freqTableSpace < (int) pages[(size_t) freqTablePage].spaceDefs.size();
}

void MainComponent::invalidateFreqTableDestination()
{
    // The editor belongs to the piece that owned the captured page/space.
    // Closing it on a model replacement prevents a stale callback from
    // indexing a different piece (and keeps undo/redo safe as well).
    freqTableEd.setVisible (false);
    freqTablePage = -1;
    freqTableSpace = -1;
}

void MainComponent::showFreqTableEditor (int space)
{
    // WP7: the destination is captured when the editor opens. A later page
    // change can never redirect an edit or a library import to another
    // page/space by accident.
    freqTablePage = juce::jlimit (0, juce::jmax (0, (int) pages.size() - 1),
                                  currentPage);
    freqTableSpace = juce::jlimit (0, 3, space);
    upic::FreqSpaceDef def;
    double lo = upic::kDefaultPitchLo, hi = upic::kDefaultPitchHi;
    {
        const juce::ScopedLock l (dataLock);
        const auto& pg = pages[(size_t) freqTablePage];
        def = pg.spaceDefs[(size_t) freqTableSpace];
        lo = pg.pitchLo;
        hi = pg.pitchHi;
    }

    freqTableEd.onCommitCurve = [this] (const std::vector<upic::TablePoint>& pts)
    {
        if (! freqTableDestinationIsValid())
        {
            flash ("The frequency-table destination is no longer part of this piece");
            return;
        }
        beginEdit ("Draw the frequency table");
        {
            const juce::ScopedLock l (dataLock);
            auto& d = pages[(size_t) freqTablePage].spaceDefs[(size_t) freqTableSpace];
            d.points = pts;
            d.editable = pts.size() >= 2;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshSpaceBox();
        score.repaint();
        flash ("Space " + juce::String (freqTableSpace + 1) + ": the table is drawn");
    };

    freqTableEd.onCommitFlags = [this] (bool inverted, bool stepped, int divisions)
    {
        if (! freqTableDestinationIsValid())
        {
            flash ("The frequency-table destination is no longer part of this piece");
            return;
        }
        beginEdit ("Edit the frequency table");
        {
            const juce::ScopedLock l (dataLock);
            auto& d = pages[(size_t) freqTablePage].spaceDefs[(size_t) freqTableSpace];
            d.inverted = inverted;
            d.stepped = stepped;
            d.stepDivisions = divisions;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        score.repaint();
    };

    freqTableEd.onReset = [this]
    {
        if (! freqTableDestinationIsValid())
        {
            flash ("The frequency-table destination is no longer part of this piece");
            return;
        }
        beginEdit ("Reset the frequency table");
        {
            const juce::ScopedLock l (dataLock);
            auto& d = pages[(size_t) freqTablePage].spaceDefs[(size_t) freqTableSpace];
            d = upic::FreqSpaceDef {};
            d.preset = pages[(size_t) freqTablePage].spaces[(size_t) freqTableSpace];
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshSpaceBox();
        score.repaint();
        flash ("Space " + juce::String (freqTableSpace + 1) + ": back to the preset");
    };

    refreshLibraryBoxes();
    if (freqTableEd.library.source() == LibraryManager::Source::User)
        refreshUserLibraryBoxes (upic::UserAssetKind::FreqTable);

    freqTableEd.open (def, lo, hi, "Frequency table - page "
                                  + juce::String (freqTablePage + 1) + ", space "
                                  + juce::String (freqTableSpace + 1));
    freqTableEd.setBounds (getLocalBounds());
    freqTableEd.toFront (true);
}

void MainComponent::closeFreqTableEditor()
{
    invalidateFreqTableDestination();
    if (isShowing()) grabKeyboardFocus();
}

//---------------- WP2: the amplitude conversion tables ----------------

int MainComponent::ampTableIndexOfId (int id) const
{
    for (int i = 0; i < (int) ampTables.size(); ++i)
        if (ampTables[(size_t) i].id == id) return i;
    return -1;
}

int MainComponent::ampTableUseCount (int id) const
{
    if (id < 0) return 0;
    int n = 0;
    for (auto& pg : pages)
        for (auto& a : pg.arcs)
            if (a.amplitudeTableId == id) ++n;
    return n;
}

void MainComponent::refreshAmpTableBox()
{
    ampTableBox.clear (juce::dontSendNotification);
    ampTableBox.addItem ("None", 1);
    for (int i = 0; i < (int) ampTables.size(); ++i)
        ampTableBox.addItem (ampTables[(size_t) i].name, i + 2);

    int cur = -1;
    const auto ids = score.getSelectedArcs();
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
            if (! ids.empty() && std::find (ids.begin(), ids.end(), a.id) != ids.end())
            { cur = a.amplitudeTableId; break; }
    }
    if (cur < 0) cur = selAmpTableId;
    const int idx = ampTableIndexOfId (cur);
    ampTableBox.setSelectedId (idx < 0 ? 1 : idx + 2, juce::dontSendNotification);
    selAmpTableId = cur;
}

void MainComponent::applyAmpTableToSelection (int id)
{
    // WP2: an id the bank does not hold is refused (a piece loaded from a
    // file without its tables, or a stale selection) - never indexed.
    const int tableIndex = ampTableIndexOfId (id);
    if (id >= 0 && tableIndex < 0)
    {
        flash ("That amplitude table is not in the bank");
        return;
    }

    const auto ids = score.getSelectedArcs();
    selAmpTableId = id;
    const juce::String tableName = tableIndex >= 0 ? ampTables[(size_t) tableIndex].name
                                                   : juce::String ("none");
    if (ids.empty())
    {
        flash (id < 0 ? juce::String ("Amplitude table: none")
                      : "Amplitude table: " + tableName + " (select arcs to assign it)");
        return;
    }

    beginEdit ("Assign the amplitude table");
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
            if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                a.amplitudeTableId = id;
    }
    markModelChanged();
    endEdit();
    publishModelSnapshot();
    refreshAmpTableBox();
    score.repaint();
    flash (id < 0 ? juce::String ("Amplitude table: none")
                  : "Amplitude table: " + tableName);
}

void MainComponent::refreshAmpTableEditor()
{
    std::vector<int> uses;
    for (auto& t : ampTables) uses.push_back (ampTableUseCount (t.id));
    ampTableEd.setBank (ampTables, uses, juce::jmax (0, ampTableIndexOfId (selAmpTableId)));
}

void MainComponent::showAmpTableEditor()
{
    std::vector<int> uses;
    for (auto& t : ampTables) uses.push_back (ampTableUseCount (t.id));
    int idx = ampTableIndexOfId (selAmpTableId);
    if (idx < 0 && ! ampTables.empty()) { idx = 0; selAmpTableId = ampTables.front().id; }

    ampTableEd.onSelectIndex = [this] (int i)
    {
        if (i < 0 || i >= (int) ampTables.size()) return;
        selAmpTableId = ampTables[(size_t) i].id;
        refreshAmpTableBox();
    };
    ampTableEd.onCurve = [this] (int i, const std::vector<upic::EnvPoint>& pts)
    {
        if (i < 0 || i >= (int) ampTables.size()) return;
        beginEdit ("Draw the amplitude table");
        {
            const juce::ScopedLock l (dataLock);
            ampTables[(size_t) i].points = pts;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshAmpTableEditor();
        refreshAmpTableBox();
        score.repaint();
        flash ("Amplitude table: " + ampTables[(size_t) i].name + " redrawn");
    };
    ampTableEd.onRename = [this] (int i, const juce::String& name)
    {
        if (i < 0 || i >= (int) ampTables.size() || name.isEmpty()) return;
        beginEdit ("Rename the amplitude table");
        {
            const juce::ScopedLock l (dataLock);
            ampTables[(size_t) i].name = name;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshAmpTableEditor();
        refreshAmpTableBox();
        flash ("Amplitude table: " + name);
    };
    ampTableEd.onNew = [this]
    {
        if ((int) ampTables.size() >= upic::limits::kMaxAmplitudeTables)
        { flash ("The amplitude table bank is full"); return; }
        const int nextId = upic::nextBankId (ampTables, 1);
        if (nextId < 1 || nextId > upic::limits::kMaxItemId)
        {
            flash ("The amplitude-table bank has no free id left");
            return;
        }
        beginEdit ("New amplitude table");
        {
            const juce::ScopedLock l (dataLock);
            ampTables.push_back (upic::makeAmplitudeTable (nextId,
                                                           "Table " + juce::String (nextId),
                                                           juce::Colour (0xff5aa0c8),
                                                           upic::ampLinear));
        }
        selAmpTableId = nextId;
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshAmpTableEditor();
        refreshAmpTableBox();
        flash ("Amplitude table: new table " + juce::String (nextId));
    };
    ampTableEd.onDuplicate = [this]
    {
        const int i = ampTableIndexOfId (selAmpTableId);
        if (i < 0 || (int) ampTables.size() >= upic::limits::kMaxAmplitudeTables)
        { flash ("The amplitude table bank is full"); return; }
        const int nextId = upic::nextBankId (ampTables, 1);
        if (nextId < 1 || nextId > upic::limits::kMaxItemId)
        {
            flash ("The amplitude-table bank has no free id left");
            return;
        }
        beginEdit ("Duplicate the amplitude table");
        {
            const juce::ScopedLock l (dataLock);
            auto copy = ampTables[(size_t) i];
            copy.id = nextId;
            copy.name = copy.name + " copy";
            ampTables.push_back (std::move (copy));
        }
        selAmpTableId = nextId;
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshAmpTableEditor();
        refreshAmpTableBox();
        flash ("Amplitude table duplicated");
    };
    ampTableEd.onDelete = [this]
    {
        const int i = ampTableIndexOfId (selAmpTableId);
        if (i < 0) return;
        if (ampTableUseCount (selAmpTableId) > 0)
        { flash ("That table is in use - assign another one first"); return; }
        beginEdit ("Delete the amplitude table");
        {
            const juce::ScopedLock l (dataLock);
            ampTables.erase (ampTables.begin() + i);
        }
        selAmpTableId = ampTables.empty() ? -1 : ampTables.front().id;
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshAmpTableEditor();
        refreshAmpTableBox();
        flash ("Amplitude table deleted");
    };
    ampTableEd.onAssign = [this] (int i)
    {
        if (i < 0 || i >= (int) ampTables.size()) return;
        applyAmpTableToSelection (ampTables[(size_t) i].id);
        refreshAmpTableEditor();
    };

    refreshLibraryBoxes();
    if (ampTableEd.library.source() == LibraryManager::Source::User)
        refreshUserLibraryBoxes (upic::UserAssetKind::AmpTable);

    ampTableEd.open (ampTables, uses, juce::jmax (0, idx));
    ampTableEd.setBounds (getLocalBounds());
    ampTableEd.toFront (true);
}

void MainComponent::closeAmpTableEditor()
{
    ampTableEd.setVisible (false);
    grabKeyboardFocus();
}

//---------------- WP3: the groups ----------------

int MainComponent::groupIndexOfId (int id) const
{
    for (int i = 0; i < (int) curPage().groups.size(); ++i)
        if (curPage().groups[(size_t) i].id == id) return i;
    return -1;
}

void MainComponent::refreshGroupPanel()
{
    const auto& page = curPage();
    std::vector<int> counts;
    for (auto& g : page.groups) counts.push_back ((int) g.arcIds.size());
    std::vector<GroupPanel::BankItem> waves, envs, tabs;
    for (auto& w : waveforms) waves.push_back ({ w.id, w.name });
    for (auto& e : envelopes) envs.push_back ({ e.id, e.name });
    for (auto& t : ampTables) tabs.push_back ({ t.id, t.name });
    groupPanel.setBank (page.groups, counts, waves, envs, tabs,
                        juce::jmax (0, groupIndexOfId (selGroupId)));
}

void MainComponent::showGroupPanel()
{
    // A group's colour follows its id, so the list and the future canvas
    // tint agree without another editable field.
    auto colourFor = [] (int id)
    {
        static const juce::uint32 palette[] = { 0xff5ac8fa, 0xff9a7ad0, 0xff6ac08a,
                                                0xffd0a050, 0xffd07272, 0xff50b8b8 };
        return juce::Colour (palette[(size_t) (juce::jmax (0, id - 1) % 6)]);
    };

    groupPanel.onSelectIndex = [this] (int i)
    {
        if (i >= 0 && i < (int) curPage().groups.size())
            selGroupId = curPage().groups[(size_t) i].id;
        refreshGroupPanel();
    };
    groupPanel.onNew = [this, colourFor]
    {
        if ((int) curPage().groups.size() >= upic::limits::kMaxGroupsPerPage)
        { flash ("The page is full of groups"); return; }
        int newId = 1;
        beginEdit ("New group");
        {
            const juce::ScopedLock l (dataLock);
            auto& page = curPage();
            newId = juce::jlimit (1, upic::limits::kMaxItemId, page.nextGroupId);
            page.nextGroupId = juce::jmin (upic::limits::kMaxItemId, newId + 1);
            upic::ArcGroup g;
            g.id = newId;
            g.name = "Group " + juce::String (newId);
            g.colour = colourFor (newId);
            page.groups.push_back (g);
        }
        selGroupId = newId;
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        flash ("Group " + juce::String (newId) + " added");
    };
    groupPanel.onDelete = [this]
    {
        const int idx = groupIndexOfId (selGroupId);
        if (idx < 0) return;
        beginEdit ("Delete group");
        {
            const juce::ScopedLock l (dataLock);
            auto& gr = curPage().groups;
            gr.erase (gr.begin() + idx);
        }
        selGroupId = curPage().groups.empty() ? -1 : curPage().groups.front().id;
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        score.repaint();
        flash ("Group deleted - its arcs keep their own data");
    };
    groupPanel.onRename = [this] (int i, const juce::String& name)
    {
        if (i < 0 || i >= (int) curPage().groups.size() || name.isEmpty()) return;
        beginEdit ("Rename group");
        {
            const juce::ScopedLock l (dataLock);
            curPage().groups[(size_t) i].name = name.substring (0, 64);
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        flash ("Group renamed");
    };
    groupPanel.onMuteSolo = [this] (int i, bool muted, bool solo)
    {
        if (i < 0 || i >= (int) curPage().groups.size()) return;
        beginEdit ("Group mute/solo");
        {
            const juce::ScopedLock l (dataLock);
            auto& g = curPage().groups[(size_t) i];
            g.muted = muted;
            g.solo = solo;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        score.repaint();
        flash (muted ? "Group muted" : (solo ? "Group soloed" : "Group unsilenced"));
    };
    groupPanel.onLevelsBegin = [this] (int)
    {
        if (! groupLevelsEditing) { beginEdit ("Group levels"); groupLevelsEditing = true; }
    };
    groupPanel.onLevels = [this] (int i, float gain, double transpose)
    {
        if (i < 0 || i >= (int) curPage().groups.size()) return;
        const bool standalone = ! groupLevelsEditing;
        if (standalone) beginEdit ("Group levels");
        {
            const juce::ScopedLock l (dataLock);
            auto& g = curPage().groups[(size_t) i];
            g.gain = juce::jlimit (0.0f, 2.0f, gain);
            g.transposeSemitones = juce::jlimit (-48.0, 48.0, transpose);
        }
        markModelChanged();
        if (standalone) endEdit();
        publishModelSnapshot();
        score.repaint();
    };
    groupPanel.onLevelsEnd = [this] (int)
    {
        if (groupLevelsEditing)
        {
            groupLevelsEditing = false;
            endEdit();
            refreshGroupPanel();
        }
    };
    groupPanel.onOverride = [this] (int i, int kind, int id)
    {
        if (i < 0 || i >= (int) curPage().groups.size()) return;
        // only ids the banks actually hold (a combo can outlive a deletion)
        if (kind == 0 && id >= 0 && ! upic::hasId (waveforms, id)) return;
        if (kind == 1 && id >= 0 && ! upic::hasId (envelopes, id)) return;
        if (kind == 2 && id >= 0 && ! upic::hasId (ampTables, id)) return;
        beginEdit ("Group override");
        {
            const juce::ScopedLock l (dataLock);
            auto& g = curPage().groups[(size_t) i];
            if (kind == 0) g.waveformId = id;
            else if (kind == 1) g.envelopeId = id;
            else g.amplitudeTableId = id;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        score.repaint();
        flash (id < 0 ? "Group override cleared" : "Group override set");
    };
    groupPanel.onAddSelection = [this]
    {
        const int idx = groupIndexOfId (selGroupId);
        const auto ids = score.getSelectedArcs();
        if (idx < 0 || ids.empty()) { flash ("Select arcs first"); return; }
        beginEdit ("Add arcs to the group");
        {
            const juce::ScopedLock l (dataLock);
            auto& page = curPage();
            // one group per arc: the selected arcs leave any other group
            for (auto& g : page.groups)
                g.arcIds.erase (std::remove_if (g.arcIds.begin(), g.arcIds.end(),
                                                [&] (int aid)
                                                {
                                                    return std::find (ids.begin(), ids.end(), aid)
                                                           != ids.end();
                                                }),
                                g.arcIds.end());
            auto& g = page.groups[(size_t) idx];
            for (int aid : ids)
                if (std::find (g.arcIds.begin(), g.arcIds.end(), aid) == g.arcIds.end())
                    g.arcIds.push_back (aid);
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        score.repaint();
        flash (juce::String ((int) ids.size()) + " arc(s) added to the group");
    };
    groupPanel.onRemoveSelection = [this]
    {
        const int idx = groupIndexOfId (selGroupId);
        const auto ids = score.getSelectedArcs();
        if (idx < 0 || ids.empty()) { flash ("Select arcs first"); return; }
        beginEdit ("Remove arcs from the group");
        {
            const juce::ScopedLock l (dataLock);
            auto& g = curPage().groups[(size_t) idx];
            g.arcIds.erase (std::remove_if (g.arcIds.begin(), g.arcIds.end(),
                                            [&] (int aid)
                                            {
                                                return std::find (ids.begin(), ids.end(), aid)
                                                       != ids.end();
                                            }),
                            g.arcIds.end());
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshGroupPanel();
        score.repaint();
        flash ("Arc(s) removed from the group");
    };
    groupPanel.onSelectMembers = [this]
    {
        const int idx = groupIndexOfId (selGroupId);
        if (idx < 0) return;
        score.setSelectedArcs (curPage().groups[(size_t) idx].arcIds);
        refreshBoxes();
        score.repaint();
        flash ("The group's arcs are selected");
    };

    refreshGroupPanel();
    groupPanel.setVisible (true);
    groupPanel.toFront (true);
    groupPanel.grabKeyboardFocus();
    groupPanel.setBounds (getLocalBounds());
}

void MainComponent::closeGroupPanel()
{
    groupPanel.setVisible (false);
    grabKeyboardFocus();
}

//---------------- WP4: the performance sequences ----------------

int MainComponent::sequenceIndexOfId (int id) const
{
    for (int i = 0; i < (int) sequences.size(); ++i)
        if (sequences[(size_t) i].id == id) return i;
    return -1;
}

void MainComponent::refreshSequencePanel()
{
    int idx = sequenceIndexOfId (selSeqId);
    if (idx < 0 && ! sequences.empty())
    {
        idx = 0;
        selSeqId = sequences.front().id;
    }
    if (sequences.empty()) selSeqId = -1;

    seqBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) sequences.size(); ++i)
        seqBox.addItem (sequences[(size_t) i].name, i + 1);
    seqBox.setSelectedId (idx < 0 ? 0 : idx + 1, juce::dontSendNotification);

    seqLoopBtn.setToggleState (idx >= 0 && sequences[(size_t) idx].loop,
                               juce::dontSendNotification);
    seqRecBtn.setButtonText (seqRecording ? "Stop" : "Rec");
    seqPlayBtn.setButtonText (seqPlayActive.load() ? "Stop" : "Play");

    std::vector<double> durations;
    for (auto& p : pages) durations.push_back (p.durationSec);
    seqPanel.setSequences (sequences, durations, juce::jmax (0, idx),
                           seqRecording, seqPlayActive.load());
}

void MainComponent::showSequencePanel()
{
    seqPanel.onRecordToggle = [this]
    {
        if (seqRecording) stopSequenceRecording(); else startSequenceRecording();
    };
    seqPanel.onPlayToggle = [this]
    {
        if (seqPlayActive.load()) stopSequencePlayback(); else startSequencePlayback();
    };
    seqPanel.onClear = [this]
    {
        const int i = sequenceIndexOfId (selSeqId);
        if (i < 0 || seqRecording) return;
        beginEdit ("Clear the performance sequence");
        {
            const juce::ScopedLock l (dataLock);
            sequences[(size_t) i].points.clear();
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshSequencePanel();
        flash ("Sequence cleared");
    };
    seqPanel.onLoopToggle = [this]{ toggleSequenceLoop(); };
    seqPanel.onPointsEdited = [this] (const std::vector<upic::PerformancePoint>& pts)
    {
        const int i = sequenceIndexOfId (selSeqId);
        if (i < 0 || seqRecording) return;
        beginEdit ("Move a performance point");
        {
            const juce::ScopedLock l (dataLock);
            sequences[(size_t) i].points = pts;
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshSequencePanel();
        flash ("Performance point moved");
    };
    seqPanel.onTrim = [this]
    {
        const int i = sequenceIndexOfId (selSeqId);
        const int j = seqPanel.selectedPointForTest();
        if (i < 0 || j < 0 || j >= (int) sequences[(size_t) i].points.size() || seqRecording)
            return;
        const double cut = sequences[(size_t) i].points[(size_t) j].t;
        beginEdit ("Trim the performance sequence");
        {
            const juce::ScopedLock l (dataLock);
            auto& pts = sequences[(size_t) i].points;
            pts.erase (std::remove_if (pts.begin(), pts.end(),
                                       [cut] (const upic::PerformancePoint& p)
                                       { return p.t > cut; }),
                       pts.end());
        }
        markModelChanged();
        endEdit();
        publishModelSnapshot();
        refreshSequencePanel();
        flash ("The sequence is trimmed after the selected point");
    };
    seqPanel.onReplace = [this]
    {
        const int i = sequenceIndexOfId (selSeqId);
        const int j = seqPanel.selectedPointForTest();
        if (i < 0 || j < 0 || j >= (int) sequences[(size_t) i].points.size() || seqRecording)
            return;
        seqReplaceFromT = sequences[(size_t) i].points[(size_t) j].t;
        startSequenceRecording();
    };

    refreshLibraryBoxes();
    if (seqPanel.library.source() == LibraryManager::Source::User)
        refreshUserLibraryBoxes (upic::UserAssetKind::Sequence);

    refreshSequencePanel();
    seqPanel.setVisible (true);
    seqPanel.toFront (true);
    seqPanel.grabKeyboardFocus();
    seqPanel.setBounds (getLocalBounds());
}

void MainComponent::closeSequencePanel()
{
    seqPanel.setVisible (false);
    grabKeyboardFocus();
}

void MainComponent::startSequenceRecording()
{
    int idx = -1;
    bool needsConfirm = false;
    {
        const juce::ScopedLock l (dataLock);
        idx = sequenceIndexOfId (selSeqId);
        if (idx < 0)
        {
            if ((int) sequences.size() >= upic::limits::kMaxSequences)
            {
                flash ("The sequence bank is full - delete one first");
                return;
            }
            upic::PerformanceSequence s;
            s.id = upic::nextBankId (sequences, 1);
            if (s.id < 1 || s.id > upic::limits::kMaxItemId)
            {
                flash ("The sequence bank has no free id left");
                return;
            }
            s.name = "Sequence " + juce::String (s.id);
            sequences.push_back (s);
            idx = (int) sequences.size() - 1;
            selSeqId = sequences[(size_t) idx].id;
        }
        needsConfirm = seqReplaceFromT < 0.0 && ! sequences[(size_t) idx].points.empty();
    }

    // WP4 fix: a recording that replaces existing material asks twice; the
    // flash is the confirmation, not a silent overwrite.
    if (needsConfirm)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (seqOverwriteArmedMs < 0.0 || now - seqOverwriteArmedMs > 5000.0)
        {
            seqOverwriteArmedMs = now;
            flash ("Press Record again within 5 s to overwrite the performance");
            return;
        }
    }
    seqOverwriteArmedMs = -1.0;

    // WP4 fix: one undo transaction spans the whole take.
    if (! seqEditOpen)
    {
        beginEdit ("Record the performance");
        seqEditOpen = true;
    }
    {
        const juce::ScopedLock l (dataLock);
        auto& s = sequences[(size_t) idx];
        if (seqReplaceFromT >= 0.0)
        {
            const double cut = seqReplaceFromT;
            s.points.erase (std::remove_if (s.points.begin(), s.points.end(),
                                            [cut] (const upic::PerformancePoint& p)
                                            { return p.t >= cut; }),
                            s.points.end());
            seqRecordTimeOffset = cut;
            // the anchor at the cut: the head ends where the new take
            // begins, so the replaced span is not bridged by a glide
            s.points.push_back ({ cut, currentPage, displayPlayhead() });
        }
        else
        {
            s.points.clear();
            seqRecordTimeOffset = 0.0;
            s.points.push_back ({ 0.0, currentPage, displayPlayhead() });
        }
    }
    seqReplaceFromT = -1.0;
    seqRecording = true;
    seqDecimated = false;
    seqRecordStartMs = juce::Time::getMillisecondCounterHiRes();
    seqPlayActive.store (false, std::memory_order_relaxed);
    markModelChanged();
    publishModelSnapshot();
    refreshSequencePanel();
    flash ("Recording - play or scrub, then press Stop");
}

void MainComponent::sequenceAppendRecorderPoint (double t, int page, double pos)
{
    const int idx = sequenceIndexOfId (selSeqId);
    if (idx < 0) return;
    auto& s = sequences[(size_t) idx];
    // WP4 fix: at the point cap, thin the take instead of silently refusing
    // the rest of the performance (every other point, keeping the last).
    if ((int) s.points.size() >= upic::limits::kMaxSequencePoints)
    {
        if (s.points.size() < 2) return;
        std::vector<upic::PerformancePoint> kept;
        kept.reserve (s.points.size() / 2 + 1);
        for (size_t i = 0; i < s.points.size(); i += 2) kept.push_back (s.points[i]);
        if (kept.back().t < s.points.back().t) kept.push_back (s.points.back());
        s.points = std::move (kept);
        seqDecimated = true;
    }
    if (! s.points.empty())
    {
        const auto& last = s.points.back();
        if (page == last.page && std::abs (pos - last.pos) < 0.002
            && t - last.t < 0.05)
            return;
    }
    s.points.push_back ({ juce::jmax (0.0, t), page, pos });
}

void MainComponent::sequenceRecordTick()
{
    if (! seqRecording) return;
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - seqRecordStartMs) * 0.001;
    const double t = seqRecordTimeOffset + juce::jmax (0.0, elapsed);
    if (t > (double) upic::limits::kMaxSequenceSeconds)
    {
        stopSequenceRecording();
        flash ("The sequence reached its length limit");
        return;
    }
    sequenceAppendRecorderPoint (t, currentPage, displayPlayhead());
}

void MainComponent::stopSequenceRecording()
{
    if (! seqRecording) return;
    seqRecording = false;
    const int idx = sequenceIndexOfId (selSeqId);
    if (idx >= 0)
    {
        const juce::ScopedLock l (dataLock);
        auto& s = sequences[(size_t) idx];
        std::sort (s.points.begin(), s.points.end(),
                   [] (const upic::PerformancePoint& a, const upic::PerformancePoint& b)
                   { return a.t < b.t; });
    }
    markModelChanged();
    if (seqEditOpen)
    {
        endEdit();
        seqEditOpen = false;
    }
    seqRecordTimeOffset = 0.0;
    publishModelSnapshot();
    refreshSequencePanel();
    const juce::String note = seqDecimated ? " (thinned to fit the point cap)" : juce::String();
    seqDecimated = false;
    flash (idx >= 0 ? "Performance recorded: "
                        + juce::String ((int) sequences[(size_t) idx].points.size())
                        + " points" + note
                    : juce::String ("Recording stopped"));
}

void MainComponent::startSequencePlayback()
{
    const int idx = sequenceIndexOfId (selSeqId);
    if (idx < 0 || sequences[(size_t) idx].points.size() < 2)
    {
        flash ("Record a performance first");
        return;
    }
    if (seqRecording) stopSequenceRecording();

    seqActiveId.store (sequences[(size_t) idx].id, std::memory_order_relaxed);
    seqClock.store (0.0, std::memory_order_relaxed);
    seqPlayActive.store (true, std::memory_order_relaxed);
    publishModelSnapshot();
    startPlayback();
    refreshSequencePanel();
    flash ("Playing the performance");
}

void MainComponent::stopSequencePlayback()
{
    seqPlayActive.store (false, std::memory_order_relaxed);
    seqClock.store (0.0, std::memory_order_relaxed);
    stopPlayback();
    refreshSequencePanel();
}

void MainComponent::toggleSequenceLoop()
{
    const int idx = sequenceIndexOfId (selSeqId);
    if (idx < 0) return;
    beginEdit ("Sequence loop");
    {
        const juce::ScopedLock l (dataLock);
        sequences[(size_t) idx].loop = ! sequences[(size_t) idx].loop;
    }
    markModelChanged();
    endEdit();
    publishModelSnapshot();
    refreshSequencePanel();
    flash (sequences[(size_t) idx].loop ? "Sequence loops" : "Sequence plays once");
}
// --- WP6.4: the mixer commands ---
// NOTE FOR WP7: keep the library/extraction host commands in their own
// marked section; these functions only touch the channel bank.

int MainComponent::channelIndexOfId (int id) const
{
    for (int i = 0; i < (int) channels.size(); ++i)
        if (channels[(size_t) i].id == id) return i;
    return -1;
}

void MainComponent::refreshChannelBox()
{
    channelBoxRefreshing = true;
    channelBox.clear (juce::dontSendNotification);
    channelBox.addItem ("Main", 1);
    for (int i = 0; i < (int) channels.size(); ++i)
        channelBox.addItem (channels[(size_t) i].name, i + 2);
    int cur = 0;
    const auto ids = score.getSelectedArcs();
    if (! ids.empty())
        for (auto& a : curPage().arcs)
            if (a.id == ids.front()) { cur = a.channelId; break; }
    const int idx = channelIndexOfId (cur);
    channelBox.setSelectedId (idx < 0 ? 1 : idx + 2, juce::dontSendNotification);
    channelBoxRefreshing = false;
}

void MainComponent::setChannelOnSelection (int channelId)
{
    if (channelId > 0 && channelIndexOfId (channelId) < 0)
    { flash ("That channel is not in the mixer"); return; }
    const auto ids = score.getSelectedArcs();
    if (ids.empty()) { flash ("Select arcs first"); return; }
    beginEdit ("Routing the arcs");
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
            if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                a.channelId = channelId;
    }
    markModelChanged();
    endEdit();
    publishModelSnapshot();
    refreshChannelBox();
    score.repaint();
    flash (channelId == 0 ? "Arcs on Main"
                          : "Arcs on " + channels[(size_t) juce::jmax (0, channelIndexOfId (channelId))].name);
}

void MainComponent::refreshChannelPanel()
{
    if (channelPanel != nullptr)
        channelPanel->setChannels (channels, juce::jmax (0, channelIndexOfId (selChannelId)));
}

void MainComponent::showChannelPanel()
{
    auto colourFor = [] (int id)
    {
        static const juce::uint32 palette[] = { 0xff5ac8fa, 0xff9a7ad0, 0xff6ac08a,
                                                0xffd0a050, 0xffd07272, 0xff50b8b8 };
        return juce::Colour (palette[(size_t) (juce::jmax (0, id - 1) % 6)]);
    };

    // WP6.4 safety: construct lazily, on the message thread, once.
    if (channelPanel == nullptr)
    {
        channelPanel = std::make_unique<ChannelPanel>();
        addChildComponent (*channelPanel);
        channelPanel->onClose = [this]{ closeChannelPanel(); };
        channelPanel->themeSliders();
    }
    channelPanel->onSelectIndex = [this] (int i)
    {
        if (i >= 0 && i < (int) channels.size()) selChannelId = channels[(size_t) i].id;
        refreshChannelBox();
    };
    channelPanel->onRename = [this] (int i, const juce::String& name)
    {
        if (i < 0 || i >= (int) channels.size() || name.isEmpty()) return;
        beginEdit ("Rename the channel");
        { const juce::ScopedLock l (dataLock); channels[(size_t) i].name = name.substring (0, 64); }
        markModelChanged(); endEdit(); publishModelSnapshot();
        refreshChannelPanel(); refreshChannelBox();
    };
    channelPanel->onMuteSolo = [this] (int i, bool muted, bool solo)
    {
        if (i < 0 || i >= (int) channels.size()) return;
        beginEdit ("Channel mute/solo");
        { const juce::ScopedLock l (dataLock); channels[(size_t) i].muted = muted; channels[(size_t) i].solo = solo; }
        markModelChanged(); endEdit(); publishModelSnapshot(); refreshChannelPanel(); score.repaint();
    };
    channelPanel->onGain = [this] (int i, float gain)
    {
        if (i < 0 || i >= (int) channels.size()) return;
        beginEdit ("Channel gain");
        { const juce::ScopedLock l (dataLock); channels[(size_t) i].gain = juce::jlimit (0.0f, 2.0f, gain); }
        markModelChanged(); endEdit(); publishModelSnapshot(); score.repaint();
    };
    channelPanel->onNew = [this, colourFor]
    {
        if ((int) channels.size() >= upic::limits::kMaxChannels) { flash ("The mixer is full"); return; }
        beginEdit ("New channel");
        int newId = 1;
        {
            const juce::ScopedLock l (dataLock);
            newId = juce::jlimit (1, upic::limits::kMaxItemId, nextChannelId);
            nextChannelId = juce::jmin (upic::limits::kMaxItemId, newId + 1);
            upic::Channel c;
            c.id = newId;
            c.name = "Channel " + juce::String (newId);
            c.colour = colourFor (newId);
            channels.push_back (c);
        }
        selChannelId = newId;
        markModelChanged(); endEdit(); publishModelSnapshot();
        refreshChannelPanel(); refreshChannelBox();
        flash ("Channel " + juce::String (newId) + " added");
    };
    channelPanel->onDelete = [this]
    {
        const int i = channelIndexOfId (selChannelId);
        if (i < 0) return;
        const int removedId = channels[(size_t) i].id;
        beginEdit ("Delete the channel");
        {
            const juce::ScopedLock l (dataLock);
            channels.erase (channels.begin() + i);
            for (auto& pg : pages)
            {
                for (auto& a : pg.arcs) if (a.channelId == removedId) a.channelId = 0;
                for (auto& g : pg.groups) if (g.channelId == removedId) g.channelId = -1;
            }
        }
        selChannelId = channels.empty() ? -1 : channels.front().id;
        markModelChanged(); endEdit(); publishModelSnapshot();
        refreshChannelPanel(); refreshChannelBox(); score.repaint();
        flash ("Channel deleted - its arcs return to Main");
    };

    refreshChannelPanel();
    channelPanel->setVisible (true);
    channelPanel->toFront (true);
    channelPanel->setBounds (getLocalBounds());
}

void MainComponent::closeChannelPanel()
{
    if (channelPanel != nullptr) channelPanel->setVisible (false);
    grabKeyboardFocus();
}
// --- end WP6.4 ---
//---------------- WP5: the independent modulating arcs ----------------

bool MainComponent::modWouldCycle (int carrierId, int modId) const
{
    int cur = modId;
    for (int guard = 0; guard < 64 && cur >= 0; ++guard)
    {
        if (cur == carrierId) return true;
        const upic::Arc* a = nullptr;
        for (auto& x : curPage().arcs) if (x.id == cur) { a = &x; break; }
        if (a == nullptr) return false;
        cur = a->modulatorArcId;
    }
    return false;
}

void MainComponent::refreshModBox()
{
    arcModBox.clear (juce::dontSendNotification);
    arcModBox.addItem ("None", 1);
    for (auto& a : curPage().arcs)
        arcModBox.addItem ("Arc " + juce::String (a.id), a.id + 1);

    int cur = -1, target = 0;
    double depth = 0.5;
    const auto ids = score.getSelectedArcs();
    if (! ids.empty())
        for (auto& a : curPage().arcs)
            if (a.id == ids.front())
            {
                cur = a.modulatorArcId;
                target = a.modTarget;
                depth = a.modArcDepth;
                break;
            }
    arcModBox.setSelectedId (cur < 0 ? 1 : cur + 1, juce::dontSendNotification);
    arcModTgtBox.setSelectedId (target == 1 ? 2 : 1, juce::dontSendNotification);
    arcModSlider.setValue (depth, juce::dontSendNotification);
}

void MainComponent::setModOnSelection (int modId, int target, double depth)
{
    const auto ids = score.getSelectedArcs();
    if (ids.empty()) { flash ("Select an arc first"); return; }
    int applied = 0, refused = 0;
    beginEdit ("Modulating arc");
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
            if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
            {
                if (modId >= 0 && (modId == a.id || modWouldCycle (a.id, modId)))
                { ++refused; continue; }
                a.modulatorArcId = modId;
                a.modTarget = juce::jlimit (0, 1, target);
                a.modArcDepth = juce::jlimit (0.0, 1.0, depth);
                ++applied;
            }
    }
    markModelChanged();
    endEdit();
    publishModelSnapshot();
    refreshModBox();
    score.repaint();
    if (refused > 0) flash ("A modulator cannot loop back to the arc it drives");
    else if (applied > 0) flash (modId < 0 ? "Modulator cleared" : "Modulator assigned");
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
        const bool drawn = curPage().spaceDefs[(size_t) i].editable;
        spaceBox.addItem (juce::String (i + 1) + ": " + juce::String (presets[(size_t) p].name)
                            + (drawn ? " (drawn)" : juce::String()),
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
        {
            juce::ScopedLock l(dataLock);
            if (page < 0 || page >= (int) pages.size()) return;
            currentPage = page;
        }
        refreshBoxes();
        refreshScoreBinding();
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
        // v1.33.4 (H9): the device is opened by its identifier - two devices
        // can have the same name, and a name can change.
        const juce::String devId = (id <= 1 || (size_t) (id - 2) >= midiDeviceIds.size())
                                     ? juce::String() : midiDeviceIds[(size_t) (id - 2)];
        const bool ok = midi.open (devId);
        if (! ok)
        {
            flash ("Could not open the MIDI input");
            midiDevBox.setSelectedId (1);
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
    matOscTog.setToggleState (true, juce::dontSendNotification);
    matOscTog.onClick    = [this]{ setMaterial(0); };
    matSampleTog.onClick = [this]{ setMaterial(1); };
    matLiveTog.onClick   = [this]{ setMaterial(2); };
    matPmTog.onClick    = [this]{ setMaterial(3); };
    refSlider.setName("refSlider");
    refSlider.setNormalisableRange(juce::NormalisableRange<double>(20.0, 4000.0, 0.01, 0.4));
    refSlider.setValue(upic::kDefaultRefFreq);
    refSlider.setTextValueSuffix(" Hz");
    refSlider.setTooltip("Reference pitch: the 1x pitch of the material and the grid's 1x.\n"
                         "On the Sample tab it is the SLOT's Ref (the material's base\n"
                         "pitch - every arc that plays the slot follows it); on the Live\n"
                         "tab it retunes the selected arc(s) and sets the reference for\n"
                         "new strokes.");
    refSlider.onValueChange = [this]{
        drawRef = refSlider.getValue();
        const auto ids = score.getSelectedArcs();
        bool changed = false;
        if (drawMatMode == 1 && selSlot >= 0 && selSlot < (int) sampleSlots.size())
        {
            // M5: the Sample tab's Ref is the SLOT's - the material's base
            // pitch (the UPIC's per-slot REF). Every arc that plays the
            // slot follows it, on every page: the material has one unity
            // point, so every arc on it shares it.
            const int sid = sampleSlots[(size_t) selSlot].id;
            juce::ScopedLock l(dataLock);
            sampleSlots[(size_t) selSlot].refFreq = drawRef;
            changed = true;
            for (auto& pg : pages)
                for (auto& a : pg.arcs)
                    if (a.mode == 1 && a.sampleId == sid)
                        a.refFreq = drawRef;
        }
        else if (! ids.empty())
        {
            // the Live tab: the selected arcs (the old behaviour)
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
            // WP7: the slot Ref is part of the captured material's identity.
            // Invalidate an extraction source before publishing the new Ref.
            if (drawMatMode == 1 && selSlot >= 0 && selSlot < (int) sampleSlots.size())
                invalidateSampleExtractSource();
            publishModelSnapshot();
            score.repaint();
        }
        pushVoice();
    };
    tempoSlider.setNormalisableRange (juce::NormalisableRange<double> (40.0, 240.0, 1.0, 0.5));
    tempoSlider.setValue (120.0);
    tempoSlider.setName ("tempoSlider");
    tempoSlider.onValueChange = [this]
    {
        {
            const juce::ScopedLock sl (dataLock);
            curPage().tempoBpm = (double) tempoSlider.getValue();
        }
        refreshBoxes();
    };
}

void MainComponent::buildPmControls()
{
    pmRatioSlider.setName ("pmRatioSlider");
    pmRatioSlider.setNormalisableRange (juce::NormalisableRange<double> (0.1, 32.0, 0.01, 0.4));
    pmRatioSlider.setValue (2.0);
    pmIndexSlider.setName ("pmIndexSlider");
    pmIndexSlider.setNormalisableRange (juce::NormalisableRange<double> (0.0, 20.0, 0.01, 0.4));
    pmIndexSlider.setValue (1.5);
    pmFeedbackSlider.setNormalisableRange (juce::NormalisableRange<double> (0.0, 1.0, 0.001, 0.4));
    pmFeedbackSlider.setValue (0.0);
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
    pmPresetBox.setSelectedId (1);
    pmOpsBox.addItem ("2-op", 1); pmOpsBox.addItem ("4-op", 2); pmOpsBox.setSelectedId (1);
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
        // v1.33.4 (H12): the chooser has to outlive the asynchronous
        // selection. A stack-local FileChooser is destroyed when this
        // handler returns, and JUCE drops the pending callback with it
        // (FileChooser::~FileChooser clears asyncCallback), so the import
        // never ran at all. The shared_ptr in the lambda keeps the chooser
        // alive until the callback has been delivered; the safe pointer
        // covers the case that the window is gone before that happens.
        juce::Component::SafePointer<MainComponent> sp (this);
        auto fc = std::make_shared<juce::FileChooser> ("Import PM preset", juce::File(), "*.preset");
        fc->launchAsync (juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
                         [sp, fc] (const juce::FileChooser& c) {
            if (sp == nullptr) return;
            // v1.33.4 (M17): the chooser can return a document-provider URL
            // (Android) - getResult() drops it (it keeps only local files),
            // so the import did nothing at all. The URL is read instead, and
            // a provider document is copied into a local file first.
            upic::PickedFile picked;
            juce::String error;
            if (! upic::readPickedFile (c.getURLResult(), picked,
                                        (juce::int64) upic::limits::kMaxPieceBytes, error))
            {
                sp->flash (error);
                return;
            }
            sp->applyPmPresetFile (picked.file);
        });
    };
    pmExportBtn.onClick = [this]{
        if (score.getSelectedArcs().empty()) { flash ("Select a PM arc first"); return; }
        const juce::String text = buildPmPresetText();
        if (text.isEmpty()) { flash ("Selected arc is not PM"); return; }
        // v1.33.4 (H12): the chooser has to outlive the asynchronous
        // selection - see the import above.
        juce::Component::SafePointer<MainComponent> sp (this);
        auto fc = std::make_shared<juce::FileChooser> ("Export PM preset", juce::File(), "*.preset");
        fc->launchAsync (juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles,
                         [sp, fc, text] (const juce::FileChooser& c) {
            if (sp == nullptr) return;
            // v1.33.4 (M17): the destination can be a document-provider URL
            // (Android) - getResult() would drop it and the export would do
            // nothing. A local destination keeps the old behaviour (the
            // extension is forced, the write goes through a temporary
            // sibling); a provider document is written through its stream.
            const auto url = upic::pickedURLWithExtension (c.getURLResult(), "preset");
            if (! url.isWellFormed()) return;
            juce::String error;
            if (! upic::writePickedBytes (url, text.toRawUTF8(),
                                          (size_t) text.getNumBytesAsUTF8(), error))
            {
                sp->flash (error);
                return;
            }
            sp->flash ("Exported " + upic::pickedFileName (url));
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
        pmRatioSlider.setValue (ratio);
        pmIndexSlider.setValue (idx);
        { int ei = 1; for (size_t j = 0; j < envelopes.size(); ++j) if (envelopes[j].id == envId) { ei = (int) j + 2; break; } pmIndexEnvBox.setSelectedId (ei); }
        pmFeedbackSlider.setValue (fb);
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
        // M5: the Sample tab's Ref is the slot's, and the material's
        // buttons follow the slot.
        syncRefToSlot();
        updateMaterialButtons();
        levelSlider.setValue (selSlot >= 0 && selSlot < (int) sampleSlots.size()
                                ? (double) sampleSlots[(size_t) selSlot].level : 1.0,
                              juce::dontSendNotification);
        updateRevButton(); // v1.19: the button shows the selected slot's state
    };
    loadSampleBtn.onClick = [this]{ loadSampleIntoSlot(selSlot); };

    // WP7: the cycle-aware extraction panel. The panel is built lazily when
    // it opens; this button only asks for it.
    sampleExtractBtn.setName ("sampleExtractBtn");
    sampleExtractBtn.setTooltip ("Find the repeating cycle in the selected material,\n"
                                  "preview it, and save it as a wavetable or envelope");
    sampleExtractBtn.onClick = [this]{ openSampleExtractPanel(); };

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
        // v1.26: the slot direction applies to the selected sample arcs too
        // (a multi-selection may use more than one slot). The selected slot
        // and the arcs' slots change under one model lock.
        const auto ids = score.getSelectedArcs();
        {
            const juce::ScopedLock sl (dataLock);
            sampleSlots[(size_t) selSlot].reversed = on;
            if (! ids.empty())
            {
                for (auto& a : curPage().arcs)
                {
                    if (a.mode != 1
                        || std::find (ids.begin(), ids.end(), a.id) == ids.end())
                        continue;
                    for (auto& s : sampleSlots)
                        if (s.id == a.sampleId) { s.reversed = on; break; }
                }
            }
        }
        updateRevButton();
        // WP7: reversing the material changes the captured audio. The
        // extraction source is stale until the user captures it again.
        invalidateSampleExtractSource();
        markModelChanged();  // v1.15: the material changes -> fade across it
        publishModelSnapshot(); // v1.18: the audio thread must see it now
        flash(on ? "Selected slot(s): reversed" : "Selected slot(s): forward");
    };
    updateRevButton();

    // --- M5: the slot's level and the material's own edits ---
    //
    // The level is the slot's own knob (the material's loudness). It
    // changes the sound, so it travels with the piece.
    levelSlider.setName("levelSlider");
    levelSlider.setNormalisableRange (juce::NormalisableRange<double> (0.0, 2.0, 0.01, 0.2));
    // The slot's level is already 1.0 in the model, so this is a view-only
    // initialisation. It must not notify: a notifying setValue defers the
    // handler to a later message-thread turn, and that stale callback would
    // then invalidate a sample-extraction preview armed after the widget was
    // built (WP7: the extraction follows the material's identity).
    levelSlider.setValue (1.0, juce::dontSendNotification);
    levelSlider.setTooltip ("The slot's level - how loud the material is "
                            "(1 = as it was loaded)");
    levelSlider.onValueChange = [this]{
        if (selSlot < 0 || selSlot >= (int) sampleSlots.size()) return;
        const float v = (float) levelSlider.getValue();
        {
            juce::ScopedLock l (dataLock);
            sampleSlots[(size_t) selSlot].level = v;
        }
        // WP7: the level is part of the captured material's identity. Do
        // not leave an extraction preview pointing at the old take.
        invalidateSampleExtractSource();
        markModelChanged();       // the level changes the sound
        publishModelSnapshot();   // the audio thread must see it now
    };

    // The material's edits. They are not a rewrite of the audio: the
    // normalise is a gain, the fades and the trim are read parameters, so
    // they can be undone (the app's undo stack and the Reset button).
    normMatBtn.setName ("normMatBtn");
    normMatBtn.setTooltip ("Normalize the material: the gain is set so the "
                           "loudest point of the material is just under full scale");
    normMatBtn.onClick = [this]{ editMaterial (1); };
    fadeMatBtn.setName ("fadeMatBtn");
    fadeMatBtn.setTooltip ("Fade the material in and out over a tenth of its "
                           "length, so the loop does not click");
    fadeMatBtn.onClick = [this]{ editMaterial (2); };
    resetMatBtn.setName ("resetMatBtn");
    resetMatBtn.setTooltip ("Undo the material's edits (the trim, the fades "
                            "and the normalise gain)");
    resetMatBtn.onClick = [this]{ editMaterial (3); };
    updateMaterialButtons();

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
    samplePitchBox.setSelectedId(1);
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
        // v1.33.4 (M21): the vocoder's working memory is prepared BEFORE the
        // audio thread can see the new mode
        prepareVocodersIfNeeded (pm, engine.getLivePitchMode());
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
            else monBtn.setToggleState (false, juce::dontSendNotification);
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
            else capBtn.setToggleState (false, juce::dontSendNotification);
        });
    };
    inGainSlider.setName("inGainSlider");
    inGainSlider.setRange(0.0, 1.0, 0.01);
    inGainSlider.setValue(0.8);
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
    pmRatioLab.setText ("Ratio", juce::dontSendNotification); pmIndexLab.setText ("Index", juce::dontSendNotification);
    // F11: the tempo group's disclosure toggle is captioned "Tempo", so the
    // control's own label carries the unit instead of repeating it.
    tempoLab.setText ("BPM", juce::dontSendNotification); tempoLab.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
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
    statusLab.setName ("statusLab");
    statusLab.setColour(juce::Label::textColourId, juce::Colour(0xff9a9aa5));
    statusLab.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    statusLab.setJustificationType(juce::Justification::centredRight);
}