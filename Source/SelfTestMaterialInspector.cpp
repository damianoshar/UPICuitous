#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "Knob.h"
#include "MaterialInspector.h"

#include <algorithm>
#include <map>
#include <vector>

//==============================================================================
// R5.6 (the review's R048, section 28): the material inspector - the contract.
//
// The material inspector is the tab strip above the canvas (Osc | Sample |
// Live | PM) and the material card under it: the current tab's own controls.
// The review asks for the inspector to be decomposed:
//
//   "| `buildMaterialControls()`                                | MaterialInspector          |"
//   "| `buildPmControls()`                                      | PhaseModulationPanel       |"
//   "| `buildSampleControls()`                                  | SampleMaterialPanel        |"
//   "| `buildLiveInputControls()`                               | LiveInputPanel             |"
//   (section 28)
//
// This suite is the inspector's contract, and it comes before the move, as
// every phase in this wave does. It pins what the inspector does today:
//
// - the four tabs: exactly one is on, and the tab follows the material;
// - the hint: each material explains itself in the sidebar;
// - the canvas: the material is the canvas's layer;
// - the material card: each material's own controls, in the card's order;
// - the shared controls: the Ref belongs to the Sample and the Live tab, the
//   carrier wave to the Osc and the PM tab.
//
// The suite drives the real component: it clicks the tab buttons and reads
// the card the layout built.

namespace
{
    // --- the component's children --------------------------------------------

    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    juce::TextButton* findButton (juce::Component& c, const juce::String& text)
    {
        for (int i = 0; i < c.getNumChildComponents(); ++i)
        {
            auto* child = c.getChildComponent (i);
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
                if (b->getButtonText() == text) return b;
        }
        return nullptr;
    }

    // --- the material card ---------------------------------------------------

    // One cell of the material card: the caption above the control and the
    // control's kind.
    struct Cell
    {
        juce::String text;
        int bottom = 0;   // the row's bottom - the reading order
        int x = 0;
    };

    const char* kindOf (juce::Component* c)
    {
        if (dynamic_cast<upic::Knob*> (c) != nullptr) return "Knob";
        if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) return "Toggle";
        if (dynamic_cast<juce::TextButton*> (c) != nullptr) return "Button";
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr) return "Combo";
        if (dynamic_cast<juce::Label*> (c) != nullptr) return "Label";
        return "Control";
    }

    // The fingerprint of a control in the material card: the caption above it
    // (or the caption it draws itself) and its kind.
    juce::String fingerprintOf (juce::Component* widget, juce::Component* label)
    {
        if (widget == nullptr) return "?";

        juce::String caption;
        if (auto* lab = dynamic_cast<juce::Label*> (label))
            caption = lab->getText();
        if (caption.isEmpty())
        {
            if (auto* k = dynamic_cast<upic::Knob*> (widget)) caption = k->getCellCaption();
            else if (auto* b = dynamic_cast<juce::Button*> (widget)) caption = b->getButtonText();
        }
        return caption + "|" + kindOf (widget);
    }
    // The material card's cells, in the card's reading order (the row's
    // bottom, then the column).
    std::vector<Cell> materialCard (MainComponent& mc)
    {
        const auto card = mc.toolbarCardBoundsForGroupForTest ("material");

        std::vector<juce::Component*> inCard;
        for (int i = 0; i < mc.getNumChildComponents(); ++i)
        {
            auto* c = mc.getChildComponent (i);
            if (c == nullptr || ! c->isVisible()) continue;
            // the card's disclosure toggle is the card's header, not a cell
            if (c->getName() == "materialFoldBtn") continue;
            const auto b = c->getBounds();
            if (b.isEmpty()) continue;
            if (! card.contains (b.getCentre())) continue;
            inCard.push_back (c);
        }

        // a cell's caption sits directly above its control
        std::map<juce::Component*, juce::Component*> captions;
        for (auto* c : inCard)
        {
            auto* lab = dynamic_cast<juce::Label*> (c);
            if (lab == nullptr) continue;
            for (auto* other : inCard)
            {
                if (other == c) continue;
                const auto ob = other->getBounds();
                if (ob.getX() != c->getBounds().getX()) continue;
                if (ob.getY() != c->getBounds().getBottom()) continue;
                captions[other] = lab;
                break;
            }
        }

        std::vector<Cell> cells;
        for (auto* c : inCard)
        {
            if (dynamic_cast<juce::Label*> (c) != nullptr) continue;   // a caption
            Cell cell;
            auto it = captions.find (c);
            juce::Component* label = (it != captions.end()) ? it->second : nullptr;
            cell.text = fingerprintOf (c, label);
            cell.bottom = c->getBounds().getBottom();
            cell.x = c->getBounds().getX();
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

    // The edge's card, seen through the app's binding: the same cells, in the
    // card engine's reading order (the plain controls first, then the knobs).
    juce::String edgeCardText (MainComponent& mc,
                               const upic::material_inspector::Description& material)
    {
        std::vector<const upic::material_inspector::Cell*> ordered;
        for (auto& cell : material.card)
            if (cell.kind != upic::material_inspector::Kind::Knob) ordered.push_back (&cell);
        for (auto& cell : material.card)
            if (cell.kind == upic::material_inspector::Kind::Knob) ordered.push_back (&cell);

        juce::String out;
        for (auto* cell : ordered)
        {
            if (out.isNotEmpty()) out += ", ";
            out += fingerprintOf (mc.materialControlForTest ((int) cell->control),
                                  mc.materialControlLabelForTest ((int) cell->control));
        }
        return out;
    }
}

void upic::runMaterialInspectorChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // The four materials, their tabs, their hints and their cards.
    //
    // The card's cells are in the card's reading order, and the layout puts
    // the plain controls first and the knobs after them (the card engine
    // keeps the rows even that way).
    struct Material
    {
        const char* tab;
        const char* hint;
        const char* card;
    };

    const Material materials[] =
    {
        { "Osc", "Osc: draw arcs on the wavetable\nWave = carrier, Env = amplitude",
          "Wave as|Combo, Rate|Knob, Depth|Knob" },
        { "Sample", "Sample: Pitch sets how arcs transpose the slot\nRef = unison",
          "Slot|Combo, Load sample|Button, Rev|Button, Pitch|Combo, Norm|Button, Fade|Button, "
          "Reset|Button, Mon|Toggle, Capture|Button, Slot -> Wave|Button, Extract|Button, "
          "Ref|Knob, Level|Knob, Grain|Knob, In|Knob" },
        { "Live", "Live: arcs multiply the input pitch\n1x = input, 2x = octave up",
          "Pitch|Combo, Rev len|Combo, Rev|Button, Ref|Knob, Grain|Knob" },
        { "PM", "PM: Ratio x Index into the carrier phase\nIdxEnv shapes the modulation index",
          "Ops|Combo, Preset|Combo, IdxEnv|Combo, Import|Button, Export|Button, "
          "Ratio|Knob, Index|Knob, Fdbk|Knob" }
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    // The hint label and the carrier wave are named; the tabs are found by
    // their text.
    auto* hintLab = dynamic_cast<juce::Label*> (findByName (mc, "matHintLab"));
    auto* waveBox = dynamic_cast<juce::ComboBox*> (findByName (mc, "waveBox"));
    check (hintLab != nullptr, "R5.6 the material's hint is there");
    check (waveBox != nullptr, "R5.6 the carrier wave is there");

    for (int m = 0; m < 4; ++m)
    {
        const auto& material = materials[m];

        // --- the tab ---------------------------------------------------------
        auto* tab = findButton (mc, material.tab);
        check (tab != nullptr, "R5.6 the material's tab is there", material.tab);
        if (tab == nullptr)
            continue;

        tab->onClick();   // the tab asks the app for the material

        // exactly one tab is on, and it is this one
        {
            juce::String on;
            int count = 0;
            for (int i = 0; i < 4; ++i)
            {
                auto* other = findButton (mc, materials[i].tab);
                if (other == nullptr) continue;
                if (other->getToggleState())
                {
                    ++count;
                    on = materials[i].tab;
                }
            }
            check (count == 1 && on == material.tab,
                   "R5.6 the material's tab is the one that is on",
                   on + " (" + juce::String (count) + ")");
        }

        // --- the hint --------------------------------------------------------
        check (hintLab != nullptr && hintLab->getText() == material.hint,
               "R5.6 the material says what it is",
               hintLab != nullptr ? hintLab->getText() : juce::String ("no label"));

        // --- the canvas ------------------------------------------------------
        check (mc.scoreEditorForTest().getLayer() == m,
               "R5.6 the material is the canvas's layer",
               juce::String (mc.scoreEditorForTest().getLayer()));

        // --- the card --------------------------------------------------------
        {
            const auto cells = materialCard (mc);
            check (cardText (cells) == material.card,
                   "R5.6 the material's card holds its own controls",
                   cardText (cells));
        }

        // --- the shared controls ---------------------------------------------
        // The Ref belongs to the Sample and the Live tab, the carrier wave to
        // the Osc and the PM tab.
        {
            const bool wantsWave = (m == 0 || m == 3);
            check (waveBox != nullptr && waveBox->isVisible() == wantsWave,
                   "R5.6 the carrier wave belongs to the Osc and the PM tab",
                   waveBox != nullptr ? (waveBox->isVisible() ? "shown" : "hidden")
                                      : juce::String ("no box"));
        }

        // --- the edge and the app (R5.6) --------------------------------------
        //
        // The edge says what the material shows; the app binds the roles to
        // its widgets; the layout asks the edge what to draw. These three
        // checks pin the three against each other.
        {
            const auto& edge = upic::material_inspector::describe (m);

            // the app shows exactly what the edge says
            int wrong = 0;
            for (auto role : upic::material_inspector::allControls())
            {
                auto* widget = mc.materialControlForTest ((int) role);
                if (widget == nullptr) continue;
                if (widget->isVisible() != edge.shows (role)) ++wrong;
            }
            check (wrong == 0, "R5.6 the visibility follows the edge",
                   juce::String (wrong));

            // the card the layout built is the edge's card
            check (edgeCardText (mc, edge) == material.card,
                   "R5.6 the material's card is the edge's card",
                   edgeCardText (mc, edge));

            // the tab and the hint are the edge's words
            check (tab->getButtonText() == edge.name && tab->getTooltip() == edge.tooltip,
                   "R5.6 the material's tab is the edge's tab",
                   tab->getButtonText());
            check (hintLab != nullptr && hintLab->getText() == edge.hint,
                   "R5.6 the material's hint is the edge's hint",
                   hintLab != nullptr ? hintLab->getText() : juce::String ("no label"));
        }
    }

    // --- the tab strip ------------------------------------------------------
    // The four tabs are radio buttons: one is on at a time.
    {
        int on = 0;
        for (int i = 0; i < 4; ++i)
        {
            auto* tab = findButton (mc, materials[i].tab);
            if (tab != nullptr && tab->getToggleState()) ++on;
        }
        check (on == 1, "R5.6 the material's tabs are one at a time", juce::String (on));
    }

    // --- the edge's own contract --------------------------------------------
    //
    // The edge is data, and the data is checked here: it needs no window.
    {
        namespace mi = upic::material_inspector;

        // every role is bound to a widget of the app
        int unbound = 0;
        for (auto role : mi::allControls())
            if (mc.materialControlForTest ((int) role) == nullptr) ++unbound;
        check (unbound == 0, "R5.6 every material control is bound",
               juce::String (unbound));

        // the four materials are named, and each of them says something
        {
            const char* const names[] = { "Osc", "Sample", "Live", "PM" };
            bool ok = true;
            juce::String detail;
            for (int i = 0; i < 4; ++i)
            {
                const auto& d = mi::describe (i);
                if (juce::String (d.name) != names[i]) { ok = false; detail = d.name; }
                if (d.hint == nullptr || *d.hint == '\0') { ok = false; detail = "no hint"; }
                if (d.tooltip == nullptr || *d.tooltip == '\0') { ok = false; detail = "no tooltip"; }
                if (d.card.empty()) { ok = false; detail = "an empty card"; }
            }
            check (ok, "R5.6 the edge names its four materials", detail);
        }

        // the shared controls: the Ref belongs to the Sample and the Live
        // tab, the carrier wave to the Osc and the PM tab
        {
            int refShown = 0, waveShown = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto& d = mi::describe (i);
                if (d.shows (mi::Control::Ref)) ++refShown;
                if (d.shows (mi::Control::Wave)) ++waveShown;
            }
            check (refShown == 2, "R5.6 the Ref belongs to two materials",
                   juce::String (refShown));
            check (waveShown == 2, "R5.6 the carrier wave belongs to two materials",
                   juce::String (waveShown));
        }

        // a card only holds controls the material shows
        {
            int wrong = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto& d = mi::describe (i);
                for (auto& cell : d.card)
                    if (! d.shows (cell.control)) ++wrong;
            }
            check (wrong == 0, "R5.6 a card only holds controls the material shows",
                   juce::String (wrong));
        }

        // every control belongs to a material, and every material's control is
        // in its card or in a card of its own (the carrier wave lives in the
        // voice card)
        {
            int orphan = 0, uncarded = 0;
            for (auto role : mi::allControls())
            {
                bool shown = false, carded = false;
                for (int i = 0; i < 4; ++i)
                {
                    const auto& d = mi::describe (i);
                    if (! d.shows (role)) continue;
                    shown = true;
                    for (auto& cell : d.card)
                        if (cell.control == role) carded = true;
                }
                if (! shown) ++orphan;
                if (shown && ! carded) ++uncarded;
            }
            check (orphan == 0, "R5.6 every material control belongs to a material",
                   juce::String (orphan));
            check (uncarded == 1, "R5.6 one control lives in another card",
                   juce::String (uncarded));
        }
    }
}
