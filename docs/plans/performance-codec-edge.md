# The performance sequence's codec edge

The architecture note for R2.12. It describes the boundary R2.12 introduced -
the performance sequence's data and the performance sequence's file format -
as it is after that phase, not as it is meant to be at the end of the
refactoring.

## The boundary

```
Source/Performance.h      the performance sequence: the point, the id, the
                          name, the points and the loop flag - and the
                          behaviour: the length of the sequence, the
                          position the performance is at, the loop time and
                          the modulation factor.
Source/PerformanceCodec.h the codec edge: the two functions that read and
                          write the file format of a sequence
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...       the rest of the app
```

A performance sequence is the model's own type: the recorded movement
through the score. What a piece file carries of it is a different thing -
the edge, where the JUCE value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `PerformanceSequence::toVar()` | `upic::performanceSequenceToVar (const PerformanceSequence&)` |
| `PerformanceSequence::fromVar (const juce::var&, int)` | `upic::performanceSequenceFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `s.` the free function needs and the
whitespace. The property names, their order, the optional key a looping
sequence does not write, the point's own three keys, the clamps a crafted
file gets, the cut of the name and the sort of the points are the file
format itself, and they did not change by a character.

The codec edge now holds ten of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the
arc group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave), `Source/ChannelCodec.h` (the channel), `Source/SampleCodec.h`
(the sample slot), `Source/TuningCodec.h` (the tuning),
`Source/SieveCodec.h` (the sieve zone) and `Source/PerformanceCodec.h` (the
performance sequence). The piece's own writer was already a free function
(`pieceToVar` in `Source/Piece.h`).

## What did not move

The sequence holds `juce::String name`. The plan defers replacing the
domain's value types (the plan's section 2), so `Source/Performance.h` still
asks for the JUCE header and the performance sequence is **not** one of the
gate's JUCE-free groups. The gate's five groups are the same five as before
this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The sequence's behaviour stayed where it was: `lengthSec`,
`sequencePositionAt`, `sequenceLoopTime` and `modulationFactor` are the
sequence's behaviour, not its file format.

**The header now stands on its own.** `Performance.h` uses
`kMaxPageSeconds` but used to ask for `UpicConstants.h` only through the
include order of the files that happened to include it first. It now asks
for it itself - a fix the phase's suite found.

## The suite

`Source/SelfTestPerformanceCodec.cpp` is the performance sequence's own
contract: 48 checks that reach the sequence through the domain's headers
alone - no `MainComponent`, no window, no component tree. It pins:

- what a sequence writes: the three keys it always has - id, name, points -
  the order they are written in, and the one that comes only when it carries
  something (loop);
- what a point writes: its three keys - t, page, pos - and their order;
- the round trip: a sequence with everything comes back the way it went;
- the defaults the file uses;
- the id: the fallback, and the clamps;
- the name: the default, and the cut;
- the loop flag;
- the points: the clamps, the cap, the order they come back in, and the
  values that are not numbers.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name through one
more helper, so the move touched the suite in one place and the checks
themselves did not change.

## What the suite found

Six things worth knowing, all pinned by the suite:

- **The name is cut to 64 characters**, and an empty name becomes
  `"Sequence"`. The sample slot's and the tuning's names are not cut at all;
  this one is.
- **The id is clamped to 1..1000000.** An explicit `id: 0` becomes **1** and
  does *not* take the fallback - the fallback applies only when `id` is
  absent. (The reader's `if (s.id <= 0)` line is unreachable, because the
  clamp's lower bound is already 1; it was kept as it is.)
- **The loop flag is written only when the sequence does not loop.** A
  looping sequence carries no flag at all - the flag's absence *is* "it
  loops".
- **The points come back in time order.** The file's order is not kept: the
  points are sorted by their time (with `std::sort`, so points that share a
  time may swap places).
- **The point's time and the point's position have different caps, both
  3600 s.** The time is capped by `kMaxSequenceSeconds` (an hour of
  performance) and the position by `kMaxPageSeconds` (the length of a
  page). They happen to be the same number today, and they are two different
  limits.
- **A point with nothing in it is at the start of the first page**, and a
  point whose time or position is not a number is there too (a NaN falls to
  the lower bound).

The clamps, for the record: the time 0..3600 s, the position 0..3600 s, the
page 0..99 (`kMaxPages - 1`), and the points at most 20000
(`kMaxSequencePoints` - an eleven-minute gesture at the 30 Hz recorder).

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `FreqSpace.h`. It needs the codec edge treatment, one type at
  a time.
- **The domain's data still holds JUCE values**: the `juce::String name` and
  the `juce::Colour` of the groups, the envelopes, the waves and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the deferred type migration (the plan's section 2),
  not this wave.
- **The page is not JUCE-free yet.** It asks for no JUCE header of its own,
  but it still sees JUCE through the types it holds (the envelope's and the
  wave's name and colour, the sieve's presets, the tuning and the theme).
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
