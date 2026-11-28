# The user library's repository edge

The architecture note for R4.4. It describes the boundary R4.4 introduced -
the user library's storage and the library's service - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/LibraryRepository.h   the storage edge: where the library lives,
                             the kind-to-folder mapping, the name-to-file-name
                             slug, the atomic write, the validated read, the
                             listing, the first free id and the reveal target
        |
        |  the edge stores what it is handed; the shell decides what and
        v  when, and words the outcome
Source/MainComponentModel.cpp   the service: the five publish/import/reveal
                                triples, the selection, the bank limits, the
                                undo transaction, the destination-bound
                                frequency tables and the words the user is
                                told
```

The library is a per-user folder of assets that outlive the piece: a
wavetable, an envelope, an amplitude table, a frequency table, a performance
sequence. An asset is one JSON file - the kind, the library's own id, the
name, the colour and the asset's own payload - and the folder is where those
files live.

The review gives the storage a name of its own - the "library repository"
(§34, Persistence/Libraries) - and the reusable assets a format of their own
- the "library asset codec" (§33). Before R4.4 both lived in
`Source/UserLibrary.h`, and the shell built the path and read the file.

R4.4 extracts the storage. The asset's format stays in `Source/UserLibrary.h`
and the service stays in the shell.

## What moved

| before | after |
|---|---|
| `UserLibrary::rootFolder()` | `upic::library_repository::rootFolder()` |
| `UserLibrary::setRootForTest()` / `clearRootForTest()` / `rootIsOverriddenForTest()` | `upic::library_repository::setRootForTest()` / `clearRootForTest()` / `rootIsOverriddenForTest()` |
| `UserLibrary::folderFor (kind)` | `upic::library_repository::folderFor (kind)` |
| `UserLibrary::slugFor (name)` | `upic::library_repository::slugFor (name)` |
| `UserLibrary::publish (kind, id, name, colour, payload, error)` | `upic::library_repository::publish (kind, id, name, colour, payload, error)` |
| `UserLibrary::read (kind, file, out, error)` | `upic::library_repository::read (kind, file, out, error)` |
| the shell's `folderFor (kind).getChildFile (fileName)` + `read` | `upic::library_repository::read (kind, fileName, out, error)` |
| `UserLibrary::list (kind, skipped, failures)` | `upic::library_repository::list (kind, skipped, failures)` |
| `UserLibrary::nextLibraryId (kind)` | `upic::library_repository::nextLibraryId (kind)` |
| `UserLibrary::revealTarget (kind, fileName, target, error)` | `upic::library_repository::revealTarget (kind, fileName, target, error)` |
| `UserLibrary::reveal (kind, fileName, error)` | `upic::library_repository::reveal (kind, fileName, error)` |
| `UserLibrary::revealFolder (kind)` | `upic::library_repository::revealFolder (kind)` |
| the envelope inside `publish` / `read` | `UserLibrary::assetToVar()` / `assetFromVar()` |

The bodies are the old `UserLibrary` methods, body for body. The
six-property envelope, the 4 MB asset cap, the atomic temp-and-rename
write, the `-2`…`-999` collision counter, the order of the checks and the
wording of every refusal do not change by a character.

## The shape of the library

The folder is the user's own data folder (the roaming one on Windows): the
library belongs to the user, not to the machine.

```
<user app data>/UPICuitous/Library/
├── waves/          *.json
├── envelopes/      *.json
├── tables/         *.json          the amplitude and the frequency tables
└── sequences/      *.json
```

The folders are coarser than the kinds: the two table kinds share `tables`,
so the file carries the kind and a file in the wrong folder is refused.

An asset name becomes a file name: the letters and digits survive, the rest
becomes an underscore, the result is lowercased, trimmed to 40 characters,
and an empty result becomes `asset`. A publish never overwrites: the same
name gets `-2`, `-3`, … up to `-999`. The name *inside* the file keeps the
original spelling.

## The envelope

Every asset is one JSON object with exactly six properties:

```json
{ "kind": "wave", "id": 1, "version": 1,
  "name": "My Wave", "colour": -1234,
  "payload": { "samples": [ ... ] } }
```

- `kind` is the asset's kind (`wave`, `envelope`, `ampTable`, `freqTable`,
  `sequence`);
- `id` is the library's own id (0…`limits::kMaxItemId`), never a model id;
- `version` is 1;
- `name` is the trimmed name (at most 128 characters);
- `colour` is the ARGB integer;
- `payload` is the asset's own var, validated against the piece's limits.

The write is atomic: the text goes to `<file>.json.tmp` first and is renamed
over the target. A crash in the middle leaves the old asset (or no asset),
never half of one. The payload is validated before the folder is made, so a
bad file cannot leave an empty folder behind.

## The failures

The refusals are the old `UserLibrary` words, unchanged:

- publish: "The asset needs a name", "The asset's name is too long", "The
  asset's id is outside the valid range", "The library folder could not be
  created: X", "The library already holds too many files called \"X\"", "The
  asset could not be written to X", "The asset could not be renamed into the
  library";
- read: "The asset file is gone", "The asset file is too large (the limit is
  4096 kB)", "The asset file is empty", "The asset file is not a valid
  library asset", "The asset file has an unknown property: X", "The asset
  file has an unsupported version", "The file does not hold a \"wave\"
  asset", "The asset's id is outside the valid range", "The asset has no
  valid name", "The asset has no name", "The asset's name is too long", "The
  asset has no valid colour", "The asset has no data", and the payload
  validators' own words;
- reveal: "No library file is selected", "The library file name must not
  contain a path", "The library file is outside its own folder", "The
  selected library file no longer exists", "The user library folder does not
  exist yet".

The service words them: `"The library asset was refused: " + error`,
`"Published \"X\" to the user library"` and `"The user library could not
reveal that X: " + error`.

## The one asymmetry, kept

The import's read takes the file name from the manager's list, so the old
shell built `folderFor (kind).getChildFile (fileName)` without a guard; the
reveal takes a typed name, so it guards. R4.4 moves the path-building into
`read (kind, fileName, …)` and keeps the asymmetry body for body. A name
with a path is still refused only on the reveal path.

## What stays in the shell

The service - the five publish/import/reveal triples in
`Source/MainComponentModel.cpp`:

- **publish**: the selected wave/envelope/table/sequence, the destination
  for a frequency table, the undo transaction, and the confirmation;
- **import**: the manager's selected file, the new local id
  (`nextLibraryId`), the bank's caps, the destination for a frequency table,
  the undo transaction, and the refusal's words;
- **reveal**: the manager's selection, the folder's reveal, and the
  refusal's words;
- **refresh**: `refreshUserLibraryBoxes()` fills the manager from `list()`
  and reports the skipped files.

Those are the app's behaviour, not the storage's.

## The suite

`Source/SelfTestLibraryRepository.cpp` is the storage's own contract: 47
checks that pin what the repository does.

- where the library lives (the user's own data folder) and how the kinds map
  to four folders, with the two table kinds sharing one;
- the root's redirect for the selftest;
- the slug rules: letters and digits survive, the rest becomes an
  underscore, an empty name becomes `asset`, the limit is 40 characters;
- the six-property envelope, the file named after the asset, and the atomic
  write (a temp file that cannot be written is refused and no asset is left
  behind);
- the collision counter: the same name publishes beside the first;
- every publish refusal, with its words, and the payload validated before
  the folder is made;
- the round trip for every kind;
- the listing: the valid assets, the skipped files and their reasons, and a
  valid table of the other kind not counted as a failure;
- the first free id, and a duplicate id not moving it;
- the reveal target's guard: an empty name, a name with a path, a file that
  is gone, and a plain name inside its own folder.

The suite came first: it was written against the old code and drove the
storage through `UserLibrary`'s static methods. The move turned them into
the edge's doors, and the checks themselves did not change.

The suite works in its own folders under the temp directory, so it never
touches the user's own library.

## What is not covered yet

- **The service is still in the shell** (the five triples in
  `Source/MainComponentModel.cpp`). It needs the window decomposition (R5)
  to move. R066 asked for this storage edge; the service remains the
  shell's.
- **The publish/import/reveal commands have no suite of their own beyond
  WP7.** Their behaviour is pinned by `Source/SelfTestWP7.cpp` (the format,
  the commands, the undo, the limits and the frequency destinations) and by
  the model's publish caps in `Source/SelfTestModel.cpp`; the repository's
  suite covers the storage under them.
- **The library's format still holds JUCE values** - that is the rest of
  the review's R020, and the plan defers it (§2).
