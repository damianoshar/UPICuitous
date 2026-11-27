#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R3.5: the edits become commands.
//
// R3.4 made the command the unit of change, but the edits that were added
// after the undo stack was built still changed the piece without one: the
// page sequencer, the live controls, the step and space controls, the
// sample material, the tuning and the capture. R3.5 gives them the
// command, and this suite is their contract: every one of them is one undo
// step under its label, the document's revision advances once (the edit is
// one change of the document), and the epoch follows every change inside
// the command.
//
// The edits are driven the way a user makes them - through the named
// controls, or the public method the control calls. The state is read
// through the component's own doors (the revision, the epoch, the unsaved
// flag, the undo stack, and the piece the component would save), so the
// move does not change how the suite looks at the document.
//
// The components are made in plugin mode: the document does not depend on
// the window, and the plugin mode does not open an audio device.

namespace
{
    // The document's state, as the component sees it.
    struct EditState
    {
        std::uint64_t revision = 0;
        std::uint64_t epoch = 0;
        bool dirty = false;
        int undoDepth = 0;
        juce::String undoLabel;
    };

    // The one door: everything the suite knows about the document's state
    // comes through here.
    EditState state (const MainComponent& mc)
    {
        EditState s;
        s.revision = mc.modelRevisionForTest();
        s.epoch = mc.modelEpochForTest();
        s.dirty = mc.pieceHasUnsavedChanges();
        s.undoDepth = mc.undoDepth();
        s.undoLabel = mc.undoLabel();
        return s;
    }

    // The controls are named when they are built. The suite finds them by
    // name instead of holding the shell's private members.
    juce::Component* findNamed (juce::Component& root, const juce::String& name)
    {
        if (root.getName() == name) return &root;
        for (int i = 0; i < root.getNumChildComponents(); ++i)
            if (auto* c = findNamed (*root.getChildComponent (i), name))
                return c;
        return nullptr;
    }

    juce::Button* findButton (MainComponent& mc, const juce::String& name)
    {
        return dynamic_cast<juce::Button*> (findNamed (mc, name));
    }

    juce::ComboBox* findCombo (MainComponent& mc, const juce::String& name)
    {
        return dynamic_cast<juce::ComboBox*> (findNamed (mc, name));
    }

    juce::Slider* findSlider (MainComponent& mc, const juce::String& name)
    {
        return dynamic_cast<juce::Slider*> (findNamed (mc, name));
    }

    // A piece with one arc of the given mode (0 = wavetable, 2 = live).
    juce::var oneArcPiece (int mode)
    {
        return juce::JSON::parse (
            "{\"pages\":[{\"duration\":2,\"nextArcId\":9,\"arcs\":["
            "{\"id\":1,\"mode\":" + juce::String (mode) + ",\"intensity\":1,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":220}]}]}]}");
    }

    // A piece with two pages: the workspace needs two canvases.
    juce::var twoPagePiece()
    {
        return juce::JSON::parse (
            "{\"pages\":["
            "{\"duration\":2,\"nextArcId\":9,\"arcs\":[]},"
            "{\"duration\":2,\"nextArcId\":9,\"arcs\":[]}]}");
    }

    // An edit that changes the piece: it is one command, so it is one undo
    // step under its label, and the document's revision advances once (the
    // epoch follows every change inside the command).
    void expectCommand (upic::SelfTest& t, const juce::String& edit,
                        const juce::String& label,
                        const EditState& before, const EditState& after,
                        int changes = 1)
    {
        t.check (after.revision == before.revision + 1,
                 edit + ": the edit changes the document once",
                 juce::String (before.revision) + " -> " + juce::String (after.revision));
        t.check (after.epoch == before.epoch + (std::uint64_t) changes,
                 edit + ": the epoch follows every change in the command",
                 juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        t.check (after.dirty, edit + ": the piece is unsaved");
        t.check (after.undoDepth == before.undoDepth + 1,
                 edit + ": the edit is one undo step",
                 juce::String (before.undoDepth) + " -> " + juce::String (after.undoDepth));
        t.check (after.undoLabel == label,
                 edit + ": the step knows what the edit was called",
                 after.undoLabel);
    }
}

void upic::runModelCommandChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the tuning -----------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        upic::Tuning tuning;
        tuning.custom = true;
        tuning.name = "The suite's scale";
        tuning.cents = { 0.0, 300.0, 700.0 };
        mc.applyTuning (tuning);

        const auto after = state (mc);
        expectCommand (t, "the tuning", "Tuning", before, after);
        check (mc.pieceToVar()["tuning"].isObject(),
               "the tuning: the piece carries the scale");
    }

    // --- the page sequencer ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        auto* seq = findButton (mc, "seqBtn");
        check (seq != nullptr, "the page sequencer: the control is there");

        if (seq != nullptr)
        {
            const auto before = state (mc);
            seq->setToggleState (true, juce::dontSendNotification);
            seq->onClick();

            const auto after = state (mc);
            expectCommand (t, "the page sequencer", "Sequence the pages", before, after);
            check ((bool) mc.pieceToVar()["seqPages"],
                   "the page sequencer: the piece carries it");
        }
    }

    // --- the live pitch --------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        auto* box = findCombo (mc, "livePitchBox");
        check (box != nullptr, "the live pitch: the control is there");

        if (box != nullptr)
        {
            const auto before = state (mc);
            box->setSelectedId (2, juce::dontSendNotification); // tape
            box->onChange();

            const auto after = state (mc);
            expectCommand (t, "the live pitch", "Live pitch", before, after);
            check (mc.livePitchModeForTest() == 1,
                   "the live pitch: the engine plays the chosen mode",
                   juce::String (mc.livePitchModeForTest()));
            check ((int) mc.pieceToVar()["livePitchMode"] == 1,
                   "the live pitch: the piece carries it");
        }
    }

    // --- the live reverse ------------------------------------------------------
    // The button sets the direction on every live arc and then re-anchors the
    // live voices: two changes in one callback.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (2)),
               "the live reverse: the piece with a live arc loads");

        auto* btn = findButton (mc, "liveRevBtn");
        check (btn != nullptr, "the live reverse: the control is there");

        if (btn != nullptr)
        {
            const auto before = state (mc);
            btn->setToggleState (true, juce::dontSendNotification);
            btn->onClick();

            const auto after = state (mc);
            expectCommand (t, "the live reverse", "Live reverse", before, after, 2);
            check ((bool) mc.pieceToVar()["pages"][0]["arcs"][0]["reversed"],
                   "the live reverse: the live arc plays backwards");
        }
    }

    // --- the stepped pitch -----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (0)),
               "the stepped pitch: the piece loads");
        mc.selectArcsForTest ({ 1 });

        auto* btn = findButton (mc, "stepBtn");
        check (btn != nullptr, "the stepped pitch: the control is there");

        if (btn != nullptr)
        {
            const auto before = state (mc);
            btn->setToggleState (true, juce::dontSendNotification);
            btn->onClick();

            const auto after = state (mc);
            expectCommand (t, "the stepped pitch", "Step the pitch", before, after);
            check ((int) mc.pieceToVar()["pages"][0]["arcs"][0]["sd"] == 12,
                   "the stepped pitch: the piece carries the divisions");
        }
    }

    // --- the page's ambitus ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        auto* box = findCombo (mc, "ambitusBox");
        check (box != nullptr, "the page's ambitus: the control is there");

        if (box != nullptr)
        {
            const auto before = state (mc);
            box->setSelectedId (3, juce::dontSendNotification); // six octaves
            box->onChange();

            const auto after = state (mc);
            expectCommand (t, "the page's ambitus", "Page ambitus", before, after);
            const auto page = mc.pieceToVar()["pages"][0];
            const auto& preset = upic::ambitusPresets()[2];
            check (std::abs ((double) page["ambLo"] - preset.lo) < 1e-9
                   && std::abs ((double) page["ambHi"] - preset.hi) < 1e-9,
                   "the page's ambitus: the piece carries the interval",
                   juce::String ((double) page["ambLo"]) + " - "
                   + juce::String ((double) page["ambHi"]));
        }
    }

    // --- the page's acceleration -----------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        auto* slider = findSlider (mc, "warpSlider");
        check (slider != nullptr, "the page's acceleration: the control is there");

        if (slider != nullptr)
        {
            const auto before = state (mc);
            slider->setValue (0.5, juce::dontSendNotification);
            slider->onValueChange();

            const auto after = state (mc);
            expectCommand (t, "the page's acceleration", "Page acceleration", before, after);
            check (std::abs (mc.pageWarpForTest() - 0.5) < 1e-9,
                   "the page's acceleration: the page carries it",
                   juce::String (mc.pageWarpForTest()));
        }
    }

    // --- the page's frequency space --------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        auto* box = findCombo (mc, "spaceDefBox1");
        check (box != nullptr, "the frequency space: the control is there");

        if (box != nullptr)
        {
            const auto before = state (mc);
            box->setSelectedId (3, juce::dontSendNotification);
            box->onChange();

            const auto after = state (mc);
            expectCommand (t, "the frequency space", "Frequency space", before, after);
            check (mc.pageSpaceForTest (1) == 2,
                   "the frequency space: the page carries the choice",
                   juce::String (mc.pageSpaceForTest (1)));
        }
    }

    // --- the material ----------------------------------------------------------
    // The material's controls need a loaded slot; the test installs one and
    // takes its state after the install (the install is not the edit under
    // test). The three edits all follow the selected slot.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        juce::AudioBuffer<float> data (1, 4096);
        for (int i = 0; i < data.getNumSamples(); ++i)
            data.setSample (0, i, std::sin (2.0f * juce::MathConstants<float>::pi
                                            * 220.0f * (float) i / 44100.0f));
        check (mc.setSampleSlotForTest (0, data, 44100.0),
               "the material: the slot loads");

        {
            const auto before = state (mc);
            mc.sampleReverseForTest();

            const auto after = state (mc);
            expectCommand (t, "the material's reverse", "Reverse the material", before, after);
            check ((bool) mc.pieceToVar()["samples"][0]["reversed"],
                   "the material's reverse: the slot plays backwards");
        }

        {
            const auto before = state (mc);
            mc.sampleLevelForTest (0.5);

            const auto after = state (mc);
            expectCommand (t, "the material's level", "Slot level", before, after);
            check (std::abs ((double) mc.pieceToVar()["samples"][0]["level"] - 0.5) < 1e-6,
                   "the material's level: the slot carries it");
        }

        auto* box = findCombo (mc, "samplePitchBox");
        check (box != nullptr, "the material's pitch: the control is there");

        if (box != nullptr)
        {
            const auto before = state (mc);
            box->setSelectedId (3, juce::dontSendNotification); // phase vocoder
            box->onChange();

            const auto after = state (mc);
            expectCommand (t, "the material's pitch", "Sample pitch", before, after);
            check (mc.samplePitchModeForTest() == 2,
                   "the material's pitch: the engine plays the chosen mode",
                   juce::String (mc.samplePitchModeForTest()));
            check ((int) mc.pieceToVar()["samplePitchMode"] == 2,
                   "the material's pitch: the piece carries it");
        }
    }

    // --- the modulation --------------------------------------------------------
    // The three controls edit the selected arcs, so the piece needs one and
    // the arc has to be selected.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (0)),
               "the modulation: the piece loads");
        mc.selectArcsForTest ({ 1 });

        auto* use = findCombo (mc, "modUseBox");
        check (use != nullptr, "the modulation: the wave use control is there");

        if (use != nullptr)
        {
            const auto before = state (mc);
            use->setSelectedId (2, juce::dontSendNotification); // + amplitude LFO
            use->onChange();

            const auto after = state (mc);
            expectCommand (t, "the wave use", "Wave use", before, after);
            check ((int) mc.pieceToVar()["pages"][0]["arcs"][0]["waveUse"] == 1,
                   "the wave use: the arc carries it");
        }

        auto* rate = findSlider (mc, "modRateSlider");
        check (rate != nullptr, "the modulation: the rate control is there");

        if (rate != nullptr)
        {
            const auto before = state (mc);
            rate->setValue (2.5, juce::dontSendNotification);
            rate->onValueChange();

            const auto after = state (mc);
            expectCommand (t, "the modulation rate", "Modulation rate", before, after);
            check (std::abs ((double) mc.pieceToVar()["pages"][0]["arcs"][0]["modRate"]
                              - 2.5) < 1e-9,
                   "the modulation rate: the arc carries it");
        }

        auto* depth = findSlider (mc, "modDepthSlider");
        check (depth != nullptr, "the modulation: the depth control is there");

        if (depth != nullptr)
        {
            const auto before = state (mc);
            depth->setValue (0.4, juce::dontSendNotification);
            depth->onValueChange();

            const auto after = state (mc);
            expectCommand (t, "the modulation depth", "Modulation depth", before, after);
            check (std::abs ((double) mc.pieceToVar()["pages"][0]["arcs"][0]["modDepth"]
                              - 0.4) < 1e-9,
                   "the modulation depth: the arc carries it");
        }
    }

    // --- the step divisions ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (0)),
               "the step divisions: the piece loads");
        mc.selectArcsForTest ({ 1 });

        // the divisions only mean something when the arc is stepped
        auto* step = findButton (mc, "stepBtn");
        check (step != nullptr, "the step divisions: the step control is there");

        if (step != nullptr)
        {
            step->setToggleState (true, juce::dontSendNotification);
            step->onClick();

            auto* divs = findSlider (mc, "stepDivSlider");
            check (divs != nullptr, "the step divisions: the divisions control is there");

            if (divs != nullptr)
            {
                const auto before = state (mc);
                divs->setValue (19.0, juce::dontSendNotification);
                divs->onValueChange();

                const auto after = state (mc);
                expectCommand (t, "the step divisions", "Step divisions", before, after);
                check ((int) mc.pieceToVar()["pages"][0]["arcs"][0]["sd"] == 19,
                       "the step divisions: the arc carries them");
            }
        }
    }

    // --- the arc's frequency space ---------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (0)),
               "the arc's frequency space: the piece loads");
        mc.selectArcsForTest ({ 1 });

        auto* box = findCombo (mc, "spaceBox");
        check (box != nullptr, "the arc's frequency space: the control is there");

        if (box != nullptr)
        {
            const auto before = state (mc);
            box->setSelectedId (2, juce::dontSendNotification);
            box->onChange();

            const auto after = state (mc);
            expectCommand (t, "the arc's frequency space", "Arc frequency space",
                           before, after);
            check ((int) mc.pieceToVar()["pages"][0]["arcs"][0]["sp"] == 1,
                   "the arc's frequency space: the arc carries it");
        }
    }

    // --- the arc list's per-arc controls ---------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (oneArcPiece (0)),
               "the arc list: the piece loads");

        {
            const auto before = state (mc);
            mc.arcListReverseForTest (0, 1, true);

            const auto after = state (mc);
            expectCommand (t, "the arc's reverse", "Arc reverse", before, after);
            check ((bool) mc.pieceToVar()["pages"][0]["arcs"][0]["reversed"],
                   "the arc's reverse: the arc carries it");
        }

        {
            const auto before = state (mc);
            mc.arcListModeForTest (0, 1, 2);

            const auto after = state (mc);
            expectCommand (t, "the arc's pitch mode", "Arc pitch mode", before, after);
            check ((int) mc.pieceToVar()["pages"][0]["arcs"][0]["pitchMode"] == 2,
                   "the arc's pitch mode: the arc carries it");
        }
    }

    // --- the workspace split ---------------------------------------------------
    // The split is a drag: the command opens at the drag's start and closes
    // at its end, and the moves in between join it.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (mc.loadFromVar (twoPagePiece()),
               "the workspace split: the piece loads");
        check (mc.setWorkspaceOpen (true),
               "the workspace split: the workspace opens");

        const auto before = state (mc);
        mc.workspaceSplitDragForTest (0.7);

        const auto after = state (mc);
        expectCommand (t, "the workspace split", "Resize the workspace", before, after);
        check (std::abs (mc.workspaceSplitForTest() - 0.7) < 1e-9,
               "the workspace split: the workspace carries it",
               juce::String (mc.workspaceSplitForTest()));
        check (std::abs ((double) mc.pieceToVar()["workspace"]["split"] - 0.7) < 1e-9,
               "the workspace split: the piece carries it");
    }
}
