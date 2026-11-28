# The document's file adapter edge

The architecture note for R4.2. It describes the boundary R4.2 introduced -
the document's file picking and the document's I/O - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/DocumentRepository.h   the file adapter edge: the reader (the cap,
                              the read, the parse, the guard), the writer
                              (the JSON build, the measurement, the refusal,
                              the write) and the size in words
        |
        |  the edge reports what happened; the shell words it
        v
Source/MainComponentPiece.cpp the chooser (the file picking), the wording
                              the user is told, and the commit
```

A piece reaches the disk through two adapters: the reader and the writer.
They were one thing with the picking - `MainComponent::loadFile()` read the
file, checked the cap and parsed the JSON *inside the chooser's callback*,
and `MainComponent::savePieceToFile()` built the JSON, measured it and
wrote it. The file picking and the document's I/O were one method of the
shell, and the I/O could only be reached through a window.

The review asks for the two to be separate (R064: "separate file picking
and document I/O"). This is step 1 of the review's loading pipeline (§32):
"Read document - file adapter and size limits".

## What moved

| before | after |
|---|---|
| the read in `MainComponent::loadFile()`'s chooser callback | `upic::readDocument (const juce::URL&, juce::int64)` |
| the write in `MainComponent::savePieceToURL()` and in the window's worker | `upic::writeDocument (const juce::URL&, const PieceModel&, juce::int64)` |
| `humanPieceSize()` in `MainComponentPiece.cpp` and a copy of it in `savePieceToURL()` | `upic::humanPieceSize (juce::int64)` |

The reader hands the *document* over - it does not know whether the
document is a piece. That is the codec's business (`Source/PieceCodec.h`),
and the commit is the shell's. The pipeline is the review's:

```
read (the edge)  ->  decode, migrate, validate (the codec)  ->  commit (the shell)
```

## The shell keeps the picking and the wording

The file picking stays in the shell: the chooser is the window's, and the
URL it hands over is the edge's input. So does the wording:

- "Not a UPIC piece (the JSON is broken) - the current piece was kept";
- "This file is not a UPIC piece - the current piece was kept";
- "Saved X".

Those are the app's state, not the file's: the first two say that the
musician's unsaved work is still there, and the third says what the app
did. The edge says *what happened*; the shell words it for the user.

## The failures are structured

The review asks for structured load errors rather than a single success
flag (§32). The plan defers the type migration (§2), so the results carry
the JUCE types the edge already uses:

```cpp
enum class DocumentReadFailure
{
    none,          // the document was read
    noFile,        // no file was chosen
    missing,       // the file does not exist
    tooLarge,      // the file is over the cap
    tooLargeText,  // the file's text is over the cap (the file's size was not)
    brokenJson,    // the file is not JSON at all
    unreadable     // the file could not be read
};

struct DocumentReadResult
{
    bool ok = false;
    juce::var document;
    DocumentReadFailure failure = DocumentReadFailure::none;
    juce::String error;   // the file layer's message (when the file is the problem)
};

struct DocumentWriteResult
{
    bool ok = false;
    juce::String error;   // the reason it could not be written
    juce::int64 bytes = 0;
};
```

The messages did not change by a character: "The file does not exist: X",
"The file is too large: X", "No file was chosen", "Could not write X" and
"Piece too large to save (X, the limit is Y) - shorten the capture or the
sample, or export the audio to a WAV instead".

The last one was written twice - once in the window's worker and once in
the synchronous saver - and it is written once now.

## Two details of the old code

- The "Piece too large (max N MB)" branch in the load was **unreachable**:
  the file layer refuses the same file first, with "The file is too large:
  X". It is gone with the branch.
- The silent return when the file's *text* is over the cap **stays silent**:
  the text of a file can be bigger than the file (an invalid byte becomes
  three), so the check is still needed. It is the edge's
  `DocumentReadFailure::tooLargeText` now, and the shell says nothing for
  it - as it did before.

The guard around the load is split the same way the work is: the edge
guards the read and the parse, the shell guards the commit, and both say
"Could not load the piece".

## The suite

`Source/SelfTestDocumentIO.cpp` is the edge's own contract: 22 checks that
pin what the adapter does.

The writer (through the shell's own door, `MainComponent::savePieceToFile`):

- a piece over the cap is refused, and the destination is not touched;
- the refusal says exactly what to do about it;
- a piece that fits is written, the write says what it saved, and the piece
  has nothing to save afterwards;
- the piece that comes back is the piece that went in;
- a refused save leaves the file that was there alone;
- a write that fails keeps what was there, and says what went wrong;
- a destination that cannot be opened is refused;
- a piece with no destination is not written and nothing is said.

The reader (through the edge's door, `upic::readDocument`):

- a file that is not there is refused, and the failure is labelled;
- a file over the cap is refused, and the failure is labelled;
- a file that was never chosen is refused, and the failure is labelled;
- a local file is read in place - nothing is copied.

And the load, through the shell's door (`MainComponent::loadFromURLForTest`):

- a file that is not there, a file that is not JSON and a file that is not
  a piece are refused with the words the user knows;
- a refused load leaves the piece alone;
- a piece file is loaded.

The suite came first: it was written against the old code and drove the
writer through `MainComponent::savePieceToFile` and the reader through the
file layer's door (`upic::readPickedFile`). The move turned the reader's
door into the edge's, and the load's sentences - which live in the
chooser's callback and cannot be reached without a window - got their
door with the move (the R3.5 precedent: the doors come with the move).

## What is not covered yet

- **The recovery snapshot's I/O is still in the shell**
  (`Source/MainComponentRecovery.cpp`). It is the review's R065
  ("Extract RecoveryService") and it was left out of this phase on
  purpose.
- **The user library's and the tuning import's file reads are still in the
  shell.** They are other formats (the library assets, the Scala tuning),
  and the review gives them their own edges (R066, R067).
- **The document's data still holds JUCE values** - that is the rest of
  the review's R020, and the plan defers it (§2).
