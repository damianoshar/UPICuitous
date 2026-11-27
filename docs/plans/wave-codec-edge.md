# The wave's codec edge

The architecture note for R2.7. It describes the boundary R2.7 introduced -
the wave's data and the wave's file format - as it is after that phase, not
as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Waveform.h         the wave: the id, the name, the colour, the table
                          of samples, the generators and the lookup the
                          oscillator reads.
Source/WaveformCodec.h    the codec edge: the two functions that read and
                          write the file format of a wave
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...       the rest of the app
```

A wave is the model's own type: a named table of samples the oscillator
reads. What a piece file carries of it is a different thing - the edge, where
the JUCE value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Waveform::toVar()` | `upic::waveformToVar (const Waveform&)` |
| `Waveform::fromVar (const juce::var&, int)` | `upic::waveformFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `w.` the free function needs and the
whitespace. The property names, their order, the defaults a file without
them falls back to and the cap a table over the limit gets are the file
format itself, and they did not change by a character.

The codec edge now holds five of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the arc
group), `Source/EnvelopeCodec.h` (the envelope) and `Source/WaveformCodec.h`
(the wave). The piece's own writer was already a free function
(`pieceToVar` in `Source/Piece.h`).

## What did not move

The wave still holds `juce::String name` and `juce::Colour colour`. The plan
defers replacing the domain's value types (the plan's section 2), so
`Source/Waveform.h` still asks for the JUCE header and the wave is **not** one
of the gate's JUCE-free groups. The gate's five groups are the same five as
before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The wave's generators and its lookup (`valueAt`) stayed where they were: they
are the wave's behaviour, not its file format. `waveFromTake` - the
digitising of a recording into a wave - stayed too, for the same reason.

## The suite

`Source/SelfTestWaveCodec.cpp` is the wave's own contract: 29 checks that
reach the wave through the domain's headers alone - no `MainComponent`, no
window, no component tree. It pins:

- what a wave writes: the names of the keys and the order they are written in
  - id, name, colour, samples - the length of the table, and the fact that a
  sample is a **double** in the file;
- the default wave: the name Sine, the cyan of the palette, and the table of
  1024 zeros;
- the round trip: a wave with everything comes back the way it went, at its
  own length;
- the id: the fallback, and the fact that it is not clamped at all;
- the name and the colour: the defaults the file uses, which are not the
  defaults the data uses;
- the samples: the length the wave keeps, the cap, the empty table, the
  values that are not clamped, and the order.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name and the
colour through two more helpers, so the move touched the suite in one place
and the checks themselves did not change.

## What the suite found

Four things worth knowing, all pinned by the suite:

- **The file's defaults are not the data's defaults.** A new wave is called
  *Sine* and has the palette's cyan (`0xff00ffff`). A file without a name
  gives *Wave*, and a file without a colour gives the file's own blue
  (`0xff5ac8fa`). The file's defaults are the ones a crafted or old file
  gets, and they are not the ones a new wave has.
- **The id is not clamped at all.** A negative id stays negative, a huge one
  stays huge, and a fractional one is cut towards zero. The other banks
  clamp their ids; the wave's does not.
- **The samples are not clamped either.** A file may carry a value outside
  -1..1 and it survives the read untouched - and so does a value that is not
  a number at all. The table is the wave's, not the engine's.
- **The table keeps the length the file gave it.** The piece carries the
  samples, so a wave that was imported with 4096 samples stays a
  4096-sample wave; the table is not padded to the default size. Two
  exceptions, both pinned: an **empty** table and a file with **no** table
  leave the default table of 1024 zeros in place.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `Channel.h`, `FreqSpace.h`, `Performance.h`, `Sample.h`,
  `Sieve.h` and `Tuning.h`. They need the codec edge treatment, one type at
  a time.
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
