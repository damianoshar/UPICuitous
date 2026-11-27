# The document session

The architecture note for R3. It describes the boundary R3 introduced - the
document's state, the document's history and the document's selection - as
it is after R3.3, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/DocumentSession.h   the document's own state: the revision, the epoch
                           and the unsaved flag - the document's history:
                           the steps, their labels, the open transaction and
                           the two bounds - and the document's selection.
                           It is the one owner of them.
        |
        |  the session owns the state, the history and the selection; the
        |  shell asks it
        v
Source/SelectionModel.h    what is selected: the wavetable, the envelope,
                           the amplitude table, the sequence, the group, the
                           mixer channel, the sample slot and the arc.
        |
        |  the session owns it; the shell asks it
        v
Source/MainComponent.h      the shell: it tells the session when the model
                            moved, it reads the session when it needs to know
                            what the document is doing, and it keeps the
                            shell's own work: the transport and the messages.
```

The review asked for one authoritative owner of the document (its R005,
"Extract DocumentSession"). The piece's *data* still lives where it lived -
in the component's members - and moving it is a much bigger job than a
phase. What R3 moves is the document's own *state* (R3.1: the three
counters that say what the document is doing), the document's own *history*
(R3.2: the steps the user can undo) and the document's own *selection*
(R3.3: what the user is working on).

## What moved

### R3.1 - the state

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

### R3.2 - the history

The history used to be the component's own: two vectors of snapshots, the
pending snapshot of the open transaction and two bounds.

| before | after |
|---|---|
| `MainComponent::UndoEntry` | `DocumentSession::Entry` |
| `MainComponent::undoStack`, `redoStack` | `DocumentSession::undoStack_`, `redoStack_` |
| `MainComponent::undoBytes`, `redoBytes` | `DocumentSession::undoBytes_`, `redoBytes_` |
| `MainComponent::pendingEdit` | `DocumentSession::pendingEdit_` |
| `MainComponent::pendingRevision` | `DocumentSession::pendingRevision_` |
| `MainComponent::pendingLabel` | `DocumentSession::pendingLabel_` |
| `MainComponent::kMaxUndoSteps`, `kMaxUndoBytes` | `DocumentSession::Limits::steps`, `Limits::bytes` |
| `estimateModelBytes()` (a free function in `MainComponentJobs.cpp`) | `DocumentSession::estimateModelBytes()` |
| `MainComponent::canUndo()`, `canRedo()`, `undoDepth()`, `redoDepth()` | the same methods, one-liners over the session |
| `MainComponent::undoLabel()`, `redoLabel()` | the same methods, over the session |

The session asks the document for the state to snapshot and for the state to
put back through two doors, which the component installs in its constructor:

```cpp
document.setDocumentSource (
    [this] { return capturePieceModel(); },
    [this] (const upic::PieceModel& model) { return restorePieceModel (model); });
```

The restore goes through the normal loader, so an undone piece gets the same
checks and the same limits as a loaded one.

What stays with the shell is the shell's own work: the gain slider's flag,
the transport (an undo does not silence the app) and the messages ("Undo:
<label>", "The undo could not be applied"). The undo's failure path moved
to the caller: the session says "failed", the component says the message.
(The selection was the shell's own work at this point; R3.3 moved it behind
the session.)

### R3.3 - the selection

The selection used to be the component's own: nine members, scattered over
three hundred lines of its header.

| before | after |
|---|---|
| `MainComponent::selWave` | `SelectionModel::wave` |
| `MainComponent::selEnv` | `SelectionModel::envelope` |
| `MainComponent::selSlot` | `SelectionModel::sampleSlot` |
| `MainComponent::selAmpTableId` | `SelectionModel::ampTable` |
| `MainComponent::selSeqId` | `SelectionModel::sequence` |
| `MainComponent::selGroupId` | `SelectionModel::group` |
| `MainComponent::selChannelId` | `SelectionModel::channel` |
| `MainComponent::selectedArcId` | `SelectionModel::arc` |
| `MainComponent::lastArcSelection` | `SelectionModel::arcSelection` |

The new class is in `Source/SelectionModel.h`, and the session owns it:

```cpp
upic::SelectionModel& selection() noexcept { return selection_; }
const upic::SelectionModel& selection() const noexcept { return selection_; }
```

Every use asks the session now - `document.selection().wave` and its eight
siblings - and the defaults are the old ones: the first wavetable, the
first envelope and the first sample slot (0), and -1 for the amplitude
table, the sequence, the group, the mixer channel and the arc.

The selection's *meaning* did not change: the same fallbacks (the first
wave, the first envelope, the first amplitude table, the first sequence),
the same sample-slot clamp, and the same loader behaviour (a load selects
the first item of the bank it loads).

The canvas's own multi-selection stays with the canvas: the `ScoreEditor`
owns it. What the model carries is the arc the panel points at and the
mirror of the canvas selection the panel last showed.

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

## The session needs JUCE

The state the session owns was three counters (`std::atomic<uint64_t>` twice
and `std::atomic<bool>` once) and asked for `<atomic>` and `<cstdint>` and
nothing else. The history changed that: the labels are strings and the
snapshots are pieces, so the session asks for `<JuceHeader.h>` and
`Piece.h` and is an application-level class.

It stays out of the boundary check's JUCE-free groups for the same reason
the codecs stay out: it is the application layer's owner of the document.

The selection model it owns (`Source/SelectionModel.h`) is JUCE-free - ids
and a vector of ids - but it is out of the groups too: it is the
application layer's own, and a group for it would be a rule that has to be
taken back when the piece's data moves.

## The suites

`Source/SelfTestDocumentSession.cpp` is the document session's own
contract for the state: 30 checks. It pins:

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

`Source/SelfTestUndoHistory.cpp` is the history's own contract: 59 checks.
It pins:

- a fresh document: nothing to undo, nothing to redo, no labels;
- one edit: one labelled step on the undo stack, the redo stack empty;
- the undo: the piece is the state from before the edit, the undo stack
  loses the step, the redo stack gets it with the same label;
- the redo: the piece is the state from after the edit, the stacks swap;
- an undo with nothing to undo: nothing happens at all;
- the new branch: an edit after an undo clears the redo stack;
- the open transaction: the undo closes an open gesture and undoes its
  step, and an undo with nothing to undo leaves the gesture open;
- the bounds: at most 50 steps and the oldest goes first; the memory bound
  keeps the steps that fit, the oldest first, and a step that does not fit
  at all is not kept;
- the selection: the undo keeps the selection when the material is still
  there, and falls back to the first wave when it is gone;
- the transport: the undo does not silence the app.

`Source/SelfTestSelection.cpp` is the selection's own contract: 27 checks.
It pins:

- a fresh document: the first wavetable and the first envelope are
  selected, the first sample slot is selected, and the amplitude table, the
  sequence, the group, the mixer channel and the arc are not;
- a load: the selection follows the piece that is loaded - the first
  wavetable, the first envelope, the first sample slot, the first mixer
  channel and the first performance sequence;
- a load does not touch the amplitude table and the group selection: the
  loader leaves them alone;
- deleting the selected item moves the selection: the delete acts on the
  selected wavetable (or envelope), and the replacement takes its place in
  the selection;
- two documents keep their own selection;
- a load leaves no arc selected.

All three suites came first: they were written against the code where it
was, and they pass unchanged after the moves. The state suite reads the
document through one door (the `state` helper at the top of the file); the
history suite reads the history through the component's own doors (canUndo,
undoLabel, undoDepth, ...); the selection suite reads the selection through
the component's own doors (the `selected*ForTest` accessors). The move
touched each suite in one place and the checks themselves did not change.

The components in the suites are made in plugin mode: the document does not
depend on the window, and the plugin mode does not open an audio device.

## What is not covered yet

- **The piece's data has not moved.** The component still holds the
  waveforms, the envelopes, the pages, the arcs, the channels, the sample
  slots, the tuning and the sieve as its own members. The session owns the
  document's *state*, its *history* and its *selection*, not its *data*;
  moving the data is a much bigger job and it comes later in R3.
- **There is no command dispatcher.** The model still changes through
  `markModelChanged()` at the end of a hundred call sites, not through a
  command that says what it did (the review's R008). One command = one
  labelled transaction = one revision is R3's exit criterion, and it is
  not reached yet.
- **The document's data still holds JUCE values** - that is the deferred
  type migration (the plan's section 2), not this wave.
