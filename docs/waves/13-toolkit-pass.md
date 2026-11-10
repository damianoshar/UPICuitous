# Wave 13 - the toolkit pass (v1.16)

**Status:** shipped in v1.16 (`0affd06`).
**Kanso:** cards 261, 262, 263 (Done).
**Goal:** the tooling around the code - one build, an honest version, a
reported latency, a validated plugin.

## Why

The toolkit review (see `TOOLKITS.md`) found the gaps that are not
bugs but make everything else harder: the Sonic Skills `vst3-review`
found the missing latency report, and APC's `known-issues.yaml` case
`build-006` found the stale Windows version resource.

## What was decided

- **One build script** (`scripts/build.ps1`). It kills the app, builds
  it, runs the selftest (and fails if the selftest exits non-zero),
  builds the plugin and the smoke test, runs it, and runs pluginval.
  `-Version` bumps the version, `-SkipApp` / `-SkipPlugin` narrow it
  down. The sequence that used to be typed by hand every wave is one
  command.
- **The stale version resource is fixed.** JUCE writes
  `<Name>_resources.rc` from `Info.txt`, but the custom command only
  depends on the icon - so a `VERSION` bump never reached the `.rc` and
  the VST3 still said `FileVersion 1.11.0` while the plugin was 1.15.0.
  The script compares the `.rc` against `Plugin/CMakeLists.txt` and
  deletes it when it is stale; the build regenerates it.
- **The host latency is reported.** `getLatencySamples()` was never
  implemented, although the live path is delayed: the granular shifter
  reads 220 ms behind the write head, the tape 40-200 ms, the phase
  vocoder a 1024-sample prime. The host can only compensate ONE number
  for the whole plugin, so the component reports the delay **only when
  every audible path on the page is delayed** (a live-only page: 220 ms
  / 120 ms / 21 ms per mode); a page that also carries generator arcs
  reports 0, because the score has to stay sample-accurate against the
  project. The value is published from the status timer (the message
  thread) and JUCE tells the host when it changes. The status line shows
  it ("LAT 220ms").
- **pluginval is the validation gate.**
  `scripts/fetch_pluginval.ps1` downloads Tracktion's validator (not
  committed) and the build script runs it with `--strictness-level 5`.
  **The VST3 passes: SUCCESS** (bus arrangements, automation, editor
  open/close, state).

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The build script would touch the user's files | It never touches `UPICuitous.jucer` or `Builds/Android/*` (that changed in wave 16, when the `.jucer` became safe to touch) |
| Reporting a latency would misalign the score | The delay is only reported when *every* audible path is delayed |
| pluginval would be flaky in CI | It is a local gate, not a CI one - it is skipped (with a warning) when the binary is not installed |

## What was verified

- Selftest 191 -> 194 checks (three latency checks).
- The plugin smoke test 13 -> 15 checks (the latency round-trip through
  the processor).
- pluginval at strictness 5: SUCCESS.

## What came out of it

- The version bump had a broken regex and the app stayed at 1.15.0 while
  the plugin said 1.17.0 - fixed in v1.18 (and the `.jucer` joined the
  version in wave 16's housekeeping).
- The DAW pass showed the plugin loads and plays; the user's own DAW
  session then found the Live-tab stall (wave 14).
