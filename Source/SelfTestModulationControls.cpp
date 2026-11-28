#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "ModulationControls.h"

#include <algorithm>
#include <memory>
#include <vector>

//==============================================================================
// R5.9 (the review's R051, section 28): the modulation controls - the contract.
//
// A modulation is one arc bending another: the modulator's contour bends the
// carrier's pitch or its level, by the depth the slider says. The toolbar's
// "mod" card carries three controls: the modulator, its target and the depth.
//
// The review asks for the modulation controls to be decomposed:
//
//   "| `refreshModBox()`                                        | ModulationInspector |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the card does today:
//
// - the card: the modulator, the target and the depth, the captions and the
//   wording;
// - the lists: the modulator list is None and the page's arcs, the target
//   list is Pitch and Amp, and the item ids that go with them;
// - the depth: its domain, and the value it falls back to;
// - the commands: picking a modulator assigns it, picking a target writes it,
//   picking None clears it, the slider writes the depth, and a cycle is
//   refused;
// - the transactions: each of those commands is one undo step.
//
// The suite drives the real component: it picks from the dropdowns the user
// picks from, moves the slider the user moves and reads the app's state.

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
        if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) return "Toggle";
        if (dynamic_cast<juce::TextButton*> (c) != nullptr) return "Button";
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr) return "Combo";
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr) return "Edit";
        if (dynamic_cast<juce::Slider*> (c) != nullptr) return "Slider";
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

    juce::String tooltipOf (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) return t->getTooltip();
        return {};
    }
}

void upic::runModulationControlsChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    namespace moc = upic::modulation_controls;

    // --- the edge ------------------------------------------------------------
    // The description is data, so it is checked without a window first.
    check (juce::String (moc::describe().name) == "Modulation"
           && moc::describe().card.size() == (size_t) moc::Control::count,
           "R5.9 the edge describes the modulation card",
           juce::String ((int) moc::describe().card.size()) + " cells");

    check (moc::allControls().size() == (size_t) moc::Control::count,
           "R5.9 the edge names every role once",
           juce::String ((int) moc::allControls().size()) + " roles");

    check (moc::kNoneItemId == 1 && moc::itemIdForArc (1) == 2
           && moc::itemIdForArc (3) == 4 && moc::itemIdForArc (-1) == 1
           && moc::isNoneItem (1) && ! moc::isNoneItem (2)
           && moc::arcForItemId (2) == 1 && moc::arcForItemId (4) == 3,
           "R5.9 the modulator list's ids are one rule",
           juce::String (moc::itemIdForArc (1)) + ", " + juce::String (moc::itemIdForArc (3)));

    check (moc::itemIdForTarget (moc::kPitchTarget) == 1
           && moc::itemIdForTarget (moc::kAmpTarget) == 2
           && moc::targetForItemId (1) == moc::kPitchTarget
           && moc::targetForItemId (2) == moc::kAmpTarget
           && juce::String (moc::targetName (moc::kPitchTarget)) == "Pitch"
           && juce::String (moc::targetName (moc::kAmpTarget)) == "Amp",
           "R5.9 the target list's ids and names are one rule",
           juce::String (moc::targetName (moc::kPitchTarget)) + " / "
             + juce::String (moc::targetName (moc::kAmpTarget)));

    check (std::abs (moc::kMinDepth - 0.0) < 1e-12
           && std::abs (moc::kMaxDepth - 1.0) < 1e-12
           && std::abs (moc::kDepthInterval - 0.001) < 1e-12
           && std::abs (moc::kDefaultDepth - 0.5) < 1e-12,
           "R5.9 the depth's domain is one domain",
           juce::String (moc::kMinDepth, 3) + " - " + juce::String (moc::kMaxDepth, 3));

    check (juce::String (moc::arcName (7)) == "Arc 7"
           && juce::String (moc::noneName()) == "None",
           "R5.9 an arc's name in the list is one rule",
           juce::String (moc::arcName (7)));

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    // --- the card ------------------------------------------------------------
    auto* modLab = dynamic_cast<juce::Label*>   (findByName (mc, "arcModLab"));
    auto* modBox = dynamic_cast<juce::ComboBox*> (findByName (mc, "arcModBox"));
    auto* tgtLab = dynamic_cast<juce::Label*>   (findByName (mc, "arcModTgtLab"));
    auto* tgtBox = dynamic_cast<juce::ComboBox*> (findByName (mc, "arcModTgtBox"));
    auto* depLab = dynamic_cast<juce::Label*>   (findByName (mc, "arcModDepthLab"));
    auto* depSl  = dynamic_cast<juce::Slider*>  (findByName (mc, "arcModSlider"));
    check (modLab != nullptr && modBox != nullptr && tgtLab != nullptr
           && tgtBox != nullptr && depLab != nullptr && depSl != nullptr,
           "R5.9 the modulation card's controls are there");

    if (modLab == nullptr || modBox == nullptr || tgtLab == nullptr
        || tgtBox == nullptr || depLab == nullptr || depSl == nullptr)
        return;

    check (modLab->getText() == "Mod" && tgtLab->getText() == "Tgt"
           && depLab->getText() == "Depth",
           "R5.9 the modulation card is captioned",
           modLab->getText() + " / " + tgtLab->getText() + " / " + depLab->getText());

    check (cardText (cardCells (mc, "mod")) == "arcModBox|Combo, arcModTgtBox|Combo, "
                                               "arcModSlider|Slider",
           "R5.9 the modulation card holds the modulator, the target and the depth",
           cardText (cardCells (mc, "mod")));

    check (tooltipOf (modBox) == "The arc that modulates the selection "
                                  "(its contour bends the pitch or its level)"
           && tooltipOf (tgtBox) == "What the modulator drives: pitch or amplitude"
           && tooltipOf (depSl) == "The modulation depth "
                                   "(1 = +/- one octave at the contour extremes)",
           "R5.9 the modulation's controls say what they do", tooltipOf (modBox));

    // --- the edge and the app's widgets are the same card -------------------
    bool allBound = mc.modulationControlCountForTest() == (int) moc::Control::count;
    bool sameWidgets = true;
    for (int role = 0; role < mc.modulationControlCountForTest(); ++role)
    {
        auto* w = mc.modulationControlForTest (role);
        if (w == nullptr) { allBound = false; continue; }
        switch ((moc::Control) role)
        {
            case moc::Control::Modulator: sameWidgets = sameWidgets && w == modBox; break;
            case moc::Control::Target:    sameWidgets = sameWidgets && w == tgtBox; break;
            case moc::Control::Depth:     sameWidgets = sameWidgets && w == depSl;  break;
            default: break;
        }
    }
    check (allBound, "R5.9 every modulation role is bound to a widget",
           juce::String (mc.modulationControlCountForTest()) + " roles");
    check (sameWidgets
           && mc.modulationControlLabelForTest ((int) moc::Control::Modulator) == modLab
           && mc.modulationControlLabelForTest ((int) moc::Control::Target) == tgtLab
           && mc.modulationControlLabelForTest ((int) moc::Control::Depth) == depLab,
           "R5.9 the roles are the card's own controls");

    juce::String fromEdge;
    for (auto& cell : moc::describe().card)
    {
        auto* w = mc.modulationControlForTest ((int) cell.control);
        if (w == nullptr) continue;
        if (fromEdge.isNotEmpty()) fromEdge += ", ";
        fromEdge += w->getName() + "|" + kindOf (w);
    }
    check (cardText (cardCells (mc, "mod")) == fromEdge,
           "R5.9 the card is the edge's cell list, in the edge's order", fromEdge);

    bool wordsMatch = true;
    for (int role = 0; role < mc.modulationControlCountForTest(); ++role)
        if (juce::String (moc::tooltip ((moc::Control) role))
            != tooltipOf (mc.modulationControlForTest (role)))
            wordsMatch = false;
    check (wordsMatch, "R5.9 the card's tooltips are the edge's wording",
           tooltipOf (modBox));

    // --- the depth's domain --------------------------------------------------
    check (std::abs (depSl->getMinimum() - 0.0) < 1e-12
           && std::abs (depSl->getMaximum() - 1.0) < 1e-12
           && std::abs (depSl->getInterval() - 0.001) < 1e-12,
           "R5.9 the depth covers its domain",
           juce::String (depSl->getMinimum(), 3) + " - "
             + juce::String (depSl->getMaximum(), 3));

    check (std::abs (depSl->getMinimum() - moc::kMinDepth) < 1e-12
           && std::abs (depSl->getMaximum() - moc::kMaxDepth) < 1e-12
           && std::abs (depSl->getInterval() - moc::kDepthInterval) < 1e-12,
           "R5.9 the slider covers the edge's depth domain",
           juce::String (depSl->getInterval(), 4));

    // --- the lists -----------------------------------------------------------
    // three arcs: arc 1 is bent by arc 3, arcs 2 and 3 are straight.
    const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"mod":3,"modArcDepth":0.25,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":220},{"t":2,"f":880}]},{"id":3,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":330},{"t":2,"f":330}]}]}]})";
    check (mc.loadFromVar (juce::JSON::parse (json)), "R5.9 the modulation piece loads");

    check (modBox->getNumItems() == 4
           && modBox->getItemText (0) == "None"
           && modBox->getItemText (1) == "Arc 1"
           && modBox->getItemText (2) == "Arc 2"
           && modBox->getItemText (3) == "Arc 3",
           "R5.9 the modulator list is None and the page's arcs",
           modBox->getItemText (0) + " / " + modBox->getItemText (1) + " / "
             + modBox->getItemText (2) + " / " + modBox->getItemText (3));

    check (modBox->getItemId (0) == 1 && modBox->getItemId (1) == 2
           && modBox->getItemId (2) == 3 && modBox->getItemId (3) == 4,
           "R5.9 the modulator list's ids are one rule",
           juce::String (modBox->getItemId (0)) + ", " + juce::String (modBox->getItemId (1))
             + ", " + juce::String (modBox->getItemId (2)) + ", "
             + juce::String (modBox->getItemId (3)));

    check (tgtBox->getNumItems() == 2
           && tgtBox->getItemText (0) == "Pitch"
           && tgtBox->getItemText (1) == "Amp",
           "R5.9 the target list is Pitch and Amp",
           tgtBox->getItemText (0) + " / " + tgtBox->getItemText (1));

    check (tgtBox->getItemId (0) == 1 && tgtBox->getItemId (1) == 2,
           "R5.9 the target list's ids are one rule",
           juce::String (tgtBox->getItemId (0)) + ", " + juce::String (tgtBox->getItemId (1)));

    // --- the app's lists follow the edge's rules ----------------------------
    // None is the list's first item; the page's arcs follow it, in order.
    bool modListFollowsEdge = modBox->getItemId (0) == moc::kNoneItemId
                          && modBox->getItemText (0) == juce::String (moc::noneName());
    for (int i = 1; i < modBox->getNumItems() && i <= 3; ++i)
    {
        const int arcId = i;   // the piece's arcs are numbered 1, 2, 3
        if (modBox->getItemId (i) != moc::itemIdForArc (arcId)) modListFollowsEdge = false;
        if (modBox->getItemText (i) != juce::String (moc::arcName (arcId)))
            modListFollowsEdge = false;
    }
    check (modListFollowsEdge,
           "R5.9 the app's modulator list follows the edge's ids and names",
           juce::String (modBox->getNumItems()) + " items");

    bool tgtListFollowsEdge = tgtBox->getNumItems() == 2;
    for (int i = 0; i < tgtBox->getNumItems() && i < 2; ++i)
    {
        const int target = i == 0 ? moc::kPitchTarget : moc::kAmpTarget;
        if (tgtBox->getItemId (i) != moc::itemIdForTarget (target)) tgtListFollowsEdge = false;
        if (tgtBox->getItemText (i) != juce::String (moc::targetName (target)))
            tgtListFollowsEdge = false;
    }
    check (tgtListFollowsEdge,
           "R5.9 the app's target list follows the edge's ids and names",
           juce::String (tgtBox->getNumItems()) + " items");

    // --- the card follows the selection --------------------------------------
    mc.selectArcsAndSyncForTest ({ 1 });
    check (modBox->getSelectedId() == 4
           && std::abs (depSl->getValue() - 0.25) < 1e-9,
           "R5.9 the card shows the selected arc's modulator and depth",
           juce::String (modBox->getSelectedId()) + " / "
             + juce::String (depSl->getValue(), 3));

    // with no selection the modulator is None and the depth falls back
    mc.selectArcsAndSyncForTest ({});
    check (modBox->getSelectedId() == 1
           && std::abs (depSl->getValue() - 0.5) < 1e-9,
           "R5.9 the depth falls back to a middle value",
           juce::String (depSl->getValue(), 3));

    // --- the commands --------------------------------------------------------
    // arc 2 is straight: the card says None
    mc.selectArcsAndSyncForTest ({ 2 });
    check (modBox->getSelectedId() == 1,
           "R5.9 an arc without a modulator says None",
           juce::String (modBox->getSelectedId()));

    const int undoBefore = mc.undoDepth();
    modBox->setSelectedId (2, juce::sendNotificationSync);   // arc 1 bends arc 2
    check (mc.arcModulatorForTest (2) == 1,
           "R5.9 picking a modulator assigns it",
           juce::String (mc.arcModulatorForTest (2)));
    check (mc.undoDepth() == undoBefore + 1,
           "R5.9 the assignment is one undo step",
           juce::String (undoBefore) + " -> " + juce::String (mc.undoDepth()));

    const int undoAfterAssign = mc.undoDepth();
    tgtBox->setSelectedId (2, juce::sendNotificationSync);   // Amp
    check (mc.arcModTargetForTest (2) == 1,
           "R5.9 picking Amp writes the target",
           juce::String (mc.arcModTargetForTest (2)));
    check (mc.undoDepth() == undoAfterAssign + 1,
           "R5.9 the target is one undo step",
           juce::String (undoAfterAssign) + " -> " + juce::String (mc.undoDepth()));

    const int undoAfterTarget = mc.undoDepth();
    depSl->setValue (0.75, juce::sendNotificationSync);
    check (std::abs (mc.arcModDepthForTest (2) - 0.75) < 1e-9,
           "R5.9 the depth slider writes the depth",
           juce::String (mc.arcModDepthForTest (2), 3));
    check (mc.undoDepth() == undoAfterTarget + 1,
           "R5.9 the depth is one undo step",
           juce::String (undoAfterTarget) + " -> " + juce::String (mc.undoDepth()));

    const int undoAfterDepth = mc.undoDepth();
    modBox->setSelectedId (1, juce::sendNotificationSync);   // None
    check (mc.arcModulatorForTest (2) == -1,
           "R5.9 picking None clears the modulator",
           juce::String (mc.arcModulatorForTest (2)));
    check (mc.undoDepth() == undoAfterDepth + 1,
           "R5.9 the clear is one undo step",
           juce::String (undoAfterDepth) + " -> " + juce::String (mc.undoDepth()));

    // --- a cycle is refused --------------------------------------------------
    // arc 1 is bent by arc 3, so arc 3 cannot be bent by arc 1.
    mc.selectArcsAndSyncForTest ({ 3 });
    check (modBox->getSelectedId() == 1,
           "R5.9 the straight arc says None again",
           juce::String (modBox->getSelectedId()));

    modBox->setSelectedId (2, juce::sendNotificationSync);   // arc 1 bends arc 3
    check (mc.arcModulatorForTest (3) == -1,
           "R5.9 the card refuses a cycle",
           juce::String (mc.arcModulatorForTest (3)));
}
