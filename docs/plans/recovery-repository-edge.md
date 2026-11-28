# The recovery snapshot's repository edge

The architecture note for R4.3. It describes the boundary R4.3 introduced -
the recovery snapshot's storage and the recovery's service - as it is after
that phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/RecoveryRepository.h   the storage edge: where the snapshots live,
                              the write (the piece JSON without the audio,
                              the sidecars, the removal of the stale ones),
                              the read (the audio put back where the loader
                              wants it) and the cleanup
        |
        |  the edge stores what it is handed; the shell decides when
        v
Source/MainComponentRecovery.cpp   the service: the 30 s interval, the
                                   epoch, the unsaved check, the offer at
                                   the next start, and the words the user
                                   is told
```

The crash recovery writes a snapshot of the piece while there is unsaved
work, and offers it at the next start. The snapshot is a small piece JSON
(the pages, without the audio) plus the audio in sidecar files, so the
unchanged audio is not embedded again and again.

The storage of that snapshot was four static methods of the shell. The
review gives the responsibility a name - the "recovery repository" (§33,
the table of the storage responsibilities) - and asks for the recovery to
be a service of its own (R065, "Extract RecoveryService").

R4.3 extracts the storage. The service stays in the shell for now: it is
the part that needs the window (the dialog), the job runner and the
document's state, and it is where the review's R065 will continue.

## What moved

| before | after |
|---|---|
| `MainComponent::recoveryDirectory()` | `upic::recoveryDirectory()` |
| `MainComponent::writeRecoverySnapshot (dir, model, error)` | `upic::writeRecoverySnapshot (dir, model, error)` |
| `MainComponent::readRecoverySnapshot (dir, pieceOut, error)` | `upic::readRecoverySnapshot (dir, pieceOut, error)` |
| `MainComponent::deleteRecoverySnapshot (dir)` | `upic::deleteRecoverySnapshot (dir)` |
| the hash of the audio (a file-static function) | `upic::recovery_detail::hashBytes()` |

The bodies are the shell's methods, body for body. The property names, the
8 MB piece cap, the `kMaxPieceBytes` audio cap, the hash (FNV-1a), the
sidecar names, the order of the checks and the wording of every refusal do
not change by a character.

## The shape of a snapshot

The folder is the user's own data folder (the roaming one on Windows): the
snapshot belongs to the user, not to the machine.

```
<user app data>/UPICuitous/recovery/
├── piece.json                       the piece, without the audio
└── sample-<id>-<hash>.pcm           the audio of one slot
```

The sidecar's name carries the hash of the audio, so a sidecar that is
already there is the right one and is not written again - the repeated
snapshots carry the pages, not the material. The piece JSON points at each
sidecar through a `recoveryFile` property; the sidecars that are not used
any more are removed at the end of the write, so the folder does not grow
with every snapshot.

The read puts the audio back where the loader wants it: every sidecar the
snapshot points at is read and set as the slot's `data` (base64), and the
`recoveryFile` property is removed again - the loader does not know it.

## The failures

The refusals are the shell's old words, unchanged:

- "the recovery folder could not be created";
- "the recovery audio could not be written" / "the recovery audio could not
  be stored";
- "the recovery snapshot is too big" (the piece JSON is over 8 MB);
- "the recovery snapshot could not be written" / "the recovery snapshot
  could not be stored";
- "there is no recovery snapshot";
- "the recovery snapshot is empty";
- "the recovery snapshot is not a piece";
- "the recovery snapshot lost its audio";
- "the recovery audio is too big" (over `kMaxPieceBytes`);
- "the recovery audio could not be read".

The service words them: "The recovery snapshot could not be written: X",
"Recovered the piece from the last session" and "The recovery snapshot
could not be opened".

## What stays in the shell

The service - and the review's R065 is about it:

- `maybeWriteRecoverySnapshot()`: the 30 s interval, the epoch (not the
  revision - a long gesture is recovered while it is still going on), the
  unsaved check, the job, and the silent failure;
- `offerRecovery()`: the offer at the next start, the dialog, the recover
  (which goes through `loadFromVar()` like any document) and the discard.

Those are the app's behaviour, not the storage's. They are what the
review's `RecoveryService` will be when the window is decomposed (R5), and
they are left where they can still be reached by the window.

## The suite

`Source/SelfTestRecoveryRepository.cpp` is the storage's own contract: 21
checks that pin what the repository does.

- where the snapshots live (the user's own data folder);
- the shape of a snapshot: the piece file, the sidecar's name, the audio in
  its own file, and the piece JSON that does not embed the audio;
- the same audio keeps its sidecar, and the sidecars that are not used any
  more are removed;
- every refusal, with its words;
- the round trip: the audio comes back as the base64 the loader wants, and
  the sidecar's name is gone;
- the cleanup: the snapshot is thrown away and the folder is kept.

The suite came first: it was written against the old code and drove the
storage through the shell's four static methods. The move turned them into
the edge's doors, and the checks themselves did not change.

The suite works in its own folders under the temp directory, so it never
touches the user's own data folder.

## What is not covered yet

- **The service is still in the shell** (`maybeWriteRecoverySnapshot()` and
  `offerRecovery()` in `Source/MainComponentRecovery.cpp`). It is the
  review's R065's remaining half, and it needs the window decomposition
  (R5) to move.
- **The recovery's periodic write has no suite of its own.** The interval,
  the epoch check and the unsaved check are the service's, and the suite
  covers the storage under them. The behaviour of the service is pinned by
  the crash-recovery checks in `Source/SelfTestFiles.cpp` and by the app's
  own use of it.
- **The snapshot's format still holds JUCE values** - that is the rest of
  the review's R020, and the plan defers it (§2).
