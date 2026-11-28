# The document session

The architecture note for R3. It describes the boundary R3 introduced - the
document's state, the document's history, the document's selection and the
command dispatcher - as it is after R3.5, not as it is meant to be at the
end of the refactoring.

## The boundary

```
Source/DocumentSession.h   the document's own state: the revision, the epoch
                           and the unsaved flag - the document's history:
                           the steps, their labels and the two bounds - the
                           document's selection and the command dispatcher.
                           It is the one owner of them.
        |
        |  the session owns the selection and the dispatcher; the shell asks
        |  it for both
        +--> Source/SelectionModel.h
        |    what is selected: the wavetable, the envelope, the amplitude
        |    table, the sequence, the group, the mixer channel, the sample
        |    slot and the arc.
        |
        +--> Source/CommandDispatcher.h
        |    the open command: its label, the state from before it, the flag
        |    that says whether it changed anything, and the rule that says
        |    what a change means.
        |
        v
Source/MainComponent.h      the shell: it opens a command and closes it, it
                            reports a change through the dispatcher, it reads
                            the session when it needs to know what the
                            document is doing, and it keeps the shell's own
                            work: the transport and the messages.
```

The review asked for one authoritative owner of the document (its R005,
"Extract DocumentSession"). The piece's *data* still lives where it lived -
in the component's members - and moving it is a much bigger job than a
phase. What R3 moves is the document's own *state* (R3.1: the three
counters that say what the document is doing), the document's own *history*
(R3.2: the steps the user can undo), the document's own *selection*
(R3.3: what the user is working on) and the document's own *command*
(R3.4: the transaction a change belongs to).

## What moved

### R3.1 - the state

The state used to be three members of the component, and ten places asked
about them.

| before | after |
|---|---|
| `MainComponent::modelEpoch` (`std::atomic<int>`) | `DocumentSession::epoch()` |
| `MainComponent::modelRevision` (`std::atomic<uint64_t>`) | `DocumentSession::revision()` |
| `MainComponent::pieceDirty` (`std::atomic<bool>`) | `DocumentSession::isDirty()` |
| `MainComponent::markModelChanged (bool affectsPiece)` | the same method, now a one-liner over the session's `reportChange (bool affectsPiece)` |
| `MainComponent::pieceHasUnsavedChanges()` | the same method, now `DocumentSession::isDirty()` |
| `MainComponent::markPieceSaved()` | the same method, now `DocumentSession::markSaved()` |

The ten read sites now ask the session instead of the component's own
members: the status timer (the revision), the snapshot publisher (the
revision), the audio thread (the epoch), the recovery snapshot (the
revision), the saver (the revision, twice), "make unique" (the revision,
twice) and the undo transaction (the revision, twice). R3.4 moved five of
them to the epoch and took the undo transaction off the counters (see
below); the audio thread already read the epoch.

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

R3.4 moved the open transaction's three members out again - they are the
command dispatcher's now; the steps, their labels and the bounds stayed with
the session.

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

### R3.4 - the command

The open transaction used to be the session's: the snapshot from before the
change, the label, and the revision from before it (to notice at the end
whether anything had changed at all).

| before | after |
|---|---|
| `DocumentSession::pendingEdit_` | `CommandDispatcher::pending_` |
| `DocumentSession::pendingLabel_` | `CommandDispatcher::label_` |
| `DocumentSession::pendingRevision_` (compare the counters at the end) | `CommandDispatcher::changed_` (the change report) |
| `DocumentSession::beginEdit()`, `endEdit()`, `hasOpenEdit()` | `CommandDispatcher::beginCommand()`, `endCommand()`, `isCommandOpen()` |
| `DocumentSession::markChanged()` | `CommandDispatcher::reportChange()` |

The new class is in `Source/CommandDispatcher.h`, and the session owns it
and installs its four doors:

```cpp
dispatcher_.install (
    [this]
    {
        if (! capture_) return std::unique_ptr<upic::PieceModel>();
        return std::make_unique<upic::PieceModel> (capture_());
    },                                                     // before the command
    [this] (upic::PieceModel&& model, const juce::String& label)
    {
        commitStep (std::move (model), label);
    },                                                     // the undo step
    [this] (bool affectsPiece) { modelMoved (affectsPiece); },  // epoch + dirty
    [this] { documentChanged(); });                        // the revision
```

The history's bookkeeping - the steps, the redo stack and the two bounds -
stays with the session, in `commitStep()`; the dispatcher decides *when* a
step is due. The shell keeps its own names: `MainComponent::beginEdit()` and
`endEdit()` are still how the shell opens and closes a command (they call
the session's `beginCommand()` and `endCommand()`), and `markModelChanged()`
is still how it reports a change (it calls the session's `reportChange()`).

**The rule.** One command = one labelled undo transaction = one revision:

- a change inside a command moves the **epoch** at once (the audio follows
  the gesture while it is drawn) and the **revision** when the command ends;
- a change outside a command is a change of its own: no transaction to put
  it in, so the epoch and the revision move straight away;
- a command that changes nothing is not an undo step and moves no counter;
- a command that is still open when the next one begins is closed first.

The suite that pins the rule is `Source/SelfTestCommands.cpp`; see below.

### R3.5 - the edits

The dispatcher was the only way into the document, but the shell's call sites
were not commands yet: the model still changed through `markModelChanged()`
at the end of about eighty call sites, and each of them was a change of its
own - no label, no undo step. R3.5 turns the twenty that are user edits
into commands.

The shell gets one helper for it:

```cpp
void MainComponent::runEdit (const juce::String& label,
                             const std::function<void()>& fn);
```

If a command is already open it just runs `fn`; otherwise it opens the
command with the label, runs `fn` and closes it. So a one-shot edit is one
command, and a gesture - which opens the command at its drag start and
closes it at its drag end - is one command too, with the value changes in
between joining it.

The twenty edits and their labels are in the plan (§6.22 and the R3.5
record). The six changes that stay raw are named there as well: the loop
toggle (a change that does not affect the piece), the page box (a page
switch is navigation), the document replacements (a load, an undo or redo
restore, a recovery) and the test door that installs a sample slot. A
change outside a command is legal - R3.4 defined what it means.

The suite that pins the new contract is
`Source/SelfTestModelCommands.cpp`; see below.

## Why the revision and the epoch are two counters

They no longer move together: the epoch moves with every model change, the
revision moves once, when the command that contains the change ends. They
answer different questions:

- **The epoch** is "the model moved" - the signal the audio follows. It
  moves on every change, inside a command or outside one, and the five
  readers that ask "did the model move since I looked?" read it now: the
  status timer, the snapshot publisher, the saver, the recovery snapshot
  and "make unique". The status timer uses it to publish a snapshot only
  when there is something new to copy (the timer used to deep-copy the
  whole model 30 times a second, even while nothing changed), the saver
  uses it to notice that the piece on the disk is already older than the
  one on the screen, and "make unique" uses it to notice that its own work
  changed something.
- **The revision** is the document's own counter: one command, one
  revision, however many changes the command has. It is the document's own
  count, and the command's label is what makes it meaningful. It has no
  production reader now - only the snapshot field and the test accessor -
  and that is deliberate. Using it as the "did it move?" signal would tie the audio
  and the saver to the command's end: a drag's snapshot would be published
  only on the mouse-up, and a change still inside a command would not make
  the saver warn that the disk is older than the screen.

The command dispatcher keeps the two apart: it reports every change through
`modelMoved` (the epoch and the unsaved flag) and holds the revision back
until `endCommand()`. A command that changed nothing moves neither.

The suite pins both rules: the epoch follows the change inside the command,
the revision waits for the command to end, and a change outside a command
moves both at once.

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

The command dispatcher (`Source/CommandDispatcher.h`) needs JUCE as well:
the label is a string and the snapshot it holds is a piece. It is the
application layer's own, like the session that owns it, and it is out of
the boundary check's JUCE-free groups for the same reason. It is a header,
so the check counts the source files, not the headers: 66 of them now
(R5.7's sieve-panel and frequency-inspector suites are the last ones added).

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
- the undo rule: a gesture that changed nothing is not an undo step, a
  gesture that changed something is, and a change that does not affect the
  piece still counts as a change;
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

`Source/SelfTestCommands.cpp` is the command dispatcher's own contract: 29
checks. It pins:

- a change outside a command: the revision and the epoch move by one, the
  piece is marked unsaved, and there is no undo step;
- a change that does not affect the piece (the loop toggle): the counters
  move, the unsaved flag does not;
- one command: one undo step with its label;
- two changes in one command: one step, the epoch +2, the revision +1;
- a command that changes nothing: no step and no counter;
- two commands: two steps;
- a command that was never closed: closed by the next one;
- two documents: independent;
- an undo: a change that leaves the piece unsaved.

`Source/SelfTestModelCommands.cpp` is the edits' own contract: 140 checks.
It pins, for each of the nineteen edits the suite can drive:

- the edit changes the document once (the revision advances by one);
- the epoch follows every change inside the command (the live reverse's
  two changes: the epoch +2, the revision +1);
- the piece is unsaved;
- the edit is one undo step, and the step knows what the edit was called;
- the piece carries the edit (the suite reads the piece the component
  would save).

The twentieth edit - the capture - needs live audio, so the suite does not
drive it; the plan says so rather than pretending the suite covers it.

All five suites came first: they were written against the code where it
was, and they pass after the moves. The state suite reads the document
through one door (the `state` helper at the top of the file); the history
suite reads the history through the component's own doors (canUndo,
undoLabel, undoDepth, ...); the selection suite reads the selection through
the component's own doors (the `selected*ForTest` accessors); the command
suite reads the command through the component's own doors
(`modelRevisionForTest`, `modelEpochForTest`, `pieceHasUnsavedChanges`,
`undoDepth`, `undoLabel` and `commandOpenForTest`); the edits' suite reads
the edits through the named controls, the public methods and the three
doors added with the move (the workspace split's drag and the arc list's
two per-arc controls). The moves touched each suite in one place and the
checks themselves did not change - except one expectation in the command
suite (a command with two changes used to advance the revision by two and
advances it by one now) and one expectation per edit in the edits' suite
(these edits used to leave no undo step and are one step now). That is the
wave's exit criterion, and the plan said so before the move.

The components in the suites are made in plugin mode: the document does not
depend on the window, and the plugin mode does not open an audio device.

## What is not covered yet

- **The piece's data has not moved.** The component still holds the
  waveforms, the envelopes, the pages, the arcs, the channels, the sample
  slots, the tuning and the sieve as its own members. The session owns the
  document's *state*, its *history*, its *selection* and its *command
  dispatcher*, not its *data*; moving the data is a much bigger job and it
  comes later in R3.
- **The edits are commands now, but the piece's data is not behind the
  session.** R3.5 gave the twenty remaining user edits a command each, so
  one command = one labelled transaction = one revision is reached. What is
  left of R3 is the piece's *data*: it still lives in the component's
  members, and moving it is a much bigger job than a phase.
- **The document's data still holds JUCE values** - that is the deferred
  type migration (the plan's section 2), not this wave.
