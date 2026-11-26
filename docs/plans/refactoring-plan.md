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
| **R2 Domain boundaries** | Make the composition model testable without a GUI | Split `Arc` data from geometry; centralize units and validation | R012–R021, R084 | Domain headers depend on no GUI header; the domain tests run headless. |
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

## 6. Phase R1.1 in detail - the card layout engine

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
| R1 Low-risk extraction | not started | - | - |
| R2 Domain boundaries | not started | - | - |
| R3 Document and commands | not started | - | - |
| R4 Persistence | not started | - | - |
| R5 UI decomposition | not started | - | - |
| R6 Real-time audio | not started | - | - |
| R7 Plugin and platform | not started | - | - |
| R8 Build and tests | runs alongside | - | - |
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
