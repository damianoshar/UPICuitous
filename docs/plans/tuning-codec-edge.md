# The tuning's codec edge

The architecture note for R2.10. It describes the boundary R2.10 introduced -
the tuning's data and the tuning's file format - as it is after that phase,
not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Tuning.h         the tuning: the name, the degrees, the base note and
                        the custom flag, the degree-to-cents and
                        cents-to-degree mappings, the step and MIDI
                        conversions, the snap, the range test, the axis steps
                        and the description. Next to it, the Scala (.scl)
                        importer.
Source/TuningCodec.h    the codec edge: the two functions that read and
                        write the file format of a tuning
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...     the rest of the app
```

A tuning is the model's own type: which pitch a step of the scale is. What
a piece file carries of it is a different thing - the edge, where the JUCE
value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Tuning::toVar()` | `upic::tuningToVar (const Tuning&)` |
| `Tuning::fromVar (const juce::var&)` | `upic::tuningFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `t.` the free function needs and the
whitespace. The property names, their order, the clamps a crafted file gets,
the rules a scale has to follow and the rule that a broken tuning falls back
to the default whole are the file format itself, and they did not change by
a character.

The codec edge now holds eight of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the arc
group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave), `Source/ChannelCodec.h` (the channel), `Source/SampleCodec.h`
(the sample slot) and `Source/TuningCodec.h` (the tuning). The piece's own
writer was already a free function (`pieceToVar` in `Source/Piece.h`).

## What did not move

The tuning still holds `juce::String name`. The plan defers replacing the
domain's value types (the plan's section 2), so `Source/Tuning.h` still asks
for the JUCE header and the tuning is **not** one of the gate's JUCE-free
groups. The gate's five groups are the same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The tuning's behaviour stayed where it was: `isDefault`, `degrees`,
`baseFreq`, `degreeToCents`, `centsToDegree`, `stepToFreq`, `freqToStep`,
`midiToFreq`, `freqToMidi`, `snapToStep`, `stepInRange`, `axisStepsInRange`,
`stepFreqsInRange` and `describe` are the tuning's behaviour, not its file
format.

**The Scala (.scl) importer is still in the domain.** `parseScalaTuning` and
its helpers read a Scala file. Reading a file is an edge of its own - the
same way the sample's file reading is - and it is not this one.

## The suite

`Source/SelfTestTuningCodec.cpp` is the tuning's own contract: 46 checks
that reach the tuning through the domain's headers alone - no
`MainComponent`, no window, no component tree. It pins:

- what a tuning writes: the three keys - name, base, cents - the order they
  are written in, and the fact that a degree is a **double** in the file;
- the default tuning: no name, no scale, middle C, and the twelve degrees
  of 12-tone equal temperament;
- the round trip: a tuning with everything comes back the way it went;
- the name and the base note: the defaults the file uses, the fact that the
  name is not cut, and the base note's 0..127 clamp;
- the scale: the base degree, the rule that the degrees must rise, the
  one-octave range, the 256-degree cap, and the values that are not numbers;
- the rule that a broken tuning falls back to the default whole.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name through one
more helper, so the move touched the suite in one place and the checks
themselves did not change.

## What the suite found

Five things worth knowing, all pinned by the suite:

- **A broken scale discards the whole tuning.** The fallback is a fresh
  `Tuning{}`, not a tuning with a bad scale: the name and the base note go
  with the degrees. A piece must always load, and the rule is "the tuning
  is good or it is the default one".
- **The scale must start at the base.** The first degree has to be 0 cents,
  within a hundredth of a cent: 0.01 is accepted, 0.02 is not.
- **The degrees must strictly rise.** Two degrees the same are refused as
  firmly as one that falls.
- **The degrees live in one octave.** A degree at 1200 cents is refused -
  the octave repeats the base, so it is not a degree.
- **The base note is clamped to MIDI 0..127**, and the name is *not* length
  cut (the channel's name is).

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `FreqSpace.h`, `Performance.h` and `Sieve.h`. They need the
  codec edge treatment, one type at a time.
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
