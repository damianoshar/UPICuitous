#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R3.1: the document session - the document's revision and its unsaved flag.
//
// The document's own state used to live in the component as three counters:
// the model epoch (the audio thread's "the model moved"), the model revision
// (the status timer's "there is something new to copy") and the piece's
// unsaved flag (the window's "ask before closing"). R3.1 gives them one
// owner - the document session - and this suite is that owner's contract.
//
// The suite reads the state through one door (the state helper), so the move
// that comes next touches the suite in one place and the checks themselves
// do not change.
//
// The components are made in plugin mode: the document's state does not
// depend on the window, and the plugin mode does not open an audio device.

namespace
{
    // The document's state, as the component sees it.
    struct DocumentState
    {
        std::uint64_t revision = 0;
        std::uint64_t epoch = 0;
        bool dirty = false;
    };

    // The one door: everything the suite knows about the document's state
    // comes through here.
    DocumentState state (const MainComponent& mc)
    {
        DocumentState s;
        s.revision = mc.modelRevisionForTest();
        s.epoch = mc.modelEpochForTest();
        s.dirty = mc.pieceHasUnsavedChanges();
        return s;
    }
}

void upic::runDocumentSessionChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- a fresh document ---------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto s = state (mc);
        check (s.revision == 0,
               "a fresh document is at revision 0",
               juce::String (s.revision));
        check (s.epoch == 0,
               "a fresh document is at epoch 0",
               juce::String (s.epoch));
        check (! s.dirty,
               "a fresh document has no unsaved changes");
    }

    // --- one model change ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);
        mc.markModelChanged();
        const auto after = state (mc);
        check (after.revision == before.revision + 1,
               "a model change advances the revision by one",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch == before.epoch + 1,
               "a model change advances the epoch by one",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (after.dirty,
               "a model change marks the piece unsaved");

        // the second change counts the same way
        const auto two = state (mc);
        mc.markModelChanged();
        const auto three = state (mc);
        check (three.revision == two.revision + 1,
               "a second model change advances the revision by one",
               juce::String (two.revision) + " -> " + juce::String (three.revision));
        check (three.epoch == two.epoch + 1,
               "a second model change advances the epoch by one",
               juce::String (two.epoch) + " -> " + juce::String (three.epoch));
        check (three.epoch - three.revision == after.epoch - after.revision,
               "the epoch and the revision count the same changes");
    }

    // --- a change that does not affect the piece ------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = state (mc);
        mc.markModelChanged (false);
        const auto after = state (mc);
        check (after.revision == before.revision + 1,
               "a change that does not affect the piece still advances the revision",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch == before.epoch + 1,
               "a change that does not affect the piece still advances the epoch",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (! after.dirty,
               "a change that does not affect the piece does not mark it unsaved");

        // the piece was already unsaved: the change does not clear the flag
        mc.markModelChanged();
        mc.markModelChanged (false);
        check (mc.pieceHasUnsavedChanges(),
               "a change that does not affect the piece does not clear the unsaved flag");
    }

    // --- saving the piece ------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.markModelChanged();
        const auto before = state (mc);
        mc.markPieceSaved();
        const auto after = state (mc);
        check (! after.dirty,
               "saving the piece clears the unsaved flag");
        check (after.revision == before.revision,
               "saving the piece does not move the revision",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch == before.epoch,
               "saving the piece does not move the epoch",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));

        // saving twice is harmless
        mc.markPieceSaved();
        const auto twice = state (mc);
        check (! twice.dirty && twice.revision == after.revision
               && twice.epoch == after.epoch,
               "saving the piece twice changes nothing");

        // a change after saving marks it unsaved again
        mc.markModelChanged();
        check (mc.pieceHasUnsavedChanges(),
               "a change after saving marks the piece unsaved again");
    }

    // --- the undo rule: the revision is what says a gesture changed something --
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const int depth = mc.undoDepth();

        // a gesture that changed nothing is not an undo step
        mc.beginEdit ("Nothing");
        mc.endEdit();
        check (mc.undoDepth() == depth,
               "an edit that changed nothing is not an undo step",
               juce::String (mc.undoDepth()) + " steps (was " + juce::String (depth) + ")");

        // a gesture that changed something is
        mc.beginEdit ("Something");
        mc.markModelChanged();
        mc.endEdit();
        check (mc.undoDepth() == depth + 1,
               "an edit that changed something is an undo step",
               juce::String (mc.undoDepth()) + " steps (was " + juce::String (depth) + ")");
        check (mc.undoLabel() == "Something",
               "the undo step knows what it was",
               mc.undoLabel());

        // a change that does not affect the piece still counts as a change
        const int depth2 = mc.undoDepth();
        mc.beginEdit ("The loop toggle");
        mc.markModelChanged (false);
        mc.endEdit();
        check (mc.undoDepth() == depth2 + 1,
               "a change that does not affect the piece is still an undo step",
               juce::String (mc.undoDepth()) + " steps (was " + juce::String (depth2) + ")");
    }

    // --- the undo is a model change --------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.beginEdit ("Something");
        mc.markModelChanged();
        mc.endEdit();

        const auto before = state (mc);
        mc.undo();
        const auto after = state (mc);
        check (after.revision > before.revision,
               "an undo advances the revision",
               juce::String (before.revision) + " -> " + juce::String (after.revision));
        check (after.epoch > before.epoch,
               "an undo advances the epoch",
               juce::String (before.epoch) + " -> " + juce::String (after.epoch));
        check (after.dirty,
               "an undo leaves the piece unsaved");

        const auto beforeRedo = state (mc);
        mc.redo();
        const auto afterRedo = state (mc);
        check (afterRedo.revision > beforeRedo.revision,
               "a redo advances the revision",
               juce::String (beforeRedo.revision) + " -> " + juce::String (afterRedo.revision));
        check (afterRedo.epoch > beforeRedo.epoch,
               "a redo advances the epoch",
               juce::String (beforeRedo.epoch) + " -> " + juce::String (afterRedo.epoch));
    }

    // --- two documents do not share a state ------------------------------------
    {
        auto firstHolder = std::make_unique<MainComponent> (true);
        auto secondHolder = std::make_unique<MainComponent> (true);
        auto& first = *firstHolder;
        auto& second = *secondHolder;

        first.markModelChanged();
        check (! second.pieceHasUnsavedChanges(),
               "two documents do not share the unsaved flag");
        check (second.modelRevisionForTest() == 0,
               "two documents do not share the revision",
               juce::String (second.modelRevisionForTest()));
        check (second.modelEpochForTest() == 0,
               "two documents do not share the epoch",
               juce::String (second.modelEpochForTest()));
    }
}
