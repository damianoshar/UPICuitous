# The document's codec edge

The architecture note for R4.1. It describes the boundary R4.1 introduced -
the document's data and the document's file format - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Piece.h          the piece as data: the PieceModel - the banks, the
                        pages, the slots, the settings. The format is not
                        here.
Source/PieceCodec.h     the codec edge: the two functions that read and
                        write the file format of a piece, and the
                        diagnostics the read hands back
        |
        |  the domain side asks for no JUCE header
        v
Source/MainComponentPiece.cpp, ...    the rest of the app
```

The piece is the model: the wavetables, the envelopes, the conversion
tables, the sequences, the channels, the pages, the material and the
session's settings. How all of that reaches a file - and how it comes
back - is a different thing.

It carried one thing that was neither: the file format. `pieceToVar()` was
a free function in `Source/Piece.h`, and the *read* side was a method of
the shell (`MainComponent::loadFromVar()`), so the piece's format was a
method of the window and could only be reached through one.

Now the piece's format is an edge like every other format in the app
(`PageCodec.h`, `ArcCodec.h` and their neighbours), and the read side is
an edge too: it decodes a document and hands the candidate over.

## What moved

The loader's two halves became two things:

| before | after |
|---|---|
| `MainComponent::loadFromVar (const juce::var&)` | `upic::pieceFromVar (const juce::var&, PieceModel&, PieceDiagnostics&)` and `MainComponent::commitPieceModel (const PieceModel&, const PieceDiagnostics&)` |
| `upic::pieceToVar (const PieceModel&)` in `Source/Piece.h` | `upic::pieceToVar (const PieceModel&)` in `Source/PieceCodec.h` |

The bodies are the old ones, body for body. The property names, their
order, the optional keys a plain piece does not write, the clamps a
crafted file gets, the caps, the renumbered ids, the references that are
put back and the two messages the user is told are the file format
itself, and they did not change by a character.

`MainComponent::loadFromVar()` is now fourteen lines: the codec reads the
document, and the commit puts the candidate in force.

## The commit stays in the shell

The *commit* is the shell's work and stays there:

- the swap under the model lock, and the selection that goes with it;
- the controls (the sieve, the stickiness, the time grid, the grains, the
  smoothing, the pitch-mode boxes);
- the engine (the grains, the pitch modes, the vocoders' memory);
- the snapshot the audio thread reads;
- the messages and the unsaved flag.

`loadFile()` keeps the file adapter too: the size cap, the JSON parse and
the guard around the parse. (R4.2 moved that half into an edge of its own
- see [`document-io-edge.md`](document-io-edge.md).)

The load stays transactional, and the transaction is now the codec's
contract: a document that is not a piece is refused, and the caller's
candidate is left exactly as it was - the unsaved work stays.

## The diagnostics

The review asks for structured load errors rather than a single success
flag (the review's §32). The plan defers the type migration (§2), so the
diagnostics carry the JUCE types the edge already uses:

```cpp
struct PieceDiagnostics
{
    bool ok = false;
    juce::String notLoaded;   // "This piece is bigger than the app supports - not loaded: ..."
    juce::String adjusted;    // "This piece was adjusted: ..."
    int renamedIds = 0;
    int fixedRefs = 0;
    int droppedItems = 0;
    int droppedFrames = 0;
};
```

The two messages are the strings the app showed before, character for
character, and the counts behind them are new: the ids that were
renumbered, the references that were put back, the items and the sample
frames that did not make it in.

## The one change

`pieceToVar()` writes the two pitch modes only when the piece carries
one.

Before, the writer always wrote them, and a piece that was saved without
a mode came back with a mode of zero: the session's mode was lost on the
way out and back. The loader already treated a document without a mode as
"the document carried none" and left the session's mode alone - the
writer and the reader disagreed, and the writer was the one that was
wrong. Now they agree.

A live piece always carries its modes (the engine's), so the golden
render and the v0.29 fixture do not move.

## The suite

`Source/SelfTestPieceCodec.cpp` is the edge's own contract: 82 checks that
reach the document's format through the codec alone - no `MainComponent`,
no window, no component tree. It pins:

- what the codec refuses (nothing, an empty object, no pages, rubbish
  pages) and that a refused document leaves the piece alone;
- the defaults an old document gets: the master gain, the page
  sequencer, the pressure recording, the scrub audition, the muted
  dragging, the four grain and smoothing times, the sieve, the stickiness,
  the time grid and the app's own banks;
- the clamps: the master gain, the stickiness, the time grid, the drag
  smoothing, the three grain times, the sieve and the page the document
  opens on - over the range and under it;
- the sieve remainders, folded into the sieve, and the empty list that
  gets the octave sieve;
- the caps: what does not fit is dropped (the pages, the wavetables, the
  envelopes, the conversion tables, the sequences, the channels) and the
  user is told, bank by bank;
- the ids that are renumbered and the references that are put back (the
  wavetable, the envelope, the conversion table, the index envelope, the
  channel, the group's overrides);
- the modulator loop that is broken;
- the material's Ref (M5): the slot takes the Ref of the first arc that
  uses it, and an arc that disagreed is told;
- the workspace layout that comes back with the document, the split that
  is pulled back to the edge and the page that is not there;
- the pitch modes: the document's modes are put in force, the ones over
  the range are pulled down, and a document that carries none leaves the
  session's modes alone;
- the round trip (the document that comes back is the document that went
  in) and the unknown keys a document may carry.

The suite came first: it was written against the old code and drove the
loader through `MainComponent`. The move turned the door from the
component to the codec, and the checks themselves did not change. The
suite reaches the codec through one door (the `load` helper at the top of
the file), so the move touched the suite in one place.

The component-level check that a rejected document leaves the *active*
piece alone stays in `Source/SelfTestDocument.cpp` - that one needs the
shell.

## What the suite found

One thing came out of writing it:

- **The writer and the reader disagreed about the pitch modes.** The
  loader read a missing mode as "the document carried none" and left the
  session's mode alone; the writer always wrote a mode, so a piece that
  was saved without one came back with a mode of zero. The suite pinned
  both halves, and the phase made the writer agree with the reader.

## What is not covered yet

- **The domain's data still holds JUCE values**: the `juce::String name` and
  the `juce::Colour` of the groups, the envelopes, the waveforms and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the rest of the review's R020, and the plan defers
  it (§2).
- **The document's file reading is still in the shell** (`loadFile()` reads
  the file and parses the JSON). The *format* is the codec's; the file is
  the shell's, and the split is deliberate for now. (R4.2 moved the file
  half into an edge of its own - see
  [`document-io-edge.md`](document-io-edge.md).)
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
