# Wave 15 - the lock-free model snapshot (design note)

**Status:** implemented (v1.18.0) - app selftest 210/210, plugin smoke 15/15,
pluginval strictness 5 SUCCESS.
**Kanso:** UPIC-37 (In progress -> done at the end of the wave).
**Why:** `juce-review` and `audio-dsp-review` (Sonic Skills) and APC's
`performance-001` all say the same thing about our `dataLock`:
`CriticalSection::tryEnter()` on the audio thread is not realtime-safe (the
`exit()` does a syscall to wake waiters), and holding a lock across the whole
render is what makes an editor "hang" in a host.

v1.15 already made the contention *inaudible* (a dropped block is faded
instead of cut). This wave removes the contention itself.

## Where we are

```
message thread                     audio thread
-------------                     ------------
timer (30 Hz)  --ScopedLock-->  ... the whole score render ...
UI handlers    --ScopedLock-->  ... under the same lock ...
                                   TryLock: if it fails -> the block is faded
```

* The audio thread holds `dataLock` for the **entire** render (every arc, the
  whole page) - so the message thread can be blocked for a whole render.
* The audio thread try-locks; when the message thread has it (the 30 Hz
  status timer does, and so does every UI action) the block is dropped and
  faded. Rare, but it is a dip in the score.
* `exit()` on a `CriticalSection` performs a syscall when a waiter exists.

## Target

The audio thread never touches a lock. It renders from an **immutable
snapshot** of the model that the message thread publishes; the snapshot is
handed over with atomic pointer exchanges and an intrusive refcount, so
neither side allocates on the audio thread.

## Architecture

### 1. The snapshot

```cpp
struct ModelSnapshot
{
    std::vector<upic::Waveform> waveforms;
    std::vector<upic::Envelope> envelopes;
    std::vector<upic::Page>     pages;     // the score
    std::vector<upic::SampleSlot> slots;   // shared audio, see below
    int currentPage = 0;
    int pageEpoch  = 0;   // bumped when the page changes (see 3)
};
```

Copying it must be cheap, so the sample slots' **audio buffers are shared**:
`SampleSlot::data` becomes `std::shared_ptr<const juce::AudioBuffer<float>>`.
A slot change replaces one pointer; copying the vector copies eight
pointers. The engine reads `smp.data->getSample(...)` - the buffer stays
alive as long as any snapshot refers to it.

### 2. The handover (no lock, no allocation on the audio thread)

```cpp
// message thread -> audio thread
std::atomic<ModelSnapshot*> pending { nullptr };
// audio thread -> message thread (so the audio thread never frees)
std::atomic<ModelSnapshot*> retired { nullptr };
```

* The message thread builds a snapshot (under `dataLock`, so it sees a
  consistent model) and does `pending.exchange(newSnapshot)`. If the old
  value comes back, the audio thread never took it - the message thread
  releases it.
* The audio thread does `pending.exchange(nullptr)` at the start of a block.
  Whatever it gets, it owns. It keeps it as `live` and pushes the previous
  one into `retired`.
* The message thread drains `retired` on the next publish and frees those
  snapshots - so the **deallocation happens on the message thread**, never on
  the audio thread.
* An intrusive refcount (`std::atomic<int> refs`) covers the hand-over
  window, so a snapshot that is in flight is never freed.

### 3. Who owns what

The audio thread keeps the state it owns (the playhead, the transport, its
own `currentPage`). The snapshot's `currentPage` is the *message* thread's
view; the audio thread compares `pageEpoch` with its own and switches pages
when it differs (and resets the engine, as today).

The transport flags (`isPlaying`, `wantsStop`, `masterGain`) stay as they
are: the audio thread owns the playhead, the message thread flips the
flags, and the gain is already an atomic.

### 4. Publishing

The status timer (30 Hz) rebuilds and publishes the snapshot while it holds
`dataLock` - the audio thread does not need the lock any more, so the
contention is gone. A 33 ms staleness is invisible (a dragged arc is heard
at most one frame late).

The engine's `render()` already takes the model as parameters, so the change
there is only the slot access (`smp.data->`).

## Complexity score: 4 (phased)

| Phase | What | Size |
|---|---|---|
| A | `SampleSlot::data` -> `std::shared_ptr<const juce::AudioBuffer<float>>` (model, engine, capture, load, selftest) | S |
| B | The snapshot + the handover (atomic exchange + refcount) | M |
| C | The audio thread renders from the snapshot; the lock leaves the audio path | M |
| D | The page epoch, the sequencer, the state save/load | S |
| E | The `dataLock` disappears from the audio callback entirely | S |

## Dependencies

* Phase A touches the model format helpers (`toVar`/`fromVar` keep the same
  JSON - only the C++ type changes) and the selftest's slot construction.
* Phase D touches the page sequencer, which currently advances `currentPage`
  from the audio thread.

## Risks

| Risk | Mitigation |
|---|---|
| A stale snapshot is played (the publish is missed) | The timer publishes unconditionally every 33 ms - there is no "publish on change" path that can be forgotten |
| A use-after-free in the hand-over | The intrusive refcount plus the `retired` list: only the message thread frees, and only what the audio thread has released |
| The page-advance logic breaks (it runs on the audio thread) | The page epoch makes the hand-over explicit; the existing page/sequencer tests stay |
| The score becomes 33 ms stale while dragging | Imperceptible; if it shows, the timer can publish on every editor change instead (a flag) |
| An allocation sneaks into the audio path | The snapshot is built on the message thread; the audio thread only exchanges pointers |

## Test plan

* The selftest keeps its 194 checks - the model change must not move any of
  them.
* New: the snapshot handover is exercised directly (publish -> take ->
  retire -> free) and the render is proven to be identical to the old
  lock-based render for the same model (bit-exact comparison).
* New: the page epoch switches the audio thread's page.
* The plugin smoke test (15 checks) and pluginval (strictness 5) must stay
  green - they are the host-level gate.

## Note

The audio path must not be rushed. v1.15 already removed the *symptom*
(the clicks); this wave removes the *cause* (the lock). If it turns out to
be too risky for one pass, stop after phase B and keep the lock - the
snapshot is then a read-only mirror with no behaviour change.

## What was built (v1.18.0)

All five phases landed. The audio thread never takes a lock, and it never
allocates or frees.

### The files

| File | What changed |
|---|---|
| `Source/ModelSnapshot.h` | **new** - `SnapshotBase` + `SnapshotHandover<T>` + `ModelSnapshot` |
| `Source/UpicModel.h` | `SampleSlot::data` is now `std::shared_ptr<const juce::AudioBuffer<float>>` (same JSON) |
| `Source/UpicEngine.h` | the engine reads `smp.data->...` |
| `Source/WaveformEditor.h` | the material preview reads the shared buffer |
| `Source/ScoreEditor.h` | the playhead comes from a getter (the audio thread owns it) + a seek hook |
| `Source/MainComponent.{h,cpp}` | the snapshot publish, the audio path, the transport signals |
| `Source/Main.cpp` | the selftest: handover, bit-exact render, lock-free render, page switch |
| `scripts/build.ps1` | the app version bump was broken (the regex missed the `}`); fixed and it warns now |

### The handover (phase B)

The design sketched a `pending` / `retired` Treiber stack. That shape has an
ABA hazard: the writer frees the node the reader may still hold as the stack
head, and the reader stores that pointer into the next node. The shipped
version keeps the same *ownership* rules but drops the stack:

* every published snapshot carries **two** references - the writer's and the
  reader's (`std::atomic<int> refs`);
* the writer owns every snapshot it published (a small `owned` vector) and
  frees a snapshot as soon as the reader has dropped its reference
  (`refs == 1`);
* the reader (`take()`) only ever *drops* a reference - it never frees and
  never allocates;
* a snapshot the reader never took is freed by the writer right away (the
  exchange hands it back), so a stopped audio thread cannot leak snapshots.

Result: all deallocation happens on the message thread, and there is no
pointer-chasing between the two threads at all.

### The audio path (phases C, E)

```
message thread                          audio thread
-------------                          ------------
timer (30 Hz) --publish(snapshot)-->    pending.exchange(nullptr) -> live
UI edit    --publish(snapshot)-->       render from *live (no lock, no alloc)
              (page change, load,        page epoch -> adopt the new page
               capture, sample change)
```

* `dataLock` is now a **message-thread** lock only: it guards the model
  against the *timer's copy* and the editors against each other. The audio
  callback does not mention it.
* The audio thread owns the playhead (`audioPlayhead`) and publishes it for
  the UI (`playheadPublished`); the message thread asks for a move with
  `seekSerial` / `seekTo` (the score editor's scrub does that).
* The page is chosen with a **page epoch**: the message thread bumps it when
  the page changes, the audio thread adopts `snapshot.currentPage` when it
  sees a new epoch and resets its voices.
* The engine is **audio-thread only** now. The message thread never calls
  `engine.reset()` (it would race with the render); it asks for a reset with
  `engineResetSerial` (Play) or bumps `modelEpoch` (a model edit, which
  fades across the change). `renderWav` got its own local engine.

### Two bugs the new path exposed (both fixed)

1. **The fade ramp was not clamped.** `scoreGain + (1 - scoreGain) * i / 96`
   kept rising past 1.0 for blocks longer than 96 samples - a 2048-sample
   block faded *in* to 21x. The selftest caught it (peak 8.1 instead of
   0.39). Both ramps are now clamped to `[0, 1]`.
2. **The page switch was silent.** The page epoch tells the audio thread
   *that* the page changed, but the new page only arrives with the next
   snapshot - and the timer is 33 ms away (in a host, never). The page
   handlers now publish the snapshot immediately, like a load or a capture
   does.

### The new selftest checks (197 -> 210)

* the handover: publish -> take -> retire -> free, with an instance counter
  that proves the reader frees nothing and the writer frees everything;
* the snapshot render is **bit-identical** to the live model render;
* the realtime path renders from the snapshot with **no timer tick**
  (the plugin-style `processBlock` loop) and the peak stays in range;
* the page switch: the audio thread adopts the new page, and the new page's
  frequency is what is sounding (880 Hz measured on a 2048-sample block).

### Still open

* The score can be up to 33 ms stale while dragging (the timer publishes).
  If it shows in the DAW, the editors can publish on every change instead.
* `docs/waves/` has a note for this wave only - the other waves could use
  one each, following the same template.
* The reverse / MIDI slice is a **future idea**, not the next wave: it stays
  flagged in the Kanso Future Ideas columns (S / M / L) and in
  `FUTURE_IDEAS.md` until it is pulled forward on purpose.
