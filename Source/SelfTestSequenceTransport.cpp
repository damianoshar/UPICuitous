#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "Knob.h"

#include <algorithm>
#include <memory>
#include <vector>

//==============================================================================
// R5.8 (the review's R050, section 28): the sequence card and the performance
// transport - the contract.
//
// The performance sequence is the performer's movement through the score: a
// list of (time, page, position) points the recorder captures and the
// transport replays. Three surfaces carry its switches:
//
// - the toolbar's sequence card (the selector, Record, Play, Loop, View);
// - the sequence panel (Record, Play, Loop, Trim, Replace, Clear, Close);
// - the performance view (its own page, scrub, reverse, loop and tempo
//   switches, and the same sequence trio).
//
// The review asks for the sequence and the performance controllers to be
// extracted:
//
//   "| `showPerformanceView()`, `refreshPerformanceTransport()` | PerformanceController |"
//   "| `showSequencePanel()`, `sequenceRecordTick()`            | SequenceController    |"
//   "| `startSequencePlayback()`, `stopSequencePlayback()`       | SequenceTransport     |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the three surfaces do today:
//
// - the card: the selector, the four switches, their labels and their
//   wording;
// - the commands: Record starts the recorder (and creates the sequence when
//   the bank is empty), Record again stops it, Play needs two points, Loop
//   writes the model's flag and View opens the panel;
// - the one transport: a recording started on one surface stops on another;
// - the view: the page, the transport switches and the tempo are the app's
//   own commands, not a second transport;
// - the panel's own editor (Clear here; Trim and Replace are the model
//   suite's).
//
// The suite drives the real component: it presses the buttons the user
// presses and reads the app's state.

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

    juce::String tooltipOf (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) return t->getTooltip();
        return {};
    }
    bool tooltipIs (juce::Component* c, const char* wording)
    {
        return c != nullptr && tooltipOf (c) == juce::String (wording);
    }
}

void upic::runSequenceTransportChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    namespace st = upic::sequence_transport;

    // --- the edge ------------------------------------------------------------
    // The description is data, so it is checked without a window first.
    check (juce::String (st::describe().name) == "Sequence"
           && st::describe().card.size() == (size_t) st::Control::count,
           "R5.8 the edge describes the sequence card",
           juce::String ((int) st::describe().card.size()) + " cells");

    check (st::allControls().size() == (size_t) st::Control::count,
           "R5.8 the edge names every role once",
           juce::String ((int) st::allControls().size()) + " roles");

    check (std::abs (st::kMinTempoBpm - 40.0) < 1e-12
           && std::abs (st::kMaxTempoBpm - 240.0) < 1e-12
           && std::abs (st::kTempoIntervalBpm - 1.0) < 1e-12
           && std::abs (st::kTempoSkew - 0.5) < 1e-12,
           "R5.8 the tempo's domain is one domain");

    check (std::abs (st::kDefaultPageSeconds - 30.0) < 1e-12,
           "R5.8 the page-duration fallback is one number",
           juce::String (st::kDefaultPageSeconds, 1));

    check (juce::String (st::recordButtonText (false)) == "Rec"
           && juce::String (st::recordButtonText (true)) == "Stop"
           && juce::String (st::playButtonText (false)) == "Play"
           && juce::String (st::playButtonText (true)) == "Stop",
           "R5.8 the transport's labels name its state");

    check (st::recordAction (false) == st::Action::Start
           && st::recordAction (true) == st::Action::Stop
           && st::playAction (false) == st::Action::Start
           && st::playAction (true) == st::Action::Stop,
           "R5.8 one press starts a stopped run and stops a running one");

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    // --- the sequence card ---------------------------------------------------
    auto* seqLab     = dynamic_cast<juce::Label*>    (findByName (mc, "seqLab"));
    auto* seqBox     = dynamic_cast<juce::ComboBox*>  (findByName (mc, "seqBox"));
    auto* seqRecBtn  = dynamic_cast<juce::TextButton*> (findByName (mc, "seqRecBtn"));
    auto* seqPlayBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "seqPlayBtn"));
    auto* seqLoopBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "seqLoopBtn"));
    auto* seqEditBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "seqEditBtn"));
    check (seqLab != nullptr && seqBox != nullptr && seqRecBtn != nullptr
           && seqPlayBtn != nullptr && seqLoopBtn != nullptr && seqEditBtn != nullptr,
           "R5.8 the sequence card's controls are there");

    if (seqLab == nullptr || seqBox == nullptr || seqRecBtn == nullptr
        || seqPlayBtn == nullptr || seqLoopBtn == nullptr || seqEditBtn == nullptr)
        return;

    check (seqLab->getText() == "Seq",
           "R5.8 the sequence card is captioned", seqLab->getText());

    check (cardText (cardCells (mc, "seq"))
               == "seqBox|Combo, seqRecBtn|Button, seqPlayBtn|Button, "
                  "seqLoopBtn|Button, seqEditBtn|Button",
           "R5.8 the sequence card holds the selector and the four switches",
           cardText (cardCells (mc, "seq")));

    // --- the edge and the app's widgets are the same card -------------------
    bool allBound = mc.sequenceControlCountForTest() == (int) st::Control::count;
    bool sameWidgets = true;
    for (int role = 0; role < mc.sequenceControlCountForTest(); ++role)
    {
        auto* w = mc.sequenceControlForTest (role);
        if (w == nullptr) { allBound = false; continue; }
        switch ((st::Control) role)
        {
            case st::Control::Sequence: sameWidgets = sameWidgets && w == seqBox; break;
            case st::Control::Record:   sameWidgets = sameWidgets && w == seqRecBtn; break;
            case st::Control::Play:     sameWidgets = sameWidgets && w == seqPlayBtn; break;
            case st::Control::Loop:     sameWidgets = sameWidgets && w == seqLoopBtn; break;
            case st::Control::Open:     sameWidgets = sameWidgets && w == seqEditBtn; break;
            default: break;
        }
    }
    check (allBound, "R5.8 every sequence role is bound to a widget",
           juce::String (mc.sequenceControlCountForTest()) + " roles");
    check (sameWidgets
           && mc.sequenceControlLabelForTest ((int) st::Control::Sequence) == seqLab,
           "R5.8 the roles are the card's own controls");

    juce::String fromEdge;
    for (auto& cell : st::describe().card)
    {
        auto* w = mc.sequenceControlForTest ((int) cell.control);
        if (w == nullptr) continue;
        if (fromEdge.isNotEmpty()) fromEdge += ", ";
        fromEdge += w->getName() + "|" + kindOf (w);
    }
    check (cardText (cardCells (mc, "seq")) == fromEdge,
           "R5.8 the card is the edge's cell list, in the edge's order",
           fromEdge);

    bool wordsMatch = true;
    for (int role = 0; role < mc.sequenceControlCountForTest(); ++role)
        if (juce::String (st::tooltip ((st::Control) role))
            != tooltipOf (mc.sequenceControlForTest (role)))
            wordsMatch = false;
    check (wordsMatch, "R5.8 the card's tooltips are the edge's wording",
           tooltipOf (seqBox));

    check (seqRecBtn->getButtonText() == "Rec"
           && seqPlayBtn->getButtonText() == "Play"
           && seqLoopBtn->getButtonText() == "Loop"
           && seqEditBtn->getButtonText() == "View",
           "R5.8 the card's switches are Record, Play, Loop and View",
           seqRecBtn->getButtonText() + " / " + seqPlayBtn->getButtonText() + " / "
             + seqLoopBtn->getButtonText() + " / " + seqEditBtn->getButtonText());

    check (tooltipIs (seqBox, "The performance sequence")
           && tooltipIs (seqRecBtn, "Record the movement through the score (page + playhead)")
           && tooltipIs (seqPlayBtn, "Replay the recorded performance")
           && tooltipIs (seqLoopBtn, "Loop the performance")
           && tooltipIs (seqEditBtn, "Open the sequence panel"),
           "R5.8 the card's controls say what they do",
           tooltipOf (seqRecBtn));

    // --- the sequence panel --------------------------------------------------
    // View opens the panel; the panel's transport is the recorder's too.
    seqEditBtn->onClick();
    check (mc.seqPanelOpenForTest(), "R5.8 View opens the sequence panel");

    auto* sqRecBtn   = dynamic_cast<juce::TextButton*> (findByName (mc, "sqRecBtn"));
    auto* sqPlayBtn  = dynamic_cast<juce::TextButton*> (findByName (mc, "sqPlayBtn"));
    auto* sqClearBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "sqClearBtn"));
    auto* sqLoopBtn  = dynamic_cast<juce::TextButton*> (findByName (mc, "sqLoopBtn"));
    auto* sqCloseBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "sqCloseBtn"));
    check (sqRecBtn != nullptr && sqPlayBtn != nullptr && sqClearBtn != nullptr
           && sqLoopBtn != nullptr && sqCloseBtn != nullptr,
           "R5.8 the sequence panel's transport is there");

    if (sqRecBtn == nullptr || sqPlayBtn == nullptr || sqClearBtn == nullptr
        || sqLoopBtn == nullptr || sqCloseBtn == nullptr)
        return;

    check (sqRecBtn->getButtonText() == st::recordButtonText (false)
           && sqPlayBtn->getButtonText() == st::playButtonText (false),
           "R5.8 the panel's recorder says Rec and Play",
           sqRecBtn->getButtonText() + " / " + sqPlayBtn->getButtonText());

    check (tooltipOf (sqRecBtn) == juce::String (st::tooltip (st::Control::Record))
           && tooltipOf (sqPlayBtn) == juce::String (st::tooltip (st::Control::Play))
           && tooltipOf (sqLoopBtn) == juce::String (st::tooltip (st::Control::Loop)),
           "R5.8 the panel's recorder says the edge's words",
           tooltipOf (sqRecBtn));

    // --- one recorder: the card starts it, the panel stops it ---------------
    seqRecBtn->onClick();
    check (mc.seqRecordingForTest() && mc.seqCountForTest() == 1,
           "R5.8 the card's Record starts the recorder",
           juce::String (mc.seqCountForTest()) + " sequences");
    check (seqRecBtn->getButtonText() == st::recordButtonText (true)
           && sqRecBtn->getButtonText() == st::recordButtonText (true),
           "R5.8 Record becomes Stop on the card and in the panel",
           seqRecBtn->getButtonText() + " / " + sqRecBtn->getButtonText());

    mc.seqAppendForTest (0.5, 0, 0.5);
    mc.seqAppendForTest (1.0, 0, 1.0);

    sqRecBtn->onClick();
    check (! mc.seqRecordingForTest(),
           "R5.8 the panel's Stop ends what the card started");
    check (seqRecBtn->getButtonText() == st::recordButtonText (false)
           && sqRecBtn->getButtonText() == st::recordButtonText (false),
           "R5.8 Stop becomes Record again",
           seqRecBtn->getButtonText() + " / " + sqRecBtn->getButtonText());

    // --- one transport: the card plays, the panel stops ---------------------
    seqPlayBtn->onClick();
    check (mc.seqPlayActiveForTest(),
           "R5.8 the card's Play arms the sequence transport");
    check (seqPlayBtn->getButtonText() == st::playButtonText (true)
           && sqPlayBtn->getButtonText() == st::playButtonText (true),
           "R5.8 Play becomes Stop on the card and in the panel",
           seqPlayBtn->getButtonText() + " / " + sqPlayBtn->getButtonText());

    sqPlayBtn->onClick();
    check (! mc.seqPlayActiveForTest(),
           "R5.8 the panel's Stop ends what the card started");
    check (seqPlayBtn->getButtonText() == st::playButtonText (false)
           && sqPlayBtn->getButtonText() == st::playButtonText (false),
           "R5.8 Stop becomes Play again",
           seqPlayBtn->getButtonText() + " / " + sqPlayBtn->getButtonText());

    // --- the loop is the model's flag --------------------------------------
    const bool loopBefore = mc.seqLoopAtForTest (0);
    seqLoopBtn->onClick();
    check (mc.seqLoopAtForTest (0) != loopBefore
           && seqLoopBtn->getToggleState() == mc.seqLoopAtForTest (0),
           "R5.8 the card's Loop writes the model's flag",
           mc.seqLoopAtForTest (0) ? "on" : "off");

    sqLoopBtn->onClick();
    check (mc.seqLoopAtForTest (0) == loopBefore,
           "R5.8 the panel's Loop writes the model's flag",
           mc.seqLoopAtForTest (0) ? "on" : "off");

    // --- the panel's Clear is one command -----------------------------------
    sqClearBtn->onClick();
    check (mc.seqPointCountAtForTest (0) == 0,
           "R5.8 the panel's Clear erases the take",
           juce::String (mc.seqPointCountAtForTest (0)) + " points");

    // --- the performance view ----------------------------------------------
    auto* perfBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "perfBtn"));
    check (perfBtn != nullptr && perfBtn->getButtonText() == "Perf",
           "R5.8 the performance entry is there");

    if (perfBtn == nullptr)
        return;

    perfBtn->onClick();
    check (mc.perfViewOpenForTest(), "R5.8 the performance entry opens the view");

    auto* perfRecBtn   = dynamic_cast<juce::TextButton*> (findByName (mc, "perfRecBtn"));
    auto* perfPlayBtn  = dynamic_cast<juce::TextButton*> (findByName (mc, "perfPlayBtn"));
    auto* perfLoopBtn  = dynamic_cast<juce::TextButton*> (findByName (mc, "perfLoopSeqBtn"));
    auto* perfPageBox  = dynamic_cast<juce::ComboBox*>  (findByName (mc, "perfPageBox"));
    auto* perfTempo    = dynamic_cast<juce::Slider*>    (findByName (mc, "perfTempoSlider"));
    auto* perfCloseBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "perfCloseBtn"));
    check (perfRecBtn != nullptr && perfPlayBtn != nullptr && perfLoopBtn != nullptr
           && perfPageBox != nullptr && perfTempo != nullptr && perfCloseBtn != nullptr,
           "R5.8 the performance view's transport is there");

    if (perfRecBtn == nullptr || perfPlayBtn == nullptr || perfLoopBtn == nullptr
        || perfPageBox == nullptr || perfTempo == nullptr || perfCloseBtn == nullptr)
        return;

    check (tooltipOf (perfRecBtn) == juce::String (st::tooltip (st::Control::Record))
           && tooltipOf (perfPlayBtn) == juce::String (st::tooltip (st::Control::Play))
           && tooltipOf (perfLoopBtn) == juce::String (st::tooltip (st::Control::Loop)),
           "R5.8 the view's recorder says the edge's words",
           tooltipOf (perfRecBtn));

    // --- one recorder again: the view starts it, the card stops it ----------
    perfRecBtn->onClick();
    check (mc.seqRecordingForTest(),
           "R5.8 the view's Record starts the recorder");

    seqRecBtn->onClick();
    check (! mc.seqRecordingForTest(),
           "R5.8 the card's Stop ends what the view started");

    mc.seqAppendForTest (0.5, 0, 0.5);
    mc.seqAppendForTest (1.0, 0, 1.0);

    perfPlayBtn->onClick();
    check (mc.seqPlayActiveForTest(),
           "R5.8 the view's Play arms the sequence transport");

    seqPlayBtn->onClick();
    check (! mc.seqPlayActiveForTest(),
           "R5.8 the card's Stop ends what the view started");

    // --- the view's loop is the model's flag too ----------------------------
    const bool viewLoopBefore = mc.seqLoopAtForTest (0);
    perfLoopBtn->onClick();
    check (mc.seqLoopAtForTest (0) != viewLoopBefore,
           "R5.8 the view's Loop writes the model's flag",
           mc.seqLoopAtForTest (0) ? "on" : "off");

    // --- the view's tempo is the page's tempo ------------------------------
    check (std::abs (perfTempo->getMinimum() - st::kMinTempoBpm) < 1e-9
           && std::abs (perfTempo->getMaximum() - st::kMaxTempoBpm) < 1e-9,
           "R5.8 the view's tempo covers the edge's range",
           juce::String (perfTempo->getMinimum(), 1) + " - "
             + juce::String (perfTempo->getMaximum(), 1));

    check (tooltipOf (perfTempo) == juce::String (st::tempoTooltip()),
           "R5.8 the tempo says the edge's words",
           tooltipOf (perfTempo));

    auto* tempoKnob = dynamic_cast<upic::Knob*> (findByName (mc, "tempoSlider"));
    check (tempoKnob != nullptr
           && std::abs (tempoKnob->getMinimum() - st::kMinTempoBpm) < 1e-9
           && std::abs (tempoKnob->getMaximum() - st::kMaxTempoBpm) < 1e-9
           && std::abs (tempoKnob->getInterval() - st::kTempoIntervalBpm) < 1e-9
           && std::abs (perfTempo->getInterval() - tempoKnob->getInterval()) < 1e-9
           && tooltipOf (tempoKnob).startsWith (juce::String (st::tempoTooltip())),
           "R5.8 the tempo knob and the tempo slider are one control",
           tempoKnob != nullptr ? juce::String (tempoKnob->getInterval(), 2) + " / "
                                    + juce::String (perfTempo->getInterval(), 2)
                                : juce::String ("no knob"));

    mc.perfSetTempoForTest (90.0);
    check (std::abs (mc.pageTempoForTest() - 90.0) < 1e-6,
           "R5.8 the view's tempo reaches the page",
           juce::String (mc.pageTempoForTest(), 2));

    // --- the two views close ------------------------------------------------
    perfCloseBtn->onClick();
    check (! mc.perfViewOpenForTest(), "R5.8 the performance view closes");

    sqCloseBtn->onClick();
    check (! mc.seqPanelOpenForTest(), "R5.8 the sequence panel closes");
}
