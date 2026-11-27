#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R3.2: the undo history - the document's steps.
//
// The history is the document's memory: what the user can undo, what each
// step was called, how far back it goes, and what happens to the selection
// and to the transport when a step is applied. R3.2 moves it behind the
// document session (the review's R007), and this suite is its contract.
//
// The suite reads the history through the component's own doors (canUndo,
// undoLabel, undoDepth, ...), so the move does not change the checks.
//
// The components are made in plugin mode: the document does not depend on
// the window, and the plugin mode does not open an audio device.
//
// Two things about the document are worth knowing before reading the
// checks: a fresh app already has the demo page (five arcs), and the
// loader puts the selection on the first item of the bank it loads.

namespace
{
    // The arcs on the piece's first page.
    int arcCount (const juce::var& piece)
    {
        auto* pages = piece.getProperty ("pages", juce::var()).getArray();
        if (pages == nullptr || pages->isEmpty()) return 0;
        auto* arcs = (*pages)[0].getProperty ("arcs", juce::var()).getArray();
        return arcs != nullptr ? arcs->size() : 0;
    }

    // A piece with n arcs on its first page.
    juce::String pieceWithArcs (int n)
    {
        juce::String json = "{\"pages\":[{\"duration\":10,\"arcs\":[";
        for (int i = 0; i < n; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1)
                    + ",\"mode\":0,\"intensity\":0.8,"
                    + "\"points\":[{\"t\":0,\"f\":220},{\"t\":4,\"f\":440}]}";
        }
        json += "]}]}";
        return json;
    }

    // A piece with one wave (id 1) and no arcs.
    const juce::String oneWavePiece =
        "{\"waveforms\":[{\"id\":1,\"name\":\"One\",\"colour\":255,\"samples\":[0.0,0.5]}],"
        "\"pages\":[{\"duration\":10,\"arcs\":[]}]}";

    // A piece with two waves (ids 1 and 2) and no arcs.
    const juce::String twoWavePiece =
        "{\"waveforms\":[{\"id\":1,\"name\":\"One\",\"colour\":255,\"samples\":[0.0,0.5]},"
        "{\"id\":2,\"name\":\"Two\",\"colour\":255,\"samples\":[0.0,0.25]}],"
        "\"pages\":[{\"duration\":10,\"arcs\":[]}]}";

    // One edit: a transaction that loads a piece.
    void edit (MainComponent& mc, const juce::String& label, const juce::String& json)
    {
        mc.beginEdit (label);
        mc.loadFromVar (juce::JSON::parse (json));
        mc.endEdit();
    }

    // One edit that changes the document without replacing the piece: the
    // revision moves, so the transaction counts it as a change.
    void touch (MainComponent& mc, const juce::String& label)
    {
        mc.beginEdit (label);
        mc.markModelChanged();
        mc.endEdit();
    }
}

void upic::runUndoHistoryChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- a fresh document ----------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        check (! mc.canUndo(), "a fresh document has nothing to undo");
        check (! mc.canRedo(), "a fresh document has nothing to redo");
        check (mc.undoDepth() == 0, "a fresh document has no undo steps",
               juce::String (mc.undoDepth()));
        check (mc.redoDepth() == 0, "a fresh document has no redo steps",
               juce::String (mc.redoDepth()));
        check (mc.undoLabel().isEmpty(), "a fresh document's undo step has no name");
        check (mc.redoLabel().isEmpty(), "a fresh document's redo step has no name");
    }

    // --- one edit ------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        edit (mc, "The edit", pieceWithArcs (3));

        check (mc.canUndo(), "an edit is an undo step");
        check (mc.undoLabel() == "The edit",
               "the undo step knows what it was", mc.undoLabel());
        check (mc.undoDepth() == 1,
               "the undo stack has one step", juce::String (mc.undoDepth()));
        check (! mc.canRedo(), "the redo stack is empty after an edit");
        check (mc.redoDepth() == 0,
               "the redo stack has no steps", juce::String (mc.redoDepth()));
        check (mc.redoLabel().isEmpty(), "the redo step has no name after an edit");
    }

    // --- the undo ------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const int arcsBefore = arcCount (mc.pieceToVar());
        edit (mc, "The edit", pieceWithArcs (3));

        mc.undo();
        check (arcCount (mc.pieceToVar()) == arcsBefore,
               "the undo takes the edit back",
               juce::String (arcCount (mc.pieceToVar())) + " arcs (was "
               + juce::String (arcsBefore) + ")");
        check (! mc.canUndo(), "the undo empties the undo stack");
        check (mc.undoDepth() == 0,
               "the undo stack has no steps after the undo",
               juce::String (mc.undoDepth()));
        check (mc.canRedo(), "the undo fills the redo stack");
        check (mc.redoDepth() == 1,
               "the redo stack has one step after the undo",
               juce::String (mc.redoDepth()));
        check (mc.redoLabel() == "The edit",
               "the redo step knows what it was", mc.redoLabel());
    }

    // --- an undo with nothing to undo -----------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.undo();
        check (! mc.canUndo(), "an undo with nothing to undo does nothing");
        check (! mc.canRedo(), "an undo with nothing to undo fills no redo step");
        check (mc.undoDepth() == 0 && mc.redoDepth() == 0,
               "an undo with nothing to undo leaves both stacks empty");
    }

    // --- the redo ------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        edit (mc, "The edit", pieceWithArcs (3));
        mc.undo();
        mc.redo();
        check (arcCount (mc.pieceToVar()) == 3,
               "the redo takes the edit forward again",
               juce::String (arcCount (mc.pieceToVar())) + " arcs");
        check (! mc.canRedo(), "the redo empties the redo stack");
        check (mc.redoDepth() == 0,
               "the redo stack has no steps after the redo",
               juce::String (mc.redoDepth()));
        check (mc.canUndo(), "the redo fills the undo stack");
        check (mc.undoLabel() == "The edit",
               "the undo step knows what it was after the redo", mc.undoLabel());
    }

    // --- the new branch --------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        edit (mc, "The first edit", pieceWithArcs (3));
        mc.undo();
        check (mc.canRedo(), "the undo leaves a redo step behind");

        // a new edit after an undo starts a new branch: the redo is gone
        edit (mc, "The second edit", pieceWithArcs (4));
        check (! mc.canRedo(),
               "a new edit after an undo clears the redo stack");
        check (mc.redoDepth() == 0,
               "the redo stack is empty after a new edit",
               juce::String (mc.redoDepth()));
        check (mc.undoDepth() == 1,
               "the new edit is the only step on the undo stack",
               juce::String (mc.undoDepth()));
    }

    // --- two steps ------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const int arcsBefore = arcCount (mc.pieceToVar());
        edit (mc, "The first edit", pieceWithArcs (7));
        edit (mc, "The second edit", pieceWithArcs (9));

        check (mc.undoDepth() == 2,
               "two edits are two undo steps", juce::String (mc.undoDepth()));
        check (mc.undoLabel() == "The second edit",
               "the newest step is on top", mc.undoLabel());

        mc.undo();
        check (arcCount (mc.pieceToVar()) == 7,
               "the first undo goes back one step",
               juce::String (arcCount (mc.pieceToVar())) + " arcs");
        check (mc.undoLabel() == "The first edit",
               "the older step is on top after the first undo", mc.undoLabel());

        mc.undo();
        check (arcCount (mc.pieceToVar()) == arcsBefore,
               "the second undo goes back two steps",
               juce::String (arcCount (mc.pieceToVar())) + " arcs (was "
               + juce::String (arcsBefore) + ")");
        check (! mc.canUndo(), "the undo stack is empty after two undos");
        check (mc.redoDepth() == 2,
               "the redo stack has the two steps", juce::String (mc.redoDepth()));
    }

    // --- the open transaction --------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        edit (mc, "The first edit", pieceWithArcs (7));

        // a gesture that is still open when the undo comes: the undo closes
        // it first, and the gesture's step is the one that is undone
        mc.beginEdit ("The open gesture");
        mc.loadFromVar (juce::JSON::parse (pieceWithArcs (11)));
        mc.undo();
        check (arcCount (mc.pieceToVar()) == 7,
               "the undo closes the open gesture and undoes its step",
               juce::String (arcCount (mc.pieceToVar())) + " arcs");
        check (mc.undoDepth() == 1,
               "the closed gesture's step is the one that was undone",
               juce::String (mc.undoDepth()));
        check (mc.undoLabel() == "The first edit",
               "the step under the gesture is still there", mc.undoLabel());
    }
    {
        // an undo with nothing to undo leaves the open gesture open: the
        // gesture's step is pushed when the gesture ends, not before
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.beginEdit ("The open gesture");
        mc.markModelChanged();
        mc.undo();
        check (! mc.canUndo(), "an undo with nothing to undo pushes no step");
        mc.endEdit();
        check (mc.canUndo(),
               "the gesture that was left open still becomes a step when it ends");
        check (mc.undoLabel() == "The open gesture",
               "the step that was left open knows what it was", mc.undoLabel());
    }

    // --- the bounds -------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        // 55 edits: the stack keeps the newest 50, and the oldest steps go
        // first
        for (int i = 1; i <= 55; ++i)
            edit (mc, "Edit " + juce::String (i), pieceWithArcs (i + 10));

        check (mc.undoDepth() == 50,
               "the undo stack keeps at most 50 steps", juce::String (mc.undoDepth()));
        check (mc.undoLabel() == "Edit 55",
               "the newest step is on top after the bound", mc.undoLabel());

        // undo all 50: the model is the state of the fifth edit - the first
        // five steps went first
        for (int i = 0; i < 50; ++i) mc.undo();
        check (mc.undoDepth() == 0,
               "the undo stack is empty after 50 undos",
               juce::String (mc.undoDepth()));
        check (arcCount (mc.pieceToVar()) == 15,
               "the oldest steps went first",
               juce::String (arcCount (mc.pieceToVar())) + " arcs");
        check (mc.redoDepth() == 50,
               "the redo stack keeps at most 50 steps", juce::String (mc.redoDepth()));
    }

    // --- the memory bound ---------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        // the document is small: the piece is loaded before the first edit, so
        // every snapshot is the same small piece
        mc.loadFromVar (juce::JSON::parse (oneWavePiece));

        edit (mc, "Edit 1", oneWavePiece);
        const juce::int64 stepBytes = mc.undoStepBytesForTest();
        check (stepBytes > 0, "a step has a size", juce::String (stepBytes));

        // the budget holds two steps: the third one pushes the oldest out
        mc.setUndoLimitsForTest (50, stepBytes * 2);
        edit (mc, "Edit 2", oneWavePiece);
        edit (mc, "Edit 3", oneWavePiece);
        edit (mc, "Edit 4", oneWavePiece);
        check (mc.undoDepth() == 2,
               "the memory bound keeps only the steps that fit",
               juce::String (mc.undoDepth()) + " steps");
        check (mc.undoLabel() == "Edit 4",
               "the newest step is on top under the memory bound", mc.undoLabel());
        mc.undo();
        check (mc.undoLabel() == "Edit 3",
               "the memory bound drops the oldest steps first", mc.undoLabel());
    }
    {
        // a step that does not fit the budget at all is not kept
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.setUndoLimitsForTest (50, 0);
        edit (mc, "The edit", oneWavePiece);
        check (mc.undoDepth() == 0,
               "a step that does not fit the memory budget is not kept",
               juce::String (mc.undoDepth()) + " steps");
        check (! mc.canUndo(), "there is nothing to undo after such a step");
    }

    // --- the selection ------------------------------------------------------------
    {
        // the undo keeps the selection: the loader puts the selection on
        // the first wave of the piece it loads, and the undo puts it back
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.loadFromVar (juce::JSON::parse (twoWavePiece));
        mc.selectWaveformForTest (2);
        check (mc.selectedWaveformIdForTest() == 2,
               "the wave is selected", juce::String (mc.selectedWaveformIdForTest()));

        touch (mc, "The edit");
        mc.undo();
        check (mc.selectedWaveformIdForTest() == 2,
               "the undo keeps the selection when the material is still there",
               juce::String (mc.selectedWaveformIdForTest()));
    }
    {
        // the restored piece does not have the selected wave: the selection
        // falls back to the first wave
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.loadFromVar (juce::JSON::parse (oneWavePiece));
        edit (mc, "The edit", twoWavePiece);      // the bank now has two waves
        mc.selectWaveformForTest (2);
        mc.undo();                                // the one-wave piece comes back
        check (mc.selectedWaveformIdForTest() == 1,
               "the undo falls back to the first wave when the selected one is gone",
               juce::String (mc.selectedWaveformIdForTest()));
    }

    // --- the transport --------------------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.prepareToPlay (512, 44100.0);
        mc.startPlaybackForTest();
        const bool wasRunning = mc.transportStateForTest() != MainComponent::TransportState::stopped;
        check (wasRunning, "the transport is running before the undo");

        // the loader stops the transport when the piece is replaced, so the
        // undo's own restart is what this check pins
        touch (mc, "The edit");
        mc.undo();
        check (mc.transportStateForTest() != MainComponent::TransportState::stopped,
               "the undo does not silence the app");
    }
}
