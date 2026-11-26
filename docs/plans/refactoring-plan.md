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
| Replace `juce::String` / `juce::var` in the domain | the piece codec *is* JSON (`juce::var`) | **Defer.** Keep JUCE value types at the codec edge. Enforce *no new* GUI dependencies instead of a type migration. |
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
| **R4 Persistence** | Loading is transactional and validated | `PieceCodec` / validation split out of `loadFromVar()`; a failed load leaves the active piece untouched | R059–R067, R090 | A malformed document fails with diagnostics and changes nothing; the v0.29 fixture still renders byte-identically. |
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

## 7. Deliberately not doing (yet)

Taken from the review's own §62 "patterns to avoid", plus the adaptations in
§2 above:

- **No bulk file move.** No `Source/Domain/…` tree until a module is actually
  extracted into it.
- **No JUCE type migration** in the domain. `juce::String` and `juce::var`
  stay at the edges; the piece format is JSON and the codec is `juce::var`.
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
| R2 Domain boundaries | **R2.1 + R2.2 + R2.3 + R2.4 shipped** | `ea83dac`, `d58120d`, `bb23e1c`, `bb0d88c`, `f3aa455`, `f44c399`, `9ff1c1c`, `829f977` | app selftest 2,272/2,272, the domain's numeric core, the arc, the arc's geometry and the page are JUCE-free and checked by the gate, the domain's own suite 61/61, the arc's own suite 87/87, the geometry's own suite 58/58, the page's own suite 73/73, golden render `0x6d7571bdd80e3cd9` unchanged, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
| R3 Document and commands | not started | - | - |
| R4 Persistence | not started | - | - |
| R5 UI decomposition | not started | - | - |
| R6 Real-time audio | not started | - | - |
| R7 Plugin and platform | not started | - | - |
| R8 Build and tests | **R8.1 shipped** | `3c3293f` | dependency boundaries 5 rules / 18 self-tests, the gate fails on a forbidden include, on a source the `.jucer` does not know and on a source missing from the plugin's list, app selftest 1,993/1,993, legacy fixture `8BF28ABA7975` unchanged, plugin 0.38.0, smoke 22/22, pluginval strictness 5 SUCCESS. |
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
