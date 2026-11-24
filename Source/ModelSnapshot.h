#pragma once
#include "UpicModel.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
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
// WP7: the sample extraction preview's transport request.
//
// The request is part of the immutable snapshot, not a collection of
// independent atomics. The message thread starts a new generation and
// publishes the finished material together with it; the audio thread adopts
// the generation as one unit and owns the playhead and remaining count from
// then on. An old audio block can therefore never write its transport state
// over a newer preview.
//==============================================================================
enum class SampleExtractRoute : uint8_t
{
    none = 0,
    original,
    extracted
};

struct SampleExtractPreviewState
{
    uint64_t generation = 0;
    SampleExtractRoute route = SampleExtractRoute::none;
    int durationFrames = 0;
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
    std::vector<upic::AmplitudeTable> amplitudeTables;  // WP2: the conversion bank
    std::vector<upic::PerformanceSequence> sequences;   // WP4
    std::vector<upic::Channel> channels;                 // WP6 (model only)
    // WP6.4: the RESOLVED mixer - the channels with the gains, the
    // mute/solo flags and the gain curve already sampled into a 64-point
    // LUT. It is built on the message thread (here) and read by the audio
    // thread, so the render path never walks a curve's point list.
    std::vector<upic::ChannelMix> channelMix;
    std::vector<upic::Page>       pages;
    std::vector<upic::SampleSlot> slots;
    // WP7: the sample extraction panel's A/B preview. It is an isolated
    // one-arc page (the real pages are untouched), and its material lives
    // in a separate slot vector. The score and freeze paths always read
    // `slots`; only the dedicated extraction preview reads these slots.
    std::shared_ptr<const upic::Page> samplePreviewPage;
    std::vector<upic::SampleSlot> samplePreviewSlots;
    // The extracted side is an oscillator preview of the converted
    // wavetable, kept outside the piece's waveform bank. It is copied
    // into the snapshot so the audio thread never sees a mutable panel.
    std::vector<upic::Waveform> samplePreviewWaves;
    // The extraction preview uses its own flat envelope bank. It must not
    // inherit an edited or deleted envelope from the piece's global bank.
    std::vector<upic::Envelope> samplePreviewEnvelopes;
    SampleExtractPreviewState sampleExtractPreview;

    // The extracted route renders from its isolated wavetable bank. The
    // original route has no oscillator arc, so the piece's bank is harmless
    // there. Keeping the choice with the snapshot prevents the audio path
    // from accidentally reading a same-id waveform from the piece bank.
    const std::vector<upic::Waveform>& wavesForSampleExtractRoute() const
    {
        return sampleExtractPreview.route == SampleExtractRoute::extracted
                 ? samplePreviewWaves : waveforms;
    }

    int  currentPage = 0;   // the message thread's view of the page
    int  pageEpoch   = 0;   // bumped when the page changes -> the reader adopts it
    bool seqPages    = false;
    bool loopPage    = true;
    // v1.33.4 (M22): the model revision this snapshot was built from - the
    // status timer publishes only when the revision moved.
    uint64_t revision = 0;

    // WP1: the drawn frequency tables compiled to render-time LUTs. Only the
    // spaces that have a curve get one (the presets stay analytic), so the
    // cost is paid by the pieces that use the feature. The LUTs are owned by
    // shared_ptrs so their addresses stay stable while the snapshot lives;
    // the audio thread only ever reads the raw pointers.
    std::vector<upic::PageLutSet> pageLuts;
    std::vector<std::shared_ptr<const upic::FreqLut>> ownedLuts;

    // WP3: the groups resolved to one override per arc per page (solo is
    // already applied). Parallel to `pages[i].arcs`.
    std::vector<std::vector<upic::ArcRenderOverride>> arcOverrides;

    const upic::PageLutSet* lutsFor (int page) const
    {
        if (page < 0 || page >= (int) pageLuts.size()) return nullptr;
        return &pageLuts[(size_t) page];
    }

    const std::vector<upic::ArcRenderOverride>* overridesFor (int page) const
    {
        if (page < 0 || page >= (int) arcOverrides.size()) return nullptr;
        return &arcOverrides[(size_t) page];
    }
};
