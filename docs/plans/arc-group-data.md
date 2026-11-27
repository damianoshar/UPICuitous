# The arc group's codec edge

The architecture note for R2.5. It describes the boundary R2.5 introduced -
the arc group's data and the arc group's file format - as it is after that
phase, not as it is meant to be at the end of the refactoring.

## The boundary

```
Source/ArcGroup.h        the group: the id, the name, the colour, the
                         membership, the mute, the solo, the gain, the
                         transpose and the four bank ids - and the
                         render-time override beside it.
Source/ArcGroupCodec.h   the codec edge: the two functions that read and
                         write the file format of a group
        |
        |  the codec edge keeps the JUCE value types
        v
Source/PageCodec.h, ...  the rest of the app
```

A group is the model's own type: a named set of arcs on the page with
shared, non-destructive attributes - mute, solo, gain, transpose and the
timbre/channel overrides. The arcs' own data is never changed; the group is
resolved into a render-time override at snapshot publication.

What it carried that was not the model: the file format of a group.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `ArcGroup::toVar()` | `upic::arcGroupToVar (const ArcGroup&)` |
| `ArcGroup::fromVar (const juce::var&)` | `upic::arcGroupFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body. The property names, their
order, the optional keys a default group does not write and the clamps a
crafted file gets are the file format itself, and they did not change by a
character.

The codec edge now holds three of them side by side: `Source/ArcCodec.h`
(the arc), `Source/PageCodec.h` (the page) and `Source/ArcGroupCodec.h`
(the group). The piece's own writer was already a free function
(`pieceToVar` in `Source/Piece.h`).

## What did not move

The group still holds `juce::String name` and `juce::Colour colour`. A
first cut of this phase replaced them with text and an ARGB number, which
made the group JUCE-free and let it join the gate's groups - but that is
the type migration the plan defers (the plan's section 2), so it was taken
back. The separation stays; the data keeps what it had.

The consequence is honest and visible: `Source/ArcGroup.h` still asks for
the JUCE header, and the group is **not** one of the gate's JUCE-free
groups. The gate's five groups are the same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

The page's group says exactly what it checks: `Page.h` asks for **no JUCE
header of its own**. The JUCE the page still sees comes from the types it
holds - the group's name and colour among them - not from its own code.

## The checker learned one thing

An empty permitted list is a list: it forbids every project header. The
old test skipped the check when the list was empty - an empty array is
falsy in PowerShell - so a group that may include no project header at all
could not be expressed. No group uses one yet (the leaf rule is for the day
the domain's value types move), but the rule is now correct, and two
self-tests cover it: the checker's own tests are 20 now.

## The suite

`Source/SelfTestArcGroup.cpp` is the group's own contract: 43 checks that
reach the group through the domain's headers alone - no `MainComponent`, no
window, no component tree. It pins:

- what a group writes: the names of the keys and the order they are written
  in - id, name, colour, arcs, mute, solo, gain, transpose, wave, env,
  ampTable, channel;
- what a group leaves out: every key that is only written when the group is
  not the default;
- the id: the fallback, the clamp, and the numbers that are not ids (0, a
  negative one, a fractional one, one above the cap);
- the name: the default, the empty name and the 64-character cut;
- the colour: the default and the one the file carries;
- the membership: the ids that are dropped, the duplicates and the cap;
- the clamps: the gain, the transpose and the four bank ids;
- the round trip: a group with everything comes back the way it went.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file) and reads the name and the
colour through two more helpers, so the move touched the suite in one place
and the checks themselves did not change.

## What the suite found

Three things worth knowing, all pinned by the suite:

- **A value that is not a number falls to the lower bound.** The domain's
  `clampFinite` returns the lower bound for a value that is not finite, so a
  NaN gain becomes 0 and a NaN transpose becomes -48. That is what the file
  has always done.
- **An id of zero is 1, not the fallback.** The clamp runs first, so the
  fallback only matters when the file carries no id at all.
- **The colour is an ARGB number, and it stays one.** The file wrote
  `(int) colour.getARGB()` and reads `(uint32) (int) value` - a negative
  number in the file, and the same colour back.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `Envelope.h`, `Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`,
  `Waveform.h`, `Channel.h`, `Performance.h` and `FreqSpace.h`. They need
  the codec edge treatment, one type at a time.
- **The domain's data still holds JUCE values**: the `juce::String name`
  and the `juce::Colour` of the groups, the envelopes, the waves and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the deferred type migration (the plan's section 2),
  not this wave.
- **The sample's file reading is still in the domain** (`Source/Sample.h`
  reads `juce::File`s). Reading a file is an edge of its own.
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
