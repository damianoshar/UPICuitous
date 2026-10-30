---
name: Projucer Project
description: Configure JUCE Projucer .jucer projects across exporters (VS, Xcode, Linux, Android Studio). Use when adding defines, tuning exporter settings, or changing generated builds without losing hand tweaks on resave.
---

# Projucer project driver

Reusable workflow for editing a JUCE `.jucer` project by hand and regenerating
builds safely. Written against JUCE 8.0.6 / Projucer; verify IDs against the
exporter sources below when in doubt.

Worked example in this repo: `UPICuitous.jucer` + `Builds/Android`.

## File layout (verified in `UPICuitous.jucer`)

- `<JUCERPROJECT>` root attributes: `defines` (global preprocessor
  definitions for **every** exporter, newline-separated as `&#10;`),
  `cppLanguageStandard`, `useAppConfig`, `bundleIdentifier`, `version`.
- `<MAINGROUP>` / `<GROUP>` / `<FILE>`: source list. `compile="1"` means
  compiled, `compile="0"` means header-only reference.
- `<MODULES>` / `<MODULE id="juce_core" showAllCode="1" useLocalCopy="1"
  useGlobalPath="0"/>`: `useLocalCopy="1"` resolves modules from the
  `JUCE/modules` tree (see `MODULEPATHS`).
- `<EXPORTFORMATS>`: one element per exporter (`VS2022`, `XCODE_MAC`,
  `LINUX_MAKE`, `ANDROIDSTUDIO`, ...), each with `targetFolder`,
  `<CONFIGURATIONS>`, and per-module `<MODULEPATHS>`.
- `<JUCEOPTIONS/>`: project-level JUCE flags (empty here).

## Durable-vs-volatile rule (the important one)

A Projucer resave **regenerates** `Builds/<Exporter>/*` and `JuceLibraryCode/`
from the `.jucer`. Any hand edit to a generated file (manifest theme, gradle
tweaks, custom CMake lines, signing configs) is wiped by the next resave
unless it also has a `.jucer` source. Before editing a generated file, find
its `.jucer` source; if none exists, the edit must be re-applied post-resave.

## Recipes

### 1. Add a global define (all exporters, incl. Android CMake)

Append to the root `defines` attribute, `&#10;`-separated:

```xml
defines="...&#10;JUCE_CORE_INCLUDE_JNI_HELPERS=1"
```

Why this matters: some JUCE features are invisible to app code without their
flag. Example: `juce_core.h` declares `juce::getEnv`/`getCurrentActivity`
only when `JUCE_CORE_INCLUDE_JNI_HELPERS=1`, and `juce_core.cpp` sets it for
its own TU only - app TUs fail with `no member named 'getEnv' in namespace
'juce'` until the define is set project-wide. Harmless on desktop builds,
where platform gates compile it out. (Identical redefinition inside JUCE's
own TUs is benign.)

Prefer project `defines` over module settings when the flag has no `Config:`
comment in its module header: without one, Projucer's module UI does not list
it. Check with: `grep "Config: JUCE_<FLAG>" JUCE/modules/<module>/*.h`.

### 2. Android exporter settings are element attributes

On the `<ANDROIDSTUDIO>` element, e.g.:

```xml
<ANDROIDSTUDIO targetFolder="Builds/Android" androidTheme="@android:style/Theme.NoTitleBar">
```

Known-good setting (verified in
`JUCE/extras/Projucer/Source/ProjectSaving/jucer_ProjectExport_Android.h`):
`androidTheme` ("Android Theme", e.g. `@android:style/Theme.NoTitleBar`),
plus `androidVersionCode`, `androidMinimumSDK`, `androidTargetSDK`. The same
file is the source of truth for every other Android setting ID - read it
before hand-adding attributes, and confirm with one resave that the generated
`AndroidManifest.xml` / `build.gradle` / `CMakeLists.txt` picked them up.

Things with **no** `.jucer` source (custom signing configs, hand-tuned gradle
blocks, one-off manifest lines) stay generated-file edits: keep a
post-resave checklist and re-apply them after every resave.

### 3. Safe resave procedure

1. `git status` / `git diff --stat` first: know which generated files carry
   hand tweaks (anything under `Builds/` or `JuceLibraryCode/`).
2. Make the `.jucer` edits (defines, exporter attributes, file list).
3. Resave in Projucer (File > Save, or open + save) - this rewrites all
   exporter folders and `JuceLibraryCode/`.
4. `git diff` the regenerated tree: confirm the intended settings landed
   (e.g. the CMake `add_definitions` line, the manifest `android:theme`)
   and re-apply any hand tweak the resave dropped.
5. Rebuild per exporter; commit the `.jucer` change together with the
   regenerated output (or leave generated dirs untracked per repo policy -
   decide once, document in `BUILDING.md`).

### 4. Verify

- Desktop exporters: open the generated solution/project and rebuild; the
  `.jucer` `defines` change must appear in the compiler command line.
- Android: `assembleDebug` in Android Studio is the real check - confirm new
  defines in the `clang++` invocation (Gradle build output) and new settings
  in the generated manifest/gradle files.
- If a hand-added `.jucer` attribute has no effect after resave, its ID is
  wrong: look up the exact setting ID in the corresponding
  `jucer_ProjectExport_*.h` exporter source.
