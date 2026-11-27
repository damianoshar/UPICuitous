#pragma once

//==============================================================================
// R3.1/R3.2: the document session - the one owner of the document's own
// state and of its history.
//
// The piece's data still lives where it lived (the component's members):
// moving it is a bigger job, and the review's point is not that the data
// moves but that ONE place says what the document is doing.
//
// What lives here is the document's own state:
//
//   - the revision: it moves when the model moves. The status timer uses it
//     to publish a snapshot only when there is something new to copy, the
//     recovery snapshot uses it to notice a change, and "make unique" uses
//     it to notice that its own work changed something.
//   - the epoch: the audio thread's "the model moved" signal. It moves with
//     the revision, but it is a separate counter because the audio thread
//     asks a different question ("did it move since I looked?") and must
//     not have to know about the revision's other users.
//   - the unsaved flag: the piece differs from what is on the disk. The
//     window asks before it closes, and the recovery snapshot is written
//     only when there is something to recover.
//
// and - since R3.2 - the document's history: the steps the user can undo,
// what each step was called, and the transaction that is open right now.
//
// The history asks the document for the state to snapshot and for the state
// to put back, through the two doors the component installs. It does not
// know how a piece is captured or loaded - that is the shell's business.
//
// and - since R3.3 - the document's selection: what is selected (the review's
// R006). The selection is plain data (the ids the piece's banks use), so it
// lives in its own class and the session owns it.
//
// The command dispatcher (R008) comes behind this class; so does one command
// = one transaction = one revision.
//
// The class needs JUCE now: the steps are labelled with strings and the
// snapshots are pieces. It is an application-level class, not a part of the
// JUCE-free groups.
#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "Piece.h"
#include "SelectionModel.h"

namespace upic
{
    class DocumentSession
    {
    public:
        // how the history takes and gives back the document's state
        using Capture = std::function<PieceModel()>;
        using Restore = std::function<bool (const PieceModel&)>;

        // the bounds of the history: the number of steps it keeps and the
        // memory its snapshots may hold (the oldest steps go first).
        struct Limits
        {
            int steps = 50;
            juce::int64 bytes = 64ll * 1024 * 1024;
        };

        DocumentSession() = default;

        // The session is not copyable: the counters and the steps are the
        // document's, and two documents must never share them.
        DocumentSession (const DocumentSession&) = delete;
        DocumentSession& operator= (const DocumentSession&) = delete;

        // The two doors. The component installs them when it is built: the
        // history cannot take a snapshot without them.
        void setDocumentSource (Capture capture, Restore restore)
        {
            capture_ = std::move (capture);
            restore_ = std::move (restore);
        }

        // The bounds of the history. The production code never changes them.
        void setLimits (const Limits& limits) { limits_ = limits; }
        const Limits& limits() const { return limits_; }

        // --- the revision: it moves when the model moves ---
        std::uint64_t revision() const noexcept
        {
            return revision_.load (std::memory_order_relaxed);
        }

        // --- the epoch: the audio thread's "the model moved" signal ---
        std::uint64_t epoch() const noexcept
        {
            return epoch_.load (std::memory_order_relaxed);
        }

        // --- the unsaved flag ---
        bool isDirty() const noexcept
        {
            return dirty_.load (std::memory_order_relaxed);
        }

        void markSaved() noexcept
        {
            dirty_.store (false, std::memory_order_relaxed);
        }

        // --- one model change ---
        //
        // `affectsPiece` is false for the changes that are not part of the
        // piece (the loop toggle), so they do not make the app ask to
        // save. They still move the counters: the model did move.
        void markChanged (bool affectsPiece = true) noexcept
        {
            epoch_.fetch_add (1, std::memory_order_relaxed);
            revision_.fetch_add (1, std::memory_order_relaxed);
            if (affectsPiece)
                dirty_.store (true, std::memory_order_relaxed);
        }

        // =====================================================================
        // The history (R3.2)
        // =====================================================================

        // What an undo or a redo did.
        enum class StepResult
        {
            nothing,   // there was no step to apply
            applied,   // the step was applied
            failed     // the step could not be put back
        };

        // --- the open transaction ---
        //
        // A transaction is a gesture: it opens before the change and closes
        // after it, and it becomes one step. A gesture that was never
        // finished (a mouse-up that was missed) must not block the next one,
        // so opening a transaction closes the one that is still open.
        void beginEdit (const juce::String& label)
        {
            if (! capture_) return;

            // A transaction that is still running is closed first: a gesture
            // that was never finished (a mouse-up that was missed) must not
            // block the next one.
            if (pendingEdit_ != nullptr)
                endEdit();

            // the state from before the change
            pendingEdit_ = std::make_unique<upic::PieceModel> (capture_());
            pendingRevision_ = revision();
            pendingLabel_ = label;
        }

        // The gesture is over. A gesture that changed nothing is not a step.
        void endEdit()
        {
            if (pendingEdit_ == nullptr) return;

            // nothing moved? then the snapshot is not worth keeping (a click
            // that changed nothing is not an undo step).
            if (revision() == pendingRevision_)
            {
                pendingEdit_.reset();
                return;
            }

            Entry e;
            e.model = std::move (*pendingEdit_);
            e.label = pendingLabel_;
            e.bytes = estimateModelBytes (e.model);
            pendingEdit_.reset();

            undoStack_.push_back (std::move (e));
            undoBytes_ += undoStack_.back().bytes;

            // a new edit after an undo starts a new branch: the redo stack is gone
            redoStack_.clear();
            redoBytes_ = 0;

            // the bounds: the oldest steps go first
            while ((int) undoStack_.size() > limits_.steps || undoBytes_ > limits_.bytes)
            {
                undoBytes_ -= undoStack_.front().bytes;
                undoStack_.erase (undoStack_.begin());
            }
        }

        // Is a transaction open right now?
        bool hasOpenEdit() const noexcept
        {
            return pendingEdit_ != nullptr;
        }

        // --- the steps ---
        bool canUndo() const noexcept { return ! undoStack_.empty(); }
        bool canRedo() const noexcept { return ! redoStack_.empty(); }
        int undoDepth() const noexcept { return (int) undoStack_.size(); }
        int redoDepth() const noexcept { return (int) redoStack_.size(); }

        juce::String undoLabel() const
        {
            return undoStack_.empty() ? juce::String() : undoStack_.back().label;
        }

        juce::String redoLabel() const
        {
            return redoStack_.empty() ? juce::String() : redoStack_.back().label;
        }

        // The size of the newest step's snapshot (the audio is shared, so the
        // snapshot costs only the pages).
        juce::int64 undoStepBytes() const
        {
            return undoStack_.empty() ? 0 : undoStack_.back().bytes;
        }

        // The undo. The label of the step that was applied comes back with
        // it: the shell says "Undo: <label>" in the status line.
        StepResult undo (juce::String& label)
        {
            if (undoStack_.empty()) return StepResult::nothing;
            if (! capture_ || ! restore_) return StepResult::nothing;

            // An open gesture (a slider drag) owns a transaction. Closing it
            // first keeps the undo stack honest: the gesture's step is
            // closed and undone like any other.
            endEdit();
            if (undoStack_.empty()) return StepResult::nothing;

            // the state we are leaving goes onto the redo stack
            Entry current;
            current.model = capture_();
            current.label = undoLabel();
            current.bytes = estimateModelBytes (current.model);
            redoStack_.push_back (std::move (current));
            redoBytes_ += redoStack_.back().bytes;
            while ((int) redoStack_.size() > limits_.steps || redoBytes_ > limits_.bytes)
            {
                redoBytes_ -= redoStack_.front().bytes;
                redoStack_.erase (redoStack_.begin());
            }

            Entry entry = std::move (undoStack_.back());
            undoStack_.pop_back();
            undoBytes_ -= entry.bytes;

            label = entry.label;

            // the piece is replaced through the normal loader, so the same
            // checks and the same limits apply as to a loaded piece
            if (! restore_ (entry.model))
                return StepResult::failed;

            return StepResult::applied;
        }

        // The redo. The label of the step that was applied comes back with
        // it: the shell says "Redo: <label>" in the status line.
        StepResult redo (juce::String& label)
        {
            if (redoStack_.empty()) return StepResult::nothing;
            if (! capture_ || ! restore_) return StepResult::nothing;

            // an open gesture transaction is closed first (see undo())
            endEdit();
            if (redoStack_.empty()) return StepResult::nothing;

            Entry current;
            current.model = capture_();
            current.label = redoLabel();
            current.bytes = estimateModelBytes (current.model);
            undoStack_.push_back (std::move (current));
            undoBytes_ += undoStack_.back().bytes;
            while ((int) undoStack_.size() > limits_.steps || undoBytes_ > limits_.bytes)
            {
                undoBytes_ -= undoStack_.front().bytes;
                undoStack_.erase (undoStack_.begin());
            }

            Entry entry = std::move (redoStack_.back());
            redoStack_.pop_back();
            redoBytes_ -= entry.bytes;

            label = entry.label;

            // the piece is replaced through the normal loader, so the same
            // checks and the same limits apply as to a loaded piece
            if (! restore_ (entry.model))
                return StepResult::failed;

            return StepResult::applied;
        }

        // =====================================================================
        // The selection (R3.3)
        // =====================================================================

        // What is selected: the wavetable, the envelope, the amplitude
        // table, the performance sequence, the group, the mixer channel,
        // the sample slot and the arc. The session owns it - the selection
        // is part of the document's state, not of the shell - and the shell
        // asks it.
        SelectionModel& selection() noexcept { return selection_; }
        const SelectionModel& selection() const noexcept { return selection_; }

    private:
        // One step: the state from before a change, and what the change was
        // called.
        struct Entry
        {
            upic::PieceModel model;
            juce::int64 bytes = 0;      // the size of the snapshot (the audio is shared)
            juce::String label;
        };

        // The size of a piece, in the words the memory bound counts.
        static juce::int64 estimateModelBytes (const upic::PieceModel& m)
        {
            juce::int64 bytes = 0;
            for (const auto& p : m.pages)
            {
                bytes += (juce::int64) p.arcs.size() * 64;
                for (const auto& a : p.arcs)
                    bytes += (juce::int64) a.points.size() * (juce::int64) sizeof (upic::ArcPoint);
                bytes += (juce::int64) p.zones.size() * 64;
            }
            for (const auto& w : m.waveforms)
                bytes += (juce::int64) w.samples.size() * (juce::int64) sizeof (float);
            for (const auto& e : m.envelopes)
                bytes += (juce::int64) e.points.size() * (juce::int64) sizeof (upic::EnvPoint);
            return bytes;
        }

        Capture capture_;
        Restore restore_;
        Limits limits_;

        std::vector<Entry> undoStack_, redoStack_;
        juce::int64 undoBytes_ = 0, redoBytes_ = 0;

        // the snapshot of the edit that is in progress (empty when there is none)
        std::unique_ptr<upic::PieceModel> pendingEdit_;
        std::uint64_t pendingRevision_ = 0;
        juce::String pendingLabel_;

        // what is selected (R3.3)
        SelectionModel selection_;

        std::atomic<std::uint64_t> epoch_ { 0 };
        std::atomic<std::uint64_t> revision_ { 0 };
        std::atomic<bool> dirty_ { false };
    };
}
