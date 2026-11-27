#pragma once

//==============================================================================
// R3.1: the document session - the one owner of the document's own state.
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
// The undo history (the review's R007) and the selection (R006) come
// behind this class; so does the command dispatcher (R008) - one command,
// one transaction, one revision.
//
// The class needs no JUCE: the state it owns is three counters. The undo
// history will bring JUCE with it (the labels are strings and the snapshots
// are pieces), and the session will be an application-level class then.
#include <atomic>
#include <cstdint>

namespace upic
{
    class DocumentSession
    {
    public:
        DocumentSession() = default;

        // The session is not copyable: the counters are the document's, and
        // two documents must never share them.
        DocumentSession (const DocumentSession&) = delete;
        DocumentSession& operator= (const DocumentSession&) = delete;

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

    private:
        std::atomic<std::uint64_t> epoch_ { 0 };
        std::atomic<std::uint64_t> revision_ { 0 };
        std::atomic<bool> dirty_ { false };
    };
}
