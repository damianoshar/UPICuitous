# Toolkits: what applies to UPICuitous

Reference for the three agent toolkits reviewed on 2026-10-21, and what we
adopt from them. Nothing here changes the app on its own - it is the map of
what to use, what to skip, and why.

The toolkits live outside the repo (they are reference material, not
dependencies):

| Toolkit | Path | What it is |
|---|---|---|
| Sonic Skills | `C:\Users\harha\Downloads\sonic-skills-main` | 20 audio-engineering skills: realtime safety, numerics, artifacts, DSP algorithms, VST3/AU/CLAP/JUCE/WebAudio reviews |
| JUCE Agent Toolkit | `C:\Users\harha\Downloads\juce-agent-toolkit-main` | JUCE project workflow: create / build / release / port / auto-update, plus portable Python scripts |
| Audio Plugin Coder (APC) | `C:\Users\harha\Downloads\audio-plugin-coder-main` | An agentic phase workflow (dream / plan / design / impl / test / ship) with rules, guides and a troubleshooting knowledge base |

## What we adopt

### 1. Sonic Skills - the review pass before a release

| Skill | Use it for | In this repo |
|---|---|---|
| `audio-artifacts-debug` | Symptom -> cause table for clicks, dropouts, DC, aliasing, latency | The v1.15 click hunt follows its checklist ("click at fixed buffer-length intervals -> buffer boundary discontinuity") |
| `audio-dsp-review` | Realtime-safety pass over the render path | Flags our `CriticalSection::tryEnter()` on the audio thread and the `AudioBuffer::setSize()` in the callback - both fixed in the v1.16 work |
| `audio-numerics-review` | Denormals, NaN, cancellation, FTZ/DAZ | The plugin has `ScopedNoDenormals` in `processBlock`; the standalone app path still needs it |
| `juce-review` | JUCE-specific violations (APVTS, MessageManager, buffer resizing) | Same two findings as above; also the guide for the upcoming MIDI work (`AbstractFifo` on the audio thread) |
| `vst3-review` | Host contracts: latency, stable ParamIDs, bypass, bus negotiation | Found the missing `getLatencySamples()` (v1.16) |
| `dsp-algorithm-guide` | Algorithm checklists (stability, latency reporting, oversampling, parameter smoothing) + a test checklist | The template for the 2-op PM proof (UPIC-26) and the FM engine (UPIC-30) |
| `audio-math-explainer`, `audio-signal-flow-explainer` | Explaining the pitch shifters for the docs | Future PROGRESS.md / reference writing |
| `webaudio-review` | The `web-upic/` prototype in this repo | Optional |

### 2. JUCE Agent Toolkit - the project mechanics

- **`scripts/build.ps1` (to write)**: the "one-script rule" - one command that
  kills the app, builds it, runs the selftest, builds the plugin, runs the
  smoke test, and keeps the Windows version resource in step with
  `Plugin/CMakeLists.txt`. We do this by hand every wave; it should be one
  script.
- **`bump_version.py` / `generate_release_notes.py`**: replace the manual
  PowerShell regex that bumps `VERSION 1.x.0` in `Plugin/CMakeLists.txt`.
- **`port_audit.py`**: the lightweight platform scan to run before a
  macOS/Linux plugin build.
- **`juce-build-release`**: the shape of a release flow (build -> test ->
  package -> publish) if the plugin ever ships publicly.
- **Not adopted**: `juce-visage-ui`, `juce-setup-visage`, `juce-setup-ios`
  (no Visage, no iOS target), `juce-project-starter` (our app is hand-rolled,
  not a JUCE-Plugin-Starter layout).

### 3. Audio Plugin Coder - the workflow and the knowledge base

- **Phase discipline**: validate the previous phase before writing files,
  keep a design note per wave, stop at the end of the requested phase. We
  already work this way (one wave = design + implement + verify + commit +
  Kanso card); the transferable piece is the short design note with a
  complexity score and a risk list.
- **`known-issues.yaml` + resolutions**: a knowledge base for the recurring
  failures. Two of our stories belong in it: the `fobject.h` corruption
  timeline, and the "VST3 install denied" blocker (their `build-003`).
- **`performance-001` (mutex blocking the whole processBlock)**: the
  strategic version of our `dataLock` problem - move the model to a
  lock-free snapshot so the audio thread never locks (UPIC-32).
- **Build gotchas worth keeping**: `build-004` (cap `cl.exe` parallelism via
  `_CL_=/MP<n>` when the compiler runs out of heap - we already build with
  `--parallel 4`), `build-006` (the stale Windows version resource, below),
  `build-003` (Program Files needs an elevated shell).
- **Not adopted**: the JUCE 9 monorepo layout, the WebView/Visage UI stacks,
  the CLAP wrapper, `juce_add_modules` rules - they describe a different
  repository shape.

## Three findings the toolkits produced (all verified in our code)

1. **The VST3 reports the wrong version in Windows file properties.**
   `UPICuitousPlugin_resources.rc` was generated on 2026-10-18 and still says
   `FILEVERSION 1,11,0,0`, so Explorer -> Details shows 1.11.0 while the
   plugin is 1.15.0 (the host and `moduleinfo.json` see the right version).
   JUCE's CMake only regenerates the `.rc` when the icon changes, never
   when `VERSION` changes. Fixed by deleting the `.rc` on a version change
   (done by `scripts/build.ps1`).

2. **The plugin reports no latency.** `getLatencySamples()` is never
   implemented, yet the live granular shifter deliberately sits 220 ms behind
   the write head, the tape shifter 40-200 ms, and the phase vocoder adds a
   1024-sample prime. The host cannot compensate, so the live arcs land
   late against the rest of the track. Fixed in v1.16.

3. **The audio thread still takes a lock.** `juce-review` and
   `audio-dsp-review` both flag `CriticalSection::tryEnter()` on the audio
   thread: the try-lock does not block, but `exit()` performs a syscall to
   wake waiters, so it is not realtime-safe. Our `dataLock` is held for the
   whole score render, and the 30 Hz status timer takes it on the message
   thread. v1.15 made the contention inaudible (the dropped block is faded);
   the lock-free snapshot (UPIC-32) removes the contention itself.

## Also worth adopting

- **pluginval** (Tracktion's VST3 validator, free download): headless bus,
  state, editor and bypass validation in seconds. It is the industry check
  that our 13-check smoke test is not. Not on this machine yet.
- **A `known-issues.yaml` for this repo**, in the APC format, so the
  recurring failures have a written root cause and prevention.

## Adoption order

1. `scripts/build.ps1` + the stale `.rc` fix (S) - one command, no more
   hand-typed version bumps.
2. Latency reporting in the VST3 (S) - a real host-correctness fix.
3. pluginval in the build as a gate (S) - headless host validation.
4. The lock-free model snapshot (L) - removes the last lock from the audio
   thread.
5. Per-wave design notes in `docs/waves/` (S) - the APC plan template,
   applied to the ideas on the Kanso board.
