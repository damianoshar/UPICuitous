# The sieve zone's codec edge

The architecture note for R2.11. It describes the boundary R2.11 introduced -
the sieve zone's data and the sieve zone's file format - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Sieve.h          the sieve zone: its id, its time, its frequency
                         range, its modulus, its remainders, its time grid
                         and its stickiness - and the behaviour: the
                         containment test, the effective stickiness, the
                         preset library, the quantisation that uses the
                         zones and the freehand curve simplification.
Source/SieveCodec.h     the codec edge: the two functions that read and
                         write the file format of a zone
        |
        |  the codec edge keeps the JUCE value types
        v
Source/PageCodec.h, ... the rest of the app
```

A sieve zone is the model's own type: a translucent magnetic region on the
page. What a piece file carries of it is a different thing - the edge, where
the JUCE value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `SieveZone::toVar()` | `upic::sieveZoneToVar (const SieveZone&)` |
| `SieveZone::fromVar (const juce::var&)` | `upic::sieveZoneFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `z.` the free function needs and the
whitespace. The property names, their order, the optional keys a default
zone does not write, the order independence of the times and the
frequencies, the clamps a crafted file gets, the folding of the remainders
into the modulus and the remainder cap are the file format itself, and they
did not change by a character.

The codec edge now holds nine of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the
arc group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave), `Source/ChannelCodec.h` (the channel), `Source/SampleCodec.h`
(the sample slot), `Source/TuningCodec.h` (the tuning) and
`Source/SieveCodec.h` (the sieve zone). The piece's own writer was already
a free function (`pieceToVar` in `Source/Piece.h`).

## What did not move

The zone holds no JUCE value type of its own - its data is plain numbers
and a vector of ints. What it needed JUCE for was the file format, and that
has gone. The header still asks for the JUCE umbrella, because the
behaviour beside it uses `juce::jlimit`, `juce::jmin` and `juce::jmax` -
but the zone is **not** one of the gate's JUCE-free groups, and the gate's
five groups are the same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The zone's behaviour stayed where it was: `contains`, `effectiveStickiness`,
`zoneAt`, `quantizePoint`, `sievePresets` and `simplifyArcPoints` are the
zone's behaviour, not its file format.

## The suite

`Source/SelfTestSieveCodec.cpp` is the sieve zone's own contract: 65 checks
that reach the zone through the domain's headers alone - no `MainComponent`,
no window, no component tree. It pins:

- what a zone writes: the seven keys it always has - id, t0, t1, f0, f1, m,
  r - the order they are written in, and the two that come only when they
  carry something (timeGrid, stick);
- the order independence: the times and the frequencies are written in
  order, whichever way round the zone was drawn;
- the round trip: a zone with everything comes back the way it went;
- the defaults the file uses - which are not the defaults the data uses;
- the clamps: the time, the frequency, the modulus, the time grid and the
  stickiness;
- the values that are not numbers;
- the remainders: they are folded into the zone's modulus, they keep their
  order and their duplicates, and they are cut at the cap;
- the id: the codec does not touch it.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file), so the move touched the suite
in one place and the checks themselves did not change.

## What the suite found

Six things worth knowing, all pinned by the suite:

- **The file's default frequencies are the whole range, not 20 Hz.** A zone
  with no `f0` and no `f1` comes back spanning 0.01 Hz to 22050 Hz. A
  *fresh* zone is a point at 20 Hz. The two defaults are not the same: the
  file's default is the safe one (a zone that covers everything), the
  data's is the useful one (a zone you can see).
- **A zone with no remainders allows nothing.** The data's default is the
  C's (`{0}`); the file's default is an empty list. An empty remainder list
  is not the chromatic set - it is no set at all.
- **The times and the frequencies are written in order.** A zone drawn
  "backwards" is written forwards, and the containment test is
  order-independent anyway - so the file never carries a zone that reads
  the wrong way round.
- **The remainders are folded into the modulus, and the modulus is clamped
  first.** A zone whose file says `m: 1` comes back with `m: 2`, and its
  remainder 5 comes back as 1. A negative remainder wraps (a `-1` in a
  12-tone sieve is the 11), a fractional one is cut to a whole one, and the
  order and the duplicates are kept - the file's list is the zone's list.
- **A stick of zero is a stick.** It is written, because it means "this zone
  does not stick" - not "follow the global". A negative stick is not
  written, and it means "follow the global"; that is how a zone written
  before the per-zone stick (v1.33.2) keeps behaving the way it did.
- **The codec does not touch the id.** A zero, a negative and a huge id
  all survive the codec. The *page's* loader is what makes a piece's ids
  unique (`uniquifyIds` in `Source/Ids.h`) - the zone's codec is not where
  that happens.

The clamps, for the record: the time 0..3600 s (the page's length), the
frequency 0.01..22050 Hz (the app's range), the modulus 2..120, the time
grid 0..64 divisions per second, the stickiness 0..1, and the remainders
at most 5000. A value that is not a number falls to the lower bound - and
a stickiness that is not a number means "follow the global", because a NaN
is not "greater than or equal to zero".

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `FreqSpace.h` and `Performance.h`. They need the codec edge
  treatment, one type at a time.
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
