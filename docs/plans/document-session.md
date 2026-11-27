# The document session

The architecture note for R3.1. It describes the boundary R3.1 introduced -
the document's state and the document's owner - as it is after that phase,
not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/DocumentSession.h   the document's own state: the revision, the epoch
                           and the unsaved flag - and the one owner of them.
        |
        |  the session owns the state; the shell asks it
        v
Source/MainComponent.h      the shell: it tells the session when the model
                            moved, and it reads the session when it needs to
                            know what the document is doing.
```

The review asked for one authoritative owner of the document (its R005,
"Extract DocumentSession"). The piece's *data* still lives where it lived -
in the component's members - and moving it is a much bigger job than a
phase. What R3.1 moves is the document's own *state*: the three counters
that say what the document is doing.

## What moved

The state used to be three members of the component, and ten places asked
about them.

| before | after |
|---|---|
| `MainComponent::modelEpoch` (`std::atomic<int>`) | `DocumentSession::epoch()` |
| `MainComponent::modelRevision` (`std::atomic<uint64_t>`) | `DocumentSession::revision()` |
| `MainComponent::pieceDirty` (`std::atomic<bool>`) | `DocumentSession::isDirty()` |
| `MainComponent::markModelChanged (bool affectsPiece)` | the same method, now a one-liner over `DocumentSession::markChanged (bool affectsPiece)` |
| `MainComponent::pieceHasUnsavedChanges()` | the same method, now `DocumentSession::isDirty()` |
| `MainComponent::markPieceSaved()` | the same method, now `DocumentSession::markSaved()` |

The ten read sites now ask the session instead of the component's own
members: the status timer (the revision), the snapshot publisher (the
revision), the audio thread (the epoch), the recovery snapshot (the
revision), the saver (the revision, twice), "make unique" (the revision,
twice) and the undo transaction (the revision, twice).

The component's methods did not change their names or their meaning: the
shell still says `markModelChanged()`, and the app still asks
`pieceHasUnsavedChanges()`. Only the owner of the state changed.

## Why the revision and the epoch are two counters

They move together - one model change moves both - but they answer
different questions and are read by different threads:

- **The revision** is the document's own counter. The status timer uses it
  to publish a snapshot only when there is something new to copy (the
  timer used to deep-copy the whole model 30 times a second, even while
  nothing changed), the recovery snapshot uses it to notice a change, the
  saver uses it to notice that the piece on the disk is already older than
  the one on the screen, "make unique" uses it to notice that its own work
  changed something, and the undo transaction uses it to notice that a
  gesture changed something at all.
- **The epoch** is the audio thread's signal. The audio thread asks "did
  the model move since I looked?", and it must not have to know about the
  revision's other users - and the revision's users must not have to know
  that the audio thread reads a counter of its own.

The suite pins that they count the same changes.

## The session needs no JUCE

The state the session owns is three counters: `std::atomic<uint64_t>` twice
and `std::atomic<bool>` once. The header asks for `<atomic>` and
`<cstdint>` and nothing else.

That will not last: the undo history (the review's R007) comes behind the
session, and it holds pieces (which hold JUCE values) and labels (which
are strings). The session will then be an application-level class, and the
boundary check will leave it out of its groups - as it already leaves the
codecs out.

## The suite

`Source/SelfTestDocumentSession.cpp` is the document session's own
contract: 30 checks. It pins:

- a fresh document: revision 0, epoch 0, no unsaved changes;
- one model change: the revision advances by one, the epoch advances by
  one, and the piece is marked unsaved;
- a change that does not affect the piece (the loop toggle): the counters
  still move - the model did move - but the piece is not marked unsaved,
  and an already-unsaved piece stays unsaved;
- saving the piece: the unsaved flag clears, and neither counter moves;
- saving twice: nothing changes;
- a change after saving marks the piece unsaved again;
- the undo rule the revision backs: a gesture that changed nothing is not
  an undo step, a gesture that changed something is, and a change that does
  not affect the piece still counts as a change;
- an undo and a redo are model changes: they advance the revision and the
  epoch, and they leave the piece unsaved;
- two documents do not share a state.

The suite came first: it was written against the counters where they were,
and it passes unchanged after the move. It reads the document's state
through one door (the `state` helper at the top of the file), so the move
touched the suite in one place and the checks themselves did not change.

The components in the suite are made in plugin mode: the document's state
does not depend on the window, and the plugin mode does not open an audio
device.

## What is not covered yet

- **The piece's data has not moved.** The component still holds the
  waveforms, the envelopes, the pages, the arcs, the channels, the sample
  slots, the tuning and the sieve as its own members. The session owns the
  document's *state*, not its *data*; moving the data is a much bigger job
  and it comes later in R3.
- **The undo history has not moved.** The stacks, the pending snapshot and
  the bounds (`kMaxUndoSteps`, `kMaxUndoBytes`) are still the component's
  own (the review's R007). The session's revision is what the transaction
  compares against, but the transaction itself is still in
  `MainComponentJobs.cpp`.
- **The selection has not moved.** The selected wave, envelope, amplitude
  table, sequence and sample slot are still the component's own (the
  review's R006).
- **There is no command dispatcher.** The model still changes through
  `markModelChanged()` at the end of a hundred call sites, not through a
  command that says what it did (the review's R008). One command = one
  labelled transaction = one revision is R3's exit criterion, and it is
  not reached yet.
- **The document's data still holds JUCE values** - that is the deferred
  type migration (the plan's section 2), not this wave.
