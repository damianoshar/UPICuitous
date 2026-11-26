#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

#include <functional>

//==============================================================================
// R1.3: the overlay cards, before their geometry moves.
//
// The app has three in-app overlays: the audio device selector, the MIDI +
// OSC settings and the "clear page?" warning. Each one is a backdrop with a
// card, and MainComponent lays the card's contents out. This suite writes
// down what those cards do today: the rows are stacked in a fixed order, the
// widgets of a row share that row, and the buttons sit at the card's
// bottom.
//
// The overlays are opened the way the user opens them - the same methods the
// buttons call - so the suite sees the production layout and not a test-only
// arrangement.
//==============================================================================

namespace
{
    struct OverlayViewport { int w, h; const char* label; };
    const OverlayViewport kOverlayViewports[] = {
        { 1280, 800, "default" },
        {  800, 600, "tablet" },
        {  640, 480, "small" },
        {  460, 360, "minimum" }
    };

    juce::Component* findNamed (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findNamed (*c.getChildComponent (i), name))
                return r;
        return nullptr;
    }

    juce::Rectangle<int> boundsOf (juce::Component& root, const juce::String& name)
    {
        if (auto* c = findNamed (root, name))
            return c->getBounds();
        return {};
    }

    /** The widget with that name that sits in the row of `rowY`. The four
        MIDI Learn buttons all carry the name "Learn" (the button icons are
        chosen by the name), so the row is what tells them apart. */
    juce::Rectangle<int> boundsOfInRow (juce::Component& root, const juce::String& name, int rowY)
    {
        std::function<juce::Rectangle<int> (juce::Component&)> search =
            [&] (juce::Component& c) -> juce::Rectangle<int>
            {
                if (c.getName() == name && c.getY() == rowY)
                    return c.getBounds();
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                {
                    const auto r = search (*c.getChildComponent (i));
                    if (! r.isEmpty())
                        return r;
                }
                return {};
            };
        return search (root);
    }

    /** Two widgets share a row when their tops and their heights match. */
    bool sameRow (const juce::Rectangle<int>& a, const juce::Rectangle<int>& b)
    {
        return a.getY() == b.getY() && a.getHeight() == b.getHeight();
    }
}

void upic::runOverlayLayoutCharacterizationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    for (const auto& vp : kOverlayViewports)
    {
        mc.setSize (vp.w, vp.h);
        const auto at = juce::String (vp.label);
        const auto window = juce::Rectangle<int> (0, 0, mc.getWidth(), mc.getHeight());

        // --- the MIDI + OSC settings card ---------------------------------
        mc.showSettingsOverlayForTest();
        auto* settings = mc.overlayForTest ("settings");
        check (settings != nullptr && settings->isVisible(),
               "R1.3 the settings overlay opens at " + at + " size");

        if (settings != nullptr)
        {
            check (settings->getBounds() == window,
                   "R1.3 the settings overlay's backdrop covers the window at " + at + " size",
                   settings->getBounds().toString());

            const auto card = mc.overlayCardRectForTest ("settings");
            const auto okBtn = boundsOf (*settings, "settingsOkBtn");
            const auto apply = boundsOf (*settings, "oscApplyBtn");

            check (sameRow (okBtn, apply) && okBtn.getX() >= apply.getRight(),
                   "R1.3 the settings card's OK and Apply share the bottom row at " + at + " size",
                   okBtn.toString() + " / " + apply.toString());
            check (card.contains (okBtn) && card.contains (apply),
                   "R1.3 the settings card's buttons stay inside the card at " + at + " size",
                   card.toString() + " / " + okBtn.toString() + " / " + apply.toString());

            const auto midiSection = boundsOf (*settings, "midiSectionLab");
            const auto devLab = boundsOf (*settings, "midiDevLab");
            const auto devBox = boundsOf (*settings, "midiDevBox");
            const auto act = boundsOf (*settings, "midiActLab");
            const auto gainLab = boundsOf (*settings, "midiGainLab");
            const auto gainLearn = boundsOfInRow (*settings, "Learn", gainLab.getY());
            const auto gainBind = boundsOf (*settings, "midiBindGainLab");
            const auto intLab = boundsOf (*settings, "midiIntLab");
            const auto intLearn = boundsOfInRow (*settings, "Learn", intLab.getY());
            const auto intBind = boundsOf (*settings, "midiBindIntLab");
            const auto scrubLab = boundsOf (*settings, "midiScrubLab");
            const auto scrubLearn = boundsOfInRow (*settings, "Learn", scrubLab.getY());
            const auto scrubBind = boundsOf (*settings, "midiBindScrubLab");
            const auto transportLab = boundsOf (*settings, "midiTransportLab");
            const auto transportLearn = boundsOfInRow (*settings, "Learn", transportLab.getY());
            const auto transportBind = boundsOf (*settings, "midiBindTransportLab");

            check (midiSection.getBottom() <= devLab.getY()
                   && sameRow (devLab, devBox) && devLab.getRight() <= devBox.getX(),
                   "R1.3 the MIDI section's title sits above its device row at " + at + " size",
                   midiSection.toString() + " / " + devLab.toString() + " / " + devBox.toString());

            check (devBox.getBottom() <= act.getY()
                   && act.getBottom() <= gainLab.getY()
                   && gainLab.getBottom() <= intLab.getY()
                   && intLab.getBottom() <= scrubLab.getY()
                   && scrubLab.getBottom() <= transportLab.getY(),
                   "R1.3 the MIDI rows are stacked in order at " + at + " size",
                   devBox.toString() + " / " + act.toString() + " / " + gainLab.toString()
                     + " / " + intLab.toString() + " / " + scrubLab.toString()
                     + " / " + transportLab.toString());

            check (sameRow (gainLab, gainLearn) && sameRow (gainLearn, gainBind)
                   && gainLab.getRight() <= gainLearn.getX()
                   && gainLearn.getRight() <= gainBind.getX(),
                   "R1.3 the MIDI gain row holds its label, Learn and binding at " + at + " size",
                   gainLab.toString() + " / " + gainLearn.toString() + " / " + gainBind.toString());

            check (sameRow (intLab, intLearn) && sameRow (intLearn, intBind)
                   && intLab.getRight() <= intLearn.getX()
                   && intLearn.getRight() <= intBind.getX(),
                   "R1.3 the MIDI interval row holds its label, Learn and binding at " + at + " size",
                   intLab.toString() + " / " + intLearn.toString() + " / " + intBind.toString());

            check (sameRow (scrubLab, scrubLearn) && sameRow (scrubLearn, scrubBind)
                   && scrubLab.getRight() <= scrubLearn.getX()
                   && scrubLearn.getRight() <= scrubBind.getX(),
                   "R1.3 the MIDI scrub row holds its label, Learn and binding at " + at + " size",
                   scrubLab.toString() + " / " + scrubLearn.toString() + " / " + scrubBind.toString());

            check (sameRow (transportLab, transportLearn) && sameRow (transportLearn, transportBind)
                   && transportLab.getRight() <= transportLearn.getX()
                   && transportLearn.getRight() <= transportBind.getX(),
                   "R1.3 the MIDI transport row holds its label, Learn and binding at " + at + " size",
                   transportLab.toString() + " / " + transportLearn.toString()
                     + " / " + transportBind.toString());

            const auto oscSection = boundsOf (*settings, "oscSectionLab");
            const auto oscStatus = boundsOf (*settings, "oscStatusLab");
            const auto oscOn = boundsOf (*settings, "oscOnBtn");
            const auto portLab = boundsOf (*settings, "oscPortLab");
            const auto portSlider = boundsOf (*settings, "oscPortSlider");
            const auto mcBtn = boundsOf (*settings, "oscMcBtn");
            const auto groupLab = boundsOf (*settings, "oscGroupLab");
            const auto groupEdit = boundsOf (*settings, "oscGroupEdit");
            const auto mcPortLab = boundsOf (*settings, "oscMcPortLab");
            const auto mcPortSlider = boundsOf (*settings, "oscMcPortSlider");

            check (transportBind.getBottom() <= oscSection.getY()
                   && oscSection.getBottom() <= oscStatus.getY(),
                   "R1.3 the OSC section starts below the MIDI block at " + at + " size",
                   transportBind.toString() + " / " + oscSection.toString()
                     + " / " + oscStatus.toString());

            check (oscStatus.getBottom() <= oscOn.getY()
                   && oscOn.getBottom() <= portLab.getY()
                   && sameRow (portLab, portSlider) && portLab.getRight() <= portSlider.getX()
                   && portSlider.getBottom() <= mcBtn.getY(),
                   "R1.3 the OSC rows are stacked in order at " + at + " size",
                   oscStatus.toString() + " / " + oscOn.toString() + " / " + portLab.toString()
                     + " / " + portSlider.toString() + " / " + mcBtn.toString());

            check (sameRow (mcBtn, groupLab) && sameRow (groupLab, groupEdit)
                   && sameRow (groupEdit, mcPortLab) && sameRow (mcPortLab, mcPortSlider)
                   && mcBtn.getRight() <= groupLab.getX()
                   && groupLab.getRight() <= groupEdit.getX()
                   && groupEdit.getRight() <= mcPortLab.getX()
                   && mcPortLab.getRight() <= mcPortSlider.getX(),
                   "R1.3 the OSC multicast row holds its controls in one row at " + at + " size",
                   mcBtn.toString() + " / " + groupLab.toString() + " / " + groupEdit.toString()
                     + " / " + mcPortLab.toString() + " / " + mcPortSlider.toString());

            // the card is laid out again on every resize - the same result
            const auto okBefore = boundsOf (*settings, "settingsOkBtn");
            const auto devBefore = boundsOf (*settings, "midiDevBox");
            mc.resized();
            check (boundsOf (*settings, "settingsOkBtn") == okBefore
                   && boundsOf (*settings, "midiDevBox") == devBefore,
                   "R1.3 the settings card is the same when it is calculated again at " + at + " size",
                   okBefore.toString() + " / " + devBefore.toString());
        }
        mc.closeOverlaysForTest();

        // --- the "clear page?" card ---------------------------------------
        mc.showClearOverlayForTest();
        auto* clear = mc.overlayForTest ("clear");
        check (clear != nullptr && clear->isVisible(),
               "R1.3 the clear overlay opens at " + at + " size");

        if (clear != nullptr)
        {
            const auto card = mc.overlayCardRectForTest ("clear");
            const auto cancel = boundsOf (*clear, "clearCancelBtn");
            const auto confirm = boundsOf (*clear, "clearConfirmBtn");

            check (sameRow (cancel, confirm) && confirm.getRight() <= cancel.getX(),
                   "R1.3 the clear card's buttons share the bottom row at " + at + " size",
                   cancel.toString() + " / " + confirm.toString());
            check (card.contains (cancel) && card.contains (confirm),
                   "R1.3 the clear card's buttons stay inside the card at " + at + " size",
                   card.toString() + " / " + cancel.toString() + " / " + confirm.toString());
        }
        mc.closeOverlaysForTest();

        // --- the audio device card ----------------------------------------
        mc.showAudioOverlayForTest();
        auto* audio = mc.overlayForTest ("audio");
        check (audio != nullptr && audio->isVisible(),
               "R1.3 the audio overlay opens at " + at + " size");

        if (audio != nullptr)
        {
            const auto card = mc.overlayCardRectForTest ("audio");
            const auto okBtn = boundsOf (*audio, "OK");

            check (card.contains (okBtn) && okBtn.getRight() <= card.getRight(),
                   "R1.3 the audio card's OK button sits at the card's bottom right at " + at + " size",
                   card.toString() + " / " + okBtn.toString());
        }
        mc.closeOverlaysForTest();
    }
}
