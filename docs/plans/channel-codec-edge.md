# The channel's codec edge

The architecture note for R2.8. It describes the boundary R2.8 introduced -
the channel's data and the channel's file format - as it is after that phase,
not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Channel.h         the channel: the id, the name, the colour, the gain,
                          the mute, the solo, the gain curve and the lookup
                          the mixer walks. Next to it, the resolved mixer
                          (ChannelMix) and the build of it.
Source/ChannelCodec.h    the codec edge: the two functions that read and
                          write the file format of a channel
        |
        |  the codec edge keeps the JUCE value types
        v
Source/Piece.h, ...       the rest of the app
```

A channel is the model's own type: a named mixer channel with a gain, a
mute, a solo and a page-long gain curve. What a piece file carries of it is
a different thing - the edge, where the JUCE value types live.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Channel::toVar()` | `upic::channelToVar (const Channel&)` |
| `Channel::fromVar (const juce::var&, int)` | `upic::channelFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `c.` the free function needs and the
whitespace. The property names, their order, the keys that are left out when
they are at their default, the clamps a crafted file gets and the cap a
curve over the limit gets are the file format itself, and they did not
change by a character.

The codec edge now holds six of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page), `Source/ArcGroupCodec.h` (the arc
group), `Source/EnvelopeCodec.h` (the envelope), `Source/WaveformCodec.h`
(the wave) and `Source/ChannelCodec.h` (the channel). The piece's own writer
was already a free function (`pieceToVar` in `Source/Piece.h`).

## What did not move

The channel still holds `juce::String name` and `juce::Colour colour`. The
plan defers replacing the domain's value types (the plan's section 2), so
`Source/Channel.h` still asks for the JUCE header and the channel is **not**
one of the gate's JUCE-free groups. The gate's five groups are the same five
as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The channel's behaviour stayed where it was: the allocation-free
`envelopeAt` lookup, the resolved mixer (`ChannelMix`, with its 64-point
LUT) and the build of it (`buildChannelMix`) are the channel's behaviour,
not its file format.

## One fix comes with it

`Source/Piece.h` holds a `std::vector<Channel>` but did not include
`Source/Channel.h` - it got the type because `Source/UpicModel.h` includes
`Channel.h` *before* it includes `Piece.h`. The header now says what it uses
(`#include "ChannelCodec.h"`, which carries `Channel.h` with it), so it
stands on its own. No behaviour changes.

## The suite

`Source/SelfTestChannelCodec.cpp` is the channel's own contract: 54 checks
that reach the channel through the domain's headers alone - no
`MainComponent`, no window, no component tree. It pins:

- what a channel writes: the three keys it always has - id, name, colour -
  the four it writes only when they are not at their default - gain, mute,
  solo, env - the order they are written in, and the fact that a gain is a
  **double** in the file;
- the default channel: the name Channel, the blue of the palette, unity
  gain, and no curve;
- the round trip: a channel with everything comes back the way it went;
- the id: the fallback, and the clamps;
- the name and the colour: the defaults the file uses, the empty name, and
  the 64-character cut;
- the gain, the mute and the solo: their defaults and their clamps;
- the curve: the defaults of a point, the clamps, the cap, the order the
  points come back in, and the round trip.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name and the
colour through two more helpers, so the move touched the suite in one place
and the checks themselves did not change.

## What the suite found

Six things worth knowing, all pinned by the suite:

- **The channel's id is clamped.** It is pulled into `1..1000000`, so a
  negative id becomes one and a huge one becomes the last id. The
  envelope's and the wave's ids are not clamped at all.
- **An id of zero becomes one - it does not take the fallback.** The
  fallback is used only when the file has no `id` at all; a file that says
  `0` gets the clamp, not the fallback. (The second line of the reader -
  `if (c.id <= 0) c.id = jmax (1, fallbackId);` - cannot run, because the
  clamp above already left the id at one or above. It is the format as it
  was, and the move kept it.)
- **The keys are conditional.** Only `id`, `name` and `colour` are always
  written. `gain`, `mute`, `solo` and `env` come only when they carry
  something: a gain of exactly 1.0 is not written, a mute of false is not
  written, and a channel with no curve has no `env` key at all. The file is
  smaller, and a file read back gives the same channel.
- **An empty name falls back to "Channel".** The envelope's and the wave's
  empty names stay empty; the channel's does not. A name longer than 64
  characters is cut to the first 64.
- **The file's colour default is the data's colour default.** The channel is
  the first bank where the two agree: a file without a colour gets the same
  blue (`0xff5ac8fa`) a new channel has.
- **A point without a `y` gets 1, not 0.** The reader's default for a
  missing `y` is 1 - the flat curve - while a fresh `EnvPoint` has `y` 0.
  The channel's curve is read with the envelope's defaults, not with the
  point's own.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `FreqSpace.h`, `Performance.h`, `Sample.h`, `Sieve.h` and
  `Tuning.h`. They need the codec edge treatment, one type at a time.
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
