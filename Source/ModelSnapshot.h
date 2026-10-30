#pragma once
#include "UpicModel.h"
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

//==============================================================================
// Wave 15 (v1.18): the lock-free model handover.
//
// The audio thread must never take a lock: CriticalSection::exit() can issue
// a syscall to wake a waiter, and holding the lock across a whole score render
// is what makes an editor hang in a host. Instead the message thread builds
// an immutable ModelSnapshot and publishes it; the audio thread renders from
// the snapshot it last took.
//
// One writer (message thread), one reader (audio thread):
//
//   publish(s)  the writer hands s over (one atomic exchange) and keeps
//               ownership. The reader picks it up with take().
//   take(live)  the reader swaps the pending snapshot in. It never allocates
//               and never frees: it only drops its reference to the snapshot
//               it replaces.
//   refs        every published snapshot carries two references: one for the
//               writer (which owns it) and one for the reader. The writer
//               frees a snapshot once the reader is done with it, so all
//               deletion happens on the message thread.
//
// A snapshot the reader never took is freed by the writer immediately (the
// exchange hands it back), so a stopped audio thread cannot leak snapshots.
//==============================================================================

struct SnapshotBase
{
    // A published snapshot carries two references: the writer's and the
    // reader's. The writer frees it when only its own reference is left.
    std::atomic<int> refs { 2 };
};

template <typename T>
class SnapshotHandover
{
    static_assert(std::is_base_of_v<SnapshotBase, T>,
                  "T must derive from SnapshotBase");

public:
    SnapshotHandover() = default;
    ~SnapshotHandover() { clear(); }

    SnapshotHandover(const SnapshotHandover&) = delete;
    SnapshotHandover& operator= (const SnapshotHandover&) = delete;

    // --- writer (message thread) ---
    // Publishes a freshly built snapshot and takes ownership of it.
    void publish(T* s)
    {
        s->refs.store(2, std::memory_order_relaxed);

        T* old = pending.exchange(s, std::memory_order_acq_rel);
        if (old != nullptr)
        {
            // the reader never took it: drop both references and free it here
            eraseOwned(old);
            if (old->refs.fetch_sub(2, std::memory_order_acq_rel) == 2)
                delete old;
        }

        owned.push_back(s);
        drain();
    }

    // Frees the snapshots the reader is done with. Message thread only.
    void drain()
    {
        for (size_t i = 0; i < owned.size(); )
        {
            T* s = owned[i];
            if (s->refs.load(std::memory_order_acquire) == 1)
            {
                owned.erase(owned.begin() + (ptrdiff_t) i);
                if (s->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    delete s;
            }
            else
            {
                ++i;
            }
        }
    }

    // --- reader (audio thread): never allocates, never frees ---
    // Returns the newest snapshot as `live`, or the previous one when nothing
    // new was published.
    T* take(T*& live)
    {
        T* s = pending.exchange(nullptr, std::memory_order_acquire);
        if (s != nullptr)
        {
            T* old = live;
            live = s; // the reader stops pointing at the old one ...
            if (old != nullptr)
            {
                // ... and only then drops its reference - the writer may free
                // it from this moment on
                old->refs.fetch_sub(1, std::memory_order_release);
            }
        }
        return live;
    }

    // Hands the live snapshot back (shutdown). Message thread only, and only
    // when the audio thread has stopped.
    void retireReader(T*& live)
    {
        if (live != nullptr)
        {
            live->refs.fetch_sub(1, std::memory_order_acq_rel);
            live = nullptr;
        }
    }

    // --- writer: drops whatever the handover still owns ---
    void clear()
    {
        T* p = pending.exchange(nullptr, std::memory_order_acq_rel);
        if (p != nullptr)
        {
            eraseOwned(p);
            if (p->refs.fetch_sub(2, std::memory_order_acq_rel) == 2)
                delete p;
        }
        for (auto* s : owned)
        {
            if (s->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete s;
        }
        owned.clear();
    }

private:
    void eraseOwned(const T* s)
    {
        for (size_t i = 0; i < owned.size(); ++i)
            if (owned[i] == s)
            {
                owned.erase(owned.begin() + (ptrdiff_t) i);
                return;
            }
    }

    std::atomic<T*> pending { nullptr };
    std::vector<T*> owned;   // writer side only
};

//==============================================================================
// The immutable view of the piece the audio thread renders from. It is built
// on the message thread (a 30 Hz status-timer tick, or right after a change
// that must be audible immediately: a load, a captured take). Sample audio is
// shared, so copying the slots is copying eight pointers.
//==============================================================================
struct ModelSnapshot : SnapshotBase
{
    std::vector<upic::Waveform>   waveforms;
    std::vector<upic::Envelope>   envelopes;
    std::vector<upic::Page>       pages;
    std::vector<upic::SampleSlot> slots;
    int  currentPage = 0;   // the message thread's view of the page
    int  pageEpoch   = 0;   // bumped when the page changes -> the reader adopts it
    bool seqPages    = false;
    bool loopPage    = true;
};
