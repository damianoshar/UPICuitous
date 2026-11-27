#pragma once

//==============================================================================
// R3.4: the command dispatcher - the document's mutation entry point.
//
// A command is one edit: it has a label (what the undo menu calls it), it
// changes the document, and the document's revision advances once when the
// command ends. The dispatcher ties the three together: the transaction
// (the undo step), the change report (the epoch, the revision and the
// unsaved flag) and the mutation.
//
// A gesture is a command that stays open while the user drags. The changes
// inside it move the epoch (the audio follows the drag) and the command's
// end moves the revision once (the document changed once) and creates the
// undo step.
//
// A change outside a command is a change of its own: there is no
// transaction to put it in, so the revision moves straight away.
//
// The dispatcher is the shell's way into the document: it is the only place
// that opens a transaction or reports a change. The session owns it and
// installs the doors it needs: the capture (the state from before the
// command), the commit (the undo step) and the two halves of the change
// report (the epoch and the revision).
//
// The class needs JUCE: the label is a string and the snapshot it holds is
// a piece. It is an application-level class, like the session that owns it.
#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "Piece.h"

namespace upic
{
    class CommandDispatcher
    {
    public:
        // The doors the session installs.
        //
        // - the capture: the state from before the command (null when the
        //   document cannot be captured - then there is no transaction and
        //   no command);
        // - the commit: the undo step, with the label the command was
        //   called;
        // - the model moved: the epoch (the audio follows the change now)
        //   and the unsaved flag;
        // - the document changed: the revision (the document changed once).
        using Capture = std::function<std::unique_ptr<PieceModel>()>;
        using Commit = std::function<void (PieceModel&& model, const juce::String& label)>;
        using ModelMoved = std::function<void (bool affectsPiece)>;
        using DocumentChanged = std::function<void()>;

        void install (Capture capture, Commit commit,
                      ModelMoved moved, DocumentChanged changed)
        {
            capture_ = std::move (capture);
            commit_ = std::move (commit);
            modelMoved_ = std::move (moved);
            documentChanged_ = std::move (changed);
        }

        // --- the command ---

        // Open a command. A command that is still open is closed first: a
        // gesture that was never finished must not swallow the next one.
        void beginCommand (const juce::String& label)
        {
            if (open_) endCommand();

            // the state from before the change
            pending_ = capture_ ? capture_() : nullptr;
            if (! pending_) return;   // nothing to snapshot: no command

            label_ = label;
            open_ = true;
            changed_ = false;
        }

        // Close the command: the document changed once (if the command
        // changed something), and the history keeps the step.
        void endCommand()
        {
            if (! open_) return;

            const bool changed = changed_;

            // the step goes to the history
            if (changed && commit_)
                commit_ (std::move (*pending_), label_);
            pending_.reset();

            open_ = false;
            changed_ = false;
            label_.clear();

            // the document changed once
            if (changed && documentChanged_)
                documentChanged_();
        }

        // Is a command open right now?
        bool isCommandOpen() const noexcept { return open_; }

        // Did the open command change anything? A command that changed
        // nothing is not an undo step.
        bool commandChanged() const noexcept { return changed_; }

        // What the open command is called.
        const juce::String& commandLabel() const noexcept { return label_; }

        // --- the change report ---

        // "The model moved." Inside a command the epoch moves now (the
        // audio follows the gesture) and the revision moves when the
        // command ends. Outside a command the change is a change of its
        // own: the revision moves straight away.
        void reportChange (bool affectsPiece = true)
        {
            // the model moved: the audio follows it now, and the piece is
            // marked unsaved if the change is part of it
            if (modelMoved_) modelMoved_ (affectsPiece);

            if (open_)
            {
                // inside a command: the revision waits for the command's end
                changed_ = true;
                return;
            }

            // outside a command: the change is a change of its own
            if (documentChanged_) documentChanged_();
        }

    private:
        Capture capture_;
        Commit commit_;
        ModelMoved modelMoved_;
        DocumentChanged documentChanged_;

        // the state from before the command (the undo step's snapshot)
        std::unique_ptr<PieceModel> pending_;

        juce::String label_;
        bool open_ = false;
        bool changed_ = false;
    };
}
