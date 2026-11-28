#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "Knob.h"
#include "SievePanel.h"

#include <algorithm>
#include <vector>

//==============================================================================
// R5.7 (the review's R049, section 28): the sieve panel - the contract.
//
// The sieve quantises the drawing's pitches: only the pitches n mod m in the
// remainder list are allowed, so the curve becomes a staircase of steps. The
// review asks for the frequency and the sieve controls to be decomposed:
//
//   "| `buildSieveControls()`                                   | SievePanel         |"
//   "| `buildFrequencyControls()`, `pushSpaceDefs()`            | FrequencyInspector |"
//   (section 28)
//
// This suite is the sieve panel's contract, and it comes before the move, as
// every phase in this wave does. It pins what the sieve does today:
//
// - the sieve card: the toggle, the preset library, the m and the R;
// - the magnets card: the stick and the time grid;
// - the sieve's own rules: the toggle, the modulus' domain, the remainder
//   list's format (the separators, the invalid tokens, the modulus), the
//   presets, the stickiness and the time grid's divisions;
// - the wording: every control says what it does.
//
// The suite drives the real component: it types into the fields, picks from
// the dropdowns and reads the canvas, which is where the sieve's state goes.

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

    // The same cells, sorted: the card engine moves the knobs after the
    // plain controls, so the card's reading order is not the edge's cell
    // order. The comparison that matters is the set of cells.
    juce::String sortedCardText (MainComponent& mc, const juce::String& group)
    {
        std::vector<juce::String> texts;
        for (auto& c : cardCells (mc, group)) texts.push_back (c.text);
        std::sort (texts.begin(), texts.end());
        juce::String out;
        for (auto& t : texts)
        {
            if (out.isNotEmpty()) out += ", ";
            out += t;
        }
        return out;
    }

    // The wording a control shows: a knob adds its live value under the
    // pointer's tooltip, so its wording is a prefix.
    juce::String tooltipOf (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) return t->getTooltip();
        return {};
    }
    bool tooltipIs (juce::Component* c, const char* wording)
    {
        return c != nullptr && tooltipOf (c) == juce::String (wording);
    }
    bool knobTooltipIs (juce::Component* c, const char* wording)
    {
        return c != nullptr && tooltipOf (c).startsWith (juce::String (wording));
    }

    // The remainders as text, so a failure says what the list was.
    juce::String remaindersText (const std::vector<int>& rs)
    {
        juce::String out;
        for (size_t i = 0; i < rs.size(); ++i)
        {
            if (i > 0) out += ",";
            out += juce::String (rs[i]);
        }
        return out;
    }
}

void upic::runSievePanelChecks (upic::SelfTest& t)
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
    auto* sieveBtn   = dynamic_cast<juce::ToggleButton*> (findByName (mc, "sieveBtn"));
    auto* sieveMEdit = dynamic_cast<juce::TextEditor*>   (findByName (mc, "sieveMEdit"));
    auto* sieveREdit = dynamic_cast<juce::TextEditor*>   (findByName (mc, "sieveREdit"));
    auto* sievePresets = dynamic_cast<juce::ComboBox*>  (findByName (mc, "sievePresetBox"));
    auto* stickSlider = dynamic_cast<upic::Knob*>        (findByName (mc, "stickSlider"));
    auto* gridBox     = dynamic_cast<juce::ComboBox*>    (findByName (mc, "gridBox"));
    check (sieveBtn != nullptr && sieveMEdit != nullptr && sieveREdit != nullptr
           && sievePresets != nullptr && stickSlider != nullptr && gridBox != nullptr,
           "R5.7 the sieve's controls are there");

    if (sieveBtn == nullptr || sieveMEdit == nullptr || sieveREdit == nullptr
        || sievePresets == nullptr || stickSlider == nullptr || gridBox == nullptr)
        return;

    // the sieve's defaults
    check (sieveMEdit->getText() == "12" && sieveREdit->getText() == "0",
           "R5.7 the sieve starts at m=12 with the octave's remainder",
           sieveMEdit->getText() + " / " + sieveREdit->getText());

    // --- the cards -----------------------------------------------------------
    // The sieve is its own card; the global magnets (the stick and the time
    // grid) are another one.
    {
        const auto cells = cardCells (mc, "sieve");
        check (cardText (cells) == "sieveBtn|Toggle, sievePresetBox|Combo, "
                                   "sieveMEdit|Edit, sieveREdit|Edit",
               "R5.7 the sieve card holds the sieve's own controls",
               cardText (cells));
    }
    {
        const auto cells = cardCells (mc, "magnets");
        check (cardText (cells) == "gridBox|Combo, stickSlider|Knob",
               "R5.7 the magnets card holds the stick and the time grid",
               cardText (cells));
    }

    // --- the toggle ----------------------------------------------------------
    sieveBtn->setToggleState (true, juce::dontSendNotification);
    sieveBtn->onClick();
    check (score.getSieveOn(), "R5.7 the sieve's toggle reaches the canvas");

    sieveBtn->setToggleState (false, juce::dontSendNotification);
    sieveBtn->onClick();
    check (! score.getSieveOn(), "R5.7 the sieve's toggle reaches the canvas both ways");

    // --- the modulus ---------------------------------------------------------
    sieveMEdit->setText ("7", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    check (score.getSieveM() == 7, "R5.7 the modulus reaches the canvas",
           juce::String (score.getSieveM()));

    // an invalid modulus keeps the last valid one: the field is being typed
    sieveMEdit->setText ("1", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    check (score.getSieveM() == 7, "R5.7 a modulus below the domain is refused",
           juce::String (score.getSieveM()));
    sieveMEdit->setText ("200", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    check (score.getSieveM() == 7, "R5.7 a modulus above the domain is refused",
           juce::String (score.getSieveM()));
    sieveMEdit->setText ("abc", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    check (score.getSieveM() == 7, "R5.7 a modulus that is not a number is refused",
           juce::String (score.getSieveM()));

    // --- the remainder list --------------------------------------------------
    sieveMEdit->setText ("12", juce::dontSendNotification);
    sieveMEdit->onTextChange();

    sieveREdit->setText ("0,2", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 0, 2 }),
           "R5.7 the remainder list reaches the canvas", remaindersText (score.getSieveR()));

    // the separators: a comma, a semicolon, a space
    sieveREdit->setText ("0; 4", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 0, 4 }),
           "R5.7 the remainder list takes any separator", remaindersText (score.getSieveR()));

    sieveREdit->setText ("0 7", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 0, 7 }),
           "R5.7 the remainder list takes a space", remaindersText (score.getSieveR()));

    // what is not a number is skipped, not refused
    sieveREdit->setText ("0,4,x,7", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 0, 4, 7 }),
           "R5.7 the remainder list skips what is not a number",
           remaindersText (score.getSieveR()));

    // the remainders are taken modulo the modulus
    sieveREdit->setText ("13", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 1 }),
           "R5.7 a remainder is taken modulo the modulus",
           remaindersText (score.getSieveR()));

    sieveREdit->setText ("-1", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 11 }),
           "R5.7 a negative remainder is taken modulo the modulus",
           remaindersText (score.getSieveR()));

    // an empty field keeps the last list: the placeholder made it look like
    // 0,7 while the real list was empty, so the checkbox quantised nothing
    sieveREdit->setText ("", juce::dontSendNotification);
    sieveREdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 11 }),
           "R5.7 an empty remainder field keeps the last list",
           remaindersText (score.getSieveR()));

    // the remainders follow the modulus: the field is read again with the
    // new m
    sieveREdit->setText ("13", juce::dontSendNotification);
    sieveREdit->onTextChange();
    sieveMEdit->setText ("7", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    check (score.getSieveR() == std::vector<int>({ 6 }),
           "R5.7 the remainder list follows the modulus",
           remaindersText (score.getSieveR()));

    // back to the defaults
    sieveMEdit->setText ("12", juce::dontSendNotification);
    sieveMEdit->onTextChange();
    sieveREdit->setText ("0", juce::dontSendNotification);
    sieveREdit->onTextChange();

    // --- the preset library --------------------------------------------------
    check (sievePresets->getNumItems() == 8
           && sievePresets->getItemText (0) == "Chromatic"
           && sievePresets->getTextWhenNothingSelected() == "Sieve preset",
           "R5.7 the sieve's presets are the library's",
           juce::String (sievePresets->getNumItems()) + " presets, first "
           + sievePresets->getItemText (0));

    // a preset sets its m and its R, and switches the sieve on
    sievePresets->setSelectedId (2, juce::dontSendNotification);
    sievePresets->onChange();
    check (score.getSieveOn() && score.getSieveM() == 12
           && score.getSieveR() == std::vector<int>({ 0, 2, 4, 6, 8, 10 }),
           "R5.7 a sieve preset sets its m and its R and switches the sieve on",
           juce::String (score.getSieveM()) + " {"
           + remaindersText (score.getSieveR()) + "}");

    // the preset dropdown goes back to its placeholder
    check (sievePresets->getSelectedId() == 0,
           "R5.7 the sieve's preset dropdown goes back to its placeholder",
           juce::String (sievePresets->getSelectedId()));

    // --- the magnets ---------------------------------------------------------
    stickSlider->setValue (0.5, juce::dontSendNotification);
    stickSlider->onValueChange();
    check (std::abs (score.getStickiness() - 0.5) < 1e-9,
           "R5.7 the stickiness reaches the canvas",
           juce::String (score.getStickiness(), 3));

    check (stickSlider->getMinimum() == 0.0 && stickSlider->getMaximum() == 1.0
           && std::abs (stickSlider->getInterval() - 0.05) < 1e-9,
           "R5.7 the stickiness runs from 0 to 1 in steps of 0.05",
           juce::String (stickSlider->getMinimum(), 2) + " - "
           + juce::String (stickSlider->getMaximum(), 2));

    // --- the time grid -------------------------------------------------------
    check (gridBox->getNumItems() == 7
           && gridBox->getItemText (0) == "Off"
           && gridBox->getItemText (1) == "0.5/s"
           && gridBox->getItemText (2) == "1/s"
           && gridBox->getItemText (3) == "2/s"
           && gridBox->getItemText (4) == "4/s"
           && gridBox->getItemText (5) == "8/s"
           && gridBox->getItemText (6) == "16/s",
           "R5.7 the time grid's divisions are the seven",
           juce::String (gridBox->getNumItems()) + " divisions");

    gridBox->setSelectedId (4, juce::dontSendNotification);
    gridBox->onChange();
    check (std::abs (score.getTimeGrid() - 2.0) < 1e-9,
           "R5.7 the time grid reaches the canvas", juce::String (score.getTimeGrid(), 3));

    gridBox->setSelectedId (1, juce::dontSendNotification);
    gridBox->onChange();
    check (std::abs (score.getTimeGrid()) < 1e-9,
           "R5.7 the time grid can be off", juce::String (score.getTimeGrid(), 3));

    // --- the wording ---------------------------------------------------------
    // Every control says what it does, and the wording is the panel's own.
    check (! sieveBtn->getTooltip().isEmpty()
           && ! sieveMEdit->getTooltip().isEmpty()
           && ! sieveREdit->getTooltip().isEmpty()
           && ! sievePresets->getTooltip().isEmpty()
           && ! stickSlider->getTooltip().isEmpty()
           && ! gridBox->getTooltip().isEmpty(),
           "R5.7 every sieve control says what it does");

    check (sieveMEdit->getTextToShowWhenEmpty() == "m=12"
           && sieveREdit->getTextToShowWhenEmpty() == "0,7",
           "R5.7 the sieve's fields show their shape when they are empty",
           sieveMEdit->getTextToShowWhenEmpty() + " / "
           + sieveREdit->getTextToShowWhenEmpty());

    // --- the edge and the app ------------------------------------------------
    // R5.7: the panel's description (Source/SievePanel.h) and the app's
    // binding must agree. Every role is bound to a widget, the app's cards
    // are the edge's cell lists, and the wording the controls show is the
    // edge's wording.
    {
        namespace sp = upic::sieve_panel;

        bool allBound = true;
        for (auto role : sp::allControls())
            if (mc.sieveControlForTest ((int) role) == nullptr) allBound = false;
        check (allBound && mc.sieveControlCountForTest() == (int) sp::Control::count,
               "R5.7 every sieve role is bound to a widget",
               juce::String (mc.sieveControlCountForTest()) + " roles");

        // the app's cards are the edge's cell lists
        const auto& panel = sp::describe();
        auto expected = [&] (const std::vector<sp::Cell>& cells)
        {
            std::vector<juce::String> texts;
            for (auto& c : cells)
            {
                auto* w = mc.sieveControlForTest ((int) c.control);
                if (w == nullptr) continue;
                texts.push_back (w->getName() + "|" + kindOf (w));
            }
            std::sort (texts.begin(), texts.end());
            juce::String out;
            for (auto& t : texts)
            {
                if (out.isNotEmpty()) out += ", ";
                out += t;
            }
            return out;
        };
        const auto sieveCells = cardCells (mc, "sieve");
        const auto magnetCells = cardCells (mc, "magnets");
        check (sortedCardText (mc, "sieve") == expected (panel.card),
               "R5.7 the sieve card is the edge's cell list",
               sortedCardText (mc, "sieve"));
        check (sortedCardText (mc, "magnets") == expected (panel.magnets),
               "R5.7 the magnets card is the edge's cell list",
               sortedCardText (mc, "magnets"));

        // a cell's kind agrees with the widget it is bound to
        bool kindsOk = true;
        auto checkCell = [&] (const sp::Cell& c)
        {
            auto* w = mc.sieveControlForTest ((int) c.control);
            auto* l = mc.sieveControlLabelForTest ((int) c.control);
            if (w == nullptr) { kindsOk = false; return; }
            if (c.kind == sp::Kind::Knob && dynamic_cast<upic::Knob*> (w) == nullptr)
                kindsOk = false;
            if (c.kind == sp::Kind::Inline && dynamic_cast<juce::Label*> (l) == nullptr)
                kindsOk = false;
            if (c.kind == sp::Kind::Labelled && dynamic_cast<juce::Label*> (l) == nullptr)
                kindsOk = false;
        };
        for (auto& c : panel.card) checkCell (c);
        for (auto& c : panel.magnets) checkCell (c);
        check (kindsOk, "R5.7 the sieve's cells and the app's widgets agree");

        // the wording the controls show is the edge's wording (the knob
        // adds its live value under the pointer's tooltip)
        check (tooltipIs (sieveBtn, sp::tooltip (sp::Control::On))
               && tooltipIs (sieveMEdit, sp::tooltip (sp::Control::Modulus))
               && tooltipIs (sieveREdit, sp::tooltip (sp::Control::Remainders))
               && tooltipIs (sievePresets, sp::tooltip (sp::Control::Preset))
               && knobTooltipIs (stickSlider, sp::tooltip (sp::Control::Stickiness))
               && tooltipIs (gridBox, sp::tooltip (sp::Control::TimeGrid)),
               "R5.7 the sieve's controls say what the edge says");

        // the time grid's divisions are one list: the dropdown offers the
        // edge's divisions, and the edge finds every one of them back
        const auto& divs = sp::timeGridDivisions();
        bool same = gridBox->getNumItems() == (int) divs.size();
        bool foundBack = true;
        for (int i = 0; i < (int) divs.size(); ++i)
        {
            if (gridBox->getItemText (i) != divs[(size_t) i].label) same = false;
            if (sp::timeGridDivisionIndex (divs[(size_t) i].value) != i) foundBack = false;
        }
        check (same, "R5.7 the time grid's dropdown is the edge's list");
        check (foundBack, "R5.7 the edge finds every division back");

        // the remainder list's text format is the edge's
        check (sieveREdit->getText() == juce::String (sp::formatRemainders (score.getSieveR())),
               "R5.7 the remainder field is the edge's text format",
               sieveREdit->getText());
        check (sp::formatRemainders ({ 0, 4, 7 }) == "0,4,7",
               "R5.7 the edge writes the remainder list");

        // and the edge reads the list the way the app does
        check (sp::parseRemainders ("0; 4", 12) == std::vector<int>({ 0, 4 }),
               "R5.7 the edge reads the separators");
        check (sp::parseRemainders ("0,4,x,7", 12) == std::vector<int>({ 0, 4, 7 }),
               "R5.7 the edge skips what is not a number");
        check (sp::parseRemainders ("-1", 12) == std::vector<int>({ 11 }),
               "R5.7 the edge takes a remainder modulo the modulus");
        check (sp::parseRemainders ("13", 7) == std::vector<int>({ 6 }),
               "R5.7 the edge takes a remainder modulo the modulus");

        // the modulus' domain is the edge's
        check (sp::kMinModulus == 2 && sp::kMaxModulus == 120,
               "R5.7 the modulus' domain is the edge's",
               juce::String (sp::kMinModulus) + " - " + juce::String (sp::kMaxModulus));
    }
}
