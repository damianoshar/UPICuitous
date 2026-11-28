#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"

#include <algorithm>
#include <memory>
#include <vector>

//==============================================================================
// R5.9 (the review's R051, section 28): the channel controls - the contract.
//
// A mixer channel is where an arc is played: the score is drawn on the page,
// and every arc sits on a channel - Main, or one of the mixer's own. The
// toolbar's "ch" card carries two controls: the channel selector and the
// button that opens the mixer.
//
// The review asks for the channel controls to be decomposed:
//
//   "| `showChannelPanel()`, `refreshChannelBox()`              | ChannelController |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the card does today:
//
// - the card: the selector, the mix button, the caption and the wording;
// - the channel list: Main first, then the mixer's own channels, and the
//   item ids that go with them;
// - the routing: picking a channel routes the selected arcs to it, picking
//   Main brings them back, and each route is one undo step;
// - the mixer: the button opens the panel and the panel closes.
//
// The suite drives the real component: it presses the button the user
// presses, picks from the dropdown the user picks from and reads the app's
// state.

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

void upic::runChannelControlsChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    // --- the card ------------------------------------------------------------
    auto* chLab = dynamic_cast<juce::Label*>     (findByName (mc, "channelLab"));
    auto* chBox = dynamic_cast<juce::ComboBox*>   (findByName (mc, "channelBox"));
    auto* chBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "channelBtn"));
    check (chLab != nullptr && chBox != nullptr && chBtn != nullptr,
           "R5.9 the channel card's controls are there");

    if (chLab == nullptr || chBox == nullptr || chBtn == nullptr)
        return;

    check (chLab->getText() == "Ch",
           "R5.9 the channel card is captioned", chLab->getText());

    check (cardText (cardCells (mc, "ch")) == "channelBox|Combo, channelBtn|Button",
           "R5.9 the channel card holds the selector and the mix button",
           cardText (cardCells (mc, "ch")));

    check (chBtn->getButtonText() == "Mix...",
           "R5.9 the mix button says Mix", chBtn->getButtonText());

    check (tooltipOf (chBox) == "The mixer channel the selected arcs play on"
           && tooltipOf (chBtn) == "The mixer: channel levels, mute/solo and routing",
           "R5.9 the channel's controls say what they do", tooltipOf (chBox));

    // --- the mixer opens from the card --------------------------------------
    chBtn->onClick();
    check (mc.channelPanelOpenForTest(), "R5.9 the mix button opens the mixer");

    auto* chCloseBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "chCloseBtn"));
    check (chCloseBtn != nullptr, "R5.9 the mixer's own close button is there");

    if (chCloseBtn != nullptr)
    {
        chCloseBtn->onClick();
        check (! mc.channelPanelOpenForTest(), "R5.9 the mixer closes");
    }

    // --- the channel list ----------------------------------------------------
    const juce::String json = R"({"currentPage":0,"channels":[{"id":1,"name":"Wood"}],"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":330},{"t":2,"f":330}]}]}]})";
    check (mc.loadFromVar (juce::JSON::parse (json)), "R5.9 the mixer piece loads");

    check (chBox->getNumItems() == 2
           && chBox->getItemText (0) == "Main"
           && chBox->getItemText (1) == "Wood",
           "R5.9 the channel list is Main and the mixer's channels",
           chBox->getItemText (0) + " / " + chBox->getItemText (1));

    check (chBox->getItemId (0) == 1 && chBox->getItemId (1) == 2,
           "R5.9 the channel list's ids are one rule",
           juce::String (chBox->getItemId (0)) + ", " + juce::String (chBox->getItemId (1)));

    // --- the routing ---------------------------------------------------------
    mc.selectArcsAndSyncForTest ({ 1 });
    const int channelId = mc.channelIdAtForTest (0);
    const int undoBefore = mc.undoDepth();

    chBox->setSelectedId (2, juce::sendNotificationSync);
    check (mc.arcChannelForTest (1) == channelId,
           "R5.9 picking a channel routes the arcs",
           juce::String (mc.arcChannelForTest (1)));
    check (mc.undoDepth() == undoBefore + 1,
           "R5.9 the routing is one undo step",
           juce::String (undoBefore) + " -> " + juce::String (mc.undoDepth()));
    check (chBox->getSelectedId() == 2,
           "R5.9 the card shows the arcs' channel",
           juce::String (chBox->getSelectedId()));

    const int undoAfterRoute = mc.undoDepth();
    chBox->setSelectedId (1, juce::sendNotificationSync);
    check (mc.arcChannelForTest (1) == 0,
           "R5.9 picking Main brings the arcs back",
           juce::String (mc.arcChannelForTest (1)));
    check (mc.undoDepth() == undoAfterRoute + 1,
           "R5.9 the route back is one undo step",
           juce::String (undoAfterRoute) + " -> " + juce::String (mc.undoDepth()));
    check (chBox->getSelectedId() == 1,
           "R5.9 the card shows Main again",
           juce::String (chBox->getSelectedId()));

    // one undo takes the arcs back to the channel they came from
    mc.undo();
    check (mc.arcChannelForTest (1) == channelId,
           "R5.9 one undo takes the arcs back to the channel",
           juce::String (mc.arcChannelForTest (1)));
}
