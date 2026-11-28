#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "OverlayCard.h"

//==============================================================================
// R5.11 (the review's R053, section 29): the overlay card shell.
//
// The app cannot use a floating dialog window: on Android there is no title
// bar and no close button, so a dialog could never be dismissed. Every editor
// and overlay is therefore a plain Component that paints a backdrop and a
// card in the middle of the window, and a tap on the backdrop dismisses it.
//
// Eight of those shells wrote the same rule again: the window loses a
// margin, the card never grows past the shell's maximum, and the card sits in
// the middle.
//
// This suite is the shell's contract. It pins the card's rectangle (at three
// window sizes), the content's padding and the rule that a tap on the
// backdrop dismisses the shell while a tap on the card does not.
//==============================================================================

namespace
{
    juce::Component* findByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }

    /** The card a shell draws, or an empty rectangle when the component is
        not one of the card shells. */
    juce::Rectangle<int> cardOf (juce::Component* c)
    {
        if (c == nullptr) return {};
        if (auto* p = dynamic_cast<GroupPanel*> (c))      return p->cardRect();
        if (auto* p = dynamic_cast<ChannelPanel*> (c))    return p->cardRect();
        if (auto* p = dynamic_cast<SequencePanel*> (c))   return p->cardRect();
        if (auto* p = dynamic_cast<AmpTableEditor*> (c))  return p->cardRect();
        if (auto* p = dynamic_cast<FreqTableEditor*> (c)) return p->cardRect();
        if (auto* p = dynamic_cast<AudioOverlay*> (c))    return p->cardRect();
        if (auto* p = dynamic_cast<ClearOverlay*> (c))    return p->cardRect();
        return {};
    }

    juce::String rectText (const juce::Rectangle<int>& r)
    {
        return juce::String (r.getX()) + ", " + juce::String (r.getY()) + ", "
             + juce::String (r.getWidth()) + " x " + juce::String (r.getHeight());
    }

    /** A mouse press at a point - the shells that read the mouse. */
    void pressAt (juce::Component& c, juce::Point<float> pos)
    {
        const auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, pos, juce::ModifierKeys(), 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, false);
        c.mouseDown (down);
    }

    /** The card's centre, in the shell's own coordinates - the edge's card. */
    juce::Point<float> cardCentre (upic::overlay_card::Shell shell, int w, int h)
    {
        const auto r = upic::overlay_card::cardFor (shell, w, h);
        return { (float) (r.x + r.w / 2), (float) (r.y + r.h / 2) };
    }

    /** The shell's own dismissal rule: the backdrop closes the shell and the
        card keeps it. The card's centre comes from the edge, so the check is
        a check of the edge too. */
    void checkDismissal (upic::SelfTest& t, const juce::String& name,
                         juce::Component& shell, std::function<void()>& hook,
                         upic::overlay_card::Shell which)
    {
        shell.setBounds (0, 0, 1280, 800);
        bool closed = false;
        hook = [&closed] { closed = true; };

        pressAt (shell, juce::Point<float> (5.0f, 5.0f));   // the backdrop
        t.check (closed, "R5.11 a tap on the backdrop closes " + name);

        closed = false;
        pressAt (shell, cardCentre (which, 1280, 800));        // the card
        t.check (! closed, "R5.11 a tap on the card keeps " + name);
    }
}

void upic::runOverlayCardChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the shells ----------------------------------------------------------
    // The card is the shell's own: the window loses a margin, the card never
    // grows past the shell's maximum and the card sits in the middle.
    struct ShellCase
    {
        const char* shell;          // the shell's name
        const char* component;      // the component that draws it
        upic::overlay_card::Shell edge;   // the shell the edge names
        juce::Rectangle<int> atDesktop;   // the card at 1280 x 800
        juce::Rectangle<int> atCompact;   // the card at 1024 x 700
    };

    const ShellCase cases[] =
    {
        { "the group panel",      "groupPanel",    upic::overlay_card::Shell::Group,
          { 220, 100, 840, 600 }, {  92,  50, 840, 600 } },
        { "the mixer",            "channelPanel",  upic::overlay_card::Shell::Channel,
          { 260, 150, 760, 500 }, { 132, 100, 760, 500 } },
        { "the sequence panel",   "sequencePanel", upic::overlay_card::Shell::Sequence,
          { 230, 130, 820, 540 }, { 102,  80, 820, 540 } },
        { "the amplitude tables", "ampTableEd",    upic::overlay_card::Shell::AmpTables,
          { 250, 115, 780, 570 }, { 122,  65, 780, 570 } },
        { "the frequency table",  "freqTableEd",   upic::overlay_card::Shell::FreqTable,
          { 260, 120, 760, 560 }, { 132,  70, 760, 560 } },
        { "the audio settings",   "audioOverlay",  upic::overlay_card::Shell::AudioSettings,
          { 360, 100, 560, 600 }, { 232,  50, 560, 600 } },
        { "the clear warning",    "clearOverlay",  upic::overlay_card::Shell::ClearPage,
          { 400, 295, 480, 210 }, { 272, 245, 480, 210 } }
    };

    auto mcHolder = std::make_unique<MainComponent>();
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    // the mixer is built when it opens, so it has to be opened once
    if (auto* mixBtn = dynamic_cast<juce::TextButton*> (findByName (mc, "channelBtn")))
        mixBtn->onClick();

    for (auto& c : cases)
    {
        auto* comp = findByName (mc, c.component);
        check (comp != nullptr && cardOf (comp) == c.atDesktop,
               juce::String ("R5.11 ") + c.shell + "'s card is in the middle of the window",
               comp != nullptr ? rectText (cardOf (comp)) : juce::String ("no shell"));
    }

    // the MIDI + OSC settings draw the same card as the audio ones
    auto* settings = findByName (mc, "settingsOverlay");
    check (settings != nullptr && cardOf (settings) == cardOf (findByName (mc, "audioOverlay")),
           "R5.11 the MIDI settings draw the same card as the audio settings",
           settings != nullptr ? rectText (cardOf (settings)) : juce::String ("no shell"));

    // the performance view keeps its card to itself, but its close button
    // shows where the card is
    auto* perfClose = dynamic_cast<juce::TextButton*> (findByName (mc, "perfCloseBtn"));
    check (perfClose != nullptr
           && perfClose->getBounds() == juce::Rectangle<int> (1048, 102, 64, 26),
           "R5.11 the performance view's card is in the middle of the window",
           perfClose != nullptr ? rectText (perfClose->getBounds()) : juce::String ("no shell"));

    // the group's content starts inside the card's padding
    auto* group = dynamic_cast<GroupPanel*> (findByName (mc, "groupPanel"));
    check (group != nullptr
           && group->listRect() == juce::Rectangle<int> (238, 138, 230, 516),
           "R5.11 the group's content sits inside the card's padding",
           group != nullptr ? rectText (group->listRect()) : juce::String ("no shell"));

    // --- the smaller window --------------------------------------------------
    mc.setSize (1024, 700);

    for (auto& c : cases)
    {
        auto* comp = findByName (mc, c.component);
        check (comp != nullptr && cardOf (comp) == c.atCompact,
               juce::String ("R5.11 ") + c.shell + "'s card follows the smaller window",
               comp != nullptr ? rectText (cardOf (comp)) : juce::String ("no shell"));
    }

    perfClose = dynamic_cast<juce::TextButton*> (findByName (mc, "perfCloseBtn"));
    check (perfClose != nullptr
           && perfClose->getBounds() == juce::Rectangle<int> (920, 52, 64, 26),
           "R5.11 the performance view's card follows the smaller window",
           perfClose != nullptr ? rectText (perfClose->getBounds()) : juce::String ("no shell"));

    // --- a window that is too small -----------------------------------------
    // the card never grows past the shell's maximum, and it never grows past
    // the window either
    mc.setSize (640, 480);
    auto* groupSmall = dynamic_cast<GroupPanel*> (findByName (mc, "groupPanel"));
    check (groupSmall != nullptr
           && groupSmall->cardRect() == juce::Rectangle<int> (10, 10, 620, 460),
           "R5.11 the card fills the window when the window is small",
           groupSmall != nullptr ? rectText (groupSmall->cardRect()) : juce::String ("no shell"));

    // --- the backdrop dismisses, the card does not ---------------------------
    {
        AudioOverlay ov;
        ov.setBounds (0, 0, 1280, 800);
        bool dismissed = false;
        ov.onDismiss = [&dismissed] { dismissed = true; };
        pressAt (ov, juce::Point<float> (5.0f, 5.0f));      // the backdrop
        check (dismissed, "R5.11 a tap on the backdrop dismisses the overlay");

        dismissed = false;
        pressAt (ov, ov.cardRect().getCentre().toFloat());  // the card
        check (! dismissed, "R5.11 a tap on the card keeps the overlay");
    }

    {
        ClearOverlay ov;
        ov.setBounds (0, 0, 1280, 800);
        bool cancelled = false;
        ov.onCancel = [&cancelled] { cancelled = true; };
        pressAt (ov, juce::Point<float> (5.0f, 5.0f));      // the backdrop
        check (cancelled, "R5.11 a tap on the backdrop cancels the warning");

        cancelled = false;
        pressAt (ov, ov.cardRect().getCentre().toFloat());  // the card
        check (! cancelled, "R5.11 a tap on the card keeps the warning");
    }

    // --- the app follows the edge (R5.11) -------------------------------------
    namespace oc = upic::overlay_card;

    // --- the editors dismiss the same way (R5.11) ---------------------------
    // The card is the shell's boundary: a tap on the backdrop closes the
    // editor, and a tap on the card does not.
    {
        GroupPanel shell;
        checkDismissal (t, "the group panel", shell, shell.onClose, oc::Shell::Group);
    }
    {
        ChannelPanel shell;
        checkDismissal (t, "the mixer", shell, shell.onClose, oc::Shell::Channel);
    }
    {
        SequencePanel shell;
        checkDismissal (t, "the sequence panel", shell, shell.onClose, oc::Shell::Sequence);
    }
    {
        AmpTableEditor shell;
        checkDismissal (t, "the amplitude tables", shell, shell.onClose, oc::Shell::AmpTables);
    }
    {
        FreqTableEditor shell;
        checkDismissal (t, "the frequency table", shell, shell.onClose, oc::Shell::FreqTable);
    }

    // the performance view is the one shell with no backdrop dismissal: it
    // has a close button instead
    {
        PerformanceView shell;
        shell.setBounds (0, 0, 1280, 800);
        bool closed = false;
        shell.onClose = [&closed] { closed = true; };
        pressAt (shell, juce::Point<float> (5.0f, 5.0f));   // the backdrop
        check (! closed, "R5.11 the performance view has no backdrop dismissal");
    }

    // the edge names every shell once, and every shell's card may take a size
    bool sizesSound = true;
    for (int i = 0; i < (int) oc::Shell::count; ++i)
    {
        const auto s = oc::sizeFor ((oc::Shell) i);
        if (s.maxWidth <= 0 || s.maxHeight <= 0) sizesSound = false;
    }
    check (sizesSound, "R5.11 the edge gives every shell a card size",
           juce::String ((int) oc::Shell::count) + " shells");

    // every shell's card is the edge's card
    bool cardsFollowEdge = true;
    for (auto& c : cases)
    {
        auto* comp = findByName (mc, c.component);
        if (comp == nullptr) { cardsFollowEdge = false; continue; }
        const auto r = oc::cardFor (c.edge, comp->getWidth(), comp->getHeight());
        if (cardOf (comp) != juce::Rectangle<int> (r.x, r.y, r.w, r.h))
            cardsFollowEdge = false;
    }
    check (cardsFollowEdge, "R5.11 every shell's card is the edge's card");

    // the performance view keeps its card in the same place as the others
    auto* perf = dynamic_cast<PerformanceView*> (findByName (mc, "performanceView"));
    bool perfFollowsEdge = false;
    if (perf != nullptr)
    {
        const auto r = oc::cardFor (oc::Shell::Performance, perf->getWidth(), perf->getHeight());
        perfFollowsEdge = perf->cardRect() == juce::Rectangle<int> (r.x, r.y, r.w, r.h);
    }
    check (perfFollowsEdge, "R5.11 the performance view's card is the edge's card");

    // the content's padding is the edge's
    auto* groupEdge = dynamic_cast<GroupPanel*> (findByName (mc, "groupPanel"));
    bool paddingFollowsEdge = false;
    if (groupEdge != nullptr)
    {
        const auto r = oc::cardFor (oc::Shell::Group, groupEdge->getWidth(), groupEdge->getHeight());
        const auto padded = oc::paddedFor (r);
        // the group's own rows: a 26 px title strip above the list
        constexpr int groupTitleH = 26;
        paddingFollowsEdge = groupEdge->listRect().getX() == padded.x
                              && groupEdge->listRect().getY() == padded.y + groupTitleH
                              && groupEdge->listRect().getWidth() > 0;
    }
    check (paddingFollowsEdge, "R5.11 the content's padding is the edge's");
}
