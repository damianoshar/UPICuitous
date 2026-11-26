# The arc's geometry

The architecture note for R2.3. It describes the separation the review's
R012 asks for - the arc's data and the arc's geometry - as it is after
that phase.

## The boundary

```
Source/Arc.h           the arc's DATA: the points, the material, the
                       settings, the four classical transformations, the
                       interpolation the engine reads per sample
Source/ArcGeometry.h   the arc's GEOMETRY: the page as a surface - the
                       frame, the bounds of a scope, the pitch centre, the
                       rule that writes a moved point back, the rotation,
                       the two mirrors, the scale, the alignment, the
                       repeat
        |
        |  no JUCE header, no GUI, no app header beyond the arc it moves
        v
Source/Page.h, Source/GraphicalCurve.h, Source/MainComponent.cpp, ...
```

The review asks for the two to be separate (R012, "Separate Arc data from
geometry operations"). The reason is the same one that made the layout
engines their own module: a type that carries both its data and the
operations on it cannot be tested without the other, and the operations
belong to the page, not to the arc.

An arc is a drawn pitch curve: where it is, how loud it is, which wave it
plays. The geometry is what the page does to a drawing - turn it, mirror
it, line it up, stretch it, repeat it - and it works in the page's own
normalised space:

```
x = t / duration                              (the time)
y = (log2 f - log2 lo) / (log2 hi - log2 lo)   (the pitch, in octaves of
                                              the page's ambitus)
```

In that space a rotation or a scale does not depend on the zoom, and a
rotation keeps the length of the curve.

## What moved

The whole WP8 block, out of `Source/Arc.h` and into `Source/ArcGeometry.h`,
the same text in the same order:

- `ArcFrame` - the page's normalised space, and the conversions into it
  and back out of it;
- `ArcRefs`, `ArcBounds` and `arcBounds()` - the scope and its box;
- `arcPitchCentre()` - the geometric mean of an arc's pitches;
- `writeTransformedPoint()` and `sortTransformedPoints()` - the rule that
  writes a moved point back and the rule that puts the points back in
  time order;
- `transformRotate()`, `transformMirrorTime()`, `transformMirrorPitch()`,
  `transformScale()`, `transformAlign()` and `transformRepeat()` - the
  six operations the UPIC's page could do to a drawing.

`Source/Arc.h` keeps the data: `ArcPoint`, `Arc`, the four classical
transformations of a drawn curve (the transposition, the inversion, the
retrograde, the time scale) and the interpolation the engine reads per
sample (the log-domain pitch, the life, the pressure, the cursor).

The direction is the point: the geometry knows the arc, the arc does not
know the geometry. The gate says so - `ArcGeometry.h` may include
`Arc.h`, `Arc.h` may not include `ArcGeometry.h`.

## The suite

`Source/SelfTestArcGeometry.cpp` is the geometry's own contract: 58
checks that reach it through the domain's headers alone - no
`MainComponent`, no window, no component tree. It pins:

- the frame: the times to 0..1, the ambitus to 0..1, the conversions are
  their own inverse - and the edges: a frame without time, a frame without
  an ambitus, a pitch below 1 Hz;
- the bounds of a scope: with points, without points, an empty scope, the
  union of two arcs' bounds, an arc that is not there;
- the pitch centre: the geometric mean;
- the write-back rule: a moved point is written, a point that leaves the
  page or the model's range is pulled back, a value that is not a number
  leaves the point as it was;
- the transforms: the rotation (the length of the curve is kept, the
  points stay sorted and in range, a rotation of no angle or of a broken
  angle changes nothing), the two mirrors (each is its own inverse, the
  mirror in pitch leaves the times alone, a broken axis changes nothing),
  the scale (the pitch scale in octaves, the time scale, the points stay
  in range, a zero or broken factor changes nothing), the alignment (the
  three edges, an arc that is not there, an empty scope) and the repeat
  (the copies, their own ids, the step in time and in pitch, the material
  they keep, the stability, and the three cases that make no copies).

The invariants are the point: a transform may move a curve, but it may not
leave it unsorted, out of range or not a number.

The suite came first: it was written against the geometry where it was,
and it passes unchanged after the move.

## The gate

The geometry is the gate's fourth JUCE-free group, so the boundary cannot
be broken quietly: a JUCE header in `ArcGeometry.h` fails the gate before
the build, and so does an app header the geometry has no business knowing.

## What is not covered yet

- **The other domain headers still include JUCE**: `Envelope.h`, `Page.h`,
  `Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`, `Waveform.h`, `ArcGroup.h`,
  `Channel.h`, `Performance.h` and `FreqSpace.h`. They carry their own
  `toVar` / `fromVar` members, so they need the codec edge treatment, one
  type at a time. The page's codec is the next one that matters: it holds
  the arcs.
- **The coordinate conversions are still in two places.** The frame's
  conversions live in the geometry, the model's live in `UpicMath.h`; the
  review's R015 asks for one place. The page's own interval is the piece
  that would have to move.
- **There is no headless domain target yet.** The suite is JUCE-free in
  its own code, but it runs inside the app's selftest binary, which is a
  JUCE application. The review's R089 - a domain-only target that links
  no GUI - is still to come, and this suite is what it would run.
