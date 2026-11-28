# UPICuitous refactoring plan (R0–R9)

Source: `UPICuitous-docs/Refactoeing.md` - an external architectural review
of `main` at `b5368b1` (0.38.0), read 2026-11-26. It proposes 100 work items
(R001–R100), ten waves (R0–R9), a new `Source/Domain | Application | Audio |
Persistence | Presentation` tree, a modular CMake target list and a 24-PR
sequence.

This document is the repository's answer to that review: what is adopted, what
is adapted, what is deferred, and in what order. It is a plan, not a rewrite
schedule - every phase is small enough to review on its own and to abandon
without stranding the tree.

The review's own framing is right: **the split into `MainComponentSomething.cpp`
files has already happened and did not fix the concentration of responsibilities.**
Splitting the same class again into more files would repeat that mistake. The
work is to give each responsibility an owner, a lifetime and a contract.

## 1. Verified baseline

Measured on `main` at `b5368b1` (0.38.0), 2026-11-26. Total lines, as GitHub
counts them.

| File | KB | Lines |
|---|---|---|
| `Source/MainComponent.h` | 150.4 | 2,904 |
| `Source/MainComponentLayout.cpp` | 114.1 | 2,475 |
| `Source/MainComponentControls.cpp` | 111.2 | 2,691 |
| `Source/ScoreEditor.cpp` | 98.3 | 2,477 |
| `Source/MainComponentAudio.cpp` | 87.4 | 1,949 |
| `Source/MainComponentModel.cpp` | 85.7 | 2,251 |
| `Source/UpicEngine.cpp` | 76.8 | 1,632 |
| `Source/MainComponent.cpp` | 70.6 | 1,521 |
| `Source/MainComponentPiece.cpp` | 55.2 | 1,168 |
| `Source/UpicEngine.h` | 34.2 | 700 |
| `Source/UserLibrary.h` | 31.8 | 739 |
| `Source/LivePitch.h` | 31.1 | 645 |
| `Source/Arc.h` | 30.1 | 582 |

The `MainComponent` family is 13 files, **762 KB / 17,169 lines**. The test
family is 11 files, **905 KB** (`SelfTestModel.cpp` alone is 4,822 lines).

The four functions the review singles out, measured by brace matching:

| Function | Location | Lines | Review said |
|---|---|---|---|
| `resized()` | `MainComponentLayout.cpp:688` | **1,400** | ~1,500 ✓ |
| `loadFromVar()` | `MainComponentPiece.cpp:78` | **532** | ~530 ✓ |
| `renderScoreBlock()` | `MainComponentAudio.cpp:1533` | **292** | ~290 ✓ |
| `getNextAudioBlock()` | `MainComponentAudio.cpp:780` | **385** | ~750 ✗ |

`MainComponent.h` holds 18 nested types - `AudioBlock`, `TransportState`,
`OrdinaryPreviewState`, `UndoEntry`, `UpicLookAndFeel`, `AudioPreviewCallbackGate`,
`AudioMessage`, `AudioCallbackScope`, `PublishedSampleExtractPreview` and the
rest. The class is the owner of the piece, the snapshot, the engine, the
selection, the history, the transport, the preview routes, the panels and the
jobs.

Other structural facts the review depends on, verified here:

- `CMakeLists.txt:53` collects the source tree with
  `file(GLOB_RECURSE ... Source/*.cpp Source/*.h)` - no dependency boundary is
  expressed anywhere.
- `Plugin/PluginProcessor.cpp`, `Plugin/PluginEditor.cpp` and
  `Plugin/PluginSmokeTest.cpp` all `#include "MainComponent.h"` - the plugin
  cannot run without the desktop shell's header.
- All seven layout constructs (`FlowItem`, `FlowGroup`, `CardCell`, `CardLine`,
  `SpannerLayout`, `CardMetrics`, `PlacedCell`) are **local structs inside
  `resized()`** (lines 702–1479). Nothing else can see them.
- The Windows gate builds the app through **MSBuild** on
  `Builds/VisualStudio2026/UPICuitous.sln` (Projucer-generated, explicit file
  list); the plugin builds through CMake in `Builds/Plugin`; the root
  `CMakeLists.txt` serves CI and Android. **A change to the root CMake file
  alone is not covered by the Windows gate.**

## 2. Where the review and the tree differ

| Review claim | Verified | Decision |
|---|---|---|
| `getNextAudioBlock()` is ~750 lines | 385 lines | Adopt the goal, correct the size: the audio wave is justified by **ownership** (transport, snapshot, preview, device, render), not by that one function. |
| `resized()` is ~1,500 lines | 1,400 lines | Adopt - it is the single best first extraction: pure geometry, no audio, no document. |
| Move the tree into `Source/Domain/…` etc. | the Projucer project lists every file explicitly | **Adapt.** No bulk move. A file moves only when it is extracted, and each move updates the `.vcxproj` in the same commit. |
| Replace `juce::String` / `juce::var` in the domain | the piece codec *is* JSON (`juce::var`) | **Defer.** Keep JUCE value types at the codec edge. Enforce *no new* GUI dependencies instead of a type migration. The domain's data keeps its `juce::String` and `juce::Colour`; what the wave moves is the *file format* out of the data (R2.2, R2.4, R2.5, R2.6, …), type by type. |
| Enforce dependency rules with CMake targets | the gate builds through MSBuild, not CMake | **Adapt.** Enforce with a source-level dependency check in the gate first; CMake targets come later and must keep both build paths green. |
| Typed IDs (`ArcId`, `PageId`, …) | ids are plain ints in the file format | **Defer** until a specific confusion is demonstrated; if adopted, the serialized values stay ints. |
| `MainComponent.h` should reach 100–250 lines | — | **Not a quota.** The target is ownership, not a line count. |
| Plugin must not depend on the desktop shell | confirmed (`PluginProcessor.cpp:3`) | Adopt, but schedule it in R7 - it is the highest-risk change in the list and needs the plugin smoke test + pluginval gates. |

## 3. Working rules for every phase

1. **The gate is the same gate**: `.\scripts\build.ps1` must end green -
   app selftest, legacy v0.29 fixture, VST3, smoke 22/22, pluginval.
2. **The identity of the product does not move**: the golden render hash stays
   `0x6d7571bdd80e3cd9` and the legacy fixture stays `8BF28ABA7975`. A phase
   that changes either needs an explanation and an explicit decision.
3. **The selftest count never goes down**: 1,785 at the start of R0. A phase
   that removes a check must replace it with an equivalent one.
4. **Characterization before extraction** (the review's R050, and the most
   important idea in the document): a subsystem's current behaviour is written
   down as tests *before* it moves. The tests are the contract the extraction
   must keep.
5. **One phase per commit**, mechanical moves separated from semantic changes.
6. **No audio-thread allocation**: the `AllocCounter` checks stay green.
7. **No production code calls a `*ForTest` accessor.**
8. Windows-only work; no build outputs, no secrets in the tree.
9. Behaviour-preserving by default. A phase that changes what the user sees or
   hears is a feature, and belongs in a feature wave, not here.

## 4. The phases

The review's R0–R9 wave structure is sound and is adopted. What changes is the
*entry condition*: each phase starts with a characterization test, not with a
move.

| Phase | Goal | First concrete task | Review items | Exit criteria |
|---|---|---|---|---|
| **R0 Baseline** | Freeze the behaviour that must survive | Characterization tests for layout geometry, load/save round trip, transport transitions and preview routing; baseline metrics recorded here | R001–R004 | The four characterization suites exist and pass; this document carries the baseline table. |
| **R1 Low-risk extraction** | Take the pure geometry out of `MainComponent` | **Extract the card layout engine** out of `resized()` into `CardLayoutEngine` (pure, no JUCE components) | R039, R040, R054, R055, R010, R056, R089 | `resized()` applies a result instead of computing it; the layout characterization suite passes unchanged. |
| **R2 Domain boundaries** | Make the composition model testable without a GUI | The domain's numeric core loses JUCE (R2.1), then `Arc`'s codec edge and its geometry are split out | R012–R021, R084 | Domain headers depend on no GUI header; the domain tests run headless. |
| **R3 Document and commands** | One authoritative owner, one transaction per edit | `DocumentSession` façade over the existing piece model, then undo/selection behind it | R005–R010, R047, R030 | One model command = one labelled undo transaction; the revision counter advances per command. |
| **R4 Persistence** | Loading is transactional and validated | `PieceCodec` / validation split out of `loadFromVar()`; the file adapter split out of `loadFile()`/`savePieceToFile()`; the recovery snapshot's storage split out of the shell; a failed load leaves the active piece untouched | R059–R067, R090 | A malformed document fails with diagnostics and changes nothing; the v0.29 fixture still renders byte-identically. |
| **R5 UI decomposition** | Rendering, hit-testing and interaction are separate | `ScoreRenderer` + `ScoreHitTester` + a coordinate mapper, out of `ScoreEditor` | R043–R053, R058, R059 | A hit test can be run without painting; the UI checks pass unchanged. |
| **R6 Real-time audio** | The callback is an orchestrator | `TransportStateMachine`, then `SnapshotPublisher`, then `PreviewRouter`, then `AudioRuntime` | R021–R038 | The callback still allocates nothing, rejects stale events, and the golden render is unchanged. |
| **R7 Plugin and platform** | The plugin outlives its editor | Plugin processor owns the session; the editor attaches | R075–R079 | The plugin processes with no editor open; smoke 22/22 and pluginval stay green. |
| **R8 Build and tests** | Boundaries enforced, suites split | A dependency-boundary check in the gate; split the 4,822-line model test | R079–R089, R097–R099 | The gate fails when a forbidden include appears; the test files are split by subsystem. |
| **R9 Integration and cleanup** | Remove the scaffolding | Delete the forwarding methods the extractions left behind | R074, R099, R100 | No dead forwarding code; the architecture note describes the tree as it is. |

R8 runs **alongside** the others: the dependency check and the test splits are
small, independent and improve every later phase.

### The 100 review items, mapped

| Review group | Items | Lands in |
|---|---|---|
| Foundation | R001–R004 | R0 |
| Application | R005–R012, R030, R069 | R3, R6 |
| Domain | R013–R020 | R2 (R013 typed IDs deferred) |
| Audio | R021–R038 | R6 |
| UI | R039–R059 | R1, R5 (R058 caches deferred until profiling) |
| Persistence | R060–R069 | R4 |
| Integrations | R070–R075 | R7, R9 |
| Platform | R076–R078 | R7 |
| Build | R079–R086 | R8 |
| Testing | R087–R098 | R0, R8 (R089 headless target lands with R2) |
| Documentation | R099, R100 | R9 |

## 5. Phase R0 in detail - the baseline

No functional change. R0 exists so that every later phase can prove it
changed nothing.

**R0.1 - the layout characterization suite.** `resized()` computes rectangles
for every card, toolbar control and panel at a given window size. Before the
engine moves, record what it produces: the component rectangles for a set of
window sizes (desktop 1600×1000, 1280×800, compact 1024×700, the minimum
supported size, and the Android portrait/landscape sizes), the card order, the
visibility decisions, and that the result is deterministic when computed twice.
The test must run without the production layout path - that is the point: the
engine becomes a pure function that the test can call.

**R0.2 - the document characterization suite.** The canonical representation of
a piece after load and after save, for the v0.29 fixture and for a piece with
every optional feature (tables, groups, sequences, channels, modulation,
workspace). This is the contract R4's codecs must keep.

**R0.3 - the transport and preview characterization suite.** The state
transitions (stopped → starting → playing → stopping), the stale-request
rejection (a stop request for session *n* must not stop session *n+1*), and
the preview route arbitration (which source interrupts which). These are the
contracts R6's `TransportStateMachine` and `PreviewRouter` must keep.

**R0.4 - the baseline metrics.** This section's tables, refreshed at the end of
R0, plus the gate line and the commit hash of the baseline.

## 6. Phase details

### 6.1 R1.1 - the card layout engine

The review's proposed first executable task, confirmed as the best one:

**What moves.** The seven local structs and the arithmetic that uses them
(lines 702–1479 of `MainComponentLayout.cpp`) become
`Source/CardLayoutEngine.h` (+ `.cpp`): `CardLayoutConstraints`,
`CardSpecification`, `CardLayoutResult`, `LayoutMetrics`, and the internal
`FlowItem` / `FlowGroup` / `CardCell` / `CardLine` / `SpannerLayout` /
`PlacedCell`.

**What stays.** `MainComponent::resized()` keeps the JUCE calls: it builds the
card specifications from the live widgets, hands them to the engine, and
applies the returned rectangles and visibility flags. The adapter is the
compatibility seam; the engine never touches a `juce::Component`.

**Constraints.** Same order, same spacing, same sizes, same visibility
decisions, desktop and Android unchanged. C++17, no new dependency, no layout
framework.

**Gate.** The R0.1 characterization suite passes unchanged (same rectangles,
same order, same visibility), the full gate is green, and the golden render
and legacy fixture are untouched.

**Deliverables.** The engine + the adapter, the characterization suite, before
and after metrics for `resized()` and the file, and an architecture note for
the new boundary.

**Shipped (R1.1, 2026-11-26).** The engine is `Source/CardLayoutEngine.{h,cpp}`
(705 lines: 140 header + 565 implementation); the adapter is the card/strip
section of `resized()`. `resized()` went from 1,400 to 1,051 lines and
`MainComponentLayout.cpp` from 2,475 lines / 114,402 bytes to 2,127 lines /
100,013 bytes (-348 lines, -14,389 bytes). The engine depends on `<vector>`,
`<algorithm>` and `<cmath>` only. The five-viewport component-tree digest was
identical before and after the extraction. See
[`card-layout-engine.md`](card-layout-engine.md) for the boundary.

### 6.2 R1.2 - the bottom layout engine

**What moves.** The bottom half of `MainComponent::resized()` becomes
`Source/BottomLayoutEngine.h` (+ `.cpp`): the editor height budget (the
150/110/300/240 dp rules, the 42% cap, the 6 px gap), the view bar's row,
each editor column (bank manager, preset row, focus button, editor), the
voice strip and voice column, the tab strip, the arc list's width rule
(300 dp, with 240 dp kept for the canvas) and the workspace splitter's
position.

**What stays.** `MainComponent::resized()` keeps the JUCE calls: it builds the
specification from the UI state and applies the rectangles. The focus views
call the engine's `layoutEditorColumn()` / `layoutTabStrip()` directly.
`PageWorkspace::boundsForCanvas()` and the canvas translation stay
component-side, because they are the workspace's own two-canvas rule.

**Constraints.** Same order, same spacing, same sizes, same visibility
decisions, including the controls that are hidden but keep their previous
bounds (PM, the score's focus button, the arc-list toggle) and the workspace
toggle's empty bounds. C++17, no new dependency, no layout framework.

**Gate.** The new R1.2 characterization suite (71 checks) passes unchanged,
the R0.1 suite is untouched, the full gate is green, and the golden render
and legacy fixture are unchanged.

**Shipped (R1.2, 2026-11-26).** The engine is `Source/BottomLayoutEngine.{h,cpp}`
(374 lines: 101 header + 273 implementation); the adapter is the bottom
section of `resized()`. `resized()` went from 1,051 to 989 lines and
`MainComponentLayout.cpp` from 2,127 lines / 100,013 bytes to 2,066 lines /
96,558 bytes (-61 lines, -3,455 bytes). The engine includes `<vector>`,
`<algorithm>` and `<cmath>` only and no JUCE header. The seven-viewport
component-tree digest was identical before and after the extraction. See
[`bottom-layout-engine.md`](bottom-layout-engine.md) for the boundary.

### 6.3 R1.3 - the overlay layout engine

**What moves.** The three in-app overlay cards (the audio device selector,
the MIDI + OSC settings and the "clear page?" warning) become
`Source/OverlayLayoutEngine.h` (+ `.cpp`): each card's padding, title row,
button row and the rows of its contents.

**What stays.** `MainComponent::layoutOverlays()` keeps the backdrops, the
visibility rules and every `setBounds()`. The card's frame stays with the
overlay component, because the overlay paints it.

**Also.** The rectangle the engines share moves into `Source/LayoutRect.h`:
the plain `upic::layout::Rect` and the `juce::Rectangle` semantics the
three engines need, so they cannot drift apart. The bottom engine's own
copies are gone.

**Constraints.** Same order, same spacing, same sizes, same visibility
decisions. The four MIDI Learn buttons keep the name "Learn", because the
button icons are chosen by the name. C++17, no new dependency.

**Gate.** The new R1.3 characterization suite (76 checks) passes unchanged,
the R0.1 and R1.2 suites are untouched, the full gate is green, and the
golden render and legacy fixture are unchanged.

**Shipped (R1.3, 2026-11-26).** The engine is
`Source/OverlayLayoutEngine.{h,cpp}` (187 lines: 64 header + 123
implementation) and the shared rectangle is `Source/LayoutRect.h` (79
lines). `layoutOverlays()` went from 100 to 73 lines, `resized()` from 989
to 980 lines, and `MainComponentLayout.cpp` from 2,066 lines / 96,558
bytes to 2,088 lines / 95,830 bytes (+22 lines: 45 of them are the
suite's hooks in the same file; -728 bytes). The four-viewport
component-tree digest was identical before and after the extraction. See
[`overlay-layout-engine.md`](overlay-layout-engine.md) for the boundary.

### 6.4 R8.1 - the dependency-boundary check

**What moves.** Nothing moves. R8.1 adds the check §2 promised: the
dependency rules are enforced by a source-level check in the gate, because
the Windows gate does not build through the root CMake file that the
review's CMake target split would touch.

**The rules.** B1: the layout engines include no JUCE header. B2: they
include no app header outside the engine group. B3: `UPICuitous.jucer`
lists every `Source/*.cpp`. B4: the plugin's two CMake targets carry the
same, complete source list. B5: the generated Visual Studio project lists
every `Source/*.cpp`.

**The checker's own contract.** The checker tests itself before it checks
the tree - 18 cases, one per rule and one per manifest reader - so a rule
that cannot be shown to fire fails the gate instead of passing it.

**Gate.** The check is green on the tree, the gate *fails* on a forbidden
include, on a source file the `.jucer` does not know and on a source missing
from the plugin's list, and the full gate is green with the new step.

### 6.5 R2.1 - the domain's numeric core

**What moves.** Nothing moves. R2.1 takes the JUCE umbrella header out of
the domain's numeric core: `UpicConstants.h` (the units),
`PieceLimits.h` (the validation), `UpicMath.h` (the conversions and the
lookups) and `Ids.h` (the id helpers).

**What stays.** Everything else. The three numeric helpers those headers
used (`juce::jmax`, `juce::jmin`, `juce::jlimit`) are the standard library's,
with the same expressions and therefore the same results.

**The contract.** A suite of the domain's own, written before the move:
`Source/SelfTestDomain.cpp`, 61 checks that exercise the domain through the
domain's headers alone - no `MainComponent`, no window, no component tree.

**Gate.** The suite passes unchanged, the rest of the selftest is untouched,
the golden render and the legacy fixture are unchanged, the full gate is
green, and the gate's second JUCE-free group is this one - a JUCE header in
any of the four headers fails the gate before the build.

### 6.6 R2.2 - the arc's codec edge

**What moves.** The arc's file format leaves the class: `Arc::toVar()` and
`Arc::fromVar()` become `upic::arcToVar (const Arc&)` and `upic::arcFromVar
(const juce::var&)` in the new `Source/ArcCodec.h`. `Source/Arc.h` loses
the JUCE umbrella header and the three numeric helpers it used for it.

**What stays.** The file format: the property names, their order, the
optional keys a default arc does not write and the clamps a crafted file
gets. The bodies are the old members, body for body. The plan keeps the
JUCE value types at the codec edge (R2).

**The contract.** A suite of the arc's own, written before the move:
`Source/SelfTestArc.cpp`, 87 checks that reach the arc through the
domain's headers alone - no `MainComponent`, no window, no component tree.
It pins the transformations, the interpolation and the codec, including
the file's short keys.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, the full
gate is green, and the gate's third JUCE-free group is the arc - a JUCE
header in `Arc.h` fails the gate before the build.

### 6.7 R2.3 - the arc's geometry

**What moves.** The page-as-a-surface operations move out of `Arc.h` into
the new `Source/ArcGeometry.h`: the frame (the page's normalised space),
the scope and its bounds, the pitch centre, the rule that writes a moved
point back, and the six operations - the rotation, the two mirrors, the
scale, the alignment, the repeat.

**What stays.** The arc's data in `Arc.h`: the points, the material, the
settings, the four classical transformations and the interpolation. The
direction is the point: the geometry knows the arc, the arc does not know
the geometry. This is the review's R012.

**The contract.** A suite of the geometry's own, written before the move:
`Source/SelfTestArcGeometry.cpp`, 58 checks that reach the geometry
through the domain's headers alone - no `MainComponent`, no window, no
component tree. It pins the frame, the bounds, the pitch centre, the
write-back rule and each transform with its edges and its invariants.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, the full
gate is green, and the gate's fourth JUCE-free group is the geometry.

### 6.8 R2.4 - the page's codec edge

**What moves.** The page's file format moves out of `Page.h` into the new
`Source/PageCodec.h`: `juce::var toVar()` and `static Page fromVar()`
become `upic::pageToVar (const Page&)` and `upic::pageFromVar (const
juce::var&, int* renamedIds)`. The bodies are the old members, body for
body. The page's own code loses JUCE with them: the numeric helpers it
used are the standard library's, and `Page.h` no longer includes
`<JuceHeader.h>`.

**What stays.** The page's data and its rules in `Page.h`: the arcs, the
zones, the four frequency spaces, the groups, the ambitus, the warp and
the time map. The JUCE the page still sees comes from the types it holds
(the group's `juce::String name` and `juce::Colour`), not from its own
code - that is the next step of the wave.

**The contract.** A suite of the edge's own, written before the move:
`Source/SelfTestPageCodec.cpp`, 73 checks that reach the page through the
domain's headers alone. It pins the keys and their order, the optional
keys, the clamps, the ids, the caps, the group's membership and the round
trip.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, the full
gate is green, and the gate's fifth group is the page.

### 6.9 R2.5 - the arc group's codec edge

**What moves.** The group's file format moves out of `ArcGroup.h` into the
new `Source/ArcGroupCodec.h`: `juce::var toVar()` and `static ArcGroup
fromVar()` become `upic::arcGroupToVar (const ArcGroup&)` and
`upic::arcGroupFromVar (const juce::var&, int fallbackId)`. The bodies are
the old members, body for body.

**What stays.** The group's data in `ArcGroup.h`: the id, the name, the
colour, the membership, the mute, the solo, the gain, the transpose and the
four bank ids - and the render-time override beside it. The name and the
colour stay JUCE's: the plan defers replacing the domain's value types
(§2), so `ArcGroup.h` still asks for the JUCE header and the group is not
one of the gate's JUCE-free groups.

**The contract.** A suite of the group's own, written before the move:
`Source/SelfTestArcGroup.cpp`, 43 checks that reach the group through the
domain's headers alone. It pins the keys and their order, the optional keys,
the id, the name, the colour, the membership, the clamps and the round trip.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.10 R2.6 - the envelope's codec edge

**What moves.** The envelope's file format moves out of `Envelope.h` into the
new `Source/EnvelopeCodec.h`: `juce::var toVar()` and `static Envelope
fromVar()` become `upic::envelopeToVar (const Envelope&)` and
`upic::envelopeFromVar (const juce::var&, int fallbackId)`. The bodies are
the old members, body for body.

**What stays.** The envelope's data in `Envelope.h`: the id, the name, the
colour, the points, the lookup the render walks and the five presets. The
name and the colour stay JUCE's: the plan defers replacing the domain's
value types (§2), so `Envelope.h` still asks for the JUCE header and the
envelope is not one of the gate's JUCE-free groups.

**The amplitude table comes along.** `AmplitudeTable` is an alias for
`Envelope` - the two banks share the type, the editor and the format - so
the table's file format *is* the envelope's, and the table's four call sites
move with it.

**The contract.** A suite of the envelope's own, written before the move:
`Source/SelfTestEnvelopeCodec.cpp`, 31 checks that reach the envelope
through the domain's headers alone. It pins the keys and their order, the
defaults the file uses (which are not the defaults the data uses), the id
(which is not clamped at all), the points and their clamps, the cap and the
round trip.

**One fix comes with it.** `Envelope.h` used `SegmentCursor` and
`followSegment` without including the header that declares them
(`Source/UpicMath.h`), so it only compiled when something else had included
it first. The header now says what it uses - the same include `Arc.h` and
`Channel.h` already have.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.11 R2.7 - the wave's codec edge

**What moves.** The wave's file format moves out of `Waveform.h` into the
new `Source/WaveformCodec.h`: `juce::var toVar()` and `static Waveform
fromVar()` become `upic::waveformToVar (const Waveform&)` and
`upic::waveformFromVar (const juce::var&, int fallbackId)`. The bodies are
the old members, body for body.

**What stays.** The wave's data in `Waveform.h`: the id, the name, the
colour, the table of samples, the generators and the lookup the oscillator
reads. The name and the colour stay JUCE's: the plan defers replacing the
domain's value types (§2), so `Waveform.h` still asks for the JUCE header
and the wave is not one of the gate's JUCE-free groups.

**The contract.** A suite of the wave's own, written before the move:
`Source/SelfTestWaveCodec.cpp`, 29 checks that reach the wave through the
domain's headers alone. It pins the keys and their order, the defaults the
file uses (which are not the defaults the data uses), the id (which is not
clamped at all), the table (its length, its cap, and the fact that its
values are not clamped) and the round trip.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.12 R2.8 - the channel's codec edge

**What moves.** The channel's file format moves out of `Channel.h` into the
new `Source/ChannelCodec.h`: `juce::var toVar()` and `static Channel
fromVar()` become `upic::channelToVar (const Channel&)` and
`upic::channelFromVar (const juce::var&, int fallbackId)`. The bodies are
the old members, body for body.

**What stays.** The channel's data in `Channel.h`: the id, the name, the
colour, the gain, the mute, the solo, the gain curve, the allocation-free
lookup, the resolved mixer (`ChannelMix`) and the build of it. The name and
the colour stay JUCE's: the plan defers replacing the domain's value types
(§2), so `Channel.h` still asks for the JUCE header and the channel is not
one of the gate's JUCE-free groups.

**The contract.** A suite of the channel's own, written before the move:
`Source/SelfTestChannelCodec.cpp`, 54 checks that reach the channel through
the domain's headers alone. It pins the keys and their order (and the ones
that are left out when they are at their default), the clamps (the id is
clamped, unlike the envelope's and the wave's), the name and its 64-character
cut, the gain, the curve and the round trip.

**One fix comes with it.** `Piece.h` holds a `std::vector<Channel>` but did
not include `Channel.h` - it got the type because `UpicModel.h` includes
`Channel.h` before it includes `Piece.h`. The header now says what it uses.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.13 R2.9 - the sample slot's codec edge

**What moves.** The sample slot's file format moves out of `Sample.h` into
the new `Source/SampleCodec.h`: `juce::var toVar()`, `juce::var toVar (bool
includeAudio)`, `static SampleSlot fromVar()` and `juce::MemoryBlock
toPcm16()` become `upic::sampleSlotToVar (const SampleSlot&)`,
`upic::sampleSlotToVar (const SampleSlot&, bool includeAudio)`,
`upic::sampleSlotFromVar (const juce::var&, int fallbackId)` and
`upic::sampleSlotToPcm16 (const SampleSlot&)`. The bodies are the old
members, body for body.

**What stays.** The slot's data and behaviour in `Sample.h`: the id, the
name, the rate, the shared audio buffer, the direction, the base pitch, the
level, the gain, the trim, the fades, the region's lookup and the material's
edits. The name stays JUCE's: the plan defers replacing the domain's value
types (§2), so `Sample.h` still asks for the JUCE header and the sample slot
is not one of the gate's JUCE-free groups.

**The PCM conversion comes along.** `toPcm16` *is* the format - it is the
PCM16 encoding the file's base64 carries, and the recovery snapshot writes
the same bytes to its sidecar. Leaving it in the data header would have
left half the format behind.

**The file reading is still in the domain.** `loadSampleFile` reads a
`juce::File` and decodes it. Reading a file is an edge of its own.

**The contract.** A suite of the sample slot's own, written before the move:
`Source/SelfTestSampleCodec.cpp`, 71 checks that reach the slot through the
domain's headers alone. It pins the keys and their order (and the ones that
come only when they carry something), the clamps, the defaults the file uses
(which are not the defaults the data uses), the two size checks the payload
gets and the duration cap.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.14 R2.10 - the tuning's codec edge

**What moves.** The tuning's file format moves out of `Tuning.h` into the
new `Source/TuningCodec.h`: `juce::var toVar()` and `static Tuning
fromVar()` become `upic::tuningToVar (const Tuning&)` and `upic::tuningFromVar
(const juce::var&)`. The bodies are the old members, body for body.

**What stays.** The tuning's data and behaviour in `Tuning.h`: the name, the
degrees, the base note and the custom flag, the degree-to-cents and
cents-to-degree mappings, the step and MIDI conversions, the snap, the range
test, the axis steps and the description. The name stays JUCE's: the plan
defers replacing the domain's value types (§2), so `Tuning.h` still asks for
the JUCE header and the tuning is not one of the gate's JUCE-free groups.

**The Scala importer is still in the domain.** `parseScalaTuning` and its
helpers read a Scala file. Reading a file is an edge of its own - the same
way the sample's file reading is.

**The contract.** A suite of the tuning's own, written before the move:
`Source/SelfTestTuningCodec.cpp`, 46 checks that reach the tuning through the
domain's headers alone. It pins the keys and their order, the clamps, the
rules a scale has to follow (the base is 0 cents, the degrees must rise,
they live in one octave, there are at most 256 of them) and the rule that a
broken tuning falls back to the default WHOLE.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.15 R2.11 - the sieve zone's codec edge

**What moves.** The sieve zone's file format moves out of `Sieve.h` into the
new `Source/SieveCodec.h`: `juce::var toVar()` and `static SieveZone
fromVar()` become `upic::sieveZoneToVar (const SieveZone&)` and
`upic::sieveZoneFromVar (const juce::var&)`. The bodies are the old
members, body for body.

**What stays.** The zone's data and behaviour in `Sieve.h`: the id, the time,
the frequency range, the modulus, the remainders, the time grid and the
stickiness, plus the containment test, the effective stickiness, the preset
library, the quantisation that uses the zones and the freehand curve
simplification.

**The contract.** A suite of the sieve zone's own, written before the move:
`Source/SelfTestSieveCodec.cpp`, 65 checks that reach the zone through the
domain's headers alone. It pins the keys and their order (and the two that
come only when they carry something), the order independence of the times
and the frequencies, the clamps, the defaults the file uses (which are not
the defaults the data uses), the folding of the remainders into the modulus
and the rule that the codec does not touch the id.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.16 R2.12 - the performance sequence's codec edge

**What moves.** The performance sequence's file format moves out of
`Performance.h` into the new `Source/PerformanceCodec.h`: `juce::var
toVar()` and `static PerformanceSequence fromVar()` become
`upic::performanceSequenceToVar (const PerformanceSequence&)` and
`upic::performanceSequenceFromVar (const juce::var&, int fallbackId)`. The
bodies are the old members, body for body.

**What stays.** The sequence's data and behaviour in `Performance.h`: the
point, the id, the name, the points and the loop flag, plus the length of
the sequence, the position the performance is at, the loop time and the
modulation factor. The name stays JUCE's: the plan defers replacing the
domain's value types (§2), so `Performance.h` still asks for the JUCE header
and the performance sequence is not one of the gate's JUCE-free groups.

**The header now stands on its own.** `Performance.h` uses
`kMaxPageSeconds` but used to ask for `UpicConstants.h` only through the
include order of the files that happened to include it first. It now asks
for it itself - a fix the phase's suite found.

**The contract.** A suite of the performance sequence's own, written before
the move: `Source/SelfTestPerformanceCodec.cpp`, 48 checks that reach the
sequence through the domain's headers alone. It pins the keys and their
order (and the one that comes only when the sequence does not loop), the
point's own three keys, the clamps, the cut of the name, the sort of the
points and the rule that an absent id takes the fallback.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.17 R2.13 - the frequency space definition's codec edge

**What moves.** The frequency space definition's file format moves out of
`FreqSpace.h` into the new `Source/FreqSpaceCodec.h`: `static juce::var
pointsToVar()`, `juce::var toVar (int index) const` and `static FreqSpaceDef
fromVar()` become `upic::freqSpacePointsToVar (const std::vector<TablePoint>&)`,
`upic::freqSpaceDefToVar (const FreqSpaceDef&, int index)` and
`upic::freqSpaceDefFromVar (const juce::var&)`. The bodies are the old
members, body for body.

**What stays.** The definition's data and behaviour in `FreqSpace.h`: the
preset, the editable flag, the table, the staircase and the inversion, plus
`isDefault`, `log2At`, the presets, the range of a space and the
render-time lookup table.

**The points helper came along.** `pointsToVar` is how the table's points
are written - it is the format, and the writer's only use of it was the
table.

**The contract.** A suite of the definition's own, written before the move:
`Source/SelfTestFreqSpaceCodec.cpp`, 47 checks that reach the definition
through the domain's headers alone. It pins the keys and their order (and
the ones that come only when they carry something), the point's own two
keys, the Hz the points carry, the clamps, the rules a table has to follow
(two points, the points in order) and the rule that the space index is
written but not read.

**The end of the sweep.** This is the last of the domain headers that
carried a file format: a sweep of `Source/` for `toVar` and `fromVar` now
finds only two comments in the codec headers. The whole piece format lives
at the edge.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.18 R3.1 - the document session

**What moves.** The document's own state leaves `MainComponent` and moves
into the new `Source/DocumentSession.h`: the model epoch, the model
revision and the piece's unsaved flag become `upic::DocumentSession`'s
`epoch()`, `revision()` and `isDirty()`, with `markChanged (bool
affectsPiece)`, `markSaved()` and the two reads beside them. The
component's `markModelChanged()`, `pieceHasUnsavedChanges()` and
`markPieceSaved()` keep their names and their meaning and become the
shell's way of asking the session.

**What stays.** The piece's data in the component's members (the
waveforms, the envelopes, the pages, the arcs, the channels, the sample
slots, the tuning, the sieve), the undo history, and the selection. They
come behind the session in R3.

**The ten read sites.** The status timer, the snapshot publisher, the
audio thread, the recovery snapshot, the saver (twice), "make unique"
(twice) and the undo transaction (twice) now ask the session.

**Why the revision and the epoch stay two counters.** They move together,
but they answer different questions for different threads: the revision is
the document's own counter (the status timer, the recovery snapshot, the
saver, "make unique", the undo transaction), the epoch is the audio
thread's "the model moved" signal. One thread must not have to know about
the other's users.

**The contract.** A suite of the session's own, written before the move:
`Source/SelfTestDocumentSession.cpp`, 30 checks that read the document's
state through one door. It pins the fresh document, what one model change
does, what a change that does not affect the piece does, what saving does,
the undo rule the revision backs, that an undo and a redo are model
changes, and that two documents share nothing.

**The class needs no JUCE.** Three counters: `<atomic>` and `<cstdint>`.
The undo history will bring JUCE with it.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.19 R3.2 - the undo history

**What moves.** The document's history leaves `MainComponent` and moves into
`Source/DocumentSession.h`: the undo stack, the redo stack, the pending
snapshot of the open transaction and the two bounds (50 steps, 64 MB)
become the session's own. `MainComponent`'s `UndoEntry` becomes the session's
private `Entry`, and `estimateModelBytes()` - the size of a snapshot, which
the memory bound counts - moves with it.

**What stays.** The shell's half of the undo: the gain slider's flag, the
selection (the selected wave, envelope, amplitude table, sequence and sample
slot), the transport (an undo does not silence the app) and the messages
("Undo: <label>", "The undo could not be applied"). The component installs
the session's two doors in its constructor - `capturePieceModel()` and
`restorePieceModel()` - and the restore goes through the normal loader, so an
undone piece gets the same checks and the same limits as a loaded one.

**The contract.** A suite of the history's own, written before the move:
`Source/SelfTestUndoHistory.cpp`, 59 checks. It pins the fresh document, one
edit, the undo, the redo, an undo with nothing to undo, the new branch, the
open transaction, the two bounds, the selection and the transport.

**The class needs JUCE now.** The labels are strings and the snapshots are
pieces, so the session asks for `<JuceHeader.h>` and `Piece.h`. It stays out
of the boundary check's JUCE-free groups, as it already did.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.20 R3.3 - the selection model

**What moves.** The document's selection leaves `MainComponent` and moves
behind the session: the nine members that said what the user is working on
- the selected wavetable, envelope, amplitude table, performance sequence,
group, mixer channel and sample slot, the arc the canvas points at, and
the arc selection the arc-list panel last showed - become
`upic::SelectionModel`'s, in the new `Source/SelectionModel.h`. The session
owns that class and has one door for it, `selection()`, beside the
revision, the epoch, the unsaved flag and the history.

**What stays.** The piece's data in the component's members, the canvas's
own multi-selection (the `ScoreEditor` owns it - the model carries the
mirror the panel and the toolbar follow), the transport and the messages.
The selection's *meaning* did not change: the same defaults, the same
fallbacks, the same loader behaviour.

**The contract.** A suite of the selection's own, written before the move:
`Source/SelfTestSelection.cpp`, 27 checks. It pins the fresh document, what
a load does to the selection, what deleting the selected item does to it,
that two documents keep their own selection, and that a load leaves no arc
selected.

**The class needs no JUCE.** The selection is ids and a vector of ids: it
asks for `<vector>` and nothing else. It is not in the boundary check's
JUCE-free groups either - it is the application layer's own, like the
session that owns it.

**Gate.** The suite passes unchanged, the rest of the selftest is
untouched, the golden render and the legacy fixture are unchanged, and the
full gate is green.

### 6.21 R3.4 - the command dispatcher

**What moves.** The way the model changes moves behind a new class,
`Source/CommandDispatcher.h`. `upic::CommandDispatcher` owns the command:
its label, the snapshot of the state from before the change, the flag
that says whether the command changed anything, and the rule that says
what a change means. The session owns the dispatcher and installs its
four doors - the capture (the piece from before the command), the commit
(the undo step) and the two halves of the change report (the epoch and
the revision).

`DocumentSession::pendingEdit_`, `pendingLabel_` and `pendingRevision_`
become the dispatcher's; `DocumentSession::beginEdit()`, `endEdit()` and
`hasOpenEdit()` become `CommandDispatcher::beginCommand()`,
`endCommand()` and `isCommandOpen()`; and the session's `markChanged()`
becomes `reportChange()` - the shell's way of asking for the change
report. The history's bookkeeping (the step, the redo stack, the bounds)
stays with the session, in `commitStep()`.

**What stays.** The call sites. The about eighty `markModelChanged()`
sites and the twenty-odd `beginEdit()` / `endEdit()` pairs keep their
names and their places, and the component's methods keep their names
too: `beginEdit` and `endEdit` are the shell's way of asking the session
for a command, `markModelChanged` is the shell's way of asking for the
change report.

**The semantics.** One command = one labelled undo transaction = one
revision:

- a change inside a command moves the **epoch** at once (the audio
  follows the gesture while it is drawn) and moves the **revision** when
  the command ends (the document changed once);
- a change outside a command is a change of its own: there is no
  transaction to put it in, so the epoch and the revision move straight
  away;
- a command that changes nothing is not an undo step and moves no
  counter.

**What changes for the user.** Nothing: the revision is an internal
counter. The five places that asked "did the model move?" ask the epoch
now - the status timer, the snapshot publisher, the saver, the recovery
snapshot and "make unique" - because the epoch is the signal that moves
with every change and the revision is the document's own counter (it
moves once per command). With the revision as the trigger, a drag's
snapshot would be published only when the mouse came up: the audio would
follow the gesture at the end of it, not while it is drawn. The saver
needs the same signal - a change that is still inside a command has to
be noticed, or the app would forget that the piece on the disk is older
than the one on the screen.

**The contract.** A suite of the command's own, written before the move:
`Source/SelfTestCommands.cpp`, 29 checks. It pins a change outside a
command, a change that does not affect the piece, one command, a command
with two changes, a command that changes nothing, two commands, a
command that was never closed, two documents and the undo.

One expectation changes with the move, and the plan says so here, before
the move: a command with two changes advances the revision by two before
the move and by one after it - that is the wave's exit criterion. Two
checks are added: the epoch follows the change inside the command, and
the revision waits for the command to end.

**The class needs JUCE.** The label is a string and the snapshot it
holds is a piece, so the dispatcher asks for `<JuceHeader.h>` and
`Piece.h`. It is the application layer's own, like the session that owns
it, and it stays out of the boundary check's JUCE-free groups.

**The mechanical check.** `upic_check_r34_dispatcher.ps1` proves that
the move changed the owner of the change report and nothing else: 120
source files identical, the history's bookkeeping identical (12 lines)
after the renames the move needs, the session's old transaction doors gone,
the five readers asking the epoch, the 109 mentions of
`markModelChanged` in the `.cpp` files untouched, and the production
code not calling the test-only door.

**What is still open.** The model still changes through
`markModelChanged()` at the end of about eighty call sites: each of them
is a change of its own, not a command with a label. The mechanism is
there - the dispatcher is the only way into the document - but the call
sites are not commands yet. That is R3.5, and the wave's exit criterion
is reached there.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.22 R3.5 - the edits become commands

**What moves.** The edits that still change the piece without a command.
R3.4 gave the document one way in; this phase makes the shell use it for
every user edit that changes the piece. Twenty of them are left, and they
are the controls added after the undo stack was built: the page
sequencer, the workspace split, the live pitch and reverse, the
modulation and step controls, the page's ambitus, acceleration and four
frequency spaces, the arc's space, the arc list's reverse and mode, the
sample's reverse, level and pitch, the tuning and the capture.

**The helper.** A gesture and a one-shot need the same thing - a command
that is open exactly once - so the shell gets one method: `runEdit
(label, fn)`. If a command is already open (a drag, a caller that
groups), it just runs `fn`; otherwise it opens the command with the
label, runs `fn` and closes it. A drag opens on drag start and closes on
drag end; a slider's value change inside the drag joins the drag's
command. This is the "standalone" pattern the channel gain already uses,
formalized for every edit.

**The rule.** A command carries the state the snapshot carries (the
`PieceModel`). The loop page is not in the piece, so its toggle stays a
change of its own. The two pitch modes are in the piece (they are written
into the file), so the live and the sample pitch selectors are commands
even though the engine holds them. A load, an undo or redo restore and a
recovery replace the document; they are not edits, they are not undoable,
and they stay changes of their own.

**The edits and their labels.**

| edit | site | label | gesture |
|---|---|---|---|
| the page sequencer | `seqBtn` | Sequence the pages | one-shot |
| the workspace split | `WorkspaceSplitter` | Resize the workspace | drag |
| the live pitch | `livePitchBox` | Live pitch | one-shot |
| the live reverse | `liveRevBtn` | Live reverse | one-shot |
| the wave use | `modUseBox` | Wave use | one-shot |
| the modulation rate | `modRateSlider` | Modulation rate | drag |
| the modulation depth | `modDepthSlider` | Modulation depth | drag |
| the stepped pitch | `stepBtn` | Step the pitch | one-shot |
| the step divisions | `stepDivSlider` | Step divisions | drag |
| the page's spaces | `spaceDefBox[]` | Frequency space | one-shot |
| the arc's space | `spaceBox` | Arc frequency space | one-shot |
| the page's ambitus | `ambitusBox` | Page ambitus | one-shot |
| the page's acceleration | `warpSlider` | Page acceleration | drag |
| the arc list's reverse | `ArcListPanel::onReverseToggled` | Arc reverse | one-shot |
| the arc list's mode | `ArcListPanel::onModeChosen` | Arc pitch mode | one-shot |
| the sample's reverse | `revBtn` | Reverse the material | one-shot |
| the sample's level | `levelSlider` | Slot level | drag |
| the sample's pitch | `samplePitchBox` | Sample pitch | one-shot |
| the tuning | `applyTuning()` | Tuning | one-shot |
| the capture | `finalizeCapture()` | Capture the material | one-shot |

**What stays raw, and why.** The loop toggle (`markModelChanged
(false)`), the page box (a page switch is navigation, not an edit), a
load, an undo or redo restore, a recovery, the refresh helpers that run
inside their caller's command, and the workspace open and close - which
changes the piece's layout but does not mark the model today, and this
phase does not add a mark. A change outside a command is legal; R3.4
defined what it means.

**The contract.** `Source/SelfTestModelCommands.cpp`, written before the
move. It drives the edits through the shell's own doors (the named
controls, the public methods, and the three doors added with the move -
the workspace split's drag and the arc list's two per-arc controls), the
way the audio and model suites already do. It pins today's behaviour -
the counters move, the piece is unsaved, there is no undo step - and
after the move each edit is one step with its label, the revision
advances once, and the epoch follows every change inside the command.
The live reverse has two changes in one callback: one step, the epoch +2
and the revision +1.

Nineteen of the twenty edits are driven by the suite: the tuning, the
page sequencer, the live pitch and reverse, the wave use, the
modulation rate and depth, the stepped pitch, the step divisions, the
page's spaces, the arc's space, the page's ambitus and acceleration, the
arc list's reverse and mode, the sample's reverse, level and pitch, and
the workspace split. The capture is the one the suite cannot drive - it
needs live audio - and the plan says so rather than pretending the suite
covers it.

**The mechanical check.** Every `markModelChanged` in the shell is
inside a command now, except the six that are named as raw above: the
loop toggle, the page box, the load, the undo or redo restore, the
recovery, and the test door that installs a sample slot. The twenty
callbacks are each inside a `runEdit` or between a drag's begin and end,
the production code does not call a test-only door, and the diff outside
the converted callbacks is empty.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.23 R4.1 - the document's codec

**What moves.** The document's read side moves out of
`MainComponent::loadFromVar()` into the new `Source/PieceCodec.h`: the
decode, the migration and the validation of a piece file, and the repairs
the loader makes when a document is not quite right. The codec hands back
a *candidate* - a `PieceModel` - and what it found out about the document
(the parts that did not make it in, the ids that were renumbered, the
references that were put back), and the caller puts the candidate in
force.

`Source/Piece.h` keeps the piece as data (`PieceModel`) and loses the
file format: `pieceToVar()` moves to `PieceCodec.h`, body for body, so
the piece's format lives in one file - the same place every other format
in the app lives (`PageCodec.h`, `ArcCodec.h` and their neighbours).

**What stays.** `MainComponent::loadFromVar()` keeps the *commit*: the
swap under the model lock, the selection, the controls, the engine, the
snapshot publication, the messages and the unsaved flag. And `loadFile()`
keeps the file adapter (the size cap, the JSON parse and the guard
around the parse). (R4.2 moved that half into an edge of its own - §6.24.) The load is transactional today and stays so: the
document is read into temporaries first, and the active piece is touched
only when the document really is a piece.

**The one change.** `pieceToVar()` writes the two pitch modes only when
the piece carries one. A document that carries none (an old piece)
leaves the session's mode alone - which is what the loader already did,
and what the old writer happened to lose on the way out. A live piece
always carries its modes, so the golden render and the v0.29 fixture do
not move.

**The diagnostics.** The review asks for structured load errors rather
than a single success flag (§32). The plan defers the type migration
(§2), so the diagnostics carry the JUCE types the edge already uses:
`PieceDiagnostics` holds the two messages the user is told (the parts
that did not make it in, the parts that were adjusted) and the counts
behind them. The wording of the messages does not change by a character.

**The contract.** `Source/SelfTestPieceCodec.cpp`, written before the
move. It pins what the loader does to a document today: what it refuses
(a document that is not a piece is refused, and the piece is left alone),
the defaults an old document gets, the clamps the numbers get, the sieve
remainders that are folded into the sieve, the caps (what does not fit
is dropped, and the user is told), the ids that are renumbered, the
references that are put back, the modulator loop that is broken, the
material's Ref that moves from the arc to the slot (M5), the workspace
layout that comes back with the document, the pitch modes, the round trip
and the unknown keys a document may carry.

The suite reaches the loader through one door, so the move touches the
suite in one place and the checks themselves do not change. After the
move the door is the codec, and the suite reaches the document's format
without a window, a component tree or an audio device.

**The mechanical check.** The codec holds the parse, the repairs and the
defaults, and the component holds the commit: the diff outside
`loadFromVar()`'s two halves is empty, the production code does not call
a test-only door, and the format's output is unchanged - the golden
render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.24 R4.2 - the document's file adapter

**What moves.** The document's file adapter moves out of the shell into the
new `Source/DocumentRepository.h`: the reader (the size cap, the read, the
JSON parse and the guard around the parse) and the writer (the JSON build,
the measurement, the refusal and the write). The review asks for the file
picking and the document's I/O to be separate (R064), and the adapter is
step 1 of the review's loading pipeline (§32) - "Read document: file
adapter and size limits".

The edge reads a document from a picked source and writes a piece to a
picked destination, and it owns the size limits and the words of the
refusals that belong to the file. The piece's *format* is the codec's
(R4.1) and the *commit* is the shell's, so the pipeline is the review's:
read (the edge), decode, migrate and validate (the codec), commit (the
shell).

**What stays.** The shell keeps the file picking (the chooser), the commit
(`loadFromVar()` and `commitPieceModel()`) and the app-state wording: "Not
a UPIC piece (the JSON is broken) - the current piece was kept", "This
file is not a UPIC piece - the current piece was kept" and "Saved X". The
edge says what happened; the shell words it for the user.

**The one change.** None in the behaviour. The duplication is gone: the
"Piece too large to save (X, the limit is Y) - shorten the capture or the
sample, or export the audio to a WAV instead" message was written twice -
once in the window's worker and once in the synchronous saver - and it is
the edge's now, written once. One unreachable branch goes with the move:
the load's "Piece too large (max N MB)" could never be reached, because
the file layer refuses the same file first, with "The file is too large:
X". The silent case stays silent: a file whose *text* is over the cap (the
text of a file can be bigger than the file) keeps the piece and says
nothing, as it did.

**The failures.** The review asks for structured load errors rather than a
single success flag (§32). The plan defers the type migration (§2), so the
results carry the JUCE types the edge already uses: `DocumentReadFailure`
names what went wrong (no file, missing, too large, the text too large,
broken JSON, unreadable), and `DocumentReadResult` and
`DocumentWriteResult` carry the document or the reason. The wording of the
messages does not change by a character.

**The contract.** `Source/SelfTestDocumentIO.cpp`, written before the move.
It pins what the adapter does today: the writer's refusal and its
message, the write and what it says, the round trip, the failed write and
the file that was there, the reader's refusals and their messages, and the
load's sentences and the piece that is kept. The writer is reached through
the shell's own door and the reader through the file layer's; the move
turns the reader's door into the edge's, and the load's sentences - which
live in the chooser's callback, where no test can reach them - get their
door with the move (the R3.5 precedent).

**The mechanical check.** The production code does not call a test-only
door, the diff outside the adapter's two halves is empty, and the
behaviour is unchanged - the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.25 R4.3 - the recovery snapshot's repository

**What moves.** The storage of the crash-recovery snapshot moves out of the
shell into the new `Source/RecoveryRepository.h`: where the snapshots live,
the write (the piece JSON without the audio, the sidecars whose names carry
the hash of the audio, the removal of the sidecars that are not used any
more), the read (the piece JSON, the audio put back where the loader wants
it) and the cleanup. The review gives that responsibility a name - the
"recovery repository" (§33, the table of the storage responsibilities) -
and asks for the recovery to be a service of its own (R065, "Extract
RecoveryService").

The edge is the storage; the service is the shell's. The snapshot is still
written by `maybeWriteRecoverySnapshot()` (the 30 s interval, the epoch,
the unsaved check) and still offered by `offerRecovery()` (the dialog, the
recover and the discard); what moves is what the shell does to the disk.

**What stays.** The service and the words: "The recovery snapshot could not
be written: X", "Recovered the piece from the last session" and "The
recovery snapshot could not be opened". The edge says what happened; the
shell words it for the user.

**The one change.** None in the behaviour. The bodies are the shell's
methods, body for body: the property names, the 8 MB piece cap, the
`kMaxPieceBytes` audio cap, the hash (FNV-1a), the sidecar names, the order
of the checks and the wording of every refusal do not change by a
character.

**The contract.** `Source/SelfTestRecoveryRepository.cpp`, written before
the move. It pins where the snapshots live, the shape of a snapshot (the
piece file, the sidecar's name, the piece JSON that points at it), the
sidecars that are not used any more, the round trip, the cleanup and every
refusal with its words. The suite reached the storage through the shell's
static methods; the move turns them into the edge's, and the checks
themselves do not change (the R3.5 precedent: the doors come with the
move).

**The mechanical check.** The production code does not call a test-only
door, the diff outside the storage's four methods is empty, and the
behaviour is unchanged - the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.26 R4.4 - the user library's repository

**What moves.** The storage of the user library moves out of
`Source/UserLibrary.h` into the new `Source/LibraryRepository.h`: where the
library lives (and the selftest's redirect), the kind-to-folder mapping, the
name-to-file-name slug, the publish (the collision counter and the atomic
temp-and-rename write), the read (the 4 MB cap, the parse and the envelope
validation), the listing, the first free library id and the reveal target.
The review gives the storage a name - the "library repository" (§34,
Persistence/Libraries) - and the reusable assets a format of their own - the
"library asset codec" (§33).

The asset's *format* stays in `Source/UserLibrary.h`: the kinds, the
envelope (`UserLibrary::assetToVar` / `assetFromVar`) and the payload
validation. The shell keeps the service: which asset is selected, the bank's
caps, the undo transaction and the words the user is told.

**What stays.** The shell keeps the five publish/import/reveal triples, the
`refreshUserLibraryBoxes()` refresh, the import's new local id, the bank
limits and the undo transaction. The edge says what happened; the shell
words it: `"The library asset was refused: " + error`, `"Published \"X\" to
the user library"` and `"The user library could not reveal that X: " +
error`.

**The one change.** None in the behaviour. The bodies are the old
`UserLibrary` methods, body for body: the six-property envelope, the 4 MB
asset cap, the atomic write, the `-2`…`-999` collision counter, the order of
the checks and the wording of every refusal do not change by a character.
The new `read (kind, fileName, …)` overload is the old shell line - the
selected name becomes a file in the kind's own folder - moved inside the
edge; it still does not guard the name (the listing produced it), while the
reveal path still does.

**The contract.** `Source/SelfTestLibraryRepository.cpp`, written before the
move, 47 checks. It pins where the library lives, the four folders and the
shared table folder, the slug rules, the envelope and the atomic write, the
publish refusals, the read refusals, the listing (the skipped files and the
other table kind), the first free id, and the reveal target's guard. It
reaches the storage through the old static methods; the move turns them
into the repository's, and the checks themselves do not change.

**The mechanical check.** The production code does not call a test-only
door, the diff outside the storage's methods is empty, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.27 R5.1 - the score canvas's render

**What moves.** The canvas's drawing moves out of `ScoreEditor` into the new
`Source/ScoreRenderer.h` / `Source/ScoreRenderer.cpp`: the frame, the grid
(the time lines, the pitch lines, the sieve's pitch guides and the zone's
time grid), the sieve zones, the arcs (the stroke, the dots and the tags),
the in-progress gesture (the stroke, the line's rubber band, the zone's
rubber rectangle and the selection marquee), the scrub line, the labels, the
playhead, the workspace's mark and the hint lines. The review asks for the
render to be a thing of its own (R043, "Separate ScoreRenderer from
ScoreEditor") and for the rendering, the hit testing and the coordinate
conversion to agree without being one operation.

The render draws what it is handed: a `View` (where the plot is and how the
page's time and pitch map onto the screen) and a `State` (the page, the
waves, the tuning, the selection, the layers, the flags and the gesture). It
owns nothing and it changes nothing.

**What stays.** `ScoreEditor` keeps the gesture (the mouse and the touch
handling, the tools, the quantisers, the commit of a stroke), the selection,
the model's commands and the wheel's view commands. The editor's public
mapping (`timeToX()`, `freqToY()`, …) stays, and now delegates to the same
`View` the render draws with, so the drawing and the hit testing cannot
drift apart.

**The one change.** None in the behaviour. The bodies are the editor's
`paint()`, body for body: the colours, the order of the passes (the zones
under the grid, the arcs over it, the labels above them, the playhead over
the labels) and the wording of the hint lines do not change by a character.
The `View` carries the mapping that used to live in the editor's five
methods; the formulas are the same, body for body.

**The contract.** `Source/SelfTestScoreRender.cpp`, written before the move,
18 checks. It paints the canvas off-screen into a `juce::Image` and pins the
picture: the background and the frame, the grid (on and off), the A440 and C
lines, the sample layer's Ref line, the arc's dot, the layer rule, the
selected arc's brighter dot, a zone's translucent fill, the playhead (and
its absence at zero), the workspace's mark, the hints (on and off change the
image), the render's determinism, and that painting mutates neither the
model nor the view. The probes read single pixels at positions the editor's
own public mapping gives them, so they do not hard-code the mapping.

**The mechanical check.** The production code does not call a test-only
door, the diff outside the render is empty, and the behaviour is unchanged -
the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.28 R5.2 - the score canvas's coordinate mapper

**What moves.** The mapping between the page's musical coordinates and the
screen moves out of `ScoreEditor` and out of the renderer's header into the
new `Source/ScoreCoordinateMapper.h`: the time axis (`xToTime()` /
`timeToX()`), the pitch axis (`yToFreq()` / `freqToY()`) and a zone's
rectangle (`zoneRect()`). The review asks for the mapping to be a thing of
its own (R044, "Introduce ScoreCoordinateMapper") and says what it is for:
"`ScoreCoordinateMapper` should supply consistent mappings between musical
and screen coordinates" (§24).

The mapper is `upic::score_coordinates::ScoreCoordinateMapper`: a small value
that carries where the plot is on the screen, how long the page is, over
which ambitus the pitch runs and where the view is zoomed and scrolled. It
converts and it owns nothing.

**What stays.** `ScoreEditor` keeps the gesture, the selection, the model's
commands and the wheel's view commands (the zoom, the pan, the fit) - the
view's *state* is the editor's, and the mapper is handed it. The editor's
public mapping (`timeToX()`, `freqToY()`, …) stays, and now delegates to the
mapper.

**The one change.** None in the behaviour. The bodies are the editor's, body
for body: the clamping of a cursor outside the page, the deliberate
*non*-clamping of a pitch outside the ambitus (it is drawn off the edge, not
along the border) and the sorting of a zone's corners do not change by a
character.

**The contract.** `Source/SelfTestCoordinateMapper.cpp`, written before the
move, 14 checks. It reaches the mapping through the editor's own doors
(`timeToX()`, `xToTime()`, `freqToY()`, `yToFreq()`, `zoneRectForTest()`) and
pins: where time 0 and the page's end are on the screen, the round trips (a
time and a pitch survive the trip out and back), the ambitus (its low end is
the plot's bottom, its high end the plot's top), the non-clamping of a pitch
outside the ambitus, the clamping of a cursor outside the page, a zone's
rectangle (its times and its frequencies are order-independent), and the zoom
(it keeps the view's centre where it was, and a time survives the round trip
through it).

**The mechanical check.** The production code does not call a test-only
door, the diff outside the mapping is empty, and the behaviour is unchanged -
the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.29 R5.3 - the score canvas's hit tester

**What moves.** The answer to "what is under the cursor?" moves out of
`ScoreEditor` into the new `Source/ScoreHitTester.h`: the arcs near a point
(`arcsNear()`), the nearest arc (`pickArc()`), the zone under a point
(`pickZone()`) and the zone's handle under a point (`zoneHandleAt()`). The
review asks for the hit testing to be a thing of its own (R045, "Extract
ScoreHitTester") and says what it is for: "`ScoreHitTester` should identify
arcs, points or handles under a given coordinate" (§24).

The hit tester answers from what it is handed: the mapper (R044), the page
(the arcs and the zones), the layer (only this layer's arcs are under the
cursor) and whether a touch pointer has been seen (a finger gets a wider
grab band). It reads them and writes nothing.

**What stays.** `ScoreEditor` keeps the gesture: it asks the hit tester the
question and acts on the answer (the selection, the marquee, the zone's
drag, the erase, the tools). The editor's private `arcsNear()`,
`pickArc()`, `pickZone()` and `zoneHandleAt()` stay as the editor's way in,
and now delegate.

**The one change.** None in the behaviour. The bodies are the editor's, body
for body: the 12-pixel grab band (20 for a touch), the nearest-first
ordering, the layer rule, the 4-pixel slack on a zone's border, the
topmost zone first and the eight handles do not change by a character.

**The contract.** `Source/SelfTestScoreHitTest.cpp`, written before the
move, 34 checks. It reaches the hit testing through the editor's own doors
(`arcsNearForTest()`, `pickArcForTest()`, `pickZoneForTest()`,
`zoneHandleAtForTest()`) and, for the last group, through the editor's real
mouse handling. It pins: the arcs near a point and their nearest-first
ordering, the nearest arc, the grab band (inside, outside and given), an
arc hit along its line and not only at its points, the layer rule, the zone
under a point, the border's slack, the topmost zone, the eight handles and
none, and the end-to-end click (the Select tool selects the arc under the
cursor, the Zone tool selects the zone under it).

**The mechanical check.** The production code does not call a test-only
door, the diff outside the hit testing is empty, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

### 6.30 R5.4 - the score canvas's editing tools

**What moves.** The per-drawing-mode conditions leave the canvas's mouse
handlers and move into the tools: the new `Source/ScoreEditingTools.h` holds
one controller per drawing mode - the freehand stroke (Draw and SineStroke),
the straight line, the selection and the move, the erase and the zone. The
review asks for exactly this (R046, "Introduce editing tools", §24):

> "Rather than allowing mouse handlers to accumulate conditions for every
> drawing mode, create an explicit tool controller."

The interface and the pointer's input are in the new
`Source/ScoreEditingTool.h`: `begin()`, `update()`, `end()` and `cancel()`,
and a `PointerInput` that carries the coordinates, the pressure, the
modifiers, the pointer's identity and where the gesture began - what the
review asks the pointer input to include (§24). The tools never see a JUCE
mouse event.

**What stays.** `ScoreEditor` keeps the canvas and the model: the gesture
state (the render borrows it), the commands (`commitStroke()`,
`commitLine()`, `deleteSelected()`, `addZone()`, the hit tester's answers)
and the canvas's own targets (the playhead's handle, the centre band's scrub
target, the pinch, the multi-touch). The mouse handlers keep only what is
the canvas's: the touch handling, the plot's bounds, the two scrub targets -
and then they ask the tool.

**The one change.** The gesture belongs to the tool that began it. Before,
the handlers asked "which tool is active?" on every event, so a tool change
in the middle of a drag changed who finished the gesture (a line drawn with
the Line tool was dropped when another tool was picked in between; a stroke
was committed). Now the tool that took the gesture finishes it. A tool change
during a drag needs a toolbar click, which needs the pointer to be released
first, so this cannot be reached by hand - it is the review's model made
explicit.

**The contract.** `Source/SelfTestEditingTools.cpp`, written before the
move, 45 checks. It drives the editor with synthesised mouse events - the
way the app's own UI tests do it - and pins: the freehand stroke (one arc,
the points sorted by time, the release's point part of the stroke, the
points on the page, the pressure, the material, the layer, the
simplification and the fine mode's exemption from it, the refusal of a
stroke that is too short, the layer switch that cancels it), the undo
transaction (one labelled transaction per gesture), the sine stroke (one
arc, the wobble around the line), the line (one arc that follows the drag,
its own label, the refusal of a line that is too short), the erase (the arc
under the cursor, and nothing on empty space), the selection (the click,
the ctrl-click, the shift-click, the click on empty space), the marquee, the
move, the zone (the drag, the times, the pitches, its label) and the scrub
band (it scrubs, and it draws nothing).

**The mechanical check.** The production code does not call a test-only
door, the bodies are the editor's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

**Gate.** The suite passes, the rest of the selftest is untouched, the
golden render and the legacy fixture are unchanged, and the full gate is
green.

## 7. Deliberately not doing (yet)

Taken from the review's own §62 "patterns to avoid", plus the adaptations in
§2 above:

- **No bulk file move.** No `Source/Domain/…` tree until a module is actually
  extracted into it.
- **No JUCE type migration** in the domain. `juce::String` and `juce::var`
  stay at the edges; the piece format is JSON and the codec is `juce::var`.
  What the wave moves is the *file format* out of the domain's data, one
  type at a time - the value types the data holds stay as they are.
- **No CMake target split before the boundary check.** The Windows gate does
  not use the root CMake file, so a CMake-only restructuring would be
  unverified by the gate.
- **No rewriting of the DSP kernels.** The synthesis is extracted as it is,
  under the golden-render gate; it is not "improved".
- **No typed-ID migration** until a concrete confusion is on record.
- **No UI redesign.** The card-based interface is preserved exactly.
- **No render cache** until a profile says a cache would pay for itself.
- **No new framework**, no event bus, no god class called `ApplicationContext`
  (it is a construction root, nothing more).

## 8. Progress

| Phase | Status | Commit | Gate |
|---|---|---|---|
| R0 Baseline | **shipped** | `0003f33`, `620a3e9`, `12f8a61` | app selftest 1,846/1,846, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R1 Low-risk extraction | **R1.3 shipped** | `1ba66d2`, `713dabd`, `84bc261` | app selftest 1,993/1,993, R0.1 unchanged, R1.2 71/71, R1.3 76/76, seven- and four-viewport digests identical, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R2 Domain boundaries | **R2.1 + R2.2 + R2.3 + R2.4 + R2.5 + R2.6 + R2.7 + R2.8 + R2.9 + R2.10 + R2.11 + R2.12 + R2.13 shipped** | `ea83dac`, `d58120d`, `bb23e1c`, `bb0d88c`, `f3aa455`, `f44c399`, `9ff1c1c`, `829f977`, `856fa7a`, `54f07c5`, `806b0de`, `2fdf7d1`, `43fc917`, `b90c295`, `2300f93`, `b44e1c1`, `46aeb11`, `6f29d51`, `d927503`, `f581985`, `1cd17b5`, `395f121`, `fce238c` | app selftest 2,706/2,706, the domain's numeric core, the arc, the arc's geometry and the page are JUCE-free and checked by the gate, the domain's own suite 61/61, the arc's own suite 87/87, the geometry's own suite 58/58, the page's own suite 73/73, the group's own suite 43/43, the envelope's own suite 31/31, the wave's own suite 29/29, the channel's own suite 54/54, the sample slot's own suite 71/71, the tuning's own suite 46/46, the sieve zone's own suite 65/65, the performance sequence's own suite 48/48, the frequency space definition's own suite 47/47, every domain header's file format at the codec edge, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R3 Document and commands | **R3.5 shipped** | `27f2a62`, `4f8be2a`, `15c77b8`, `63f3576`, `9b4e6f7`, `ab32a05`, `9142a17`, `0432be3` | app selftest 2,993/2,993, the document's revision, the epoch, the unsaved flag, the undo history, the selection and the command dispatcher have one owner - the document session, the session's own suite 30/30, the history's own suite 59/59, the selection's own suite 27/27, the command's own suite 29/29 and the edits' own suite 140/140 - every user edit that changes the piece is one labelled undo transaction and the revision advances once per edit, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R4 Persistence | **R4.1 + R4.2 + R4.3 + R4.4 shipped** | `479d99b`, `6691a55`, `808dd88`, `f2e6fe2`, `d9762fb`, `04b9619`, `27b4305`, `ac0b707` | app selftest 3,166/3,166, the document's file format lives at the codec edge (`Source/PieceCodec.h`) - the decode, the migration, the validation and the repairs - and the shell keeps the commit, the codec's own suite 82/82, a refused document leaves the piece alone, the document's file adapter lives at an edge of its own (`Source/DocumentRepository.h`) - the read, the write and the size limits - and the shell keeps the picking and the wording, the adapter's own suite 22/22, the recovery snapshot's storage lives at an edge of its own (`Source/RecoveryRepository.h`) - the folder, the write, the read and the cleanup - and the shell keeps the service and the words, the repository's own suite 21/21, the user library's storage lives at an edge of its own (`Source/LibraryRepository.h`) - the folder, the atomic write, the validated read, the listing and the reveal - while the asset's format stays in `Source/UserLibrary.h` and the shell keeps the service, the library repository's own suite 47/47, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R5 UI decomposition | **R5.1, R5.2, R5.3 and R5.4 shipped** | `bf1ad70`, `e7dd14c`, `c2437c2`, `3b23d62`, `be94fc2`, `a563576`, `a3b3eb0`, `1d6ad4d` | app selftest 3,277/3,277, the score canvas's render lives at an edge of its own (`Source/ScoreRenderer.h`) - the frame, the grid, the zones, the arcs, the gesture and the playhead - while the editor keeps the gesture, the selection and the commands; the canvas's coordinates live at an edge of their own (`Source/ScoreCoordinateMapper.h`) and the hit testing at another (`Source/ScoreHitTester.h`), so the drawing and the hit testing share one mapping; and each drawing mode has its own controller (`Source/ScoreEditingTools.h`), so the mouse handlers no longer accumulate a condition per mode. The render's own suite 18/18, the mapper's own suite 14/14, the hit tester's own suite 34/34, the tools' own suite 45/45, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R6 Real-time audio | not started | - | - |
| R7 Plugin and platform | not started | - | - |
| R8 Build and tests | **R8.1 shipped** | `3c3293f` | dependency boundaries 5 rules / 20 self-tests over five JUCE-free groups, the gate fails on a forbidden include, on a source the `.jucer` does not know and on a source missing from the plugin's list, app selftest 2,706/2,706, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R9 Integration and cleanup | not started | - | - |

Baseline commit: `b5368b1` (0.38.0), app selftest 1,785/1,785, legacy fixture
`8BF28ABA7975…`, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS.

### R0 record (2026-11-26)

Three characterization suites, 61 new checks, no production-code change:

| Suite | File | Checks | Pins |
|---|---|---|---|
| Layout | `Source/SelfTestLayout.cpp` | 30 | The card rank order and placement at five viewports (1600×1000, 1280×800, 1024×700, 800×600, 460×360), no overlap, the cards inside the window and the toolbar, every toolbar control on a card, determinism (twice, and after a resize and back), the document untouched, the permanent strip at every size. |
| Document | `Source/SelfTestDocument.cpp` | 10 | A piece with every optional feature (WP2 table, WP4 sequence, WP5 modulation, WP6 channel, M4 tuning, WP9 workspace) keeps its banks and the arcs' material names through load and save, the canonical form is stable across save/load/save, a non-piece is rejected without touching the active piece, and a plain piece does not grow the optional properties. |
| Transport and preview | `Source/SelfTestTransport.cpp` | 21 | Play and Stop from each of the four states (including the no-ops and the cancelled fade), previews refused while the transport is not stopped, one route at a time, a stale cancel cannot release a newer route, Play from Stopped cancels the routes. |

Test family after R0: 14 files, 938 KB, 19,296 lines (was 11 files,
905 KB). The `MainComponent` family is unchanged: 13 files, 762 KB,
17,169 lines. The R0 commits contain test code and three `.jucer` file
entries only.

### R1.1 record (2026-11-26)

The card layout engine is out of `resized()`; `upic::layout` owns the
geometry and `MainComponent` applies it.

| Measure | Before | After |
|---|---|---|
| `resized()` | 1,400 lines | 1,051 lines |
| `MainComponentLayout.cpp` | 2,475 lines / 114,402 bytes | 2,127 lines / 100,013 bytes |
| the engine | - | `CardLayoutEngine.h` 140 + `CardLayoutEngine.cpp` 565 lines |

The engine's types: `LayoutMetrics`, `CellSpecification`,
`CardSpecification`, `CardLayoutConstraints`, `CellPlacement`,
`CardPlacement`, `CardLayoutResult`, `StripCardPlacement`,
`StripLayoutResult`. It includes `<vector>`, `<algorithm>` and `<cmath>` and
no JUCE header. The adapter keeps the widget pointers (`FlowItem` /
`FlowGroup`), the text measurement (`neededWidth`, `captionWidthDp`,
`headerWidthDp`), the knob's design scale/caption/fallback and every
`setBounds` / `setVisible` call. See `card-layout-engine.md`.

The proof is two-layered: the R0.1 suite (30 checks) passes unchanged, and
a temporary digest of every descendant's bounds and visibility plus every
card rectangle was identical at all five viewports before and after
(`ec5437a1…`, `40f5233c…`, `bedc4aa2…`, `9b09ff5f…`, `1f982028…`). The
digest was removed; the structural suite stays.

The plugin's `Plugin/CMakeLists.txt` and the `.jucer` list carry the new
source. The Android generated list (`Builds/Android/app/CMakeLists.txt`,
explicit sources) picks it up on the next Projucer resave; the H5 wave owns
that step.

Gate: app selftest 1,846/1,846, legacy fixture `8BF28ABA7975` unchanged,
plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS.

### R1.2 record (2026-11-26)

The bottom half of `resized()` is out as well; `upic::layout` owns the
panels below the toolbar and `MainComponent` applies them.

| Measure | Before | After |
|---|---|---|
| `resized()` | 1,051 lines | 989 lines |
| `MainComponentLayout.cpp` | 2,127 lines / 100,013 bytes | 2,066 lines / 96,558 bytes |
| the engine | - | `BottomLayoutEngine.h` 101 + `BottomLayoutEngine.cpp` 273 lines |
| the suite | - | `Source/SelfTestBottomLayout.cpp`, 71 checks |

The engine's types: `EditorColumnLayout`, `VoiceLayout`, `TabStripLayout`,
`BottomLayoutConstraints`, `BottomLayoutResult`, and the entry points
`layoutEditorColumn()`, `layoutTabStrip()` and `layoutBottom()`. It
includes `<vector>`, `<algorithm>` and `<cmath>` and no JUCE header. The
adapter keeps the UI state flags, the preset buttons' ideal widths, every
`setBounds` / `setVisible` call, the rule that a hidden control keeps its
previous bounds, `PageWorkspace::boundsForCanvas()` and the canvas origin.
See `bottom-layout-engine.md`.

The proof is two-layered: the new structural suite (71 checks) passes, and
a temporary digest of every descendant's bounds and visibility was
identical at all seven viewports before and after (`5d39547a…`,
`13c5ae71…`, `1e9bb2f4…`, `f6286086…`, `39644fb4…`, `8905b165…`,
`8460fc44…`). The digest was removed; the structural suite stays. The R0.1
suite is untouched.

The seven viewports are the five R0 ones plus 700×900 (a wide portrait
window, the editors side by side) and 460×1400 (a tall portrait window,
the editors stacked). The tall window is deliberate: at 460×800 the
toolbar's fit-shrink drops the layout scale to 0.55, and the 460 dp
side-by-side threshold shrinks with the scale, so the editors do not stack
there. At 460×1400 the scale stays at 1.0 and the stacked branch is
reached.

The plugin's `Plugin/CMakeLists.txt` (both targets) and the `.jucer` list
carry the new source. The Android generated list picks it up on the next
Projucer resave; the H5 wave owns that step.

Gate: app selftest 1,917/1,917, legacy fixture `8BF28ABA7975` unchanged,
plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS.

### R1.3 record (2026-11-26)

The overlay cards are out as well; `upic::layout` owns the three cards and
`MainComponent` applies them. With R1.3, `MainComponent` no longer computes
any layout geometry of its own.

| Measure | Before | After |
|---|---|---|
| `layoutOverlays()` | 100 lines | 73 lines |
| `resized()` | 989 lines | 980 lines |
| `MainComponentLayout.cpp` | 2,066 lines / 96,558 bytes | 2,088 lines / 95,830 bytes |
| the engine | - | `OverlayLayoutEngine.h` 64 + `OverlayLayoutEngine.cpp` 123 lines |
| the shared rectangle | - | `Source/LayoutRect.h`, 79 lines |
| the suite | - | `Source/SelfTestOverlayLayout.cpp`, 76 checks |

The engine's types: `AudioOverlayLayout`, `SettingsOverlayLayout`,
`ClearOverlayLayout`, and the entry points `layoutAudioOverlay()`,
`layoutSettingsOverlay()` and `layoutClearOverlay()`. It includes
`<cmath>` only and no JUCE header. The adapter keeps the backdrops, the
visibility rules and every `setBounds()` call; the card's frame stays with
the overlay component, because the overlay paints it. See
`overlay-layout-engine.md`.

`Source/LayoutRect.h` now holds the rectangle the engines share and the
`juce::Rectangle` semantics they need (`reduced`, `removeFromTop`,
`removeFromBottom`, `removeFromLeft`, `removeFromRight`, with the
clamping). The card engine, the bottom engine and the overlay engine use
that one implementation.

The proof is two-layered: the new structural suite (76 checks) passes, and
a temporary digest of every widget's bounds and visibility was identical at
all four viewports before and after (`1298551f…`, `3d438b40…`,
`d1fc67f6…`, `411e71af…`). The digest was removed; the structural suite
stays. The R0.1 and R1.2 suites are untouched.

The suite runs last in the selftest: it opens the audio device selector and
refreshes the MIDI device list, and nothing after it should see that. The
four MIDI Learn buttons all carry the name "Learn" (the button icons are
chosen by the name), so the suite tells them apart by the row they are in.

The plugin's `Plugin/CMakeLists.txt` (both targets) and the `.jucer` list
carry the new sources. The Android generated list picks them up on the
next Projucer resave; the H5 wave owns that step.

Gate: app selftest 1,993/1,993, legacy fixture `8BF28ABA7975` unchanged,
plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS.

### R8.1 record (2026-11-26)

The boundary is now enforced instead of described. R8 runs alongside the
other waves; this is its first piece.

| Measure | Before | After |
|---|---|---|
| the dependency rules | written down in the notes | 5 rules, checked by the gate |
| the check | - | `scripts/check-boundaries.ps1`, 432 lines, 18 self-tests |
| the gate | 6 steps | 7 steps - the boundaries run before the build |

The rules: **B1** the layout engines include no JUCE header; **B2** they
include no app header outside the engine group (`LayoutRect.h`,
`CardLayoutEngine.h`, `BottomLayoutEngine.h`, `OverlayLayoutEngine.h`);
**B3** `UPICuitous.jucer` lists every `Source/*.cpp` on disk; **B4** the
plugin's two CMake targets carry the same, complete source list; **B5** the
generated Visual Studio project lists every `Source/*.cpp` on disk.

B3 to B5 are the silent-omission rules. A new `.cpp` that the Projucer does
not know about is not compiled by the app project, so the gate would pass
without ever building it - the failure mode R1.3 walked into by hand. B4
catches the same omission in the plugin, where a new engine would otherwise
be missing from the VST3 and from its smoke test.

The checker is tested before it is trusted: 18 cases prove that every rule
fires on a tree that breaks it, that the permitted includes stay quiet, and
that each manifest reader reads the real manifest (the `.jucer` entries
under `Source/`, the right CMake target's list across the lines, the Visual
Studio project's entries). The self-test runs on every gate run, so a
checker that stops detecting anything fails the gate rather than passing
it. Two of the readers were wrong when the check was first written - a
multi-line CMake block that was not matched and a selftest file that the
plugin pattern caught - and the tree check found both before the check
reached the gate.

The negative test is the real proof, and it was run on the tree: a JUCE
header in `OverlayLayoutEngine.cpp`, a source file the `.jucer` does not
know, and a source missing from the plugin's list each fail the gate with
the file and the line named, before the build starts.

What the check does not cover yet: the plugin still depends on the desktop
shell (`PluginProcessor.cpp` includes `MainComponent.h`) - that is R7's job
and the highest-risk change in the list; the CMake targets do not exist yet
(R079–R082); the test suites are not split yet (R086–R089); and the
Android generated list is not checked, because it is only rewritten by a
Projucer resave, which the H5 wave owns. See
[`dependency-boundary-check.md`](dependency-boundary-check.md).

Gate: dependency boundaries 5 rules / 18 self-tests, app selftest
1,993/1,993, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.1 record (2026-11-26)

The domain wave starts with its numeric core: the four headers the rest of
the model stands on no longer depend on JUCE.

| Measure | Before | After |
|---|---|---|
| the domain's numeric core | 4 headers, 3 of them including `<JuceHeader.h>` | 4 headers, no JUCE header |
| the numeric helpers | `juce::jmax` / `jmin` / `jlimit` | `std::max` / `std::min` / `std::clamp` |
| the domain's own suite | - | `Source/SelfTestDomain.cpp`, 61 checks |
| the gate's JUCE-free groups | 1 (the layout engines) | 2 (the layout engines, the domain's numeric core) |

The four headers are `UpicConstants.h` (the units - the frequency range, the
default ambitus, the page's length), `PieceLimits.h` (the validation - the
file caps, the id range, the array sizes, and the clamps that reject a
NaN), `UpicMath.h` (the conversions and the lookups - MIDI and frequency,
the pitch space, the page's own interval, the snap, the time grid, the stop
fade, the segment search and the cursor) and `Ids.h` (the id helpers).
Three of them included the JUCE umbrella header for three numeric helpers
and nothing else.

The replacements are the standard library's, with the same expressions:
`jmax (a,b)` and `std::max (a,b)` are both `a < b ? b : a`, `jmin (a,b)`
and `std::min (a,b)` are both `b < a ? b : a`, and `jlimit (lo,hi,v)` and
`std::clamp (v,lo,hi)` are both `v < lo ? lo : (hi < v ? hi : v)`. The NaN
behaviour is therefore the same - all three leave a NaN unchanged, which is
exactly why the domain has its own `clampFinite`.

The suite came first. `Source/SelfTestDomain.cpp` (61 checks) was written
against the headers as they were, and it passes unchanged after the move. It
pins the units (A4 is 440 Hz, the round trip, the clamping; the pitch
space's ends and its logarithmic middle; the page's own interval, including
the rule that a pitch outside it is *off the page* and not clamped), the
validation (the clamps, the NaN rule, the infinities, the gain and the
intensity), the lookups (the segment search finds the first point at or
after the key; the cursor follows the time forwards and backwards and
answers the same) and the ids (the duplicate that gets a free id, the
impossible id that gets replaced, the bank's first hole). Those checks did
not exist before: the units and the lookups were covered only indirectly,
through the app.

The gate's second JUCE-free group is this one, so the boundary cannot be
broken quietly: a JUCE header in any of the four headers fails the gate
before the build. The negative test was run on the tree - `#include
<JuceHeader.h>` in `UpicMath.h` fails the gate with the file and the line
named.

What is still open: `Arc.h` needs JUCE for its `juce::var` serialization,
so the codec edge has to move out of the class first (R2.2); the other
domain headers still carry their own `toVar` / `fromVar` members; and
there is no headless domain target yet - the suite runs inside the app's
selftest binary, which is a JUCE application (the review's R089). See
[`domain-numeric-core.md`](domain-numeric-core.md).

Gate: app selftest 2,054/2,054, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.2 record (2026-11-26)

The arc's codec edge: the model's central type no longer depends on JUCE.

| Measure | Before | After |
|---|---|---|
| the arc | `Source/Arc.h`, 582 lines, including `<JuceHeader.h>` | `Source/Arc.h`, 524 lines, no JUCE header |
| the codec | two members of the class | `Source/ArcCodec.h`, two free functions |
| the arc's own suite | - | `Source/SelfTestArc.cpp`, 87 checks |
| the gate's JUCE-free groups | 2 (the layout engines, the domain's numeric core) | 3 (the layout engines, the domain's numeric core, the arc) |

The two members became `upic::arcToVar (const Arc&)` and `upic::arcFromVar
(const juce::var&)`. The bodies are the old members, body for body: the
property names, their order, the optional keys a default arc does not
write and the clamps a crafted file gets are the file format, and not a
character of them changed. The rest of the class lost JUCE in the same
step - `juce::jmax`, `juce::jmin` and `juce::jlimit` became `std::max`,
`std::min` and `std::clamp`, the same substitution R2.1 made in the
numeric core, with the same expressions and therefore the same results.

The suite came first. `Source/SelfTestArc.cpp` (87 checks) was written
against the arc as it was, and it passes unchanged after the move; it
reaches the codec through two helpers at the top of the file, so the
rename touched the suite in one place only. It pins the arc's defaults,
the four classical transformations with their edges (an empty arc, a
single-point arc, a zero or non-finite factor, the clamps), the
interpolation the engine reads per sample (the log-domain pitch, the life,
the pressure, the cursor that follows the time forwards and backwards),
and the codec: the round trip of every field, the defaults of an arc that
is not there, the clamps a crafted file gets, the points that come back in
time order, and the file's shape - the short keys and the optional keys a
default arc does not write.

The last part is why the suite is written this way: the property names
are the file format. A rename in the codec would silently drop everything
a piece carries, and no golden render would notice.

The gate's third JUCE-free group is the arc, so the boundary cannot be
broken quietly: a JUCE header in `Arc.h` fails the gate before the build.
The negative test was run on the tree - `#include <JuceHeader.h>` in
`Arc.h` fails the gate with the file and the line named.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (the page's codec is the next one that
matters - it holds the arcs); the arc's geometry is still in the same file
as its data (the review's R012); and there is no headless domain target
yet - the suite runs inside the app's selftest binary, which is a JUCE
application (the review's R089). See
[`arc-codec-edge.md`](arc-codec-edge.md).

Gate: app selftest 2,141/2,141, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.3 record (2026-11-26)

The arc's data and the arc's geometry are separate now - the review's
R012.

| Measure | Before | After |
|---|---|---|
| the arc | `Source/Arc.h`, 524 lines: the data and the geometry | `Source/Arc.h`, 264 lines: the data only |
| the geometry | in the same file | `Source/ArcGeometry.h`, 291 lines |
| the geometry's own suite | - | `Source/SelfTestArcGeometry.cpp`, 58 checks |
| the gate's JUCE-free groups | 3 (the layout engines, the domain's numeric core, the arc) | 4 (the layout engines, the domain's numeric core, the arc, the arc's geometry) |

The whole WP8 block moved, the same text in the same order: the frame (the
page's normalised space), the scope and its bounds, the pitch centre, the
rule that writes a moved point back, the rule that puts the points back in
time order, and the six operations the UPIC's page could do to a drawing
- the rotation, the two mirrors, the scale, the alignment, the repeat.

`Source/Arc.h` keeps the data: `ArcPoint`, `Arc`, the four classical
transformations and the interpolation the engine reads per sample.

The direction is the point: the geometry knows the arc, the arc does not
know the geometry. The gate says so - `ArcGeometry.h` may include
`Arc.h`, `Arc.h` may not include `ArcGeometry.h` - and the geometry is the
gate's fourth JUCE-free group, so a JUCE header in it fails the gate
before the build. The negative test was run on the tree: `#include
<JuceHeader.h>` in `ArcGeometry.h` fails the gate with the file and the
line named.

The suite came first. `Source/SelfTestArcGeometry.cpp` (58 checks) was
written against the geometry where it was, and it passes unchanged after
the move; it reaches the geometry through one include, so the move touched
the suite in one place. It pins the frame (the times to 0..1, the ambitus
to 0..1, the conversions are their own inverse - and the edges: a frame
without time, a frame without an ambitus, a pitch below 1 Hz), the bounds
of a scope, the pitch centre, the write-back rule, and each transform
with its edges and its invariants: a transform may move a curve, but it may
not leave it unsorted, out of range or not a number.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (the page's codec is the next one that
matters - it holds the arcs); the coordinate conversions are still in two
places - the frame's in the geometry, the model's in `UpicMath.h` (the
review's R015 asks for one); and there is no headless domain target yet -
the suites run inside the app's selftest binary, which is a JUCE
application (the review's R089). See
[`arc-geometry.md`](arc-geometry.md).

Gate: app selftest 2,199/2,199, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.4 record (2026-11-27)

The page's data and the page's file format are separate now - the review's
R020, "Remove unnecessary JUCE dependencies from domain", one step further.

| Measure | Before | After |
|---|---|---|
| the page | `Source/Page.h`, 547 lines: the data, the rules and the file format | `Source/Page.h`, 381 lines: the data and the rules, no JUCE header |
| the page's codec | two members of the class | `Source/PageCodec.h`, 214 lines: `pageToVar` and `pageFromVar` |
| the page's own suite | - | `Source/SelfTestPageCodec.cpp`, 73 checks |
| the gate's JUCE-free groups | 4 (the layout engines, the domain's numeric core, the arc, the arc's geometry) | 5 (the same, plus the page) |

The two members became two free functions:

| before | after |
|---|---|
| `Page::toVar()` | `upic::pageToVar (const Page&)` |
| `Page::fromVar (const juce::var&)` | `upic::pageFromVar (const juce::var&, int* renamedIds)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `p.` the free function needs. The
property names, their order, the optional keys a default page does not
write and the clamps a crafted file gets are the file format itself, and
they did not change by a character.

`Source/Page.h` keeps the data and the rules: the arcs, the zones, the
four frequency spaces, the groups, the ambitus, the warp and the time map.
It lost JUCE with the codec: the numeric helpers it used (`juce::jlimit`,
`juce::jmax`, `juce::jmin`) are the standard library's, with the same
expressions and therefore the same results - the same substitution R2.1
made in the numeric core and R2.2 made in the arc. `Page.h` no longer
includes `<JuceHeader.h>`.

The page is the gate's fifth group, so the boundary cannot be broken
quietly: a JUCE header in `Page.h` fails the gate before the build, and so
does a shell header the page has no business knowing. The negative test was
run on the tree: `#include <JuceHeader.h>` in `Page.h` fails the gate with
the file and the line named.

The JUCE the page still sees comes from the types it holds (the group's
`juce::String name` and `juce::Colour`), not from its own code. That is
the next step of the wave: the `juce::String` and the `juce::Colour` leave
the domain's data.

The suite came first. `Source/SelfTestPageCodec.cpp` (73 checks) was
written against the codec where it was, and it passes unchanged after the
move; it reaches the codec through one door (the `encode` / `decode`
helpers at the top of the file), so the move touched the suite in one
place. It pins the keys and their order, the optional keys, the clamps,
the ids, the caps, the group's membership and the round trip.

Two things came out of writing it:

- **`Source/Page.h` called `uniquifyIds()` without including `Ids.h`.** It
  only compiled because every file that reached the page included
  `UpicModel.h` first, and the model included `Ids.h` before `Page.h`. The
  new suite reaches the page directly, and the compiler said so. The page
  now includes what it uses.
- **A renamed group is not reported.** The counter the loader hands back
  counts the arcs and the zones; the groups are counted after the counter
  was read. The suite pins the behaviour as it is - a refactoring phase does
  not change what a piece load tells the user - and the note is on record
  so that a later phase can decide whether it is a bug worth fixing on its
  own.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Envelope.h`, `Piece.h`, `Sample.h`,
`Tuning.h`, `Sieve.h`, `Waveform.h`, `ArcGroup.h`, `Channel.h`,
`Performance.h`, `FreqSpace.h`), and the domain's data still holds JUCE
values (the `juce::String name` and the `juce::Colour` of the groups, the
envelopes, the waveforms and the channels); the sample's file reading is
still in the domain (`Source/Sample.h` reads `juce::File`s); and there is
no headless domain target yet - the suites run inside the app's selftest
binary, which is a JUCE application (the review's R089). See
[`page-codec-edge.md`](page-codec-edge.md).

Gate: app selftest 2,272/2,272, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.5 record (2026-11-27)

The arc group's data and the arc group's file format are separate now - the
third codec edge, after the arc's (R2.2) and the page's (R2.4).

| Measure | Before | After |
|---|---|---|
| the group | `Source/ArcGroup.h`, 112 lines: the data and the file format | `Source/ArcGroup.h`, 65 lines: the data only |
| the group's codec | two members of the class | `Source/ArcGroupCodec.h`, 79 lines: `arcGroupToVar` and `arcGroupFromVar` |
| the group's own suite | - | `Source/SelfTestArcGroup.cpp`, 43 checks |
| the gate's JUCE-free groups | 5 | 5 (the group is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `ArcGroup::toVar()` | `upic::arcGroupToVar (const ArcGroup&)` |
| `ArcGroup::fromVar (const juce::var&)` | `upic::arcGroupFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `g.` the free function needs. The
property names, their order, the optional keys a default group does not
write and the clamps a crafted file gets are the file format itself, and
they did not change by a character.

**The value types stay.** The group holds `juce::String name` and
`juce::Colour colour`. A first cut of this phase replaced them with text
and an ARGB number, which made the group JUCE-free and let it join the
gate's groups; that was the type migration §2 defers, so it was taken back.
The separation stays - the file format is at the edge - and the data keeps
what it had. The gate's groups are the same five as before this phase.

**The checker learned one thing.** An empty permitted list is a list: it
forbids every project header. The old test skipped the check when the list
was empty (an empty array is falsy in PowerShell), so a group that may
include no project header at all could not be expressed. No group uses one
yet - the leaf rule is for the day the domain's value types move - but the
rule is now correct, and two self-tests cover it (18 → 20).

**The suite came first.** `Source/SelfTestArcGroup.cpp` (43 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name and the colour through two more
helpers, so the move touched the suite in one place and the checks
themselves did not change.

What the suite pins, and what is worth knowing:

- **A value that is not a number falls to the lower bound.** The domain's
  `clampFinite` returns the lower bound for a value that is not finite, so a
  NaN gain becomes 0 and a NaN transpose becomes -48. That is what the file
  has always done, and the suite says so.
- **An id of zero is 1, not the fallback.** The clamp runs first, so the
  fallback only matters when the file carries no id at all.
- **The colour is an ARGB number, and it stays one.** The file wrote
  `(int) colour.getARGB()` and reads `(uint32) (int) value` - a negative
  number in the file, and the same colour back.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Envelope.h`, `Piece.h`, `Sample.h`,
`Tuning.h`, `Sieve.h`, `Waveform.h`, `Channel.h`, `Performance.h`,
`FreqSpace.h`), and the domain's data still holds JUCE values (the
`juce::String name` and the `juce::Colour` of the groups, the envelopes,
the waves and the channels) - that is the deferred type migration; the
sample's file reading is still in the domain (`Source/Sample.h` reads
`juce::File`s); and there is no headless domain target yet - the suites run
inside the app's selftest binary, which is a JUCE application (the review's
R089). See [`arc-group-data.md`](arc-group-data.md).

Gate: app selftest 2,315/2,315, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.6 record (2026-11-27)

The envelope's data and the envelope's file format are separate now - the
fourth codec edge, after the arc's (R2.2), the page's (R2.4) and the arc
group's (R2.5).

| Measure | Before | After |
|---|---|---|
| the envelope | `Source/Envelope.h`, 86 lines: the data and the file format | `Source/Envelope.h`, 61 lines: the data only |
| the envelope's codec | two members of the class | `Source/EnvelopeCodec.h`, 56 lines: `envelopeToVar` and `envelopeFromVar` |
| the envelope's own suite | - | `Source/SelfTestEnvelopeCodec.cpp`, 31 checks |
| the gate's JUCE-free groups | 5 | 5 (the envelope is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `Envelope::toVar()` | `upic::envelopeToVar (const Envelope&)` |
| `Envelope::fromVar (const juce::var&, int)` | `upic::envelopeFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `e.` the free function needs and the
whitespace: 8 lines for the writer, 14 for the reader. The property names,
their order, the defaults a file without them falls back to and the clamps
a crafted file gets are the file format itself, and they did not change by
a character.

**The amplitude table comes along.** `AmplitudeTable` is an alias for
`Envelope` - the two banks share the type, the editor and the format - so
the table's file format *is* the envelope's, and the table's four call sites
(the piece's writer and reader, the library's publish and import) moved
with it.

**The value types stay.** The envelope holds `juce::String name` and
`juce::Colour colour`. The plan defers replacing the domain's value types
(§2), so `Source/Envelope.h` still asks for the JUCE header and the
envelope is not one of the gate's JUCE-free groups. The gate's groups are
the same five as before this phase.

**One fix came with the phase.** `Envelope.h` used `SegmentCursor` and
`followSegment` without including the header that declares them
(`Source/UpicMath.h`), so it only compiled when something else had included
it first. The suite reaches the envelope on its own, so the header now says
what it uses - the same include `Arc.h` and `Channel.h` already have. No
behaviour changes.

**The suite came first.** `Source/SelfTestEnvelopeCodec.cpp` (31 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name and the colour through two more
helpers, so the move touched the suite in one place and the checks
themselves did not change.

What the suite pins, and what is worth knowing:

- **The file's defaults are not the data's defaults.** A new envelope is
  called *Flat* and has the palette's orange (`0xffffa500`); a file without
  a name gives *Env*, and a file without a colour gives the file's own
  orange (`0xffff9f0a`). Two different colours, and the file's default is
  the one a crafted or old file gets.
- **The id is not clamped at all.** A negative id stays negative, a huge one
  stays huge, and a fractional one is cut towards zero. The other banks
  clamp their ids; the envelope's does not.
- **A value that is not a number falls to the lower bound.** The domain's
  `clampFinite` returns the lower bound for a value that is not finite, so a
  NaN x falls to 0 and a NaN y falls to 0 as well - not to the y default of
  1, which is what a *missing* y gets.
- **The cap is applied in the file's order, before the sort.** A file with
  more than a thousand points keeps the first thousand *as the file wrote
  them*, and only then are the points sorted in time order.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`,
`Waveform.h`, `Channel.h`, `Performance.h`, `FreqSpace.h`), and the domain's
data still holds JUCE values (the `juce::String name` and the
`juce::Colour` of the groups, the envelopes, the waves and the channels) -
that is the deferred type migration; the sample's file reading is still in
the domain (`Source/Sample.h` reads `juce::File`s); and there is no headless
domain target yet - the suites run inside the app's selftest binary, which
is a JUCE application (the review's R089). See
[`envelope-codec-edge.md`](envelope-codec-edge.md).

### R2.7 record (2026-11-27)

The wave's data and the wave's file format are separate now - the fifth
codec edge, after the arc's (R2.2), the page's (R2.4), the arc group's
(R2.5) and the envelope's (R2.6).

| Measure | Before | After |
|---|---|---|
| the wave | `Source/Waveform.h`, 139 lines: the data and the file format | `Source/Waveform.h`, 108 lines: the data only |
| the wave's codec | two members of the class | `Source/WaveformCodec.h`, 61 lines: `waveformToVar` and `waveformFromVar` |
| the wave's own suite | - | `Source/SelfTestWaveCodec.cpp`, 29 checks |
| the gate's JUCE-free groups | 5 | 5 (the wave is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `Waveform::toVar()` | `upic::waveformToVar (const Waveform&)` |
| `Waveform::fromVar (const juce::var&, int)` | `upic::waveformFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `w.` the free function needs and the
whitespace: 9 lines for the writer, 19 for the reader. The property names
(id, name, colour, samples), their order, the defaults a file without them
falls back to ("Wave", `0xff5ac8fa`) and the cap a table over the limit gets
(4096) are the file format itself, and they did not change by a character.

**The value types stay.** The wave holds `juce::String name` and
`juce::Colour colour`. The plan defers replacing the domain's value types
(§2), so `Source/Waveform.h` still asks for the JUCE header and the wave is
not one of the gate's JUCE-free groups. The gate's groups are the same five
as before this phase.

**The behaviour stayed too.** The wave's generators (`makeSine`, `makeSaw`,
`makeSquare`, `makeTriangle`, `makeNoise`, `makeRandomSmooth`), its
`normalize`, its `smooth`, its `valueAt` lookup and `waveFromTake` - the
digitising of a recording - are the wave's behaviour, not its file format,
so they stayed in `Waveform.h`.

**The suite came first.** `Source/SelfTestWaveCodec.cpp` (29 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name and the colour through two more
helpers, so the move touched the suite in one place and the checks
themselves did not change.

What the suite pins, and what is worth knowing:

- **The file's defaults are not the data's defaults.** A new wave is called
  *Sine* and has the palette's cyan (`0xff00ffff`); a file without a name
  gives *Wave*, and a file without a colour gives the file's own blue
  (`0xff5ac8fa`). The file's defaults are the ones a crafted or old file
  gets, and they are not the ones a new wave has.
- **The id is not clamped at all.** A negative id stays negative, a huge one
  stays huge, and a fractional one is cut towards zero. The other banks
  clamp their ids; the wave's does not.
- **The samples are not clamped either.** A file may carry a value outside
  -1..1 and it survives the read untouched - and so does a value that is not
  a number at all. The table is the wave's, not the engine's.
- **The table keeps the length the file gave it.** The piece carries the
  samples, so a wave imported with 4096 samples stays a 4096-sample wave;
  the table is not padded to the default size. Two exceptions, both pinned:
  an **empty** table and a file with **no** table leave the default table of
  1024 zeros in place.
- **A sample is a double in the file**, not a float: the writer adds
  `(double) s`, and the reader reads it back as `(float) (double)`.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`,
`Channel.h`, `Performance.h`, `FreqSpace.h`), and the domain's data still
holds JUCE values (the `juce::String name` and the `juce::Colour` of the
groups, the envelopes, the waves and the channels) - that is the deferred
type migration; the sample's file reading is still in the domain
(`Source/Sample.h` reads `juce::File`s); and there is no headless domain
target yet - the suites run inside the app's selftest binary, which is a
JUCE application (the review's R089). See
[`wave-codec-edge.md`](wave-codec-edge.md).

### R2.8 record (2026-11-27)

The channel's data and the channel's file format are separate now - the
sixth codec edge, after the arc's (R2.2), the page's (R2.4), the arc
group's (R2.5), the envelope's (R2.6) and the wave's (R2.7).

| Measure | Before | After |
|---|---|---|
| the channel | `Source/Channel.h`, 168 lines: the data and the file format | `Source/Channel.h`, 123 lines: the data only |
| the channel's codec | two members of the class | `Source/ChannelCodec.h`, 77 lines: `channelToVar` and `channelFromVar` |
| the channel's own suite | - | `Source/SelfTestChannelCodec.cpp`, 54 checks |
| the gate's JUCE-free groups | 5 | 5 (the channel is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `Channel::toVar()` | `upic::channelToVar (const Channel&)` |
| `Channel::fromVar (const juce::var&, int)` | `upic::channelFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `c.` the free function needs and the
whitespace: 20 lines for the writer, 21 for the reader. The property names
(id, name, colour, gain, mute, solo, env), their order, the keys that are
left out when they are at their default, the clamps a crafted file gets and
the cap a curve over the limit gets are the file format itself, and they
did not change by a character.

**The value types stay.** The channel holds `juce::String name` and
`juce::Colour colour`. The plan defers replacing the domain's value types
(§2), so `Source/Channel.h` still asks for the JUCE header and the channel
is not one of the gate's JUCE-free groups. The gate's groups are the same
five as before this phase.

**The behaviour stayed too.** The allocation-free `envelopeAt` lookup, the
resolved mixer (`ChannelMix`, with its 64-point LUT) and the build of it
(`buildChannelMix`) are the channel's behaviour, not its file format, so
they stayed in `Channel.h`.

**One fix came with the phase.** `Piece.h` holds a `std::vector<Channel>`
but did not include `Channel.h` - it got the type because `UpicModel.h`
includes `Channel.h` *before* it includes `Piece.h`. The header now says
what it uses (`#include "ChannelCodec.h"`, which carries `Channel.h` with
it), so it stands on its own. No behaviour changes.

**The suite came first.** `Source/SelfTestChannelCodec.cpp` (54 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name and the colour through two more
helpers, so the move touched the suite in one place and the checks
themselves did not change.

What the suite pins, and what is worth knowing:

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

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Sample.h`, `Tuning.h`, `Sieve.h`,
`Performance.h`, `FreqSpace.h`), and the domain's data still holds JUCE
values (the `juce::String name` and the `juce::Colour` of the groups, the
envelopes, the waves and the channels) - that is the deferred type
migration; the sample's file reading is still in the domain
(`Source/Sample.h` reads `juce::File`s); and there is no headless domain
target yet - the suites run inside the app's selftest binary, which is a
JUCE application (the review's R089). See
[`channel-codec-edge.md`](channel-codec-edge.md).

### R2.9 record (2026-11-27)

The sample slot's data and the sample slot's file format are separate now -
the seventh codec edge, after the arc's (R2.2), the page's (R2.4), the arc
group's (R2.5), the envelope's (R2.6), the wave's (R2.7) and the channel's
(R2.8).

| Measure | Before | After |
|---|---|---|
| the sample slot | `Source/Sample.h`, 411 lines: the data, the behaviour and the file format | `Source/Sample.h`, 286 lines: the data and the behaviour |
| the sample slot's codec | four members of the class | `Source/SampleCodec.h`, 156 lines: `sampleSlotToPcm16`, `sampleSlotToVar` (twice) and `sampleSlotFromVar` |
| the sample slot's own suite | - | `Source/SelfTestSampleCodec.cpp`, 71 checks |
| the gate's JUCE-free groups | 5 | 5 (the sample slot is not one of them - its value types are deferred, §2) |

The members became free functions:

| before | after |
|---|---|
| `SampleSlot::toVar()` | `upic::sampleSlotToVar (const SampleSlot&)` |
| `SampleSlot::toVar (bool includeAudio)` | `upic::sampleSlotToVar (const SampleSlot&, bool includeAudio)` |
| `SampleSlot::fromVar (const juce::var&, int)` | `upic::sampleSlotFromVar (const juce::var&, int fallbackId)` |
| `SampleSlot::toPcm16()` | `upic::sampleSlotToPcm16 (const SampleSlot&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `s.` the free functions need and the
whitespace: 29 lines for the writer, 57 for the reader, 18 for the PCM16
conversion, 1 for the one-argument writer. The property names, their order,
the keys that come only when they carry something, the clamps a crafted file
gets, the size checks the payload gets and the PCM conversion are the file
format itself, and they did not change by a character.

**The PCM conversion came along.** `toPcm16` *is* the format - it is the
PCM16 encoding the file's base64 carries, and the recovery snapshot writes
the same bytes to its sidecar. Leaving it in the data header would have
left half the format behind.

**The value types stay.** The slot holds `juce::String name` and a
`juce::AudioBuffer`. The plan defers replacing the domain's value types
(§2), so `Source/Sample.h` still asks for the JUCE header and the sample
slot is not one of the gate's JUCE-free groups. The gate's groups are the
same five as before this phase.

**The behaviour stayed too.** `loaded`, the region's lookup, the peak,
`normalise`, `applyFade` and `resetEdits` are the material's behaviour, not
its file format, so they stayed in `Sample.h`.

**The file reading is still in the domain.** `loadSampleFile` reads a
`juce::File` and decodes it. Reading a file is an edge of its own.

**The suite came first.** `Source/SelfTestSampleCodec.cpp` (71 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name through one more helper, so the
move touched the suite in one place and the checks themselves did not
change.

What the suite pins, and what is worth knowing:

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
  A frame count over the cap is *cut*, not refused: the slot comes back with
  the capped number of frames.

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Tuning.h`, `Sieve.h`,
`Performance.h`, `FreqSpace.h`), and the domain's data still holds JUCE
values (the `juce::String name` and the `juce::Colour` of the groups, the
envelopes, the waves and the channels) - that is the deferred type
migration; the sample's file reading is still in the domain
(`Source/Sample.h` reads `juce::File`s); and there is no headless domain
target yet - the suites run inside the app's selftest binary, which is a
JUCE application (the review's R089). See
[`sample-codec-edge.md`](sample-codec-edge.md).

### R2.10 record (2026-11-27)

The tuning's data and the tuning's file format are separate now - the
eighth codec edge, after the arc's (R2.2), the page's (R2.4), the arc
group's (R2.5), the envelope's (R2.6), the wave's (R2.7), the channel's
(R2.8) and the sample slot's (R2.9).

| Measure | Before | After |
|---|---|---|
| the tuning | `Source/Tuning.h`, 505 lines: the data, the behaviour and the file format | `Source/Tuning.h`, 468 lines: the data and the behaviour |
| the tuning's codec | two members of the class | `Source/TuningCodec.h`, 65 lines: `tuningToVar` and `tuningFromVar` |
| the tuning's own suite | - | `Source/SelfTestTuningCodec.cpp`, 46 checks |
| the gate's JUCE-free groups | 5 | 5 (the tuning is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `Tuning::toVar()` | `upic::tuningToVar (const Tuning&)` |
| `Tuning::fromVar (const juce::var&)` | `upic::tuningFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `t.` the free function needs and the
whitespace: 7 lines for the writer, 23 for the reader. The property names
(name, base, cents), their order, the clamps a crafted file gets, the rules
a scale has to follow and the rule that a broken tuning falls back to the
default WHOLE are the file format itself, and they did not change by a
character.

**The value types stay.** The tuning holds `juce::String name`. The plan
defers replacing the domain's value types (§2), so `Source/Tuning.h` still
asks for the JUCE header and the tuning is not one of the gate's JUCE-free
groups. The gate's groups are the same five as before this phase.

**The behaviour stayed too.** `isDefault`, `degrees`, `baseFreq`,
`degreeToCents`, `centsToDegree`, `stepToFreq`, `freqToStep`, `midiToFreq`,
`freqToMidi`, `snapToStep`, `stepInRange`, `axisStepsInRange`,
`stepFreqsInRange` and `describe` are the tuning's behaviour, not its file
format, so they stayed in `Tuning.h`.

**The Scala importer is still in the domain.** `parseScalaTuning` and its
helpers read a Scala file. Reading a file is an edge of its own - the same
way the sample's file reading is.

**The suite came first.** `Source/SelfTestTuningCodec.cpp` (46 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file) and reads the name through one more helper, so the
move touched the suite in one place and the checks themselves did not
change.

What the suite pins, and what is worth knowing:

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

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Performance.h`, `FreqSpace.h`), and
the domain's data still holds JUCE values (the `juce::String name` and the
`juce::Colour` of the groups, the envelopes, the waves and the channels) -
that is the deferred type migration; the file
readers are still in the domain (the Scala importer in `Source/Tuning.h` and
the sample's `loadSampleFile`); and there is no headless domain target yet
- the suites run inside the app's selftest binary, which is a JUCE
application (the review's R089). See
[`tuning-codec-edge.md`](tuning-codec-edge.md).

Gate: app selftest 2,546/2,546, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.11 record (2026-11-27)

The sieve zone's data and the sieve zone's file format are separate now - the
ninth codec edge, after the arc's (R2.2), the page's (R2.4), the arc
group's (R2.5), the envelope's (R2.6), the wave's (R2.7), the channel's
(R2.8), the sample slot's (R2.9) and the tuning's (R2.10).

| Measure | Before | After |
|---|---|---|
| the sieve zone | `Source/Sieve.h`, 198 lines: the data, the behaviour and the file format | `Source/Sieve.h`, 161 lines: the data and the behaviour |
| the sieve zone's codec | two members of the class | `Source/SieveCodec.h`, 79 lines: `sieveZoneToVar` and `sieveZoneFromVar` |
| the sieve zone's own suite | - | `Source/SelfTestSieveCodec.cpp`, 65 checks |
| the gate's JUCE-free groups | 5 | 5 (the zone is not one of them - its neighbours' value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `SieveZone::toVar()` | `upic::sieveZoneToVar (const SieveZone&)` |
| `SieveZone::fromVar (const juce::var&)` | `upic::sieveZoneFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `z.` the free function needs and the
whitespace: 13 lines for the writer, 21 for the reader. The property names
(id, t0, t1, f0, f1, m, r), their order, the optional keys a default zone
does not write (timeGrid, stick), the order independence of the times and
the frequencies, the clamps a crafted file gets, the folding of the
remainders into the modulus and the 5000 remainder cap are the file format
itself, and they did not change by a character.

**The behaviour stayed too.** `contains`, `effectiveStickiness`, `zoneAt`,
`quantizePoint`, `sievePresets` and `simplifyArcPoints` are the zone's
behaviour, not its file format, so they stayed in `Sieve.h`.

**The suite came first.** `Source/SelfTestSieveCodec.cpp` (65 checks) was
written against the codec where it was, and it passes unchanged after the
move. It reaches the codec through one door (the `encode` / `decode` helpers
at the top of the file), so the move touched the suite in one place and the
checks themselves did not change.

What the suite pins, and what is worth knowing:

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

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `Performance.h`, `FreqSpace.h`), and
the domain's data still holds JUCE values (the `juce::String name` and the
`juce::Colour` of the groups, the envelopes, the waves and the channels) -
that is the deferred type migration; and there is no headless domain target
yet - the suites run inside the app's selftest binary, which is a JUCE
application (the review's R089). See
[`sieve-codec-edge.md`](sieve-codec-edge.md).

Gate: app selftest 2,611/2,611, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.12 record (2026-11-27)

The performance sequence's data and the performance sequence's file format
are separate now - the tenth codec edge, after the arc's (R2.2), the page's
(R2.4), the arc group's (R2.5), the envelope's (R2.6), the wave's (R2.7),
the channel's (R2.8), the sample slot's (R2.9), the tuning's (R2.10) and
the sieve zone's (R2.11).

| Measure | Before | After |
|---|---|---|
| the performance sequence | `Source/Performance.h`, 153 lines: the data, the behaviour and the file format | `Source/Performance.h`, 111 lines: the data and the behaviour |
| the performance sequence's codec | two members of the class | `Source/PerformanceCodec.h`, 82 lines: `performanceSequenceToVar` and `performanceSequenceFromVar` |
| the performance sequence's own suite | - | `Source/SelfTestPerformanceCodec.cpp`, 48 checks |
| the gate's JUCE-free groups | 5 | 5 (the sequence is not one of them - its value types are deferred, §2) |

The two members became two free functions:

| before | after |
|---|---|
| `PerformanceSequence::toVar()` | `upic::performanceSequenceToVar (const PerformanceSequence&)` |
| `PerformanceSequence::fromVar (const juce::var&, int)` | `upic::performanceSequenceFromVar (const juce::var&, int fallbackId)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `s.` the free function needs and the
whitespace: 15 lines for the writer, 23 for the reader. The property names
(id, name, points), their order, the optional key a looping sequence does
not write (loop), the point's own three keys (t, page, pos), the clamps a
crafted file gets, the cut of the name and the sort of the points are the
file format itself, and they did not change by a character.

**The value types stay.** The sequence holds `juce::String name`. The plan
defers replacing the domain's value types (§2), so `Source/Performance.h`
still asks for the JUCE header and the performance sequence is not one of
the gate's JUCE-free groups. The gate's groups are the same five as before
this phase.

**The behaviour stayed too.** `lengthSec`, `sequencePositionAt`,
`sequenceLoopTime` and `modulationFactor` are the sequence's behaviour, not
its file format, so they stayed in `Performance.h`.

**The header now stands on its own.** `Performance.h` uses
`kMaxPageSeconds` but used to ask for `UpicConstants.h` only through the
include order of the files that happened to include it first. It now asks
for it itself - a fix the phase's suite found.

**The suite came first.** `Source/SelfTestPerformanceCodec.cpp` (48 checks)
was written against the codec where it was, and it passes unchanged after
the move. It reaches the codec through one door (the `encode` / `decode`
helpers at the top of the file) and reads the name through one more helper,
so the move touched the suite in one place and the checks themselves did
not change.

What the suite pins, and what is worth knowing:

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

What is still open: the other domain headers still carry their own
`toVar` / `fromVar` members (`Piece.h`, `FreqSpace.h`), and the domain's
data still holds JUCE values (the `juce::String name` and the `juce::Colour`
of the groups, the envelopes, the waves and the channels) - that is the
deferred type migration; and there is no headless domain target yet - the
suites run inside the app's selftest binary, which is a JUCE application
(the review's R089). See
[`performance-codec-edge.md`](performance-codec-edge.md).

Gate: app selftest 2,659/2,659, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R2.13 record (2026-11-27)

The frequency space definition's data and the frequency space definition's
file format are separate now - the eleventh codec edge, and the last of
them: after the arc's (R2.2), the page's (R2.4), the arc group's (R2.5),
the envelope's (R2.6), the wave's (R2.7), the channel's (R2.8), the sample
slot's (R2.9), the tuning's (R2.10), the sieve zone's (R2.11) and the
performance sequence's (R2.12).

| Measure | Before | After |
|---|---|---|
| the frequency space definition | `Source/FreqSpace.h`: the data, the behaviour and the file format | `Source/FreqSpace.h`: the data and the behaviour |
| the definition's codec | three members of the class | `Source/FreqSpaceCodec.h`: `freqSpacePointsToVar`, `freqSpaceDefToVar` and `freqSpaceDefFromVar` |
| the definition's own suite | - | `Source/SelfTestFreqSpaceCodec.cpp`, 47 checks |
| the gate's JUCE-free groups | 5 | 5 |

The three members became three free functions:

| before | after |
|---|---|
| `FreqSpaceDef::pointsToVar (const std::vector<TablePoint>&)` | `upic::freqSpacePointsToVar (const std::vector<TablePoint>&)` |
| `FreqSpaceDef::toVar (int index) const` | `upic::freqSpaceDefToVar (const FreqSpaceDef&, int index)` |
| `FreqSpaceDef::fromVar (const juce::var&)` | `upic::freqSpaceDefFromVar (const juce::var&)` |

The bodies are the old members, body for body - the comparison is
mechanical, line for line, modulo the `d.` the free function needs and the
whitespace: 9 lines for the points, 9 for the writer, 31 for the reader.
The property names, their order, the optional keys a default definition
does not write, the Hz the points carry, the clamps a crafted file gets, the
rules a table has to follow and the fact that the space index is written but
not read are the file format itself, and they did not change by a character.

**The points helper came along.** `pointsToVar` is how the table's points
are written - it is the format, and the writer's only use of it was the
table.

**The behaviour stayed too.** `isDefault` and `log2At` are the definition's
behaviour, not its file format, so they stayed in `FreqSpace.h`.

**The suite came first.** `Source/SelfTestFreqSpaceCodec.cpp` (47 checks)
was written against the codec where it was, and it passes unchanged after
the move. It reaches the codec through one door (the `encode` / `decode`
helpers at the top of the file), so the move touched the suite in one place
and the checks themselves did not change.

What the suite pins, and what is worth knowing:

- **The file carries the table's frequency in Hz, not in log2.** The data
  holds `log2Hz`; the writer converts back with `exp2` and the reader
  converts forward with `log2`. The file is therefore in the same units a
  person reads - and it is a *float* on the way in, so a pitch comes back
  to within a hundredth of a Hz, not exactly.
- **A table of fewer than two points is dropped whole.** The editable flag
  is turned off *and* the points are cleared: one point is not a curve, and
  a constant pitch is not what the composer asked for.
- **A table on a space that is not editable is dropped too** - the points
  are cleared whatever they are. The editable flag is what decides whether
  the table means anything.
- **The `editable` flag is always written**, even when it is false. Every
  other flag is written only when it carries something; the editable flag
  is the one key that is always there.
- **The space index is written but never read.** The definition's reader
  ignores the `space` key: the *page's* codec is what puts a table in the
  right slot (and what clamps the index to the four spaces).
- **A point with nothing in it is at `x = 0` at 440 Hz.** The `x` default is
  zero and the `hz` default is middle A.

The clamps, for the record: the preset 0..6 (the seven presets), the
divisions 0..99 (`kMaxStepDivisions`), the point's `x` 0..1, the point's
frequency 0.01..22050 Hz (the app's range), and the points at most 16384
(`kMaxTablePoints`). A value that is not a number falls to the lower bound.

**The end of the sweep.** This is the last of the domain headers that
carried a file format: a sweep of `Source/` for `toVar` and `fromVar` now
finds only two comments in the codec headers. The whole piece format lives
at the edge.

What is still open: the domain's data still holds JUCE values (the
`juce::String name` and the `juce::Colour` of the groups, the envelopes,
the waves and the channels, the `juce::String name` of the sample slots,
the sequences and the tunings) - that is the deferred type migration; the
file readers are still in the domain (the Scala importer in
`Source/Tuning.h` and the sample's `loadSampleFile`); and there is no
headless domain target yet - the suites run inside the app's selftest
binary, which is a JUCE application (the review's R089). See
[`freqspace-codec-edge.md`](freqspace-codec-edge.md).

Gate: app selftest 2,706/2,706, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R3.1 record (2026-11-27)

R3 begins. The document's own state - the revision, the epoch and the
unsaved flag - has one owner now: the document session.

| Measure | Before | After |
|---|---|---|
| the document's state | three members of `MainComponent` | `Source/DocumentSession.h`: the session owns them |
| the model's own counter | `MainComponent::modelRevision` | `DocumentSession::revision()` |
| the audio thread's signal | `MainComponent::modelEpoch` | `DocumentSession::epoch()` |
| the unsaved flag | `MainComponent::pieceDirty` | `DocumentSession::isDirty()` |
| the session's own suite | - | `Source/SelfTestDocumentSession.cpp`, 30 checks |
| the gate's JUCE-free groups | 5 | 5 |

The state moved:

| before | after |
|---|---|
| `MainComponent::modelEpoch` (`std::atomic<int>`) | `DocumentSession::epoch()` |
| `MainComponent::modelRevision` (`std::atomic<uint64_t>`) | `DocumentSession::revision()` |
| `MainComponent::pieceDirty` (`std::atomic<bool>`) | `DocumentSession::isDirty()` |
| `markModelChanged (bool affectsPiece)` | the same method, over `markChanged (bool affectsPiece)` |
| `pieceHasUnsavedChanges()` | the same method, over `isDirty()` |
| `markPieceSaved()` | the same method, over `markSaved()` |

The ten read sites now ask the session: the status timer, the snapshot
publisher, the audio thread, the recovery snapshot, the saver (twice),
"make unique" (twice) and the undo transaction (twice).

**The revision and the epoch stay two counters.** They move together - one
model change moves both - but they answer different questions for
different threads. The revision is the document's own counter (the status
timer publishes a snapshot only when it moved, the recovery snapshot
notices a change, the saver notices that the disk is already older than
the screen, "make unique" notices that its own work changed something, and
the undo transaction notices that a gesture changed something at all). The
epoch is the audio thread's "the model moved" signal: the audio thread
asks "did it move since I looked?", and it must not have to know about the
revision's other users.

**The suite came first.** `Source/SelfTestDocumentSession.cpp` (30 checks)
was written against the counters where they were, and it passes unchanged
after the move. It reads the document's state through one door (the
`state` helper), so the move touched the suite in one place and the checks
themselves did not change. The components in the suite are made in plugin
mode: the document's state does not depend on the window, and the plugin
mode does not open an audio device.

What the suite pins, and what is worth knowing:

- **A change that does not affect the piece still moves the counters.**
  The loop toggle is not part of the piece, so it does not make the app
  ask to save - but the model did move, so the revision and the epoch move
  with it. It is also still a change for the undo rule: a gesture whose
  only change was the loop toggle is an undo step.
- **Saving moves neither counter.** It clears the unsaved flag and nothing
  else: the piece on the disk now matches the piece on the screen, but the
  model did not change.
- **An undo and a redo are model changes.** They advance the revision and
  the epoch, and they leave the piece unsaved - the piece on the screen is
  not what is on the disk any more.
- **The epoch and the revision count the same changes.** The suite pins
  that their difference stays what it was.

**The class needs no JUCE.** Three counters: `<atomic>` and `<cstdint>`.
That will not last - the undo history comes behind the session, and it
holds pieces (which hold JUCE values) and labels (which are strings) - and
the session will be an application-level class then.

**The piece's data has not moved.** The component still holds the
waveforms, the envelopes, the pages, the arcs, the channels, the sample
slots, the tuning and the sieve as its own members. The session owns the
document's *state*, not its *data*; moving the data is a much bigger job,
and it comes later in R3.

What is still open: the undo history (the review's R007) and the
selection (R006) are still the component's own, and the model still
changes through `markModelChanged()` at the end of a hundred call sites
rather than through a command that says what it did (R008) - one command
= one labelled transaction = one revision is R3's exit criterion, and it
is not reached yet. See
[`document-session.md`](document-session.md).

Gate: app selftest 2,736/2,736, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R3.2 record (2026-11-27)

The document's history has one owner now: the document session. The
review's R007 ("the undo history lives outside the document session") is
answered.

| Measure | Before | After |
|---|---|---|
| the history's owner | `MainComponent` | `Source/DocumentSession.h`: the session owns the steps |
| the steps | `MainComponent::undoStack`, `redoStack` (`std::vector<UndoEntry>`) | `DocumentSession::undoStack_`, `redoStack_` (`std::vector<Entry>`) |
| one step | `MainComponent::UndoEntry` | `DocumentSession::Entry` |
| the open transaction | `pendingEdit`, `pendingRevision`, `pendingLabel` | `pendingEdit_`, `pendingRevision_`, `pendingLabel_` |
| the memory the steps take | `undoBytes`, `redoBytes` | `undoBytes_`, `redoBytes_` |
| the bounds | `kMaxUndoSteps`, `kMaxUndoBytes` | `DocumentSession::Limits` (50 steps, 64 MB) |
| the size of a snapshot | `estimateModelBytes()` in `MainComponentJobs.cpp` | `DocumentSession::estimateModelBytes()` |
| the history's own suite | - | `Source/SelfTestUndoHistory.cpp`, 59 checks |
| the gate's JUCE-free groups | 5 | 5 |

The state moved:

| before | after |
|---|---|
| `MainComponent::UndoEntry` | `DocumentSession::Entry` |
| `MainComponent::undoStack`, `redoStack` | `DocumentSession::undoStack_`, `redoStack_` |
| `MainComponent::undoBytes`, `redoBytes` | `DocumentSession::undoBytes_`, `redoBytes_` |
| `MainComponent::pendingEdit` | `DocumentSession::pendingEdit_` |
| `MainComponent::pendingRevision` | `DocumentSession::pendingRevision_` |
| `MainComponent::pendingLabel` | `DocumentSession::pendingLabel_` |
| `MainComponent::kMaxUndoSteps`, `kMaxUndoBytes` | `DocumentSession::Limits::steps`, `Limits::bytes` |
| `estimateModelBytes()` (a free function) | `DocumentSession::estimateModelBytes()` |
| `canUndo()`, `canRedo()`, `undoDepth()`, `redoDepth()` | the same methods, one-liners over the session |
| `undoLabel()`, `redoLabel()` | the same methods, over the session |

**The suite came first.** `Source/SelfTestUndoHistory.cpp` (59 checks) was
written against the history where it was, and it passes unchanged after the
move. It reads the history through the component's own doors (canUndo,
undoLabel, undoDepth, ...), so the move touched the suite in one place and
the checks themselves did not change.

What the suite pins, and what is worth knowing:

- **A fresh app already has the demo page.** Five arcs, not none: the suite
  had to learn that before it could count the arcs.
- **The loader puts the selection on the first item of the bank it loads.**
  The undo's own restore puts the selection back when the material is still
  there, and falls back to the first wave when it is gone.
- **An undo with nothing to undo leaves an open gesture open.** The gesture's
  step is pushed when the gesture ends, not before - so an undo that has
  nothing to undo does not close a slider drag behind the user's back.
- **An undo closes an open gesture and undoes its step.** The gesture's step
  is closed and undone like any other.
- **A new edit after an undo clears the redo stack.** A new branch.
- **Both bounds evict the oldest first.** 50 steps, and 64 MB of snapshots;
  a step that does not fit the memory bound at all is not kept.
- **The failed restore loses the entry.** The step is popped before the
  restore, so a piece that cannot be put back is gone from the stack. That
  is the existing behaviour and it is kept as it is; it is not reachable
  through the public API (the loader only refuses a piece that is not a
  piece, and a snapshot is always one).

**The shell keeps its half.** The gain slider's flag, the selection, the
transport and the messages stay with the component: they are the shell's
work, not the document's. The component installs the session's two doors in
its constructor - `capturePieceModel()` and `restorePieceModel()` - and the
restore goes through the normal loader, so an undone piece gets the same
checks and the same limits as a loaded one. The undo's failure path moved to
the caller: the session says "failed", the component says "The undo could
not be applied".

**The mechanical check.** The bodies moved line for line, after the renames
the move needs: `estimateModelBytes` (15 lines), `beginEdit` (7), `endEdit`
(22), `undoLabel` (3), `redoLabel` (3), the undo's stacks (14), the redo's
stacks (14), the undo's selection (6) and restore (4), the redo's selection
(6) and restore (4), the loader call and the four messages. All identical.

**The class needs JUCE now.** The labels are strings and the snapshots are
pieces, so the session asks for `<JuceHeader.h>` and `Piece.h`. It stays
out of the boundary check's JUCE-free groups - as it already did, and for
the reason the note gives.

What is still open: the selection (the review's R006) is still the
component's own, and the model still changes through `markModelChanged()`
at the end of a hundred call sites rather than through a command that says
what it did (R008) - one command = one labelled transaction = one revision
is R3's exit criterion, and it is not reached yet. See
[`document-session.md`](document-session.md).

Gate: app selftest 2,795/2,795, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R3.3 record (2026-11-27)

The document's selection has one owner now: the document session. The
review's R006 ("Extract SelectionModel") is answered.

| Measure | Before | After |
|---|---|---|
| the selection's owner | `MainComponent` | `Source/SelectionModel.h`, owned by `DocumentSession` |
| the door | - | `DocumentSession::selection()` |
| the selected wavetable | `MainComponent::selWave` | `SelectionModel::wave` |
| the selected envelope | `MainComponent::selEnv` | `SelectionModel::envelope` |
| the selected sample slot | `MainComponent::selSlot` | `SelectionModel::sampleSlot` |
| the selected amplitude table | `MainComponent::selAmpTableId` | `SelectionModel::ampTable` |
| the selected performance sequence | `MainComponent::selSeqId` | `SelectionModel::sequence` |
| the selected group | `MainComponent::selGroupId` | `SelectionModel::group` |
| the selected mixer channel | `MainComponent::selChannelId` | `SelectionModel::channel` |
| the selected arc | `MainComponent::selectedArcId` | `SelectionModel::arc` |
| the panel's mirror of the canvas selection | `MainComponent::lastArcSelection` | `SelectionModel::arcSelection` |
| the selection's own suite | - | `Source/SelfTestSelection.cpp`, 27 checks |
| the gate's JUCE-free groups | 5 | 5 |

The nine members were scattered over three hundred lines of the component's
header - the channel's selection sat between the channels and the next
channel id, the amplitude table's between the editor and its button, the
arc's beside the arc-list panel. They are one class now, and the session
owns it beside the revision, the epoch, the unsaved flag and the history.
Every use asks the session: `document.selection().wave` and its eight
siblings. 220 uses, and the mechanical check counts all 220.

**The suite came first.** `Source/SelfTestSelection.cpp` (27 checks) was
written against the selection where it was, and it passes unchanged after
the move. It reads the selection through the component's own doors (the
`selected*ForTest` accessors), so the move touched the suite in one place
and the checks themselves did not change.

What the suite pins, and what is worth knowing:

- **A fresh document selects the first wavetable and the first envelope,
  and the first sample slot** - and no amplitude table, no sequence, no
  group, no mixer channel and no arc.
- **A load follows the piece it loads**: the first wavetable, the first
  envelope, the first sample slot, the first mixer channel and the first
  performance sequence.
- **A load does not touch the amplitude table and the group selection.**
  The loader leaves them alone - the two are not part of the piece's own
  banks in the same way.
- **Deleting the selected item moves the selection.** The delete acts on
  the selected wavetable (or envelope), and the replacement takes its place
  in the selection.
- **Two documents keep their own selection.**
- **A load leaves no arc selected.** The canvas's own selection is the
  editor's; the model carries the mirror the panel follows.

**The defaults did not move.** The first wavetable, the first envelope and
the first sample slot (0), and -1 for the amplitude table, the sequence,
the group, the mixer channel and the arc.

**The mechanical check.** The text moved line for line, after the renames
the move needs: the eight component `.cpp` files are identical (8,669
lines), `MainComponent.h` is identical apart from the nine removed
declarations (1,986 lines), and the 220 uses all ask the session.

**The class needs no JUCE.** The selection is ids and a vector of ids: it
asks for `<vector>` and nothing else. It is not in the boundary check's
JUCE-free groups either - it is the application layer's own, like the
session that owns it.

What is still open: the model still changes through `markModelChanged()`
at the end of a hundred call sites rather than through a command that says
what it did (R008) - one command = one labelled transaction = one revision
is R3's exit criterion, and it is not reached yet. See
[`document-session.md`](document-session.md).

Gate: app selftest 2,822/2,822, golden render `0x6d7571bdd80e3cd9`
unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke
22/22, pluginval strictness 5 SUCCESS.

### R3.4 record (2026-11-27)

The document's changes go through one owner now: the command dispatcher.
The review's R008 ("Introduce CommandDispatcher") is answered - the
mechanism is there; the call sites become commands in R3.5.

The characterization commit is `9b4e6f7`, the move `ab32a05`.

| Measure | Before | After |
|---|---|---|
| the open transaction | `DocumentSession::pendingEdit_`, `pendingLabel_`, `pendingRevision_` | `Source/CommandDispatcher.h`, owned by `DocumentSession` |
| the transaction's doors | `DocumentSession::beginEdit()` / `endEdit()` / `hasOpenEdit()` | `CommandDispatcher::beginCommand()` / `endCommand()` / `isCommandOpen()` |
| the change report | `DocumentSession::markChanged()` | `CommandDispatcher::reportChange()` (the session's `reportChange()`) |
| "did anything change?" | compare the revision at the end (`pendingRevision_`) | the dispatcher's `changed_` flag, set by the change report |
| the undo step | made by the session at `endEdit()` | still made by the session, asked by the dispatcher's commit door |
| the epoch | moves once per change | moves once per change; the five readers ask it |
| the revision | moves once per change | moves once per command |
| the session's own suite | 30 checks | 30 checks |
| the history's own suite | 59 checks | 59 checks |
| the selection's own suite | 27 checks | 27 checks |
| the command's own suite | - | `Source/SelfTestCommands.cpp`, 29 checks |

**What the dispatcher owns.** The label, the snapshot of the state from
before the change, the flag that says whether the command changed anything,
and the rule that says what a change means. The session installs four
doors: the capture (the piece from before the command), the commit (the
undo step), the model-moved half of the change report (the epoch and the
unsaved flag) and the document-changed half (the revision). The history's
own bookkeeping - the steps, the redo stack, the two bounds - stays with
the session, in `commitStep()`.

**The semantics.** One command = one labelled undo transaction = one
revision:

- a change inside a command moves the **epoch** at once and the
  **revision** when the command ends - however many changes it has;
- a change outside a command is a change of its own: no transaction to
  put it in, so the epoch and the revision move straight away;
- a command that changes nothing is not an undo step and moves no
  counter;
- a command that is still open when the next one begins is closed first.

**The suite came first.** `Source/SelfTestCommands.cpp` (29 checks) was
written against the old session and passes after the move with exactly one
expectation changed: a command with two changes advanced the revision by
two and advances it by one now. That is the wave's exit criterion, and the
plan said so before the move (§6.21). Two checks were added: the epoch
follows the change inside the command, and the revision waits for the
command to end.

What the suite pins:

- **A change outside a command** moves the revision and the epoch by one,
  marks the piece unsaved, and leaves no undo step.
- **A change that is not part of the piece** (the loop toggle) moves the
  counters but not the unsaved flag.
- **One command** is one undo step with its label.
- **Two changes in one command** are one step, epoch +2, revision +1.
- **A command that changes nothing** is no step and moves no counter.
- **Two commands** are two steps.
- **An unclosed command** is closed by the next one.
- **Two documents** are independent.
- **An undo** is a change that leaves the piece unsaved.

**What changes for the readers.** The five places that asked "did the
model move?" ask the epoch now - the status timer (`MainComponent.cpp`),
the snapshot publisher (`MainComponentAudio.cpp`), the saver
(`MainComponentPiece.cpp`), the recovery snapshot
(`MainComponentRecovery.cpp`) and "make unique"
(`MainComponentModel.cpp`). The epoch moves with every change; the
revision moves once per command. With the revision as the trigger, a
drag's snapshot would be published only on mouse-up - the audio would
follow the gesture at its end, not while it is drawn - and the saver
would miss a change still inside a command, losing the "the disk is older
than the screen" warning.

**The revision has no production reader now.** Only the snapshot field and
the test accessor. This is deliberate: the revision is the document's own
counter (one command, one revision), not the signal the audio, the saver or
the recovery follow. R3.5 will turn the call sites into the commands that
move it.

**The mechanical check.** The move changed the owner of the change report
and nothing else: 120 source files identical, the history's bookkeeping
identical (12 lines) after the renames the move needs, the session's old
transaction doors gone, the five readers asking the epoch, the 109 mentions of
`markModelChanged` in the `.cpp` files untouched, and the production code
not calling the test-only door `commandOpenForTest()`.

**The class needs JUCE.** The label is a string and the snapshot it holds
is a piece, so the dispatcher asks for `<JuceHeader.h>` and `Piece.h`. It
is the application layer's own, like the session that owns it, and it is
not in the boundary check's JUCE-free groups; the check still counts 51
source files, because a header does not add one.

What is still open: the model still changes through `markModelChanged()`
at the end of about eighty call sites, and each of them is a change of its
own, not a command with a label. The mechanism is there - the dispatcher
is the only way into the document - but the call sites are not commands
yet. That is **R3.5**, and it is where one command = one labelled
transaction = one revision is reached.

Gate: app selftest 2,853/2,853, the session's own suite 30/30, the
history's own suite 59/59, the selection's own suite 27/27 and the
command's own suite 29/29, golden render `0x6d7571bdd80e3cd9` unchanged,
legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22,
pluginval strictness 5 SUCCESS, boundaries 5 rules / 0 violations (51
source files).

### R3.5 record (2026-11-27)

The edits are commands now, and the wave's exit criterion is reached: one
model command = one labelled undo transaction, and the revision advances
per command.

The characterization commit is `9142a17`, the move `0432be3`.

| Measure | Before | After |
|---|---|---|
| the edits that changed the piece | 20 raw changes | 20 commands, one label each |
| the shell's helper | - | `MainComponent::runEdit (label, fn)` |
| a gesture | one raw change per tick | one command, from the drag's start to its end |
| the revision | once per mark | once per command |
| an undo step | none for these edits | one per edit |
| the edits' own suite | - | `Source/SelfTestModelCommands.cpp`, 140 checks |
| the source files | 52 | 53 (the suite) |

**The suite came first.** `Source/SelfTestModelCommands.cpp` was written
against the old code: 68 checks, and every edit they drove changed the
document (the revision and the epoch move by one), marked the piece
unsaved and left no undo step. The move turned that last expectation
around - each edit is now one step under its label - and the suite grew
by 72 checks: the eight edits that had no driving door (the three doors
added with the move, and the five that could be driven through the named
controls).

**What the helper is.** One method in the shell:

```cpp
void MainComponent::runEdit (const juce::String& label,
                             const std::function<void()>& fn);
```

If a command is already open it just runs `fn`; otherwise it opens the
command with the label, runs `fn` and closes it. So a one-shot edit is
one command, and a gesture - which opens the command at its drag start
and closes it at its drag end - is one command too, with the value
changes in between joining it. The knobs use the slider's own
`onDragStart` / `onDragEnd`; the workspace's divider grows two callbacks
for the drag's edges (`PageWorkspace.h`).

This is the "standalone" pattern the channel gain already used, with the
`modLevelsEditing` flag replaced by the dispatcher's own "is a command
open?" answer.

**The twenty edits and their labels.**

| edit | site | label |
|---|---|---|
| the page sequencer | `seqBtn` | Sequence the pages |
| the workspace split | `WorkspaceSplitter` | Resize the workspace |
| the live pitch | `livePitchBox` | Live pitch |
| the live reverse | `liveRevBtn` | Live reverse |
| the wave use | `modUseBox` | Wave use |
| the modulation rate | `modRateSlider` | Modulation rate |
| the modulation depth | `modDepthSlider` | Modulation depth |
| the stepped pitch | `stepBtn` | Step the pitch |
| the step divisions | `stepDivSlider` | Step divisions |
| the page's spaces | `spaceDefBox[]` | Frequency space |
| the arc's space | `spaceBox` | Arc frequency space |
| the page's ambitus | `ambitusBox` | Page ambitus |
| the page's acceleration | `warpSlider` | Page acceleration |
| the arc list's reverse | `ArcListPanel::onReverseToggled` | Arc reverse |
| the arc list's mode | `ArcListPanel::onModeChosen` | Arc pitch mode |
| the sample's reverse | `revBtn` | Reverse the material |
| the sample's level | `levelSlider` | Slot level |
| the sample's pitch | `samplePitchBox` | Sample pitch |
| the tuning | `applyTuning()` | Tuning |
| the capture | `finalizeCapture()` | Capture the material |

**The behaviour change.** These edits are undoable now, and the
document's revision advances once per edit instead of once per mark.
That is the point of the phase: before it, a user who moved the page's
ambitus or the slot's level could not undo it, and the undo menu's
labels said nothing about them.

The live reverse is the one edit with two changes in one callback (the
arcs' direction and the re-anchor of the live voices): it is one step,
the epoch +2 and the revision +1.

**What stays raw, and why.** Six sites, and the plan names them before
the move:

- the **loop toggle** (`markModelChanged (false)`) - a change that does
  not affect the piece, so it is not a command and does not make the app
  ask to save;
- the **page box** - a page switch is navigation, not an edit;
- a **load** (`loadFromVar`), an **undo or redo restore**
  (`restorePieceModel`) and a **recovery** (`offerRecovery`) - these
  replace the document, so they are not edits and they are not
  undoable;
- the **test door** that installs a sample slot (`setSampleSlotForTest`)
  - the suite's own way in, not the user's.

The workspace's open and close change the piece's layout but do not mark
the model today, and this phase does not add a mark.

**The mechanical check.** Every `markModelChanged` in the shell is
inside a command now, except the six named above: the twenty callbacks
are each inside a `runEdit` or between a drag's begin and end, the
production code does not call a test-only door, and the diff outside the
converted callbacks is empty.

**The revision still has no production reader.** The epoch is the signal
the audio, the saver and the recovery follow; the revision is the
document's own counter (one command, one revision), and the suite reads
it. That is deliberate and unchanged by this phase.

**The capture is the one edit the suite cannot drive** - it needs live
audio - and the plan says so rather than pretending the suite covers it.

Gate: app selftest 2,993/2,993, the edits' own suite 140/140, golden
render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975`
unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS,
boundaries 5 rules / 0 violations (53 source files).

### R4.1 record (2026-11-27)

The document's file format is an edge now, and the shell keeps only the
commit.

The characterization commit is `479d99b`, the move `6691a55`.

| Measure | Before | After |
|---|---|---|
| the document's read side | 345 lines in `MainComponentPiece.cpp` | 433 lines in `Source/PieceCodec.h` |
| `MainComponent::loadFromVar()` | 532 lines (decode + commit) | 14 lines: decode, hand over, commit |
| the shell's commit | the tail of `loadFromVar()` | `MainComponent::commitPieceModel (m, diag)` |
| `Source/Piece.h` | the data **and** the file format | the data only (75 lines) |
| the load's diagnostics | a `bool` and two local strings | `upic::PieceDiagnostics` (the two messages and the counts) |
| the codec's own suite | - | `Source/SelfTestPieceCodec.cpp`, 82 checks |
| the source files | 53 | 54 (the suite) |

**The suite came first.** `Source/SelfTestPieceCodec.cpp` was written
against the old code and drove the loader through `MainComponent`: 82
checks that pin what the loader refuses, what it repairs, what it drops,
what it tells the user and what the document looks like afterwards. The
move turned the door from the component to the codec, and the checks
themselves did not change - the suite now reaches the document's format
without a window, a component tree or an audio device.

**The codec.** `Source/PieceCodec.h` holds the piece's format, the same
place every other format in the app lives (`PageCodec.h`, `ArcCodec.h`
and their neighbours):

```cpp
namespace upic
{
    struct PieceDiagnostics
    {
        bool ok = false;
        juce::String notLoaded;   // "This piece is bigger than the app supports - not loaded: ..."
        juce::String adjusted;    // "This piece was adjusted: ..."
        int renamedIds = 0;
        int fixedRefs = 0;
        int droppedItems = 0;
        int droppedFrames = 0;
    };

    bool pieceFromVar (const juce::var& v, PieceModel& out, PieceDiagnostics& diag);
    juce::var pieceToVar (const PieceModel& m, bool includeSampleAudio);
}
```

`pieceFromVar()` decodes, migrates, validates and repairs the document
into a *candidate* and hands it over. It touches nothing of the running
app, and a document that is not a piece is refused with the caller's
candidate left exactly as it was - the transaction the loader already
had, now at the edge.

**The commit.** `MainComponent::commitPieceModel()` puts the candidate
in force: the swap under the model lock, the selection, the controls,
the engine, the snapshot the audio thread reads, the messages and the
unsaved flag. The diff outside the two halves is empty - the commit is
the old tail of `loadFromVar()`, with the values read from the candidate
instead of from the document.

**The diagnostics.** The review asks for structured load errors rather
than a single success flag (§32). The plan defers the type migration
(§2), so the diagnostics carry the JUCE types the edge already uses,
and the wording of the two messages does not change by a character. The
counts behind them are new: the ids that were renumbered, the references
that were put back, the items and the sample frames that did not make
it in.

**The one change.** `pieceToVar()` writes the two pitch modes only when
the piece carries one. Before, a piece that was saved without a mode
came back with a mode of zero - the session's mode was lost on the way
out and back. Now a document that carries none (an old piece) leaves
the session's mode alone, which is what the loader already did. A live
piece always carries its modes, so the golden render and the v0.29
fixture do not move.

**The mechanical check.** The production code does not call a test-only
door, the diff outside `loadFromVar()`'s two halves is empty, and the
format's output is unchanged - the golden render and the v0.29 fixture
say so.

The boundary, the diagnostics and the one change are described in
[`piece-codec-edge.md`](piece-codec-edge.md).

Gate: app selftest 3,076/3,076, the codec's own suite 82/82, golden
render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975`
unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS,
boundaries 5 rules / 0 violations (54 source files).

### R4.2 record (2026-11-28)

The document's file adapter is an edge now, and the shell keeps the picking
and the wording.

The characterization commit is `808dd88`, the move `f2e6fe2`.

| Measure | Before | After |
|---|---|---|
| the document's file adapter | 105 lines in `MainComponentPiece.cpp`, inside the chooser's callback, the saver and the window's worker | 209 lines in `Source/DocumentRepository.h` (the reader, the writer and the failures) |
| the load's file half | inside `MainComponent::loadFile()`'s callback | `upic::readDocument (url, maxBytes)` |
| the write | twice (the window's worker and `savePieceToURL()`) | once: `upic::writeDocument (url, model, maxBytes)` |
| the "too large to save" message | written twice | written once, in the edge |
| the size in words | `humanPieceSize()` and a copy of it | `upic::humanPieceSize (bytes)` |
| the load's failures | a `bool` and two local strings | `upic::DocumentReadFailure` and `upic::DocumentReadResult` |
| the shell's load | the chooser's callback | `MainComponent::loadFromURL (url)` (80 lines: the wording and the commit) |
| the adapter's own suite | - | `Source/SelfTestDocumentIO.cpp`, 22 checks |
| the source files | 54 | 55 (the suite) |

**The suite came first.** `Source/SelfTestDocumentIO.cpp` was written
against the old code: 17 checks that pin what the adapter refuses, what it
writes, what it says and what it leaves alone. The writer was driven
through the shell's own door (`MainComponent::savePieceToFile`) and the
reader through the file layer's (`upic::readPickedFile`).

**The door.** The load's sentences - "Not a UPIC piece (the JSON is
broken) - the current piece was kept" and "This file is not a UPIC piece -
the current piece was kept" - lived in the chooser's callback, where no
test could reach them: the read had no door. The move gives it one
(`MainComponent::loadFromURLForTest`), the R3.5 precedent - the doors come
with the move - and five more checks pin the sentences and the piece that
is kept.

**The edge.** `Source/DocumentRepository.h` is the file adapter:

```cpp
enum class DocumentReadFailure
{
    none, noFile, missing, tooLarge, tooLargeText, brokenJson, unreadable
};

struct DocumentReadResult
{
    bool ok = false;
    juce::var document;
    DocumentReadFailure failure = DocumentReadFailure::none;
    juce::String error;
};

struct DocumentWriteResult
{
    bool ok = false;
    juce::String error;
    juce::int64 bytes = 0;
};

DocumentReadResult readDocument (const juce::URL& url, juce::int64 maxBytes);
DocumentWriteResult writeDocument (const juce::URL& url, const PieceModel& model,
                                   juce::int64 maxBytes);
```

The reader hands the *document* over: it does not know whether the
document is a piece. That is the codec's business (`Source/PieceCodec.h`),
and the commit is the shell's. The pipeline is the review's: read (the
edge), decode, migrate and validate (the codec), commit (the shell).

**The wording stays in the shell.** "The current piece was kept" and "Saved
X" are the app's state, not the file's, so they are the shell's. The edge
says what happened - `brokenJson`, `tooLarge`, `missing` - and the shell
words it.

**The one change.** None in the behaviour. The duplication is gone (the
"too large to save" message was written twice), and one unreachable branch
went with the move: the load's "Piece too large (max N MB)" could never be
reached, because the file layer refuses the same file first. The silent
case stays silent: a file whose text is over the cap keeps the piece and
says nothing, as it did.

**The mechanical check.** The production code does not call a test-only
door, the diff outside the adapter's two halves is empty, and the
behaviour is unchanged - the golden render and the v0.29 fixture say so.

The boundary, the failures and the two details of the old code are
described in [`document-io-edge.md`](document-io-edge.md).

Gate: app selftest 3,098/3,098, the adapter's own suite 22/22, golden
render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975`
unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS,
boundaries 5 rules / 0 violations (55 source files).

### R4.3 record (2026-11-28)

The recovery snapshot's storage is an edge now, and the shell keeps the
service.

The characterization commit is `d9762fb`, the move `04b9619`.

| Measure | Before | After |
|---|---|---|
| the snapshot's storage | 206 lines in `MainComponentRecovery.cpp`, four static methods of the shell | 262 lines in `Source/RecoveryRepository.h` (the folder, the write, the read and the cleanup) |
| where the snapshots live | `MainComponent::recoveryDirectory()` | `upic::recoveryDirectory()` |
| the write | `MainComponent::writeRecoverySnapshot()` | `upic::writeRecoverySnapshot (dir, model, error)` |
| the read | `MainComponent::readRecoverySnapshot()` | `upic::readRecoverySnapshot (dir, pieceOut, error)` |
| the cleanup | `MainComponent::deleteRecoverySnapshot()` | `upic::deleteRecoverySnapshot (dir)` |
| the service | `maybeWriteRecoverySnapshot()` and `offerRecovery()` in the shell | unchanged - the shell's |
| the repository's own suite | - | `Source/SelfTestRecoveryRepository.cpp`, 21 checks |
| the source files | 55 | 56 (the suite) |

**The suite came first.** `Source/SelfTestRecoveryRepository.cpp` was
written against the old code, through the shell's four static methods: 21
checks that pin where the snapshots live, the shape of a snapshot, the
sidecars that are not used any more, the round trip, the cleanup and every
refusal with its words.

**The edge.** `Source/RecoveryRepository.h` is the storage:

```cpp
juce::File recoveryDirectory();

bool writeRecoverySnapshot (const juce::File& dir, const PieceModel& model,
                            juce::String& error);
bool readRecoverySnapshot (const juce::File& dir, juce::var& pieceOut,
                           juce::String& error);
void deleteRecoverySnapshot (const juce::File& dir);
```

The bodies are the shell's methods, body for body. The property names, the
8 MB piece cap, the `kMaxPieceBytes` audio cap, the hash (FNV-1a), the
sidecar names, the order of the checks and the wording of every refusal do
not change by a character.

**The service stays in the shell.** When the snapshot is written (the 30 s
interval, the epoch, the unsaved check) and how it is offered (the dialog,
the recover and the discard) are the app's behaviour, not the storage's.
The review's R065 asks for that service to be a thing of its own too; R4.3
extracts the repository (§33's storage responsibility) and leaves the
service where it can still be reached by the window.

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the shell's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the shell are described in
[`recovery-repository-edge.md`](recovery-repository-edge.md).

Gate: app selftest 3,119/3,119, the repository's own suite 21/21, golden
render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975`
unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS,
boundaries 5 rules / 0 violations (56 source files).

### R4.4 record (2026-11-28)

The user library's storage is an edge now, and the shell keeps the service.

The characterization commit is `27b4305`, the move `ac0b707`.

| Measure | Before | After |
|---|---|---|
| the library's storage | the storage methods of `UserLibrary`, in `Source/UserLibrary.h` (813 lines) | 366 lines in `Source/LibraryRepository.h` (the folder, the write, the read, the listing, the first free id and the reveal) |
| the asset's format | the envelope and the validation inside the same class | `Source/UserLibrary.h` (534 lines): the kinds, `assetToVar` / `assetFromVar` and `payloadIsValid` |
| where the library lives | `UserLibrary::rootFolder()` | `upic::library_repository::rootFolder()` |
| the write | `UserLibrary::publish()` | `upic::library_repository::publish()` |
| the read | `UserLibrary::read (kind, file, …)` | `upic::library_repository::read (kind, file, …)` and `read (kind, fileName, …)` |
| the listing | `UserLibrary::list()` | `upic::library_repository::list()` |
| the first free id | `UserLibrary::nextLibraryId()` | `upic::library_repository::nextLibraryId()` |
| the reveal | `UserLibrary::reveal*()` | `upic::library_repository::reveal*()` |
| the service | the five publish/import/reveal triples in the shell | unchanged - the shell's |
| the repository's own suite | - | `Source/SelfTestLibraryRepository.cpp`, 47 checks |
| the source files | 56 | 57 (the suite) |

**The suite came first.** `Source/SelfTestLibraryRepository.cpp` was
written against the old code, through `UserLibrary`'s static methods: 47
checks that pin where the library lives, the four folders and the shared
table folder, the slug rules, the envelope and the atomic write, the
publish refusals, the read refusals, the listing (the skipped files and the
other table kind), the first free id, and the reveal target's guard.

**The edge.** `Source/LibraryRepository.h` is the storage:

```cpp
juce::File rootFolder();
juce::File folderFor (UserAssetKind kind);
juce::String slugFor (const juce::String& name);

bool publish (UserAssetKind kind, int id, const juce::String& name,
              juce::Colour colour, const juce::var& payload,
              juce::String& error);
bool read (UserAssetKind kind, const juce::File& file,
           UserAsset& out, juce::String& error);
bool read (UserAssetKind kind, const juce::String& fileName,
           UserAsset& out, juce::String& error);
std::vector<UserAsset> list (UserAssetKind kind, int* skipped = nullptr,
                             juce::StringArray* failures = nullptr);
int nextLibraryId (UserAssetKind kind);
bool revealTarget (UserAssetKind kind, const juce::String& fileName,
                   juce::File& target, juce::String& error);
bool reveal (UserAssetKind kind, const juce::String& fileName,
             juce::String& error);
bool revealFolder (UserAssetKind kind);
```

The bodies are the old `UserLibrary` methods, body for body. The
six-property envelope, the 4 MB asset cap, the atomic temp-and-rename
write, the `-2`…`-999` collision counter, the order of the checks and the
wording of every refusal do not change by a character.

**The codec stays.** `Source/UserLibrary.h` keeps the asset's format: the
kinds, `userAssetKindName()`, `userAssetFolderName()`, `UserAsset`, the new
envelope `UserLibrary::assetToVar()` / `assetFromVar()`, and the payload
validation with all of its refusals. The envelope's property names and
order are the old file's.

**The service stays in the shell.** The five publish/import/reveal triples
in `Source/MainComponentModel.cpp` keep the selection, the bank limits, the
undo transaction, the destination-bound frequency tables and the words. The
edge says what happened; the shell words it.

**The one asymmetry, kept.** The import's read takes the file name from the
manager's list, so the old shell built the path without a guard; the reveal
takes a typed name, so it guards. R4.4 moves the path-building into the
edge's `read (kind, fileName, …)` and keeps the asymmetry body for body. It
is recorded in the edge's header and in the architecture note.

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the old methods body for body, and the behaviour
is unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the shell are described in
[`library-repository-edge.md`](library-repository-edge.md).

Gate: app selftest 3,166/3,166, the library repository's own suite 47/47,
golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture
`8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness
5 SUCCESS, boundaries 5 rules / 0 violations (57 source files).

### R5.1 record (2026-11-28)

The score canvas's render is an edge now, and the editor keeps the gesture.

The characterization commit is `bf1ad70`, the move `e7dd14c`.

| Measure | Before | After |
|---|---|---|
| the canvas's render | 380 lines in `ScoreEditor::paint()` | 372 lines in `Source/ScoreRenderer.cpp` (the frame, the grid, the zones, the arcs, the gesture, the labels, the playhead and the hints) |
| the canvas's source | `Source/ScoreEditor.cpp`, 2,477 lines / 98.3 KB | 2,136 lines / 79.8 KB (the render is out) |
| the coordinate mapping | five methods in `ScoreEditor` (`xToTime`, `timeToX`, `yToFreq`, `freqToY`, `zoneRect`) | `upic::score_render::View` (`Source/ScoreRenderer.h`); the editor's methods delegate to it |
| the render's own suite | - | `Source/SelfTestScoreRender.cpp`, 18 checks |
| the source files | 57 | 59 (the suite and the render) |

**The suite came first.** `Source/SelfTestScoreRender.cpp` was written
against the old code: 18 checks that paint the canvas off-screen into a
`juce::Image` and read single pixels - the background and the frame, the
grid on and off, the A440 and C lines, the sample layer's Ref line, the
arc's dot, the layer rule, the selected arc's brighter dot, a zone's
translucent fill, the playhead and its absence at zero, the workspace's
mark, the hints, the determinism and the untouched model.

The probes read the pixels at positions the editor's *own* public mapping
gives them (`timeToX()`, `freqToY()`), so the suite pins the agreement
between the drawing and the mapping as well as the colours.

**The edge.** `Source/ScoreRenderer.h` is the render's contract:

```cpp
namespace upic::score_render
{
    struct View   // where the plot is and how time and pitch map onto it
    {
        juce::Rectangle<float> plot;
        double durationSec, pitchLo, pitchHi;
        double timeZoom, timeOffset, freqZoom, freqOffset;

        double xToTime (float x) const;
        float timeToX (double t) const;
        double yToFreq (float y) const;
        float freqToY (double f) const;
        juce::Rectangle<float> zoneRect (const SieveZone& z) const;
    };

    struct State  // what is drawn
    {
        View view;
        const Page* page; const std::vector<Waveform>* waves;
        const Tuning* tuning; const std::vector<int>* selArcs;
        const std::vector<int>* sieveR;
        int layer; bool showGrid, showHints, sieveOn; int sieveM;
        double timeGrid, stickiness; bool recordPressure;
        bool workspaceMarked; juce::String workspaceCaption, toolName;
        bool zoneTool; double curRef; int selectedZone;
        const std::vector<StrokePoint>* strokePts;
        bool hasLineAnchor; juce::Point<float> lineAnchor, lastMouse;
        bool drawingZone; juce::Rectangle<float> zoneRubberRect;
        bool selectingMarquee; juce::Rectangle<float> marqueeRect, scrubGrip;
        bool hasPlayhead; double playhead; bool isPlaying, scrubbing;
    };

    void paint (juce::Graphics& g, const State& s);
}
```

The bodies are the editor's `paint()`, body for body. The colours, the
order of the passes and the wording of the hint lines do not change by a
character.

**The one view.** The mapping that used to live in the editor's five methods
is now the `View`, and the editor's methods delegate to it. The drawing and
the hit testing therefore share one mapping - which is what the review asks
for ("Rendering, hit testing and coordinate conversion should agree, but
they should not be implemented as one operation", §24). The review gives
the mapping a name of its own in R044 ("Introduce ScoreCoordinateMapper");
until then it lives next to the render that needs it, and R044 moves it.
(R5.2 did: the `View` is now
`upic::score_coordinates::ScoreCoordinateMapper`, in
`Source/ScoreCoordinateMapper.h`.)

**The service stays in the editor.** The gesture, the tools, the
quantisers, the commit of a stroke, the selection and the model's commands
stay in `ScoreEditor`. They need the editor's state, the model's lock and
the app's hooks; they are not the picture.

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the editor's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the editor are described in
[`score-renderer-edge.md`](score-renderer-edge.md).

Gate: app selftest 3,184/3,184, the render's own suite 18/18, golden render
`0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged,
plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS, boundaries 5
rules / 0 violations (59 source files).

### R5.2 record (2026-11-28)

The canvas's coordinates are a thing of their own, and the editor keeps the
view's state.

The characterization commit is `c2437c2`, the move `3b23d62`.

| Measure | Before | After |
|---|---|---|
| the coordinate mapping | `upic::score_render::View` in `Source/ScoreRenderer.h`, with the bodies in `Source/ScoreRenderer.cpp` | `upic::score_coordinates::ScoreCoordinateMapper` in `Source/ScoreCoordinateMapper.h` (100 lines, header-only) |
| the renderer's header | 125 lines, the mapping among them | 99 lines - the drawing's contract only |
| the renderer's source | 453 lines | 414 lines |
| the canvas's source | `Source/ScoreEditor.cpp`, 2,136 lines | 2,136 lines (the editor still builds the mapper) |
| the mapper's own suite | - | `Source/SelfTestCoordinateMapper.cpp`, 14 checks |
| the source files | 59 | 60 (the suite) |

**The suite came first.** `Source/SelfTestCoordinateMapper.cpp` was written
against the old code: 14 checks that reach the mapping through the editor's
own doors (`timeToX()`, `xToTime()`, `freqToY()`, `yToFreq()`,
`zoneRectForTest()`) and pin where time 0 and the page's end are on the
screen, the round trips (a time and a pitch survive the trip out and back),
the ambitus (its low end is the plot's bottom, its high end the plot's
top), the non-clamping of a pitch outside the ambitus, the clamping of a
cursor outside the page, a zone's rectangle (its times and its frequencies
are order-independent), and the zoom (it keeps the view's centre where it
was, and a time survives the round trip through it).

The tolerances are the float's precision, not the last bit: the screen
coordinates are floats, so the round trip is exact to the float's
precision.

**The edge.** `Source/ScoreCoordinateMapper.h` is the mapper's contract:

```cpp
namespace upic::score_coordinates
{
    struct ScoreCoordinateMapper
    {
        // The plot's rectangle on the screen.
        juce::Rectangle<float> plot;

        // The page's length and ambitus.
        double durationSec = 30.0;
        double pitchLo = kDefaultPitchLo;
        double pitchHi = kDefaultPitchHi;

        // The view's zoom and scroll.
        double timeZoom = 1.0, timeOffset = 0.0;
        double freqZoom = 1.0, freqOffset = 0.0;

        // The time axis: seconds <-> pixels.
        double xToTime (float x) const;
        float timeToX (double t) const;

        // The pitch axis: Hz <-> pixels (logarithmic).
        double yToFreq (float y) const;
        float freqToY (double f) const;

        // A zone's rectangle (its times and its frequencies are sorted).
        juce::Rectangle<float> zoneRect (const SieveZone& z) const;
    };
}
```

The bodies are the editor's, body for body. The clamping of a cursor
outside the page, the deliberate *non*-clamping of a pitch outside the
ambitus and the sorting of a zone's corners do not change by a character.

**The mapper is a value, and the editor builds it.** The view's *state* -
the zoom, the scroll, the plot's rectangle, the page's current length and
ambitus - is the editor's, and the editor hands it to the mapper
(`ScoreEditor::coordinateMapper()`). The mapper converts and owns nothing.
That is why the editor's public doors (`timeToX()`, `freqToY()`, …) stay:
they are the app's way in, and they now go through the mapper.

**One mapping, three users.** The render (R043) draws with it, the editor
maps the cursor and the strokes with it, and the hit tester (R045) will use
it too. The review asks for exactly this: "Rendering, hit testing and
coordinate conversion should agree, but they should not be implemented as
one operation" (§24).

**The service stays in the editor.** The gesture, the tools, the
quantisers, the commit of a stroke, the selection, the model's commands and
the wheel's view commands (the zoom, the pan, the fit) stay in
`ScoreEditor`. The view commands change the view's state and then repaint;
they belong with the editor, not with the conversion.

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the editor's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the editor are described in
[`score-coordinate-mapper-edge.md`](score-coordinate-mapper-edge.md).

Gate: app selftest 3,198/3,198, the mapper's own suite 14/14, the render's
own suite 18/18, golden render `0x6d7571bdd80e3cd9` unchanged, legacy
fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval
strictness 5 SUCCESS, boundaries 5 rules / 0 violations (60 source files).

### R5.3 record (2026-11-28)

What is under the cursor is answered by the hit tester, and the editor
keeps the gesture.

The characterization commit is `be94fc2`, the move `a563576`.

| Measure | Before | After |
|---|---|---|
| the hit testing | four private methods in `ScoreEditor` (`arcsNear`, `pickArc`, `pickZone`, `zoneHandleAt`), about 75 lines | `upic::score_hit_test::ScoreHitTester` in `Source/ScoreHitTester.h` (142 lines, header-only); the editor's methods delegate |
| the canvas's source | `Source/ScoreEditor.cpp`, 2,136 lines | 2,132 lines |
| the hit tester's own suite | - | `Source/SelfTestScoreHitTest.cpp`, 34 checks |
| the source files | 60 | 61 (the suite) |

**The suite came first.** `Source/SelfTestScoreHitTest.cpp` was written
against the old code: 34 checks that reach the hit testing through the
editor's own doors (`arcsNearForTest()`, `pickArcForTest()`,
`pickZoneForTest()`, `zoneHandleAtForTest()`) and, for the last group,
through the editor's real mouse handling - a synthesised click, the way the
app's own tests do it.

It pins:

- the arcs near a point, nearest first, and the nearest arc alone;
- the grab band: 12 pixels for the mouse (20 for a touch), inside, outside
  and given;
- an arc is hit along its line, not only at its points;
- the layer rule: an arc of another layer is not under the cursor;
- the zone under a point, the border's slack (4 pixels), the topmost zone
  first;
- the zone's handles: the four edges (1..4), the four corners (5..8) and
  none (0);
- the end-to-end click: the Select tool selects the arc under the cursor and
  clears the selection on empty space, the Zone tool selects the zone under
  the cursor.

The probes place the cursor with the editor's own mapping (`timeToX()`,
`freqToY()`), so they do not hard-code where a time or a pitch is.

**The edge.** `Source/ScoreHitTester.h` is the hit tester's contract:

```cpp
namespace upic::score_hit_test
{
    struct ScoreHitTester
    {
        score_coordinates::ScoreCoordinateMapper mapper;
        const Page* page = nullptr;
        int layer = 0;
        bool touchSeen = false;

        std::vector<int> arcsNear (juce::Point<float> pos, float maxD = -1.0f) const;
        int pickArc (juce::Point<float> pos) const;
        int pickZone (juce::Point<float> pos) const;
        int zoneHandleAt (const SieveZone& z, juce::Point<float> pos) const;
    };
}
```

The bodies are the editor's, body for body. The 12-pixel grab band (20 for a
touch), the nearest-first ordering, the layer rule, the 4-pixel slack on a
zone's border, the topmost zone first and the eight handles do not change
by a character.

**The hit tester answers; the editor acts.** The hit tester is a value: the
editor builds one for every use (`ScoreEditor::hitTester()`), handing it the
mapper, the page, the layer and the touch's grab band. The editor's
private `arcsNear()`, `pickArc()`, `pickZone()` and `zoneHandleAt()` stay
as its way in, and the gesture - the selection, the marquee, the zone's
drag, the erase, the tools - stays in the editor.

**One mapping, two answers.** The render (R043) draws with the mapper and
the hit tester (R045) asks with the same mapper, so the picture and the
grab agree about where a time and a pitch are. That is the review's
requirement: "Rendering, hit testing and coordinate conversion should
agree, but they should not be implemented as one operation" (§24).

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the editor's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the editor are described in
[`score-hit-tester-edge.md`](score-hit-tester-edge.md).

Gate: app selftest 3,232/3,232, the hit tester's own suite 34/34, the
mapper's own suite 14/14, the render's own suite 18/18, golden render
`0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged,
plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS, boundaries 5
rules / 0 violations (61 source files).

### R5.4 record (2026-11-28)

Each drawing mode has its own controller, and the canvas's mouse handlers
have stopped accumulating a condition per mode.

The characterization commit is `a3b3eb0`, the move `1d6ad4d`.

| Measure | Before | After |
|---|---|---|
| the mouse handlers | about 390 lines, with a branch per drawing mode (the zone, the right button, the selection, the marquee, the move, the erase, the stroke, the line) | about 110 lines: the touch handling, the plot's bounds, the two scrub targets - and then the tool |
| the drawing modes | conditions in `ScoreEditor::mouseDown()`, `mouseDrag()` and `mouseUp()` | five controllers in `Source/ScoreEditingTools.h` (the freehand stroke for Draw and SineStroke, the line, the selection and the move, the erase, the zone) |
| the interface | none | `Source/ScoreEditingTool.h`: `EditingTool` (`begin`, `update`, `end`, `cancel`) and `PointerInput` |
| the canvas's source | `Source/ScoreEditor.cpp`, 2,136 lines | 1,933 lines |
| the tools' own suite | - | `Source/SelfTestEditingTools.cpp`, 45 checks |
| the source files | 61 | 62 (the suite) |

**The suite came first.** `Source/SelfTestEditingTools.cpp` was written
against the old code: 45 checks that drive the editor with synthesised
mouse events - the way the app's own UI tests do it.

It pins:

- the freehand stroke: one arc per drag, the points sorted by time, the
  release's point part of the stroke, the points on the page, the pressure,
  the stroke temporary until the release, the current material, the layer the
  stroke was drawn in, the simplification (a straight drag is simplified to
  its two ends) and the fine mode's exemption from it, the refusal of a
  stroke that is too short (and the message), and the layer switch that
  cancels the stroke;
- the undo transaction: one labelled transaction per gesture ("Draw", "Draw
  a line", "Delete", "Move the arc", "Add a sieve zone");
- the sine stroke: one arc, and the wobble around the line (0.166 octaves in
  the suite's stroke);
- the line: one arc that runs from the drag's start to its end and is
  subdivided along it, its own label, and the refusal of a line that is too
  short (and the message);
- the erase: the arc under the cursor is deleted, and nothing on empty
  space;
- the selection: a click selects the arc under the cursor, a ctrl-click
  unselects it, a shift-click adds another, a click on empty space clears the
  selection;
- the marquee: it takes the arcs inside it;
- the move: a selected arc follows the pointer;
- the zone: a drag draws one, with the times and the pitches of the drag, and
  its own label;
- the scrub band: it moves the playhead and draws nothing, whatever the tool
  is.

**The edge.** `Source/ScoreEditingTool.h` is the interface and the pointer's
input:

```cpp
namespace upic::score_tools
{
    struct PointerInput
    {
        juce::Point<float> position;
        juce::Point<float> downPosition;
        float pressure = 1.0f;
        juce::ModifierKeys mods;
        int pointerId = 0;
        bool isTouch = false;
    };

    class EditingTool
    {
    public:
        virtual ~EditingTool() = default;
        virtual void begin  (const PointerInput&) = 0;
        virtual void update (const PointerInput&) = 0;
        virtual void end    (const PointerInput&) = 0;
        virtual void cancel () = 0;
    };
}
```

`Source/ScoreEditingTools.h` holds the five controllers. The bodies are the
editor's, body for body: the stroke's resampling, the line's anchor and its
rubber band, the selection's click rules (the ctrl-click's toggle, the
shift-click's add), the marquee's rectangle, the move's ratio, the erase's
pick and delete, the zone's draw and drag - none of them changes by a
character.

**The canvas keeps what is the canvas's.** The scrub targets (the playhead's
handle, the centre band), the multi-touch and the pinch are not tools: they
belong to the canvas, whatever tool is active. The right button is a tool's
business, because the Zone tool has its own menu and every other tool opens
the arc's.

**The tool that began the gesture finishes it.** Before, every event asked
"which tool is active?", so a tool change in the middle of a drag changed
who finished the gesture. Now the canvas remembers the tool that took the
gesture (`gestureTool`) and the drag, the release and the cancel go to it.
This is the review's model made explicit; it cannot be reached by hand (a
tool change needs a toolbar click, which needs the pointer to be released
first).

**The tools act; the canvas owns.** The tools are the canvas's own
controllers, so they act on the canvas's gesture state (the render borrows
it) and call the canvas's commands (`commitStroke()`, `commitLine()`,
`deleteSelected()`, `addZone()`). The canvas declares them friends, which
keeps the app's public API unchanged.

**The mechanical check.** The production code does not call a test-only
door, the moved bodies are the editor's body for body, and the behaviour is
unchanged - the golden render and the v0.29 fixture say so.

The boundary, the API and what stays in the canvas are described in
[`score-editing-tools-edge.md`](score-editing-tools-edge.md).

Gate: app selftest 3,277/3,277, the tools' own suite 45/45, the hit
tester's own suite 34/34, the mapper's own suite 14/14, the render's own
suite 18/18, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture
`8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness
5 SUCCESS, boundaries 5 rules / 0 violations (62 source files).
