#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

//==============================================================================
// R5.10 (the review's R052, section 28): the MIDI settings.
//
// The MIDI input lives in the MIDI + OSC settings overlay. The section has a
// device row, an activity line and four learn rows - the master gain, the
// draw intensity, the scrub and the transport. Each learn row is a caption, a
// Learn button and the binding's text.
//
// The review asks for the MIDI controls to be decomposed:
//
//   "| `buildMidiControls()`                                    | MidiSettingsPanel  |"
//   (section 28)
//
// This suite is the contract, and it comes before the move, as every phase in
// this wave does. It pins what the section does today:
//
// - the section: the device row's caption and tooltip, the activity line's
//   first text, the four captions and the four binding labels;
// - the device list: "None" is the first item (id 1) and a device follows it;
// - the learn: every Learn button starts a learn on its own target and says
//   "Cancel" while it does, the row's binding says "learning..." and the
//   other rows are untouched;
// - the binding's text: a CC and a note, with the app's channel index shown
//   as the user's channel;
// - the cleared binding.
//
// The suite drives the real component: it presses the buttons the user
// presses and reads the labels the user reads.

namespace
{
    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    juce::String tooltipOf (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) return t->getTooltip();
        return {};
    }

    // The section's Learn buttons, in the order they were added (the rows'
    // order: the gain, the intensity, the scrub, the transport).
    std::vector<juce::TextButton*> learnButtons (juce::Component& mc)
    {
        std::vector<juce::TextButton*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (int i = 0; i < c.getNumChildComponents(); ++i)
            {
                auto* child = c.getChildComponent (i);
                if (auto* b = dynamic_cast<juce::TextButton*> (child))
                    if (b->getButtonText() == "Learn") out.push_back (b);
                walk (*child);
            }
        };
        walk (mc);
        return out;
    }
}

void upic::runMidiSettingsChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto* dev    = dynamic_cast<juce::ComboBox*> (findByName (mc, "midiDevBox"));
    auto* devLab = dynamic_cast<juce::Label*>    (findByName (mc, "midiDevLab"));
    auto* act    = dynamic_cast<juce::Label*>    (findByName (mc, "midiActLab"));
    check (dev != nullptr && devLab != nullptr && act != nullptr,
           "R5.10 the MIDI section's device row and activity line are there");

    if (dev == nullptr || devLab == nullptr || act == nullptr)
        return;

    check (devLab->getText() == "MIDI in",
           "R5.10 the device row is captioned", devLab->getText());

    check (tooltipOf (dev) == "The MIDI input the app listens to",
           "R5.10 the device selector says what it is", tooltipOf (dev));

    check (dev->getNumItems() >= 1
           && dev->getItemText (0) == "None"
           && dev->getItemId (0) == 1,
           "R5.10 the device list starts with None",
           juce::String (dev->getNumItems()) + " item(s)");

    check (act->getText() == "no MIDI yet",
           "R5.10 the activity line starts quiet", act->getText());

    struct Row
    {
        const char* captionName;
        const char* caption;
        const char* bindName;
    };
    const Row rows[] =
    {
        { "midiGainLab",      "Master gain", "midiBindGainLab" },
        { "midiIntLab",       "Intensity",   "midiBindIntLab" },
        { "midiScrubLab",     "Scrub",       "midiBindScrubLab" },
        { "midiTransportLab", "Transport",   "midiBindTransportLab" }
    };

    bool captions = true;
    bool unbound = true;
    for (auto& r : rows)
    {
        auto* lab  = dynamic_cast<juce::Label*> (findByName (mc, r.captionName));
        auto* bind = dynamic_cast<juce::Label*> (findByName (mc, r.bindName));
        captions = captions && lab != nullptr && lab->getText() == r.caption;
        unbound  = unbound  && bind != nullptr && bind->getText() == "-";
    }
    check (captions, "R5.10 the four learn rows are captioned");
    check (unbound, "R5.10 the four bindings start unbound");

    auto buttons = learnButtons (mc);
    check (buttons.size() == 4,
           "R5.10 the section has four Learn buttons",
           juce::String ((int) buttons.size()) + " button(s)");

    bool allLearn = buttons.size() == 4;
    for (auto* b : buttons)
        allLearn = allLearn && b->getButtonText() == "Learn";
    check (allLearn, "R5.10 the buttons say Learn");

    // The hub has four targets, and the four rows' labels follow all four.
    // The app wires only two of the four buttons today (the master gain's and
    // the transport's): the intensity's and the scrub's onClick is empty, so
    // pressing them does nothing. R5.10 pins that as it is - the phase moves
    // the words, it does not add handlers.
    auto* gainBind = dynamic_cast<juce::Label*> (findByName (mc, "midiBindGainLab"));
    check (gainBind != nullptr, "R5.10 the master gain's binding is there");
    if (gainBind == nullptr)
        return;

    bool cancels = true;
    bool showsLearning = true;
    bool allBack = true;
    std::vector<int> wired;
    for (auto* b : buttons)
    {
        if (! b->onClick) continue;   // the row's button is not wired today

        b->onClick();
        const int target = (int) mc.midiHub().learningTarget();
        wired.push_back (target);

        for (auto* other : buttons)
            if ((other == b) != (other->getButtonText() == "Cancel"))
                cancels = false;

        if (target >= 0 && target < 4)
        {
            auto* bind = dynamic_cast<juce::Label*> (findByName (mc, rows[target].bindName));
            if (bind == nullptr || bind->getText() != "learning...")
                showsLearning = false;
        }

        b->onClick();   // cancel
        for (auto* other : buttons)
            if (other->getButtonText() != "Learn")
                allBack = false;
    }

    check ((int) wired.size() == 2,
           "R5.10 two of the four Learn buttons are wired today",
           juce::String ((int) wired.size()) + " wired");
    check (std::find (wired.begin(), wired.end(), (int) MidiHub::Target::masterGain) != wired.end()
           && std::find (wired.begin(), wired.end(), (int) MidiHub::Target::transport) != wired.end(),
           "R5.10 the wired buttons are the gain's and the transport's");
    check (cancels, "R5.10 the learning button says Cancel");
    check (showsLearning, "R5.10 the learning row says learning...");
    check (allBack, "R5.10 the buttons say Learn again after a cancel");

    // an unwired row still shows the hub's learning state (the labels follow
    // all four targets; only the buttons are not wired)
    for (auto target : { MidiHub::Target::masterGain, MidiHub::Target::drawIntensity,
                         MidiHub::Target::scrub, MidiHub::Target::transport })
    {
        mc.midiHub().startLearn (target);
        // handleMidi only refreshes the labels when something arrived; a clock
        // message is activity without a binding, so the row shows the learn
        mc.midiHub().injectForTest (MidiHub::kTypeOther, 0, 0, 0);
        mc.timerCallback();
        auto* bind = dynamic_cast<juce::Label*> (findByName (mc, rows[(int) target].bindName));
        bool saysCancel = false;
        for (auto* b : buttons)
            if (b->getButtonText() == "Cancel") saysCancel = true;
        check (bind != nullptr && bind->getText() == "learning..."
               && saysCancel,
               juce::String ("R5.10 the ") + rows[(int) target].caption
                 + " row shows the hub's learning state",
               bind != nullptr ? bind->getText() : juce::String());
        mc.midiHub().cancelLearn();
        mc.midiHub().injectForTest (MidiHub::kTypeOther, 0, 0, 0);
        mc.timerCallback();
    }

    // --- the binding's text ---------------------------------------------------
    juce::TextButton* gainLearn = nullptr;
    for (auto* b : buttons)
    {
        if (! b->onClick) continue;
        b->onClick();
        if (mc.midiHub().learningTarget() == MidiHub::Target::masterGain)
        {
            gainLearn = b;
            break;
        }
        b->onClick();
    }
    check (gainLearn != nullptr, "R5.10 the master gain's Learn button is found");

    mc.midiHub().injectForTest (MidiHub::kTypeCc, 2, 7, 100);
    mc.timerCallback();
    check (gainBind->getText() == "CC 7 ch 3",
           "R5.10 a learned CC names the control and the user's channel",
           gainBind->getText());

    // the intensity's button is not wired today: its binding is learned
    // through the hub, and the row's text follows all the same
    auto* intBind = dynamic_cast<juce::Label*> (findByName (mc, "midiBindIntLab"));
    mc.midiHub().startLearn (MidiHub::Target::drawIntensity);
    mc.midiHub().injectForTest (MidiHub::kTypeNoteOn, 0, 60, 64);
    mc.timerCallback();
    check (intBind != nullptr && intBind->getText() == "note 60 ch 1",
           "R5.10 a learned note names the key and the user's channel",
           intBind != nullptr ? intBind->getText() : juce::String());

    // --- the cleared binding --------------------------------------------------
    mc.midiHub().clearBinding (MidiHub::Target::masterGain);
    mc.midiHub().injectForTest (MidiHub::kTypeOther, 0, 0, 0);
    mc.timerCallback();
    check (gainBind->getText() == "-",
           "R5.10 a cleared binding reads -", gainBind->getText());
}
