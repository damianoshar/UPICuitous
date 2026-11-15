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
        // v1.33.4 (H9): the device is opened by its identifier - two devices
        // can have the same name, and a name can change.
        const juce::String devId = (id <= 1 || (size_t) (id - 2) >= midiDeviceIds.size())
                                     ? juce::String() : midiDeviceIds[(size_t) (id - 2)];
        const bool ok = midi.open (devId);
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
