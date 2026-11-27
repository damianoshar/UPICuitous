#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R3.4: the command dispatcher - the document's mutation entry point.
//
// A command is one edit: it has a label (what the undo menu calls it), it
// changes the document, and the document's revision advances once. R3.4
// introduces the dispatcher (the review's R008) and makes the command the
// unit of change; this suite is its contract.
//
// The suite reads the document through the component's own doors (the
// revision, the epoch, the unsaved flag, the undo stack), so the move does
// not change the checks.
//
// The components are made in plugin mode: the document does not depend on
// the window, and the plugin mode does not open an audio device.
//
// Two things about the document are worth knowing before reading the
// checks: a change outside a command is a change of its own (there is no
// transaction to put it in), and a command that changes nothing is not an
// undo step.

namespace
{
    // The document's state, as the component sees it.
    struct DocumentState
    {
        std::uint64_t revision = 0;
        std::uint64_t epoch = 0;
        bool dirty = false;
        int undoDepth = 0;
        juce::String undoLabel;
    };

    // The one door: everything the suite knows about the document's state
    // comes through here.
    DocumentState state (const MainComponent& mc)
    {
        DocumentState s;
        s.revision = mc.modelRevisionForTest();
        s.epoch = mc.modelEpochForTest();
        s.dirty = mc.pieceHasUnsavedChanges();
        s.undoDepth = mc.undoDepth();
        s.undoLabel = mc.undoLabel();
        return s;
    }
}

void upic::runCommandChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- a change outside a command -----------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        mc.markModelChanged();

        const auto after = state (mc);
        check (after.revision == before.revision + 1,
               "a change outside a command advances the revision by one",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch == before.epoch + 1,
               "a change outside a command advances the epoch by one",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (after.dirty,
               "a change outside a command marks the piece unsaved");
        check (after.undoDepth == before.undoDepth,
               "a change outside a command is not an undo step",
               juce::String (after.undoDepth));
    }

    // --- a change that does not affect the piece ------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        mc.markModelChanged (false);

        const auto after = state (mc);
        check (after.revision == before.revision + 1
               && after.epoch == before.epoch + 1,
               "a change that does not affect the piece still moves the counters",
               juce::String (before.revision) + " -> " + juce::String (after.revision)
               + ", " + juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (! after.dirty,
               "a change that does not affect the piece does not mark it unsaved");
    }

    // --- one command -----------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        mc.beginEdit ("The command");
        check (mc.commandOpenForTest(),
               "a command is open between its begin and its end");
        mc.markModelChanged();
        mc.endEdit();

        const auto after = state (mc);
        check (! mc.commandOpenForTest(),
               "the command is closed at its end");
        check (after.undoDepth == before.undoDepth + 1,
               "a command is one undo step",
               juce::String (after.undoDepth));
        check (after.undoLabel == "The command",
               "the undo step knows what the command was called",
               after.undoLabel);
        check (after.epoch == before.epoch + 1,
               "the epoch follows the change in the command",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (after.revision == before.revision + 1,
               "a command advances the revision by one",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.dirty,
               "a command marks the piece unsaved");
    }

    // --- a command with two changes --------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        mc.beginEdit ("The command");
        mc.markModelChanged();
        mc.markModelChanged();
        mc.endEdit();

        const auto after = state (mc);
        check (after.undoDepth == before.undoDepth + 1,
               "two changes in one command are one undo step",
               juce::String (after.undoDepth));
        check (after.epoch == before.epoch + 2,
               "the epoch follows every change in the command",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (after.revision == before.revision + 2,
               "a command advances the revision once per change",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
    }

    // --- a command that changes nothing -----------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);

        mc.beginEdit ("The command");
        mc.endEdit();

        const auto after = state (mc);
        check (after.undoDepth == before.undoDepth,
               "a command that changes nothing is not an undo step",
               juce::String (after.undoDepth));
        check (after.revision == before.revision,
               "a command that changes nothing does not advance the revision",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch == before.epoch,
               "a command that changes nothing does not advance the epoch",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (! after.dirty,
               "a command that changes nothing does not mark the piece unsaved");
    }

    // --- two commands ------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        mc.beginEdit ("The first");
        mc.markModelChanged();
        mc.endEdit();
        mc.beginEdit ("The second");
        mc.markModelChanged();
        mc.endEdit();

        check (mc.undoDepth() == 2,
               "two commands are two undo steps", juce::String (mc.undoDepth()));
        check (mc.undoLabel() == "The second",
               "the newest command is the step on top", mc.undoLabel());
    }

    // --- a command that was never closed -------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        mc.beginEdit ("The open one");
        mc.markModelChanged();
        // the gesture was interrupted: the next command closes it
        mc.beginEdit ("The next one");
        mc.markModelChanged();
        mc.endEdit();

        check (mc.undoDepth() == 2,
               "a command that was never closed is closed by the next one",
               juce::String (mc.undoDepth()));
        check (mc.undoLabel() == "The next one",
               "the newest command is the step on top", mc.undoLabel());
    }

    // --- two documents --------------------------------------------------------------
    {
        auto firstHolder = std::make_unique<MainComponent> (true);
        auto& first = *firstHolder;
        auto secondHolder = std::make_unique<MainComponent> (true);
        auto& second = *secondHolder;

        first.beginEdit ("The first");
        first.markModelChanged();
        first.endEdit();

        check (first.undoDepth() == 1 && second.undoDepth() == 0,
               "two documents keep their own commands",
               juce::String (first.undoDepth()) + "/" + juce::String (second.undoDepth()));
        check (second.modelRevisionForTest() == 0,
               "the second document's revision is its own",
               juce::String (second.modelRevisionForTest()));
    }

    // --- an undo is a change ---------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        mc.beginEdit ("The command");
        mc.markModelChanged();
        mc.endEdit();

        const auto before = state (mc);
        mc.undo();
        const auto after = state (mc);

        check (after.revision > before.revision,
               "an undo is a change of the document",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.dirty,
               "an undo marks the piece unsaved");
        check (after.undoDepth == before.undoDepth - 1,
               "the undo takes the step off the stack",
               juce::String (after.undoDepth));
    }
}
