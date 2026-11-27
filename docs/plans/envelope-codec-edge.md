# The envelope's codec edge

The architecture note for R2.6. It describes the boundary R2.6 introduced -
the envelope's data and the envelope's file format - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Envelope.h         the envelope: the id, the name, the colour, the
                          points, the lookup the render walks and the five
                          presets.
Source/EnvelopeCodec.h    the codec edge: the two functions that read and
                          write the file format of an envelope
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...       the rest of the app
```

An envelope is the model's own type: a named set of points over the arc's
life, with a gain at each point. What a piece file carries of it is a
different thing - the edge, where the JUCE value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Envelope::toVar()` | `upic::envelopeToVar (const Envelope&)` |
| `Envelope::fromVar (const juce::var&, int)` | `upic::envelopeFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `e.` the free function needs and the
whitespace. The property names, their order, the defaults a file without
them falls back to and the clamps a crafted file gets are the file format
itself, and they did not change by a character.

The codec edge now holds four of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the
arc group) and `Source/EnvelopeCodec.h` (the envelope). The piece's own
writer was already a free function (`pieceToVar` in `Source/Piece.h`).

## The amplitude table comes along

`AmplitudeTable` is an alias for `Envelope`:

```cpp
using AmplitudeTable = Envelope;
```

The two banks reuse the envelope's data, the envelope editor and the
serialization on purpose - only the banks are separate (a table can never
be picked as an amplitude envelope). So the amplitude table's file format
*is* the envelope's file format, and its four call sites - the piece's
writer, the piece's reader, the library's publish and the library's import
- moved with the envelope's.

## What did not move

The envelope still holds `juce::String name` and `juce::Colour colour`. The
plan defers replacing the domain's value types (the plan's section 2), so
`Source/Envelope.h` still asks for the JUCE header and the envelope is
**not** one of the gate's JUCE-free groups. The gate's five groups are the
same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

## One fix comes with it

`Source/Envelope.h` used `SegmentCursor` and `followSegment` without
including the header that declares them (`Source/UpicMath.h`), so it only
compiled when something else had included it first - `Arc.h` and `Channel.h`
both include it for the same reason. The header now says what it uses. No
behaviour changes; the suite for the envelope is the first thing that
reaches the envelope on its own, and it needs the header to stand on its
own.

## The suite

`Source/SelfTestEnvelopeCodec.cpp` is the envelope's own contract: 31
checks that reach the envelope through the domain's headers alone - no
`MainComponent`, no window, no component tree. It pins:

- what an envelope writes: the names of the keys and the order they are
  written in - id, name, colour, points - and what a point writes (x, y);
- the default envelope: the name Flat, the orange of the palette, and the
  flat curve;
- the round trip: an envelope with everything comes back the way it went;
- the id: the fallback, and the fact that it is not clamped at all;
- the name and the colour: the defaults the file uses;
- the points: the defaults, the clamps, the cap, the order they come back
  in, and the curve an envelope with no points falls back to.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name and the
colour through two more helpers, so the move touched the suite in one place
and the checks themselves did not change.

## What the suite found

Four things worth knowing, all pinned by the suite:

- **The file's defaults are not the data's defaults.** A new envelope is
  called *Flat* and has the palette's orange (`0xffffa500`). A file without
  a name gives *Env*, and a file without a colour gives the file's own
  orange (`0xffff9f0a`). The two are different colours, and the file's
  default is the one a crafted or old file gets.
- **The id is not clamped at all.** A negative id stays negative, a huge one
  stays huge, and a fractional one is cut towards zero. The other banks
  clamp their ids; the envelope's does not.
- **A value that is not a number falls to the lower bound.** The domain's
  `clampFinite` returns the lower bound for a value that is not finite, so a
  NaN x falls to 0 and a NaN y falls to 0 as well - not to the y default of
  1, which is what a *missing* y gets.
- **The cap is applied in the file's order, before the sort.** A file with
  more than a thousand points keeps the first thousand *as the file wrote
  them*, and only then are the points sorted in time order.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `Channel.h`, `FreqSpace.h`, `Performance.h`, `Sample.h`,
  `Sieve.h`, `Tuning.h` and `Waveform.h`. They need the codec edge
  treatment, one type at a time.
- **The domain's data still holds JUCE values**: the `juce::String name` and
  the `juce::Colour` of the groups, the envelopes, the waves and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the deferred type migration (the plan's section 2),
  not this wave.
- **The page is not JUCE-free yet.** It asks for no JUCE header of its own,
  but it still sees JUCE through the types it holds (the envelope's and the
  wave's name and colour, the sieve's presets, the tuning and the theme).
- **The sample's file reading is still in the domain** (`Source/Sample.h`
  reads `juce::File`s). Reading a file is an edge of its own.
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
