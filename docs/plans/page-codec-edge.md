# The page's codec edge

The architecture note for R2.4. It describes the boundary R2.4 introduced -
the page's data and the page's file format - as it is after that phase, not
as it is meant to be at the end of the refactoring.

## The boundary

```
Source/Page.h          the page: its data and its rules - the arcs, the
                       zones, the four frequency spaces, the groups, the
                       ambitus, the warp and the time map. It asks for no
                       JUCE header of its own.
Source/PageCodec.h     the codec edge: the two functions that read and
                       write the file format of a page
        |
        |  the domain side asks for no JUCE header
        v
Source/Piece.h, Source/MainComponentPiece.cpp, ...    the rest of the app
```

The page is the model's surface: the arcs, the sieve zones, the four
frequency spaces, the groups, the ambitus and the page's own time. How all
of that reaches a file is a different thing from what a page is.

It carried one thing that was neither: the file format. `juce::var toVar()`
and `static Page fromVar()` were members of the class, so the JUCE value
types - and with them the JUCE umbrella header - were inside the page.

The piece's own writer was already a free function (`pieceToVar` in
`Source/Piece.h`); the page's is now one too, and the two edges sit side by
side: `Source/ArcCodec.h` and `Source/PageCodec.h`.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Page::toVar()` | `upic::pageToVar (const Page&)` |
| `Page::fromVar (const juce::var&)` | `upic::pageFromVar (const juce::var&, int* renamedIds)` |

The bodies are the old members, body for body. The property names, their
order, the optional keys a default page does not write and the clamps a
crafted file gets are the file format itself, and they did not change by a
character.

The rest of the class lost JUCE in the same step: the numeric helpers it
used (`juce::jlimit`, `juce::jmax`, `juce::jmin`) are the standard
library's, with the same expressions and therefore the same results - the
same substitution R2.1 made in the numeric core and R2.2 made in the arc.
`Source/Page.h` no longer includes `<JuceHeader.h>`.

## What the page still sees

The page holds types that carry JUCE values of their own: a group has a
`juce::String name` and a `juce::Colour`, a zone and a table have theirs.
The page's header asks for no JUCE header - the JUCE it still sees comes
from the types it holds, not from its own code. That is the next step of
the wave: the `juce::String` and the `juce::Colour` leave the domain's
data, and the page becomes JUCE-free in the whole sense.

The gate says what it means. `Source/Page.h` is the gate's fifth group:

```
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

- **B1** - the page's header includes no JUCE header.
- **B2** - the page's header includes only the domain's headers it holds:
  the arc, the geometry, the group, the envelope, the frequency space, the
  ids, the limits, the sieve, the units, the theme and the waveform.

A JUCE header in `Source/Page.h` fails the gate before the build, and so
does a shell header the page has no business knowing.

## The suite

`Source/SelfTestPageCodec.cpp` is the edge's own contract: 73 checks that
reach the page through the domain's headers alone - no `MainComponent`, no
window, no component tree. It pins:

- what a page writes: the names of the keys and the order they are written
  in - duration, tempo, nextArcId, arcs, zones, nextZoneId, ambLo, ambHi,
  spaces, spaceTables, warp, groups, nextGroupId;
- what a page leaves out: every key that is only written when the page is
  not the default, so an old piece's file is byte for byte what it was;
- the clamps a crafted file gets: the duration, the tempo, the warp, the
  ambitus (and the ambitus that is too narrow), the spaces and the drawn
  tables;
- the ids: the duplicate that is made unique, the counter that is derived
  from the arcs that are there, the file's counter when it is sane and
  higher, and the renames that are reported;
- the caps: the arcs, the zones and the groups;
- the group's membership: only the arcs that are on the page, no
  duplicates, one group per arc;
- the round trip: a page with everything comes back the way it went, and a
  page written twice has the same keys.

The suite came first: it was written against the codec where it was, and it
passes unchanged after the move. It reaches the codec through one door
(`encode` / `decode` at the top of the file), so the move touched the suite
in one place.

## What the suite found

Two things came out of writing it:

- **`Source/Page.h` called `uniquifyIds()` without including `Ids.h`.** It
  only compiled because every file that reached the page included
  `UpicModel.h` first, and the model included `Ids.h` before `Page.h`. The
  new suite reaches the page directly, and the compiler said so. The page
  now includes what it uses.
- **A renamed group is not reported.** The counter the loader hands back
  counts the arcs and the zones; the groups are counted after the counter
  was read. The suite pins the behaviour as it is - a refactoring phase does
  not change what a piece load tells the user - and the note is here so
  that the next phase can decide whether it is a bug worth fixing on its
  own.

## What is not covered yet

- **The other domain headers still carry their own `toVar` / `fromVar`
  members**: `Envelope.h`, `Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`,
  `Waveform.h`, `ArcGroup.h`, `Channel.h`, `Performance.h` and
  `FreqSpace.h`. They need the codec edge treatment, one type at a time.
- **The domain's data still holds JUCE values**: the `juce::String name` and
  the `juce::Colour` of the groups, the envelopes, the waveforms and the
  channels, the `juce::String name` of the sample slots, the sequences and
  the tunings. That is the rest of the review's R020.
- **The sample's file reading is still in the domain** (`Source/Sample.h`
  reads `juce::File`s). Reading a file is an edge of its own.
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
