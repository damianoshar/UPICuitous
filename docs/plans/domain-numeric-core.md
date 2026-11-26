# The domain's numeric core

The architecture note for the boundary R2.1 introduced. It describes the
domain's numeric core as it is after that phase, not as it is meant to be at
the end of the refactoring.

## The boundary

```
Source/UpicConstants.h    the units: the frequency range, the page's length,
Source/PieceLimits.h      the wavetable's size, the sample slots, the id
Source/UpicMath.h         range, the default ambitus
Source/UpicMath.h         the conversions and the lookups
Source/Ids.h              the id helpers
        |
        |  no JUCE header, no GUI header
        v
Source/Arc.h, Source/Page.h, Source/Piece.h, ...   the rest of the domain
```

Four small headers carry the domain's numbers. They are the foundation the
rest of the model stands on: every frequency, every limit and every id in a
piece passes through them. They included the JUCE umbrella header for three
numeric helpers - `juce::jmax`, `juce::jmin` and `juce::jlimit` - and
nothing else.

After R2.1 they include no JUCE header at all, and the gate checks it: the
boundary check's second group is this one.

## What the headers own

- **`UpicConstants.h`** - the units. The frequency range (0.01 Hz to
  22,050 Hz - the range the 1990s UPIC reached), the default page's ambitus
  (20 Hz to 12,000 Hz - the interval the clone has always drawn in), the
  page's length (5 ms to an hour), the wavetable's size and the sample
  slots. It was already JUCE-free; it is in the rule because the rule is
  about the *group*.
- **`PieceLimits.h`** - the validation. A piece file is untrusted input, so
  every parser clamps against these numbers: the file size, the sample
  budget, the id range, the array sizes, the sample rate. The helpers here
  (`clampFinite`, `clampIntensity`, `clampGain`) are the ones that reject a
  NaN - JUCE's `jlimit` returns a NaN unchanged, which would let a NaN read
  from a file reach the audio thread.
- **`UpicMath.h`** - the conversions and the lookups. MIDI to frequency, the
  frequency to the page's pitch space (logarithmic, so a glissando is
  straight), the page's own interval, the magnetic snap, the time grid, the
  stop fade, and the segment search the audio thread runs per sample (the
  binary search and the cursor that follows the time).
- **`Ids.h`** - the id helpers. `uniquifyIds` gives a duplicate or an
  impossible id a free one, `hasId` answers whether an id is there, and
  `nextBankId` finds the first hole in a bank.

## The three replacements

The numeric helpers are JUCE's, and the replacements are the standard
library's - with the same expressions, so the same results:

| JUCE | standard | the expression both use |
|---|---|---|
| `jmax (a, b)` | `std::max (a, b)` | `a < b ? b : a` |
| `jmin (a, b)` | `std::min (a, b)` | `b < a ? b : a` |
| `jlimit (lo, hi, v)` | `std::clamp (v, lo, hi)` | `v < lo ? lo : (hi < v ? hi : v)` |

The NaN behaviour is the same in both, which is the part that matters: all
three leave a NaN unchanged, and the domain's own `clampFinite` is the one
that refuses it.

`std::clamp` also drops JUCE's debug assertion that the range is the right
way round. No call site passes a reversed range - the gate would have said
so - so the assertion never fired and nothing changes.

## The suite

`Source/SelfTestDomain.cpp` is the domain's own suite - 61 checks. It
exercises the domain through the domain's headers alone - no `MainComponent`,
no window, no component tree (the harness it reports through is still the
app's). It pins:

- the units: MIDI and frequency (A4 is 440 Hz, the round trip, the
  clamping), the pitch space (the ends, the geometric mean in the middle,
  the clamping of the position), the page's own interval (the ends map to
  the ends, a pitch outside the interval is *off the page* and not clamped);
- the validation: the clamps (below, inside, above), the NaN rule, the
  infinities, the gain and the intensity clamps;
- the lookups: the segment search (the first point at or after the key, the
  key before the first point, the key past the last one), the cursor
  (forwards, backwards, and the same answer as the search);
- the ids: the duplicate that gets a free id, the impossible id that gets
  replaced, the lookup, and the bank's first hole.

Those checks did not exist before: the units and the lookups were only
covered indirectly, through the app. This is the first suite that pins the
domain's behaviour without the app around it - the groundwork for the
review's R089 (a domain-only headless target).

## What is not covered yet

- **`Arc.h` still includes JUCE.** It needs it for the `juce::var`
  serialization - the codec edge, where the plan keeps the JUCE value
  types. Moving that serialization out of the class is R2.2.
- **The other domain headers still include JUCE**: `Envelope.h`,
  `Page.h`, `Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`, `Waveform.h` and
  the rest. They carry their own `toVar` / `fromVar` members, so they need
  the same treatment, one type at a time.
- **The plugin still depends on the desktop shell.** That is R7's job.
- **There is no headless domain target yet.** The suite is JUCE-free in its
  own code, but it runs inside the app's selftest binary, which is a JUCE
  application. The review's R089 - a domain-only target that links no GUI -
  is still to come, and this suite is what it would run.
