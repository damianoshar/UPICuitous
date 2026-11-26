# The arc's codec edge

The architecture note for the boundary R2.2 introduced. It describes the
arc's boundary as it is after that phase, not as it is meant to be at the
end of the refactoring.

## The boundary

```
Source/Arc.h           the arc: its data, its four classical
                       transformations, its interpolation - no JUCE header
Source/ArcCodec.h      the codec edge: the two functions that read and
                       write the file format
        |
        |  the domain side knows no JUCE
        v
Source/Page.h, Source/UpicEngine.cpp, ...    the rest of the app
```

The arc is the model's central type: the drawn pitch curve. It was data
and rules - its points, its material, its settings, the four classical
transformations of a drawn curve (the transposition, the inversion, the
retrograde, the time scale) and the interpolation the engine reads per
sample (the log-domain pitch, the life over the arc's own span, the
pressure, and the cursor that follows the time).

It carried one thing that was neither: the file format. `juce::var toVar()`
and `static Arc fromVar()` were members of the class, so the JUCE value
types - and with them the JUCE umbrella header - were inside the arc.

## What moved

The two members became two free functions:

| before | after |
|---|---|
| `Arc::toVar()` | `upic::arcToVar (const Arc&)` |
| `Arc::fromVar (const juce::var&)` | `upic::arcFromVar (const juce::var&)` |

The bodies are the old members, body for body. The property names, their
order, the optional keys a default arc does not write and the clamps a
crafted file gets are the file format itself, and they did not change by a
character.

The rest of the class lost JUCE in the same step: the three numeric
helpers it used (`juce::jmax`, `juce::jmin`, `juce::jlimit`) are the
standard library's, with the same expressions and therefore the same
results - the same substitution R2.1 made in the numeric core.

## What did not move

The plan keeps the JUCE value types at the codec edge (R2): the piece
format is JSON, and `juce::var` is the value type that carries it. What
changed is which side of the edge the JUCE types live on, not the format
itself. A piece file written today is byte for byte the piece file written
before the phase.

## The suite

`Source/SelfTestArc.cpp` is the arc's own contract: 87 checks that reach
the arc through the domain's headers alone - no `MainComponent`, no
window, no component tree. It pins:

- the arc a model starts with (the level, the material, the polyagogy, the
  continuum, the page's own space);
- the four transformations, with their edges: an empty arc, a single-point
  arc, a zero or non-finite factor, and the clamps that keep a curve
  inside the page and inside the model's range;
- the interpolation: the log-domain pitch (the middle of a glissando is
  the geometric mean, not the arithmetic one), the life over the arc's own
  span, the pressure, and the cursor that follows the time forwards and
  backwards;
- the codec: the round trip of every field, the defaults of an arc that is
  not there, the clamps a crafted file gets, the points that come back in
  time order, and the file's shape - the short keys (`sd`, `ampTable`,
  `mod`, `ch`, `sp`) and the optional keys a default arc does not write.

The last part is the reason the suite is written this way: the property
names are the file format. A rename in the codec would silently drop
everything a piece carries, and no golden render would notice. The suite
reaches the codec through two helpers at the top of the file, so the move
that renamed the codec's entry points touched the suite in one place only.

The suite came first: it was written against the arc as it was, and it
passes unchanged after the move.

## The gate

The arc is the gate's third JUCE-free group, so the boundary cannot be
broken quietly: a JUCE header in `Arc.h` fails the gate before the build.
The negative test was run on the tree - `#include <JuceHeader.h>` in
`Arc.h` fails the gate with the file and the line named.

## What is not covered yet

- **The other domain headers still include JUCE**: `Envelope.h`, `Page.h`,
  `Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`, `Waveform.h`, `ArcGroup.h`,
  `Channel.h`, `Performance.h` and `FreqSpace.h`. They carry their own
  `toVar` / `fromVar` members, so they need the same treatment, one type
  at a time. The page's codec is the next one that matters: it holds the
  arcs.
- **The arc's geometry is still in the same file as its data.** The WP8
  transforms (`ArcFrame`, `ArcBounds`, the rotation, the mirrors, the
  scale, the alignment, the repeat) are geometry operations on a drawing,
  and the review's R012 asks for them to be separated from the data.
- **There is no headless domain target yet.** The suite is JUCE-free in
  its own code, but it runs inside the app's selftest binary, which is a
  JUCE application. The review's R089 - a domain-only target that links
  no GUI - is still to come, and this suite is what it would run.
