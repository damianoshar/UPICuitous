# Wave 8 - the VST3 plugin (v1.11)

**Status:** shipped in v1.11 (`7cbbda4`).
**Kanso:** card 100 (Done).
**Goal:** the same instrument inside a DAW.

## Why

The app is a standalone program; a DAW user cannot put it in a project,
cannot automate it and cannot record it. The instrument had to become a
plugin - with the *same* UI and the *same* engine, not a second
implementation.

## What was decided

- **`Plugin/` is a separate CMake project.** The repo-root
  `CMakeLists.txt` stays the stand-alone app / APK build; the plugin
  compiles the unchanged `Source/` UI and engine into
  `UPICuitous.vst3` (Release x64) with the JUCE-bundled VST3 SDK.
- **One component, two drivers.** `MainComponent` got a `pluginMode`
  flag: in plugin mode no audio device is opened, the host's
  `processBlock` drives `prepareToPlay` / `getNextAudioBlock`, the host's
  input bus count replaces the device input count for the live tap, the
  Audio button is hidden and the device timer refresh is skipped.
- **The host state is the piece.** `getStateInformation` returns the
  same JSON text as a saved `.upic.json`, so a DAW project and a file
  round-trip identically - no second format, no conversion.
- **The editor is the app's UI**, resizable, in the processor's editor.
- **A host-less smoke test** (`Plugin/PluginSmokeTest.cpp`) drives the
  wrapper the way a host would - without a DAW - so the plugin is
  verifiable in CI: plugin-mode construct, in-place process buffer,
  monitor passthrough from the host input bus, Play render, state
  round-trip, editor lifecycle.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The plugin would need its own UI | It reuses the app's UI unchanged - one code path, one look |
| The host's buffer convention would differ from the device's | The host buffer follows the in-place convention (channel k carries input k on entry), so the live tap needs no special case |
| The JUCE VST3 SDK in the local tree was corrupt | `VST3_SDK/base/source/fobject.h` had a stray line break inside a `#ifndef`; restored from the official 8.0.6 zip after verifying all 4222 JUCE files (only that one differed). This is `known-issues.yaml` KI-001, and `scripts/check_juce.ps1` checks for it now |

## What was verified

- The plugin smoke test (13 checks then, 15 now) - the host-level gate.
- The stand-alone selftest stayed at 162/162 - the plugin changed no
  behaviour of the app.

## What came out of it

- The latency was never reported to the host (wave 13 fixed it).
- The version resource stayed stale (wave 13 fixed it).
- The editor's size handling and the DAW's caching of the plugin list
  are the two things a user hits first (the install script in wave 16
  covers the second).
