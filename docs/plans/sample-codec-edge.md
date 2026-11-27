# The sample slot's codec edge

The architecture note for R2.9. It describes the boundary R2.9 introduced -
the sample slot's data and the sample slot's file format - as it is after
that phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Sample.h         the sample slot: the id, the name, the rate, the
                        shared audio buffer, the direction, the base pitch,
                        the level, the gain, the trim, the fades, the
                        region's lookup and the material's edits.
Source/SampleCodec.h    the codec edge: the PCM16 conversion and the three
                        functions that read and write the file format of a
                        slot
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...     the rest of the app
```

A sample slot is the model's own type: one loadable piece of material, with
its rate, its base pitch, its level, its trim and its fades. What a piece
file carries of it is a different thing - the edge, where the JUCE value
types live.

## What moved

The members became free functions:

| before | after |
|---|---|
| `SampleSlot::toVar()` | `upic::sampleSlotToVar (const SampleSlot&)` |
| `SampleSlot::toVar (bool includeAudio)` | `upic::sampleSlotToVar (const SampleSlot&, bool includeAudio)` |
| `SampleSlot::fromVar (const juce::var&, int)` | `upic::sampleSlotFromVar (const juce::var&, int fallbackId)` |
| `SampleSlot::toPcm16()` | `upic::sampleSlotToPcm16 (const SampleSlot&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `s.` the free functions need and the
whitespace. The property names, their order, the keys that come only when
they carry something, the clamps a crafted file gets, the size checks the
payload gets and the PCM conversion are the file format itself, and they did
not change by a character.

`toPcm16` came along because it *is* the format: it is the PCM16 encoding
the file's base64 carries, and the recovery snapshot writes the same bytes
to its sidecar. Leaving it in the data header would have left half the
format behind.

The codec edge now holds seven of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the arc
group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave), `Source/ChannelCodec.h` (the channel) and
`Source/SampleCodec.h` (the sample slot). The piece's own writer was
already a free function (`pieceToVar` in `Source/Piece.h`).

## What did not move

The slot still holds `juce::String name` and a `juce::AudioBuffer`. The
plan defers replacing the domain's value types (the plan's section 2), so
`Source/Sample.h` still asks for the JUCE header and the sample slot is
**not** one of the gate's JUCE-free groups. The gate's five groups are the
same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The slot's behaviour stayed where it was: `loaded`, the region's lookup
(`regionStart`, `regionEnd`, `regionFrames`, `durationSec`), the peak,
`normalise`, `applyFade` and `resetEdits` are the material's behaviour, not
its file format.

**The sample's file reading is still in the domain.** `loadSampleFile` in
`Source/Sample.h` reads a `juce::File` and decodes it. Reading a file is an
edge of its own, and it is not this one.

## The suite

`Source/SelfTestSampleCodec.cpp` is the sample slot's own contract: 71
checks that reach the slot through the domain's headers alone - no
`MainComponent`, no window, no component tree. It pins:

- what a slot writes: the twelve keys it always has - id, name, sampleRate,
  ref, level, gain, loopStart, loopEnd, fadeIn, fadeOut, channels, frames -
  the order they are written in, and the two that come only when they carry
  something (reversed, data);
- the default slot: no name, 44.1 kHz, the default base pitch, no trim, no
  fades, and no audio;
- the round trip: a slot with everything comes back the way it went, and a
  slot with audio comes back with its audio, to the nearest step;
- the id: the fallback, and the clamps;
- the name and the rate: the defaults the file uses and the clamps;
- the direction: absent means forwards;
- the base pitch, the level and the gain: the defaults the file uses and the
  clamps;
- the trim and the fades: the defaults and the clamps;
- the audio: the frames and the channels, a payload shorter than the frames
  it claims, a payload longer than the size it claims allows, and a frame
  count over the duration cap.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name through one
more helper, so the move touched the suite in one place and the checks
themselves did not change.

## What the suite found

Six things worth knowing, all pinned by the suite:

- **The id is clamped to the bank's size.** The slot's id is pulled into
  `1..8` (`kMaxSampleSlots`) - the tightest id range of any bank. The
  envelope's and the wave's ids are not clamped at all.
- **The file's default base pitch is 0, not the data's 261.63.** A file
  without a `ref` means "not set": a piece written before M5 carries no Ref
  on the slot, and the loader then gives the slot the Ref of the first arc
  that uses it - so such a piece sounds exactly as it did. A fresh slot
  starts at the app's default. The two defaults are not the same, and the
  file's is the one an old piece gets.
- **The name is not cut.** The channel's name is cut to 64 characters; the
  slot's is not cut at all.
- **The gain has a floor of 0.01, not 0.** A file that says `gain: 0` gets
  0.01 - the material is never silenced by the file. The level, on the
  other hand, may be 0.
- **The audio is guarded twice.** The base64's length is checked against
  the size the file claims *before* it is decoded, so an oversized payload
  is rejected without ever being expanded; and the decoded size must cover
  the claimed frames, so a payload shorter than it claims is refused too.
  Either way the slot comes back without audio.
- **The frame count is clamped before anything is allocated.** The cap is
  the duration - 60 s at the material's *own* rate - and the memory budget.
  A frame count over the cap is *cut*, not refused: the slot comes back
  with the capped number of frames.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `FreqSpace.h`, `Performance.h`, `Sieve.h` and `Tuning.h`. They
  need the codec edge treatment, one type at a time.
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
