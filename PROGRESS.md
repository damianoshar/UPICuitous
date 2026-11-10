# UPIC Clone - Progress Log

_Updated 2026-10-25 - v1.27: the bottom wavetable editor forks a private copy for the selected arcs instead of rewriting every arc that shares the wave, saw/square display without the closing diagonal, double-click Stop rewinds to the start, and Clear asks first with a red confirm button - selftest 323/323, smoke 15/15, pluginval SUCCESS_

A Xenakis-UPIC homage: draw pitch curves on a time-vs-pitch page, give each
arc a wavetable + envelope, and hear it synthesized - now with sample
material, live-input transposition and a tablet-friendly UI. The repo holds
three builds of the same idea (`Source/` JUCE is the main one, plus the
`python-upic/` and `web-upic/` sketches sharing the `.upic.json` format).

## Status at a glance

| Item | State |
|---|---|
| Build | MSBuild `Debug x64` (VS 2026) - clean, no warnings blocking |
| VST3 | `Builds/Plugin/UPICClonePlugin_artefacts/Release/VST3/UPIC Clone.vst3` (Release x64) - 15/15 smoke checks |
| Selftest | `UPICClone.exe --selftest` -> **323 checks, ALL PASSED**, exit 0 (a failure now exits 1) |
| Snapshots | 6 offscreen PNGs in `%TEMP%` (see "Test setup") |
| Git | all source committed, including `UPICClone.jucer` and `Builds/Android` (committed after the tablet pass); generated build trees are gitignored |
| Device test | done: the tablet pass covers v1.6-v1.17 (tabs, grids, both pitch selectors, rotation, view toggles); APKs built in Android Studio |

## Progress tracking

The same status also lives in Nextcloud as a dedicated Kanso kanban board,
**"UPIC Clone"** - columns Backlog / Next up / In progress / Done: one done
card per wave (base app, v1.2, v1.3, live input, v1.6/v1.7, v1.8, v1.9,
v1.10, v1.11 VST3, this log) with per-card details and commit hashes, plus
open cards for the next steps (lock-free snapshot, Projucer housekeeping,
Scala import, per-slot Ref). Labels: `milestone` / `android` / `docs` / `backlog`.
`docs` / `backlog`.
(The Applications board keeps a pointer card in its UPICClone stack.)

## Milestone log (all commits, oldest first)

Every wave has a design note in `docs/waves/` - what it was for, what was
decided, which risks were real, what was verified and what came out of
it. The index is `docs/waves/README.md`.

### Wave 0 - base app (initial commit)
- `972b998` Initial commit: UPIC clone (JUCE app, Python and web versions).
  Score pages, arcs, wavetable editor, envelope editor, polyphonic engine,
  `.upic.json` save/load, WAV render. Sample slots and the sample
  pitch-shift material were already part of the model.

### Wave 1 - v1.2 composition tools
- `d27f250` v1.2: magnetic sieves with zones, multi-select editing,
  generators, click-free stop. Nine-remainder sieve quantizer drawn as
  magnetic zones, shift-click multi-select + Ctrl+A/D, arrow nudge, four
  nUPIC-style procedural generators, fade-out stop.

### Wave 2 - v1.3 mobile & Android
- `72deb0d` v1.3: responsive dp layout, touch gestures, Android app project.
- `cb74302` Add Projucer project file for the current source tree.
- `fcdfcda` Fix Debug startup assertion: keep UI string literals ASCII-only.
- `b89f59f` Android: immersive fullscreen + display refit for rotation.
- `8529006` Android landscape: NoTitleBar theme, voice strip replaces empty
  side column.
- `2ee9fe9` Add juce-android-window-fit skill (reusable recipe).
- `cf70f2c` Apply bar-autohide skill: app-side `hideSystemBars` + refit
  re-assert (`AndroidBars.h`).
- `59a6afd` Fix NDK `getEnv` error: `JUCE_CORE_INCLUDE_JNI_HELPERS` +
  durable Android theme.
- `603267d` Add projucer-project skill (generic `.jucer`/exporter workflow).

### Wave 3 - live input & capture
- `b8ffca8` Live input: monitor + capture takes into sample slots (no
  live-gate voice). In gain, Mon, Capture button; captures record straight
  into the selected slot.
- `5f2bb65` Live input follows arc pitch: mode-2 (live) arcs via the
  granular shifter (`LivePitch.h`).

### Wave 4 - v1.6/v1.7 tabs and UI polish
- `5731ea6` Live pitch tab: same page + timeline, mode-2 arcs, Hz-labelled
  grid; mic requested on tab switch.
- `7018a3f` Score and Live pitch tabs show only their own arcs (strict
  two-way layer visibility).
- `083ae9f` UI polish: canvas texts above shapes (dark label plates),
  larger right-aligned hints.
- `1e1aac8` Canvas instructions right-aligned and bold; compact wave/env
  preset dropdowns (box capped at 150 dp) so preset buttons get the space.
- `98bba31` View toggles: Wave / Env / Hints. One editor hidden -> the other
  takes full width; both hidden -> slim voice strip; Hints hides canvas
  instruction lines + toolbar hints.
- `952373b` Audio settings: in-app overlay with an OK button (an Android fix
  - a JUCE `DialogWindow` there has no title bar and no way to close).

### Wave 5 - v1.8 one tab strip
- `81a3750` v1.8 material tabs (first cut): Osc | Sample | Live replace the
  Material dropdown.
- `00b34d3` v1.8 one tab strip: the old `Score | Live pitch` strip and the
  bottom material strip merged into a single strip above the canvas; each
  tab is a material **and** its own canvas layer.
- `fe7ce67` Sample tab: playback-speed pitch grid (`Ref x 2^k`) with ratio
  labels; follows the Ref slider live.

### Wave 6 - v1.9 live pitch modes
- `ca6bd6c` v1.9: the Audio settings overlay gains a **Live pitch**
  selector - Granular (the v1.5 default) / Tape / Phase vocoder.
  `LivePitch.h` holds all three behind a per-voice `LiveShifter`; a mode
  change drops the live voices so each re-anchors. Selftest 141 -> 153
  checks (shifter frequency checks for every mode, selector round-trip).

### Wave 7 - v1.10 sample pitch modes
- `97d1115` v1.10: the Sample tab gains the same **Pitch** selector as
  the Live tab - Varispeed (tape, the original behaviour) / Granular /
  Phase vocoder. `LivePitch.h` refactored into source-agnostic cores
  (`GrainCore`, `VocoderCore`) with thin ring wrappers for live input and
  a `SampleVoice` (position + grain + vocoder) for material playback;
  the engine reads the material once (`readMaterial`) for all modes, and
  mode changes drop the sample voices so each re-anchors. Also fixed a
  **phase-vocoder overlap-add bug**: OLA rows accumulated across ring
  wraps so a stale slot echoed back one ring length (4096 rows) later -
  audible as periodic ~90 ms level dips and lost zero crossings at
  ratios != 1. The frame's fresh hop now assigns its rows, so every
  output row is exactly its four window contributions (verified in a
  Python model before the C++ fix). Selftest 153 -> 162 checks (sample
  granular / vocoder frequency at r=1, 2, 0.5 + peak, selector
  round-trip).

### Wave 8 - v1.11 VST3 plugin (current)
- `7cbbda4` v1.11: VST3 build of the same app. `Plugin/` is a separate
  CMake project (the repo-root `CMakeLists.txt` stays the stand-alone
  app / APK build) that compiles the unchanged `Source/` UI + engine into
  `UPIC Clone.vst3` (Release x64) with the JUCE-bundled VST3 SDK.
  `MainComponent` gained a `pluginMode` ctor flag: no audio device is
  opened, the host's `processBlock` drives
  `prepareToPlay`/`getNextAudioBlock`, the host input bus count replaces
  the device input count for the live tap, the Audio button is hidden and
  the device timer refresh is skipped. Host state is the same JSON text as
  a saved `.upic.json` piece, so a DAW project and a file round-trip
  identically. `Plugin/PluginSmokeTest.cpp` drives the wrapper like a host
  would without a DAW (13 checks); the stand-alone selftest stays 162/162.
- Also: one JUCE file in the local tree was corrupt
  (`VST3_SDK/base/source/fobject.h` had a stray line break inside
  `#ifndef SMTG_HIDE_DEPRECATED_INLINE_FUNCTIONS`) which broke the first
  VST3 compile; restored from the official 8.0.6 zip after verifying all
  4222 JUCE files against it (only that one differed).

### Wave 9 - v1.12 live ratio grid (current)
- v1.12: the **Live** tab can draw the **Sample tab's grid** (strong lines at
  `Ref x 2^k`, faint half-octaves, ratio labels `0.125x ... 1x (Ref) ... 32x`,
  Ref line in orange) instead of the Hz frequency grid - the same lines in
  the same places, so the two readings can be compared on the same canvas.
  A `Ratio grid` toggle appears next to the Live tab (it is shown only while
  the Live tab is active, and shrinks or hides on a narrow strip); the
  default is still the Hz grid, so nothing changes until the user opts in.
  The grid is a view preference: session-only, like the live pitch mode, and
  not part of the saved piece. Selftest 162 -> 169 checks (the grid defaults
  to Hz, the toggle switches it on/off, the button is laid out in the strip,
  and the live tab renders the sample grid offscreen).
- The VST3 build (`Plugin/`) compiles the same `Source/`, so the plugin
  artefact is rebuilt for this wave as well (version 1.12.0).

### Wave 10 - v1.13 the live path takes the sample path's pitch range (current)
- v1.13: the **Live** tab was clamped to `0.25x - 3.0x` while the **Sample**
  tab could shift `1/16x - 16x` (four octaves) - the same ratio formula
  (`arcPitch / Ref`), a different limit. The live range is now the sample
  range, so a live arc reaches its pitch at any ratio:
  - `kLiveMinRatio`/`kLiveMaxRatio` are now `1/16` / `16`.
  - The granular shifter's grain **shrinks with the ratio** above ~5.5x: the
    source window (40 ms) stays constant, so the read never needs more
    history than one grain and the 0.22 s read lag does not grow with the
    shift. Below 5.5x the old fixed grain is kept, so the sound of the
    usual range is unchanged. `GrainCore::setHop()` keeps the crossfade
    position when the grain length changes, so there is no click.
  - The phase vocoder needed no change (it already remaps partials and
    drops what falls outside the spectrum).
  - **Tape** keeps an octave up/down: it reads the mic faster than the mic
    arrives, so above ~2x it would have to re-anchor every few ms.
- Selftest 169 -> 175 checks: the live range matches the sample range, the
  grain shifter reaches 4x and clamps at 16x, and a live arc measures
  400 Hz (4x), 1600 Hz (8x) and 25 Hz (1/4x) at the engine level. A
  failing selftest now exits non-zero (it always exited 0 before).

### Wave 11 - v1.14 the live vocoder stops buzzing (current)
- v1.14: the **live** phase vocoder buzzed while the Sample tab's vocoder
  (same `VocoderCore`) was clean. Cause: the live ring is filled per block,
  so the write head only advances every block - and the engine passed that
  *block* position to the shifter for every sample in the block. The vocoder
  reads AT the write head, so `readLiveRing` clamped it to the newest frame
  and it fed the core **the same frame for all 256 samples** (a staircase).
  The vocoder faithfully re-synthesised that staircase: the measured
  spectrum had a sideband at 87.5 Hz (187.5 - 100) *louder than the 100 Hz
  fundamental*, plus 287.5 and 475 Hz - the zero-order-hold signature, and
  exactly the "buzz" that was heard. (The granular shifter reads 220 ms
  behind the write head, so it never hit the clamp - which is why only the
  vocoder buzzed.)
- Fix: the engine now passes the **per-sample** frame position
  (`liveWriteEnd - (i1 - n) - 1`) to the live shifter, and the vocoder
  reads the frame that is current at that sample instead of running its own
  read position. The read is continuous across block boundaries again, and
  the tape shifter's re-anchor window (which also saw the block steps) is
  smoother.
- Result: the sidebands collapse - 87.5 Hz 0.0471 -> 0.0026, 287.5 Hz
  0.0145 -> 0.0003, 475 Hz 0.0089 -> 0.0003 - while the fundamental gets
  *stronger* (0.0418 -> 0.0715). Selftest 175 -> 176 checks: the live
  vocoder's sideband energy must stay under 25% of the fundamental (it was
  169% before the fix).

### Wave 12 - v1.15 no more clicks (current)
- v1.15: the user reported "audio sometimes has clicks, as if the buffer is
  small even in large buffers". A selftest harness was added to find them
  (see below), and three real click sources were found and fixed. None of
  them was the DSP: the engine renders **bit-identically** whatever block
  size the host uses, and the peak second-difference of every mode is
  ~0.0002 (the natural curvature of a sine).
- **Fix 1 - a dropped block was cut, not faded.** The audio thread never
  blocks on the model lock (it try-locks), but when the UI thread held it
  (the 30 Hz status timer does, and so does every UI action) the callback
  returned with a **cleared** buffer: the previous block's last sample to
  zero is a step, i.e. a click - and it happens whatever the host's buffer
  size is, which matches "clicks even in large buffers". The score now
  renders into its own buffer, and a block that cannot be rendered is
  faded out from the last sample over ~2 ms and faded back in afterwards
  (`MainComponent::renderScoreBlock`, `scoreGain`/`scoreTail`).
- **Fix 2 - `engine.reset()` restarted every oscillator's phase.** That is
  a full-scale jump (measured: 0.97 on a 0.4 peak) whenever the engine was
  reset mid-flight: a page change, a loaded sample, a capture install, a
  host state load, a pitch-mode change. The phase is free-running state,
  so `reset()` now only drops the material/ring readers (they have to
  re-anchor); the phase keeps running.
- **Fix 3 - gains stepped.** The master gain and the monitor (input) gain
  were applied per block, so a slider move or host automation was a step
  in the output - a click. Both are now smoothed per sample (~17 ms time
  constant).
- **Fix 4 - the model changing under the audio thread.** A page change, a
  loaded sample, a capture install, a pitch-mode switch or a host state
  load replaces the sound that is playing - a step, i.e. a click. The
  message thread now bumps a model epoch counter and the audio thread
  fades across the change (the old tail ramps down, the new content
  ramps in).
- New selftest section (14 checks, 176 -> 191): a click hunt that renders
  every mode (osc / sample varispeed / sample granular / sample vocoder /
  live granular / live tape / live vocoder) in 64- and 1024-sample blocks
  and asserts (a) the second-difference peak stays under 0.002 and (b) the
  two renders are identical (block-size independence, measured 0.00000),
  plus the same with a moving pitch curve, plus the phase-continuity
  check for `reset()` (it fails with the old code: max diff 0.97).
- Known and left alone: the SAMPLE tab's granular mode shifts the grain
  taps while the ratio moves, which leaves a small tick at the grain
  boundary (measured 0.010 on a 0.32 peak, about -40 dB). It is inherent
  to granular pitch shifting with a moving ratio; the guard allows it.

### Wave 13 - v1.16 the toolkit pass: one build script, the version resource, host latency, pluginval (current)
- v1.16 came out of the toolkit review (see `TOOLKITS.md`): the Sonic
  Skills `vst3-review` found the missing latency report, and APC's
  `known-issues.yaml` case `build-006` found the stale Windows version
  resource.
- **One build script** (`scripts/build.ps1`): kills the app, builds it,
  runs the selftest (the script fails if it exits non-zero), builds the
  plugin + the smoke test, runs it, and runs pluginval. It never touches
  `UPICClone.jucer` or `Builds/Android/*`. `-Version 1.16.0` bumps the
  plugin version, `-SkipApp` / `-SkipPlugin` narrow it down.
- **The stale version resource is fixed.** JUCE writes
  `<Name>_resources.rc` from `Info.txt`, but the custom command only
  depends on the icon - so a `VERSION` bump never reached it and the VST3
  still said `FileVersion 1.11.0` while the plugin was 1.15.0. The script
  compares the `.rc` against `Plugin/CMakeLists.txt` and deletes it when it
  is stale; the build regenerates it (now 1.15.0).
- **The host latency is reported.** `getLatencySamples()` was never
  implemented, although the live path is delayed: the granular shifter reads
  220 ms behind the write head, the tape 40-200 ms, the phase vocoder a
  1024-sample prime. The host can only compensate ONE number for the whole
  plugin, so the component reports the delay only when every audible path
  on the page is delayed (a live-only page: 220 ms / 120 ms / 21 ms per
  mode); a page that also carries generator arcs reports 0, because the
  score has to stay sample-accurate against the project. The value is
  published from the status timer (the message thread) and JUCE tells the
  host when it changes. The status line shows it ("LAT 220ms").
- **pluginval is the validation gate.** `scripts/fetch_pluginval.ps1`
  downloads Tracktion's validator (not committed); `scripts/build.ps1` runs
  it with `--strictness-level 5`. **The VST3 passes: SUCCESS** (bus
  arrangements, automation, editor open/close, state).
- Selftest 191 -> 194 checks (three latency checks), plugin smoke test
  13 -> 15 checks (the latency round-trip through the processor).

### Wave 14 - v1.17 the Live-tab click no longer opens the mic (current)
- Reported as "the UI lags when I click Live". It was not DSP latency and
  not the paint: clicking the tab called `ensureLiveInputFor`, which opens
  the microphone with `setAudioChannels(2, 2)` - a **synchronous device
  restart on the message thread**. Measured with a temporary probe in the
  app: the click took **4735 ms**; the paint of the whole window was
  40 ms (Debug). After the fix the same click takes **0.14 ms**.
- The tab click is now pure UI. The mic is opened lazily where it is
  actually needed: **Mon**, **Capture**, or **Play** when the page has
  live arcs - all three already did this, so the tab call was redundant.
  Nothing about the live audio path changed (the shifter lag, the latency
  reporting and the sound are untouched).
- The selftest now clicks the real Live tab (it used to set the canvas
  layer directly to avoid the mic) and asserts the switch stays under
  500 ms and lands on the live layer; it also clicks Osc back. Selftest
  194 -> 197 checks.
- Version 1.17.0 (app + plugin); pluginval SUCCESS, smoke 15/15.
- Installed to `C:\Program Files\VstPlugIns\UPIC Clone.vst3`; the installed
  binary's SHA-256 matches the build output, and FileVersion / ProductVersion
  / moduleinfo all read 1.17.0. (The previous install said 1.15.0 but was
  actually the v1.16 build - the version number lagged the wave numbers.)

### Wave 15 - v1.18 the lock-free model snapshot (current)
- The last UI/audio-thread coupling is gone. The audio thread **never takes
  a lock**: it renders from an immutable `ModelSnapshot` that the message
  thread publishes (30 Hz timer, plus immediately after a load, a capture,
  a sample change or a page change). `dataLock` is now a message-thread
  lock only - it guards the model against the timer's copy, not the render.
- Why it mattered: `CriticalSection::tryEnter()` on the audio thread is not
  realtime-safe (the `exit()` can syscall to wake a waiter), and the lock
  was held across the **whole** score render - so a UI action could block a
  render, and a render could block the UI. v1.15 had already made the
  contention inaudible (a dropped block is faded, not cut); this removes
  the contention itself.
- The sample material is now **shared**: `SampleSlot::data` is a
  `std::shared_ptr<const juce::AudioBuffer<float>>`, so copying a snapshot
  copies eight pointers instead of the audio (the JSON format is unchanged).
- The handover has no Treiber stack (that shape has an ABA hazard): every
  published snapshot carries two references - the writer's and the reader's
  - the reader only ever *drops* its reference, and the writer frees. So
  **all deallocation happens on the message thread**, and the audio thread
  neither allocates nor frees.
- The audio thread owns the playhead and the page (a page epoch tells it
  when to switch); the message thread asks for a move (the score editor's
  scrub) or a voice reset with atomics. The engine is audio-thread only
  now - the message thread asks for a reset instead of calling it, and the
  WAV render got its own local engine.
- Two bugs the new path exposed, both fixed:
  1. the score fade ramp was not clamped, so a block longer than 96 samples
     faded *in* to up to 21x (an audible blast after a model change) - the
     new selftest caught it (peak 8.1 instead of 0.39);
  2. a page switch was silent: the epoch said *that* the page changed, but
     the new page only arrived with the next snapshot (33 ms away, and in a
     host never) - the page handlers now publish right away.
- The selftest grew 197 -> 210: the handover (publish -> take -> retire ->
  free, with an instance counter that proves the reader frees nothing), the
  **bit-exact** proof that a snapshot render equals the live model render,
  a lock-free render with no timer tick at all (the plugin-style
  `processBlock` loop), and the page switch (the new page's frequency is
  what is sounding: 880 Hz measured).
- `scripts/build.ps1 -Version` had a broken regex: the app version stayed at
  1.15.0 while the plugin said 1.17.0. Fixed (and it warns now if the app
  version cannot be updated). The app and the plugin are both 1.18.0.
- pluginval SUCCESS, smoke 15/15, selftest 210/210. The design note with
  the full details is `docs/waves/15-lock-free-model.md`.

### Wave 16 - the housekeeping (current)
No behaviour change: this is the maintenance that keeps the repo honest.
- **The Android project cannot be undone by a Projucer resave any more.**
  The settings that were only in the generated files (SDK 35, minSdk 24, the
  Android Gradle plugin 8.10.0, the gradle wrapper 8.11.1, the debug
  keystore) are now exporter settings in `UPICClone.jucer`, so a resave
  reproduces the same files. The one value Projucer hardcodes - the NDK
  version - is put back by `scripts/fix_android.ps1`, which also verifies
  every other value against the `.jucer` (`docs/android-build.md` has the
  ownership table).
- **The version cannot drift apart any more.** `build.ps1 -Version` writes
  the version into the plugin, the app *and* the `.jucer` (the Android
  manifest's versionName was stuck at 1.2.0), and the bump now runs before
  the app build - it used to sit inside the plugin block, so `-SkipPlugin`
  skipped it and the app was built with the old version.
- **`known-issues.yaml`** (Kanso 265): the failures that keep coming back,
  each with the symptom, the error pattern, the root cause, the resolution
  and the prevention - the damaged JUCE `fobject.h`, the VST3 install /
  UAC / stale-plugin case, the granular tick, the C4456 warning, the 33 ms
  score staleness, the version drift and the resave hazard.
- **`scripts/check_juce.ps1`**: the JUCE tree is gitignored, so git cannot
  repair it - the script compares the VST3 SDK headers against the SHA-256
  list and says which file is wrong (it caught a damaged header on purpose).
- **`Source/ModelSnapshot.h`** (v1.18) was missing from the `.jucer` file
  list - it compiled, but a resave would have dropped it from the generated
  project listing.
- The root `CMakeLists.txt` still says 1.2.0: that file is the user's (the
  app/APK build) and is deliberately not touched. It only affects the
  version resource of the app built through that path.

### Wave 17 - the sample reverse read (current)
- The first slice of the reverse idea in `FUTURE_IDEAS.md` (UPIC-25, the
  S-size one): **a sample slot can play its material backwards**. The
  future-ideas text says the reverse slice "can be proved with an
  on-screen Reverse button *before* any MIDI plumbing exists" - this is
  that button (the Sample row, next to **Load sample...**).
- The flag belongs to the slot (`upic::SampleSlot::reversed`), so every
  arc that uses the slot hears it backwards. It is **session state**, like
  the pitch modes and the grid: the piece format is untouched (the
  future-ideas text says the file format is not part of this slice). A
  capture replaces the material but keeps the slot's direction.
- **The engine mirrors the read, not the position.** The design note
  sketched "the slot reader advances at `-ratio`" - that is what the
  varispeed mode does, but the granular and vocoder modes keep the
  material advancing at 1x and shift the pitch, so a negative position
  would mean teaching the grain crossfade and the vocoder's overlap-add
  to run backwards. Mirroring the *material* gives the same sound with
  far less risk: the read index runs backwards, with the same
  interpolation and the same wrap rules. All three pitch modes work
  unchanged, and the seam stays seamless (a seamless material ends where
  it starts, so the mirrored read is seamless too).
- The pitch does not change with the direction: the read speed is the
  same, only the time axis is mirrored. Measured at three ratios - 220 /
  440 / 110 Hz forward *and* reversed - so the reverse neither cancels the
  ratio nor changes the pitch on its own. What it does change is the
  material's time axis and its formants (the "tape played backwards"
  effect); the arc's own pitch curve still runs forward.
- Selftest 211 -> 227: the reversed render **is** the forward render
  mirrored (max difference 0.000012), the pitch follows the ratio in both
  directions, all three pitch modes still sound, the flag is not in the
  piece format, and the Reverse button toggles. Plugin smoke 15/15,
  pluginval SUCCESS.
- The design note is `docs/waves/17-sample-reverse.md`. The next reverse
  step is the **live pitched reverse delay** (UPIC-27, M) - the live ring
  has to be read backwards for real, with a short feedback - and then the
  signed playhead (UPIC-28) that would use it.

### Wave 18 - the live reverse (current)
- The Live tab can play the microphone **backwards** (UPIC-27): the audio
  settings overlay gets a **Rev len** combo (0.5 / 1 / 2 s - the buffer
  duration the user picks) and a **Rev** toggle.
- **The capture is mirrored, not the read.** The capture's write head
  moves *backwards*, so the buffer holds the last D seconds mirrored in
  time - and reading it forward plays the input backwards. That is the
  same trick as the sample reverse (wave 17), and it means **the shifters
  are not changed at all**: a mirrored ring is an ordinary signal that
  happens to run backwards. The alternative (teaching the grain
  crossfade, the tape re-anchor and the vocoder's overlap-add to run
  backwards) is the expensive half the future-ideas note warned about.
- **Two buffers that swap roles**: one is captured, the other is the
  frozen segment the shifter reads. When the capture is full they swap,
  and both take exactly D seconds, so the swap lands when the playback
  ends. Nothing is ever **copied** - a copy of a 2 s stereo segment would
  be a ~0.3 ms stall in the audio thread (an audible dropout in a small
  block).
- The segment's ends are **faded** (5 ms), so the loop is click-free
  (the "fade the ring ends" of the future-ideas note), and the live
  voices re-anchor on the new segment.
- The message thread never touches the audio thread's state: the buffers
  are allocated once (at the maximum duration) and only the frame count
  changes, so nothing is reallocated while the audio thread may be
  reading.
- The pitch is the arc's, as usual - the reverse changes the direction of
  the material, not the ratio.
- Selftest 230 -> 237: the captured segment is mirrored (a rising ramp
  comes out falling: 0.750 vs 0.250), it keeps the input level, and a
  forward segment plays forwards while a mirrored one plays backwards
  (the slope of the rendered output flips sign: +0.1495 -> -0.1425) -
  the load-bearing property, measured end to end through the shifter.
  Plugin smoke 15/15, pluginval SUCCESS.
- The design note is `docs/waves/18-live-reverse.md`. What is **not** in
  it: the feedback of the original idea (30-50% back into the write head)
  - the segment simply loops - and the signed playhead (UPIC-28), which
  is what would make a mirrored *stroke* sound mirrored.

### Wave 19 - the arc list panel
- The **arc list**: the score's inventory on the right of the display, with
  an `Arcs` toggle next to the Wave / Env / Hints toggles. One row per
  arc, grouped **OSC / SAMPLE / LIVE**, showing the arc id, its **pitch
  range** (min-max Hz), its **time span** and its **page**. Clicking a row
  selects the arc on the canvas (the tab follows the arc's material), and
  the canvas's selection is followed back into the list.
- The sample and the live rows carry the **per-arc controls**: a **Rev**
  box (the material backwards) and a **mode** dropdown (the pitch-shift
  algorithm - Varispeed / Granular / Vocoder for a sample, Granular / Tape
  / Vocoder for the live input). The drawing options stay as they are:
  the voice row still sets what the *next* arc you draw gets.
- **The piece format is untouched.** The per-arc settings are **session
  state**, like the pitch modes, the grids and the slot's reverse - so the
  "expensive part" (the data model) never happened: no migration, no
  format change, the old files and the Python and web prototypes keep
  working. They are lost on a reload, like the other performance
  settings.
- The engine reads them per arc: the sample path takes the arc's mode
  (and re-anchors the voice when the algorithm changes - the grain taps
  and the vocoder's overlap-add belong to the old one), and the live path
  takes the arc's mode and its Rev (the live arcs read the mirrored ring
  instead of the forward one - the engine gets both).
- The sample's direction stays the **material's** (the slot's Rev button,
  which the panel's Rev box shows and edits for the sample rows); the
  live's direction is the **arc's** - the microphone is shared, so the
  direction belongs to the arc that uses it. The global live Rev button is
  a bulk edit over the live arcs (the v1.20 effect stays: everything is
  reversed) and the new arcs inherit it.
- The rows are rebuilt **on demand** (a dirty flag), not on every 30 Hz
  timer tick, and the panel never takes so much width that the canvas
  gets too narrow.
- Selftest 237 -> 243: the per-arc Rev genuinely flips the direction (the
  rendered output's slope goes from +0.19 to -0.11), the per-arc mode is
  used, the toggle and the panel are there. Plugin smoke 15/15, pluginval
  SUCCESS.
- The design note is `docs/waves/19-arc-list-panel.md`.

### Wave 20 - the MIDI input foundation
- **MIDI input**: the app can listen to a MIDI device. The Audio Settings
  overlay has a **MIDI in** combo (the system's inputs, `None` when there
  is none; the list is rebuilt each time the overlay opens), an **activity
  line** (the last CC / note, its value, channel and device) and a
  **Learn** row for two parameters - **Master gain** and **Draw
  intensity**.
- **The learn** binds the next CC or note on (with its channel, so two
  controllers can share a CC number). Real-time messages are ignored, the
  button cancels, and an unbound parameter belongs to the UI again.
- **The MIDI thread never touches the app**: the callback packs the message
  and a counter into one `std::atomic<uint64_t>`, and the 30 Hz timer does
  the learn, the binding and the parameter application on the message
  thread. No locks, no per-message `callAsync`.
- **Session state**: the bindings are a controller map, not part of the
  piece - the format is untouched, like the pitch modes.
- Selftest 243 -> 252: the device list is there, a CC drives the gain
  (100/127), a note drives the intensity (64/127), the monitor shows the
  message, a clock does not steal the learn, and a cleared binding leaves
  the parameter alone. Plugin smoke 15/15, pluginval SUCCESS.
- The design note is `docs/waves/20-midi-input-foundation.md`.

### Wave 21 - envelope points (+ / -)
- The envelope editor's header is a small toolbar: **+** adds a point,
  **-** removes the selected one, and the header shows the point count.
  **+** inserts **on the current curve** (the shape does not jump) at,
  in order: the spot last tapped on the curve (a hollow ring marks it),
  the segment after the selected point, or the widest gap - so pressing
  it repeatedly densifies evenly. The new point is selected for the
  immediate drag.
- **Tapping a dot selects it** (a white ring) and starts the drag; the
  selection survives the drag, so - is always one tap away. The ends are
  free: the first/last point is pinned to x=0/1 only while it really sits
  there, so deleting an end gives a late onset (the curve holds its first
  value) instead of a silent snap. Two points must remain.
- **The old gestures stay**: double-click adds at the cursor, right-click
  deletes the nearest point. They are no longer the only way in, which is
  what made the envelope feel like five presets on a tablet (no
  right-click, no reliable double-tap).
- **No model change**: the points were already in `Envelope` and already
  saved with any count in `.upic.json`; the cap stays at 32 (the
  per-sample scan). Editing reaches the audio through the usual 30 Hz
  snapshot.
- Selftest 252 -> 268: the midpoint insert on the curve, the round trip,
  remove/refuse/end removal, the late-onset hold, tap-to-place, the cap,
  and the buttons' placement and enable states. Plugin smoke 15/15,
  pluginval SUCCESS.
- The design note is `docs/waves/21-envelope-points.md`.

### Wave 24 - v1.26 the toolbar / selection ticket sweep
- The Kanso **Next up** tickets 328-334 are fixed: the toolbar is a live
  editor for the selected arcs, not only a template for the next stroke.
  Wave / Env / Intensity / Ref / sample slot apply to every selected arc;
  the PM toolbar values also seed newly drawn PM arcs.
- Arc selection is now multi-selection: Ctrl/Cmd/Shift-click on the canvas
  or in the arc list, with the toolbar showing the primary arc and editing
  all of them. Selecting an arc in the list switches to its tab (the
  `setMaterial` path) instead of only changing the tab flags, and it uses
  the row's page so arc ids that repeat between pages cannot collide.
- Envelope points get the same treatment: Ctrl/Cmd/Shift-click builds a
  group, dragging any selected point moves the group, and **-** removes the
  whole group (two points always remain).
- Ref is visible on both Sample and Live; moving it retunes the selected
  arcs immediately. PM arcs no longer show the sample/live "Follow the tab"
  mode menu (it only applies to modes 1 and 2).
- The audio-settings **OK** button is raised above the device selector so
  its hit area is not eaten by the selector's viewport.
- Sample granular was degenerating to varispeed because `GrainCore` clamped
  its hop to 2.0 while the sample path passes 0.04 seconds. The minimum is
  now 1e-6; the granular test uses a grain-friendly 100 Hz tone (4 cycles
  per 40 ms hop) so the measurement is not skewed by grain-rate combing.
- Selftest 283 -> 312: multi-select envelope drag/delete, arc-list layer
  switch, toolbar multi-edit, Ref on Sample/Live, PM menu absence, audio
  overlay hit test, generated PM inheritance, and varispeed vs granular at
  a 2x arc ratio. Plugin smoke 15/15, pluginval SUCCESS.
- The design note is `docs/waves/24-v126-ticket-sweep.md`.
- Commit: `df9b073` (v1.26.0).

### Wave 25 - v1.27 the wavetable fork and destructive safety
- The bottom wavetable editor is copy-on-write. With a selection, the
  first edit clones the current wave if any unselected arc (on any page)
  shares it, assigns the selected arcs to the clone, and edits the
  clone. With no selection, the old library/template edit is unchanged.
  Only Osc and PM allow wave edits.
- The editor forks before the first sample changes: mouse drawing uses
  the new `WaveformEditor::beginEdit()` hook, and the shape buttons use
  the same path. Arc ids are checked page-locally because they repeat
  across pages.
- Saw and square now draw as open strokes; the fill closes to the zero
  line separately, so the old diagonal from the last sample to the first
  is gone.
- Double-click Stop (400 ms window) stops and seeks the playhead to 0. A
  single Stop click still stops normally.
- Clear opens an in-app warning overlay with Cancel and a red "Clear
  page" button. The toolbar button and File > Clear page... share it;
  the arcs are untouched until the confirm button is pressed.
- Selftest 312 -> 323: shape-button fork, drawing fork, old-wave
  untouched, Stop rewind, Clear warning / cancel / confirm, destructive
  colour. Plugin smoke 15/15, pluginval SUCCESS, version 1.27.0.
- The built VST3 is 1.27.0; the system-installed copy is still 1.20.0
  until `install_plugin.ps1 -Version 1.27.0` is run with UAC approval.
- The design note is `docs/waves/25-v127-wavetable-and-safety.md`.
- Commit: `714a1c5` (v1.27.0).

## Feature inventory (what exists today)

### Canvas / score page
- X = time (pages run 5-120 s), Y = log pitch 20 Hz - 12 kHz; the timeline
  (time zoom/scroll, pitch zoom/pan, playhead) is shared by all tabs.
- Tools: Draw (snap), Line, Sine, Erase, Select/Move, Zone (magnetic sieve
  region). Right-click menu, shift-click cycles selection, Ctrl+A, Ctrl+D,
  Del, arrow nudge, Mute.
- Wheel: pan pitch / Alt+wheel zoom pitch / Ctrl+wheel zoom time /
  Shift+wheel pan time; drag the centre line or the playhead triangle to
  scrub. Touch: 1 finger acts like the mouse, a 2nd finger switches to
  pinch/pan.
- Pen pressure -> per-point intensity ("PRESS" / Press tool), stickiness
  slider controls snap pull, T-grid box sets the time grid (0.5-16 /s).
- Sieve quantizer: m + remainder list + presets, drawn and hit-tested as
  zones with labels.
- Canvas texts (grid labels, zone/arc tags) painted above arcs/zones on
  dark plates; stacking: zones < grid < arcs < stroke < scrub line < texts <
  playhead < border/hints. Instruction lines are right-aligned and bold,
  switchable with the Hints view toggle.
- Strict layer separation: each tab draws, hits, selects and marquees only
  its own arcs (`a.mode == layer`).

### The single tab strip (v1.8)
- One strip above the canvas: `[Osc] [Sample] [Live]` on the left,
  `[Wave] [Env] [Hints]` view toggles on the right.
- **Osc** - oscillator arcs (mode 0): note-name grid + orange A440 line;
  the wave editor shows the wavetable.
- **Sample** - sample arcs (mode 1): playback-speed grid (see below); the
  wave editor shows the selected slot; the Slot / Load sample / Ref / Pitch
  / In / Mon / Capture controls above drive it.
- **Live** - live arcs (mode 2): multiply-ratio grid (1x = input, 2x =
  octave up); the mic is opened lazily on Play with live arcs (v1.17).
  The old Hz frequency grid and its `Ratio grid` toggle are gone (v1.25):
  the ratio grid is always on. The sample input/capture group (gain, Mon,
  Capture) now lives on the Sample tab only.
- Tab colours: Osc/Sample blue highlight, Live orange; the tab is the
  material, so one state drives layer, drawing mode, editors and the
  generator / Assign-to-sel material.

### Pitch grids (per tab)
- Osc: note names at C octaves, G/D faint lines, A440 strong + named.
- Sample (v1.8): strong lines at `Ref x 2^k` (the unison/octave series)
  labelled `0.125x ... 0.5x ... 1x (Ref) ... 2x ... 32x`, faint half-octave
  lines between, the Ref line in orange; repositions live when Ref moves.
- Live (v1.25): the same multiply-ratio grid as Sample - strong lines at
  `Ref x 2^k`, faint half-octaves, ratio labels. 1x is the mic input as-is
  (labelled "input" instead of "Ref"), so there is no Hz scale.

### Voice editors (bottom area)
- Wavetable editor: 1024-sample single cycle, freehand draw/drag; presets
  Sine / Saw / Sqr / Tri / Noise / Smooth / Norm; per-waveform colour.
  With a selection, an edit forks a private copy when an unselected arc
  shares the wave, so it only changes the selected arcs.
- Envelope editor: 0-1 over arc life, up to 32 points; presets Flat / Perc /
  Swell / Gate / Strings; double-click add, right-click remove.
- Voice row: Assign to sel. / Del sel. / Mute sel. (or the slim strip when
  the editors are hidden); Inten and Dur sliders feed new strokes.

### Samples & capture
- Up to 8 slots (`kMaxSampleSlots`), selected with the Slot dropdown.
- Load sample... loads a WAV into the slot; the wave editor shows a
  read-only min/max envelope with name, duration, rate and slot number.
- Ref slider (20-4000 Hz, default C4 261.63) is the unison pitch: arc pitch
  / Ref = playback speed, which is exactly what the Sample grid shows.
- Pitch selector (v1.10, session-only, the same three algorithms as the
  Live tab):
  - **Varispeed (tape)** (default): the material position advances at the
    arc ratio (1/16x-16x), so pitch and length change together - the
    original tape behaviour.
  - **Granular (stretch)**: the material advances at 1x while the grain
    taps are read at the ratio, so the slot keeps its length at any arc
    pitch (grain-hop artefacts on transients).
  - **Phase vocoder**: the material streams at 1x through the shared
    smb-style vocoder (1024/4) - clean on sustained material.
  A mode change drops the sample voices so each re-anchors; empty slots
  fall back to the wavetable material.
- Capture records the live input into the selected slot (Mon monitors while
  you play, In sets the input gain).
- Note: an empty slot falls back to showing the current wavetable in the
  material view (pre-existing behaviour).

### Live input (masks / realtime)
- Live arcs transpose the mic/line input in realtime; the Audio settings
  overlay picks the algorithm (session-only, per user request v1.9):
  - **Granular** (default, v1.5): SMB-style overlapping Hann grains (40 ms
    hop) read at `ratio` while grain starts advance naturally; reads trail
    the write head by 0.22 s (bounded latency; unison passes through
    delayed but untouched).
  - **Tape** (v1.9): a true varispeed read at `ratio`, re-anchored with a
    20 ms equal-power crossfade only when the delay leaves a 40-200 ms
    window - no periodic re-graining, so transients pass through; low
    latency.
  - **Phase vocoder** (v1.9): smbPitchShift-style STFT (1024/4) - per-bin
    true-frequency analysis, partials remapped to `ratio`x their bin and
    re-emitted with the true phase advance per hop, so the overlap-add
    stays coherent at every ratio; clean on sustained material, smears
    transients. (v1.10 fixed the OLA: rows were accumulating across ring
    wraps, so a stale slot echoed back one ring length later - periodic
    level dips at ratios != 1.) (v1.14 fixed the live buzz: the ring is
    filled per block, so a vocoder reading at the write head saw the same
    frame for a whole block - a staircase it re-synthesised as a buzz at
    the block rate. The engine now hands the shifter the per-sample frame
    position, so the read is continuous.)
- The same cores (v1.10) drive the Sample tab's Pitch selector - the live
  shifter wrappers and the sample voices share `GrainCore`/`VocoderCore`.
- **Range (v1.13)**: the live path now takes the Sample path's range,
  `1/16x - 16x` (four octaves) - `kLiveMinRatio`/`kLiveMaxRatio` are the
  sample path's limits, and the ratio formula is the same
  (`arcPitch / Ref`). Two things make the wide range work on live input:
  - The granular shifter's grain **shrinks with the ratio** above ~5.5x
    (the source window stays 40 ms, so the read never needs more history
    than one grain). The 0.22 s read lag therefore does not grow with the
    shift, and the sound of the usual range is unchanged.
  - The phase vocoder already remaps partials to `ratio`x their bin and
    drops what falls outside the spectrum, so any ratio is safe.
  - **Tape** is the exception: it reads the mic *faster than the mic
    delivers*, so it keeps an octave up/down (`kLiveTapeMin/MaxRatio`).
- The audio thread feeds a 2 s ring buffer; Monitor is independent of the
  arcs; offline renders and stems deliberately produce silence for live
  arcs.

### Engine & transport
- Polyphonic wavetable synthesis: log-frequency interpolation, per-arc
  phase, 12 ms edge fades, tanh soft clip.
- Pages: Page box + Add/Dup/Del, per-page duration, Loop or Seq playback,
  Play/Stop (click-free), master gain. Double-click Stop rewinds to the
  start; Clear asks first (in-app overlay, red confirm button).
- Render WAV (16-bit) and Stems (one WAV per voice into a folder).

### Persistence
- `.upic.json` shared with `python-upic/` (and the web sketch): pages
  (`duration`, `arcs`, optional `zones`), `waveforms` (name, colour,
  1024 samples), `envelopes` (points x/y), sample slots (`samples`: name,
  sampleRate, embedded 16-bit frames), arcs carry `waveformId`,
  `envelopeId`, `intensity`, `muted`, `mode` (0/1/2), `sampleId`,
  `refFreq` (clamped on load).
- Loading resets transports/editors to defaults but keeps the current tab.

### UI infrastructure
- dp-responsive layout: a scale `s` from the window size, wrapped flow rows
  with measured heights, then a fit-shrink iteration (x0.92, floor 0.55)
  until the minimum height fits; trailing flex labels (status/time/hints)
  use `minSlice` so they wrap instead of being squashed.
- Portrait/landscape: editors side-by-side when wide, stacked when narrow;
  a single visible editor takes the full width; hidden editors give their
  space to the canvas; both hidden -> slim voice strip.
- View state (Wave/Env/Hints, audio overlay) is session-only - nothing is
  written to the piece.
- Audio settings overlay: dark backdrop + centred card with the device
  selector and an OK button; closes on OK, backdrop tap or Esc; device
  changes apply live. Replaces `DialogWindow` (unusable on Android).
- Status line + transient flash messages; tooltips on the tabs.

### Android
- `Builds/Android` Android Studio project (user builds APKs); `UPICClone.jucer`
  has the Android exporter, `microphonePermissionNeeded`, NoTitleBar theme
  and the JNI defines.
- Immersive fullscreen with bar autohide (app side, `AndroidBars.h`),
  rotation refit re-assert, landscape voice strip layout.
- RECORD_AUDIO runtime permission is requested on the first Mon / Capture /
  Play with live arcs (v1.17: opening the Live tab does not ask).
- Two reusable skills in `.opencode/skills/`: `juce-android-window-fit`
  (fullscreen/rotation recipe) and `projucer-project` (exporter settings
  that survive resaves).

### VST3 plugin (v1.11)
- Built by `Plugin/` (own CMake project, JUCE taken from `../JUCE`):
  `cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64`,
  then `cmake --build Builds/Plugin --config Release --parallel 4`.
  Artefact: `Builds/Plugin/UPICClonePlugin_artefacts/Release/VST3/UPIC
  Clone.vst3` (copy it into a DAW's VST3 folder to use).
- Category **Fx** (so hosts route the track audio in for the Live tab);
  transport stays the app's own Play/Stop (no host transport sync);
  output mono/stereo, input disabled/mono/stereo; no MIDI.
- The whole app UI is the editor (resizable 640x400 .. 4096x2400, opens at
  1280x800). Live input comes from the host's input bus, so monitor,
  Capture and mode-2 arcs work on the track input; the audio-device
  button is hidden (the host owns audio settings).
- Host state (save/load, presets) is the `.upic.json` piece text.
- Host-less smoke test (own target): `cmake --build Builds/Plugin
  --config Release --target UPICClonePluginSmokeTest`, then run the exe
  from `Builds/Plugin/UPICClonePluginSmokeTest_artefacts/Release/`.

## Architecture (file map)

```
Source/
  Main.cpp             entry point + the --selftest harness + offscreen snapshots
  MainComponent.h/.cpp window: toolbars, tab strip, transport, pages, editors,
                       file IO, audio devices, audio overlay, responsive layout
                       (pluginMode: host-driven audio, no device)
  ScoreEditor.h        canvas: tools, layers, grids, zones, selection, hints
  UpicModel.h          model (Waveform/Envelope/Arc/Page/Zone/SampleSlot),
                       JSON IO, palette + pitch helpers, demo page, defaults
                       (a SampleSlot's audio is a shared, immutable buffer)
  ModelSnapshot.h      the lock-free hand-over: the immutable model snapshot
                       the message thread publishes and the audio thread
                       renders from (no lock, no allocation on the audio
                       thread; the writer frees, the reader only reads)
  UpicEngine.h         realtime + offline renderer, mode 0/1/2 dispatch,
                       live-arc rendering, stems
  LivePitch.h          pitch-shift cores (GrainCore / VocoderCore) shared by
                       live wrappers (GrainShifter / TapeShifter /
                       VocoderShifter) and sample playback (SampleVoice)
  WaveformEditor.h     wavetable drawing + sample preview
  EnvelopeEditor.h     envelope drawing
  AndroidBars.h        immersive mode / autohide JNI glue
Plugin/                VST3 target (own CMake project)
  PluginProcessor.h/.cpp host-facing AudioProcessor; owns the MainComponent
  PluginEditor.h/.cpp  resizable editor hosting the processor UI
  PluginSmokeTest.cpp  host-less 13-check smoke test (own target)
python-upic/           tkinter prototype sharing the .upic.json format
web-upic/              single-file WebAudio sketch
Tools/fetch_juce.py    JUCE download helper (JUCE 8.0.6 in JUCE/)
Builds/VisualStudio2026/  VS solution (link UPICClone.sln, x64)
Builds/Android/           Android Studio project
Builds/Plugin/            CMake build dir for the VST3 (artefacts inside)
```

## Test & verification setup

- Build: `MSBuild Builds\VisualStudio2026\UPICClone.sln /p:Configuration=Debug
  /p:Platform=x64 /m`. Kill leftover `UPICClone.exe` first or the link fails
  with LNK1168.
- Selftest: run the exe with `--selftest` (`-PassThru`, check exit code, then
  clean up). Log: `%TEMP%\upic_selftest.txt`; expect `ALL CHECKS PASSED`,
  exit 0. 323 checks cover DSP (wavetable render, loop-seam continuity,
  live shifter frequency for granular / tape / vocoder, sample pitch modes
  for granular / vocoder at r=1, 2, 0.5 plus the output peak), model/JSON
  round-trips, layout at 1280x800 / 640x400 / 360x640 (recursive `find`,
  hidden views skipped), tab wiring, view toggles, the audio-overlay
  open/close cycle, both pitch-selector round-trips (Live, Sample), the
  v1.18 lock-free path (the snapshot hand-over, the bit-exact snapshot
  render, the lock-free render with no timer tick, the page switch), and
  the v1.27 wavetable fork / Stop rewind / Clear warning checks.
- Snapshots (offscreen, no screen access): `%TEMP%\upic_snapshot.png`
  (1280x800), `_640x400`, `_360x640`, `_noviews` (editors+hints hidden),
  `_audio` (overlay open), `_sample` (Sample tab active).
- VST3: `cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026"
  -A x64` + `cmake --build Builds/Plugin --config Release --parallel 4`
  (kill nothing; the plugin links fine while the app runs). The
  `UPICClonePluginSmokeTest` target then checks the wrapper without a host
  (15 checks: plugin-mode construct, in-place process buffer, monitor
  passthrough, Play render, state round-trip, editor lifecycle).
- Visual inspection: PowerShell `System.Drawing` crops with
  `NearestNeighbor`; pixel probes for exact colours (e.g. grid text
  `0xff8a8a99`).
- Caveat: JUCE timers never fire in `--selftest`, so timer-driven and
  device paths (mic, rotation) are verified on the tablet instead.

## Design decisions & conventions

- The tab **is** the material: layer, drawing mode, editor content and
  Assign/generator material can never disagree (one `setMaterial` entry
  point).
- Arcs are layer-filtered everywhere (draw, hit test, marquee, menus,
  selection).
- UI string literals stay ASCII-only (a Debug startup assert on Android
  caught non-ASCII literals before).
- View toggles / audio overlay are session-only; the piece format is
  untouched by UI state. Both pitch selectors are session-only audio
  preferences too: the Live one defaults to the v1.5 granular shifter, the
  Sample one to varispeed (the original tape behaviour), so a loaded piece
  sounds as it always did unless the user switches.
- Sample-toned UI lives in the Sample tab, live-input UI in the Live tab;
  the Live tab equals the old "Live pitch" layer.
- Live arcs render neutral white; sample/osc arcs take the waveform colour.
- Offline renders are deterministic: live arcs are silent there by design.
- The audio thread never locks and never allocates (v1.18): it renders from
  an immutable model snapshot that the message thread publishes, and the
  writer frees every snapshot - the reader only reads. The model lock
  (`dataLock`) is a message-thread lock now.
- The audio thread owns the playhead and the page while playing; the UI
  reads the published playhead and asks for a move (scrub) with atomics.

## Pending / next steps

The plan for what comes next is in **`docs/waves/19-arc-list-panel.md`** -
"the priority plan" section, ordered by what unlocks the most for the
least. The same order is on the Kanso cards (each one carries its
priority).

| # | Feature | Size | Notes |
|---|---|---|---|
| 1 | **The arc list panel** | M | **done (wave 19, v1.22)**: the arcs are listed on the right with their position info, grouped by type, with a per-arc Rev and mode |
| 2 | **MIDI input foundation** | S | **done (wave 20, v1.23)**: the device list, the activity monitor and the learn - the controller layer the scrub wave needs |
| 3 | **The signed playhead** | M | the transport that runs the score backwards - a mirrored stroke sounds mirrored |
| 4 | **The host transport follow** | M | the DAW's transport drives the app |
| 5 | **The 2-op PM proof** | M | a new voice: the arc's pitch is the carrier, the ratio the modulator |
| 6 | **The per-slot Ref + richer sample editing** | M | the Sample tab's material work |
| 7 | **Scala import** | M | the tuning file format |
| 8 | The full FM engine, the OSC wire, the musical time | L | the research-sized ideas, only after the 2-op proof |

The Future Ideas columns (S / M / L) stay flagged "do not implement yet"
until an item is pulled forward on purpose.

**Known and accepted**: see `known-issues.yaml` (the granular tick, the
33 ms score staleness, the root `CMakeLists.txt` version, the Nextcloud
sync).

**Backlog**: the two feature cards above (6 and 7).

The housekeeping is done: the Android settings live in the `.jucer`
(`docs/android-build.md`), the version cannot drift
(`build.ps1 -Version`), the recurring failures are in
`known-issues.yaml`, and every wave has a design note in `docs/waves/`.
