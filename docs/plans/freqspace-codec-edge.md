# The frequency space definition's codec edge

The architecture note for R2.13. It describes the boundary R2.13 introduced -
the frequency space definition's data and the frequency space definition's
file format - as it is after that phase, not as it is meant to be at the end
of the refactoring.

## The boundary

```
Source/FreqSpace.h       the frequency space definition: the preset, the
                         editable flag, the table, the staircase and the
                         inversion - and the behaviour: isDefault, log2At,
                         the presets, the range of a space and the
                         render-time lookup table.
Source/FreqSpaceCodec.h  the codec edge: the three functions that read and
                         write the file format of a definition
        |
        |  the codec edge keeps the JUCE value types
        v
Source/PageCodec.h, ...  the rest of the app
```

A frequency space definition is the model's own type: the page's four
frequency spaces. What a piece file carries of it is a different thing -
the edge, where the JUCE value types live.

## What moved

The three members became three free functions:

| before | after |
|---|---|
| `FreqSpaceDef::pointsToVar (const std::vector<TablePoint>&)` | `upic::freqSpacePointsToVar (const std::vector<TablePoint>&)` |
| `FreqSpaceDef::toVar (int index) const` | `upic::freqSpaceDefToVar (const FreqSpaceDef&, int index)` |
| `FreqSpaceDef::fromVar (const juce::var&)` | `upic::freqSpaceDefFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `d.` the free function needs and the
whitespace. The property names, their order, the optional keys a default
definition does not write, the Hz the points carry, the clamps a crafted
file gets, the rules a table has to follow and the fact that the space
index is written but not read are the file format itself, and they did not
change by a character.

`pointsToVar` came along because it is the format: it is how the table's
points are written, and the writer's only use of it was the table.

## The end of the sweep

The codec edge now holds eleven of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the
arc group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave), `Source/ChannelCodec.h` (the channel), `Source/SampleCodec.h`
(the sample slot), `Source/TuningCodec.h` (the tuning),
`Source/SieveCodec.h` (the sieve zone), `Source/PerformanceCodec.h` (the
performance sequence) and `Source/FreqSpaceCodec.h` (the frequency space
definition). The piece's own writer was already a free function
(`pieceToVar` in `Source/Piece.h`).

**This is the last of the domain headers that carried a file format.** A
sweep of `Source/` for `toVar` and `fromVar` now finds only two *comments*
in the codec headers - the ones that explain what the wave moved. No domain
header carries a format member any more: the whole piece format lives at
the edge.

## What did not move

The definition holds no JUCE value type of its own - its data is plain
numbers, a float and a vector of points. What it needed JUCE for was the
file format, and that has gone.

The gate's five groups are the same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The definition's behaviour stayed where it was: `isDefault` and `log2At`
are the definition's behaviour, not its file format, so they stayed in
`FreqSpace.h`.

## The suite

`Source/SelfTestFreqSpaceCodec.cpp` is the frequency space definition's own
contract: 47 checks that reach the definition through the domain's headers
alone - no `MainComponent`, no window, no component tree. It pins:

- what a definition writes: the keys it always has - space, editable - the
  ones that come only when they carry something (preset, stepped,
  divisions, inverted, points), and the order they are written in;
- what a point writes: its two keys - x, hz - and the fact that the file
  carries the frequency in Hz and not in log2;
- the round trip: a definition with everything comes back the way it went;
- the defaults the file uses;
- the clamps: the preset, the divisions, the point's `x` and the point's
  frequency;
- the rules a table has to follow: two points, and the points in order;
- the rule that the space index is written but not read.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file), so the move touched the suite
in one place and the checks themselves did not change.

## What the suite found

Six things worth knowing, all pinned by the suite:

- **The file carries the table's frequency in Hz, not in log2.** The data
  holds `log2Hz`; the writer converts back with `exp2` and the reader
  converts forward with `log2`. The file is therefore in the same units a
  person reads - and it is a *float* on the way in, so a pitch comes back
  to within a hundredth of a Hz, not exactly.
- **A table of fewer than two points is dropped whole.** The editable flag
  is turned off *and* the points are cleared: one point is not a curve, and
  a constant pitch is not what the composer asked for.
- **A table on a space that is not editable is dropped too** - the points
  are cleared whatever they are. The editable flag is what decides whether
  the table means anything.
- **The `editable` flag is always written**, even when it is false. Every
  other flag is written only when it carries something; the editable flag
  is the one key that is always there.
- **The space index is written but never read.** The definition's reader
  ignores the `space` key: the *page's* codec is what puts a table in the
  right slot (and what clamps the index to the four spaces).
- **A point with nothing in it is at `x = 0` at 440 Hz.** The `x` default is
  zero and the `hz` default is middle A.

The clamps, for the record: the preset 0..6 (the seven presets), the
divisions 0..99 (`kMaxStepDivisions`), the point's `x` 0..1, the point's
frequency 0.01..22050 Hz (the app's range), and the points at most 16384
(`kMaxTablePoints`). A value that is not a number falls to the lower bound.

## What is not covered yet

- **The domain's data still holds JUCE values**: the `juce::String name` and
  the `juce::Colour` of the groups, the envelopes, the waves and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the deferred type migration (the plan's section 2),
  not this wave.
- **The page is not JUCE-free yet.** It asks for no JUCE header of its own,
  but it still sees JUCE through the types it holds (the envelope's and the
  wave's name and colour, the sieve's presets, the tuning and the theme).
- **The file readers are still in the domain**: the Scala importer in
  `Source/Tuning.h` and the sample's `loadSampleFile`. Reading a file is an
  edge of its own.
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
