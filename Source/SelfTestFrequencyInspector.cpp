#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "Knob.h"
#include "FreqSpace.h"
#include "FreqTableEditor.h"

#include <algorithm>
#include <vector>

//==============================================================================
// R5.7 (the review's R049, section 28): the frequency inspector - the contract.
//
// A frequency space is a range of the frequency axis: the position on the
// page's vertical axis is mapped into it, so the same drawing can be heard in
// another register, with its intervals scaled. The page owns four of them
// (the UPIC's four tables), and an arc names the one it is played in. The
// page also owns its ambitus (the interval its vertical axis covers) and its
// acceleration (a tempo sweep across the page).
//
// The review asks for the frequency controls to be decomposed:
//
//   "| `buildFrequencyControls()`, `pushSpaceDefs()`            | FrequencyInspector |"
//   (section 28)
//
// This suite is the frequency inspector's contract, and it comes before the
// move, as every phase in this wave does. It pins what the frequency
// controls do today:
//
// - the page card: the ambitus, the four frequency spaces and the table
//   button;
// - the pitch card: the arc's own space;
// - the acceleration card: the page's acceleration;
// - the rules: the ambitus's presets (and its "Custom"), the four spaces,
//   the acceleration's range and the table button's target.
//
// The suite drives the real component: it picks from the dropdowns, moves the
// knob and reads the page.

namespace
{
    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    const char* kindOf (juce::Component* c)
    {
        if (dynamic_cast<upic::Knob*> (c) != nullptr) return "Knob";
        if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) return "Toggle";
        if (dynamic_cast<juce::TextButton*> (c) != nullptr) return "Button";
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr) return "Combo";
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr) return "Edit";
        return "Control";
    }

    // One cell of a card: the control's name, its kind and where it sits.
    struct Cell
    {
        juce::String text;
        int bottom = 0;   // the row's bottom - the reading order
        int x = 0;
    };

    // The cells of a card, in the card's reading order (the row's bottom,
    // then the column). The card's disclosure toggle is the card's header,
    // not a cell, and a caption is not a cell either.
    std::vector<Cell> cardCells (MainComponent& mc, const juce::String& group)
    {
        const auto card = mc.toolbarCardBoundsForGroupForTest (group);

        std::vector<Cell> cells;
        for (int i = 0; i < mc.getNumChildComponents(); ++i)
        {
            auto* c = mc.getChildComponent (i);
            if (c == nullptr || ! c->isVisible()) continue;
            if (c->getName() == group + "FoldBtn") continue;
            const auto b = c->getBounds();
            if (b.isEmpty()) continue;
            if (! card.contains (b.getCentre())) continue;
            if (dynamic_cast<juce::Label*> (c) != nullptr) continue;

            Cell cell;
            cell.text = c->getName() + "|" + kindOf (c);
            cell.bottom = b.getBottom();
            cell.x = b.getX();
            cells.push_back (cell);
        }

        std::sort (cells.begin(), cells.end(), [] (const Cell& a, const Cell& b)
        {
            if (a.bottom != b.bottom) return a.bottom < b.bottom;
            return a.x < b.x;
        });
        return cells;
    }

    juce::String cardText (const std::vector<Cell>& cells)
    {
        juce::String out;
        for (auto& c : cells)
        {
            if (out.isNotEmpty()) out += ", ";
            out += c.text;
        }
        return out;
    }

    // Does the named control sit in the card?
    bool inCard (MainComponent& mc, const juce::String& group, const juce::String& name)
    {
        auto* c = findByName (mc, name);
        if (c == nullptr || ! c->isVisible()) return false;
        const auto card = mc.toolbarCardBoundsForGroupForTest (group);
        return card.contains (c->getBounds().getCentre());
    }
}

void upic::runFrequencyInspectorChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);
    auto& score = mc.scoreEditorForTest();

    // --- the controls --------------------------------------------------------
    auto* ambitusBox = dynamic_cast<juce::ComboBox*> (findByName (mc, "ambitusBox"));
    auto* spaceBox   = dynamic_cast<juce::ComboBox*> (findByName (mc, "spaceBox"));
    auto* warpSlider = dynamic_cast<upic::Knob*>      (findByName (mc, "warpSlider"));
    auto* tableBtn   = dynamic_cast<juce::TextButton*> (findByName (mc, "spaceTableBtn"));
    juce::ComboBox* spaceDefBox[4] = { nullptr, nullptr, nullptr, nullptr };
    for (int i = 0; i < 4; ++i)
        spaceDefBox[i] = dynamic_cast<juce::ComboBox*> (findByName (mc, "spaceDefBox" + juce::String (i)));

    check (ambitusBox != nullptr && spaceBox != nullptr && warpSlider != nullptr
           && tableBtn != nullptr && spaceDefBox[0] != nullptr
           && spaceDefBox[1] != nullptr && spaceDefBox[2] != nullptr
           && spaceDefBox[3] != nullptr,
           "R5.7 the frequency's controls are there");

    if (ambitusBox == nullptr || spaceBox == nullptr || warpSlider == nullptr
        || tableBtn == nullptr || spaceDefBox[0] == nullptr || spaceDefBox[1] == nullptr
        || spaceDefBox[2] == nullptr || spaceDefBox[3] == nullptr)
        return;

    // --- the cards -----------------------------------------------------------
    // The page's frequency settings are a card of their own: its ambitus and
    // its four frequency spaces.
    {
        const auto cells = cardCells (mc, "page");
        check (cardText (cells) == "ambitusBox|Combo, spaceDefBox0|Combo, "
                                   "spaceDefBox1|Combo, spaceDefBox2|Combo, "
                                   "spaceDefBox3|Combo, spaceTableBtn|Button",
               "R5.7 the page card holds the ambitus, the four spaces and the table button",
               cardText (cells));
    }
    // The arc's own space sits in the pitch card (with the snap and the step).
    check (inCard (mc, "pitch", "spaceBox"),
           "R5.7 the arc's own space sits in the pitch card");
    // The acceleration has a card of its own.
    {
        const auto cells = cardCells (mc, "accel");
        check (cardText (cells) == "warpSlider|Knob",
               "R5.7 the acceleration card holds the page's acceleration",
               cardText (cells));
    }

    // --- the ambitus ---------------------------------------------------------
    // The UPIC's normal ambitus was six octaves; it could be ten, or as
    // small as a semitone. "Custom" is the page's own interval.
    check (ambitusBox->getNumItems() == 7
           && ambitusBox->getItemText (0) == "The full range (20 Hz - 12 kHz)"
           && ambitusBox->getItemText (2) == "Six octaves (C1 - C7)"
           && ambitusBox->getItemText (5) == "A semitone (C4 - C#4)"
           && ambitusBox->getItemText (6) == "Custom",
           "R5.7 the ambitus's presets are the six, plus Custom",
           juce::String (ambitusBox->getNumItems()) + " items, last "
           + ambitusBox->getItemText (ambitusBox->getNumItems() - 1));

    ambitusBox->setSelectedId (6, juce::dontSendNotification);
    ambitusBox->onChange();
    check (std::abs (score.ambitusLo() - 261.63) < 1e-6 && score.ambitusHi() > score.ambitusLo(),
           "R5.7 the ambitus reaches the page",
           juce::String (score.ambitusLo(), 1) + " - " + juce::String (score.ambitusHi(), 1));

    // "Custom" is the page's own interval: picking it changes nothing
    ambitusBox->setSelectedId (7, juce::dontSendNotification);
    ambitusBox->onChange();
    check (std::abs (score.ambitusLo() - 261.63) < 1e-6,
           "R5.7 the Custom ambitus changes nothing",
           juce::String (score.ambitusLo(), 1));

    // back to the default interval
    ambitusBox->setSelectedId (1, juce::dontSendNotification);
    ambitusBox->onChange();
    check (std::abs (score.ambitusLo() - upic::kDefaultPitchLo) < 1e-6,
           "R5.7 the ambitus goes back to the default interval",
           juce::String (score.ambitusLo(), 1));

    // --- the four frequency spaces ------------------------------------------
    // The page owns four of them; an arc names the one it is played in.
    for (int i = 0; i < 4; ++i)
    {
        check (spaceDefBox[i]->getNumItems() == (int) upic::freqSpacePresets().size()
               && spaceDefBox[i]->getItemText (0) == "The page's range"
               && spaceDefBox[i]->getItemText (1) == "Half the range",
               "R5.7 a frequency space offers the space presets",
               juce::String (spaceDefBox[i]->getNumItems()) + " presets");
    }

    // a space definition reaches the page
    spaceDefBox[1]->setSelectedId (3, juce::dontSendNotification);
    spaceDefBox[1]->onChange();
    check (mc.pageSpaceForTest (1) == 2,
           "R5.7 a frequency space definition reaches the page",
           juce::String (mc.pageSpaceForTest (1)));

    // back to the default
    spaceDefBox[1]->setSelectedId (1, juce::dontSendNotification);
    spaceDefBox[1]->onChange();
    check (mc.pageSpaceForTest (1) == 0,
           "R5.7 a frequency space definition goes back to the default",
           juce::String (mc.pageSpaceForTest (1)));

    // the arc's own space lists the four spaces of the page
    check (spaceBox->getNumItems() == 4,
           "R5.7 the arc's space lists the four spaces", juce::String (spaceBox->getNumItems()));
    check (spaceBox->getItemText (0).startsWith ("1: "),
           "R5.7 the arc's space names the space it is", spaceBox->getItemText (0));

    // --- the acceleration ----------------------------------------------------
    check (warpSlider->getMinimum() == -0.9 && warpSlider->getMaximum() == 0.9
           && std::abs (warpSlider->getInterval() - 0.05) < 1e-9,
           "R5.7 the acceleration runs from -0.9 to 0.9 in steps of 0.05",
           juce::String (warpSlider->getMinimum(), 2) + " - "
           + juce::String (warpSlider->getMaximum(), 2));

    warpSlider->setValue (0.5, juce::dontSendNotification);
    warpSlider->onValueChange();
    check (std::abs (mc.pageWarpForTest() - 0.5) < 1e-9,
           "R5.7 the acceleration reaches the page", juce::String (mc.pageWarpForTest(), 3));

    warpSlider->setValue (0.0, juce::dontSendNotification);
    warpSlider->onValueChange();
    check (std::abs (mc.pageWarpForTest()) < 1e-9,
           "R5.7 the acceleration goes back to the default",
           juce::String (mc.pageWarpForTest(), 3));

    // --- the table button ----------------------------------------------------
    // "Pick space 2, press Table" is the whole gesture: the button edits the
    // space the arc's combo points at.
    spaceBox->setSelectedId (2, juce::dontSendNotification);
    tableBtn->onClick();
    check (mc.freqTableOpenForTest() && mc.freqTableSpaceForTest() == 1,
           "R5.7 the table button opens the table of the selected space",
           juce::String (mc.freqTableSpaceForTest()));

    // close it again, so the rest of the run sees the same window
    if (auto* ed = dynamic_cast<FreqTableEditor*> (findByName (mc, "freqTableEd")))
        if (ed->onClose) ed->onClose();
    check (! mc.freqTableOpenForTest(),
           "R5.7 the frequency table closes again");
}
