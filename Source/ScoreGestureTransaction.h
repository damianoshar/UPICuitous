#pragma once

//==============================================================================
// R5.5 (the review's R047, section 25): the score canvas's gesture
// transactions.
//
// A gesture on the score canvas is one transaction: the pointer's path is
// temporary geometry in the editor, and the release produces one command - one
// labelled undo step. The review asks for that lifecycle to be explicit:
//
//   "A stroke should have an explicit lifecycle: during drawing, temporary
//    geometry should normally remain in the editor. On release, produce one
//    validated editing command." (section 25)
//
// This class is the lifecycle. It has three exits:
//
//   - commit(): the gesture produced a command. The transaction is closed,
//     and the document keeps it as one undo step (if the model really
//     changed);
//   - cancel(): the gesture produced nothing. The transaction is closed and
//     dropped - no undo step, and the next command is not swallowed;
//   - a gesture that was never opened.
//
// The class owns no model and no document: the doors are the canvas's hooks
// (the app's transaction), and the class only remembers whether a transaction
// is open and what it was called.

#include <JuceHeader.h>

#include <functional>
#include <utility>

namespace upic
{
    namespace score_gesture
    {
        class GestureTransaction
        {
        public:
            using Open  = std::function<void (const juce::String& label)>;
            using Close = std::function<void()>;   // the command is kept
            using Drop  = std::function<void()>;   // the command is dropped

            // The doors. The canvas sets them once, and they reach the app's
            // transaction when the gesture needs it.
            void setDoors (Open open, Close close, Drop drop)
            {
                open_ = std::move (open);
                close_ = std::move (close);
                drop_ = std::move (drop);
            }

            // Opens the transaction. A transaction that is still open is
            // closed first - a gesture that was never finished must not
            // swallow the next one (the document's dispatcher does the same).
            void begin (const juce::String& label)
            {
                if (isOpen()) commit();

                label_ = label;
                isOpen_ = true;
                if (open_) open_ (label);
            }

            // The gesture produced a command: the transaction is closed, and
            // the document keeps it (if the model really changed).
            void commit()
            {
                if (! isOpen()) return;

                isOpen_ = false;
                label_.clear();
                if (close_) close_();
            }

            // The gesture produced nothing: the transaction is closed and
            // dropped - no undo step, and the document's revision does not
            // move.
            void cancel()
            {
                if (! isOpen()) return;

                isOpen_ = false;
                label_.clear();
                if (drop_) drop_();
            }

            bool isOpen() const noexcept { return isOpen_; }
            const juce::String& label() const noexcept { return label_; }

        private:
            Open open_;
            Close close_;
            Drop drop_;
            juce::String label_;
            bool isOpen_ = false;
        };
    }
}
