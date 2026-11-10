# Building - UPIC Clone

Current version: **1.27.0** (app + VST3). The build is Windows/MSVC;
JUCE 8 does not support MinGW.

## TL;DR

| Goal | Command |
|---|---|
| Everything (app + selftest + VST3 + smoke + pluginval) | `.\scripts\build.ps1` |
| Standalone only | `.\scripts\build.ps1 -SkipPlugin` |
| VST3 only | `.\scripts\build.ps1 -SkipApp` |
| Bump the version first | `.\scripts\build.ps1 -Version 1.27.0` |
| Install the VST3 (asks for admin rights) | `.\scripts\install_plugin.ps1 -Version 1.27.0` |
| Android APK | open `Builds/Android` in Android Studio, or `.\gradlew.bat assembleDebug` |
| Prototypes | `python python-upic/upic_clone.py` / open `web-upic/index.html` |

## Requirements

- **Visual Studio 2026** (v18) with *Desktop development with C++*
  (MSBuild + MSVC 14.51). The `Builds/VisualStudio2022` exporter also
  exists but the build script uses VS2026.
- **CMake 3.22+** (4.x is fine; its deprecation warning is harmless).
- **JUCE 8.0.6** in `JUCE/`. `python Tools/fetch_juce.py` downloads it
  without git; the root CMakeLists can also fetch it.
- Optional: **pluginval** for the validation gate -
  `.\scripts\fetch_pluginval.ps1` puts it in `Tools/pluginval/`
  (gitignored).
- Android: JDK 17, SDK 35, NDK 28.1.13356709, CMake 3.22.1.

### Why not MinGW?

JUCE 8 refuses to build with it:

```
JUCE/modules/juce_core/system/juce_TargetPlatform.h:113:6:
  #error "MinGW is not supported. Please use an alternative compiler."
```

Use MSVC. Do not spend time on MinGW flags.

## Option 1 - one-command build (recommended)

`scripts/build.ps1` runs the full sequence and stops on the first
failure:

1. kills a running `UPICClone.exe` (a running exe locks the link),
2. builds the app `Debug|x64` from
   `Builds/VisualStudio2026/UPICClone.sln` with MSBuild,
3. runs `UPICClone.exe --selftest` and fails if any check fails,
4. deletes a stale `*_resources.rc` (otherwise Explorer keeps showing an
   old VST3 version), builds the plugin `Release` with CMake,
5. builds + runs the host-less plugin smoke test,
6. runs pluginval at strictness 5 if it is installed.

Outputs:

```
Builds\VisualStudio2026\x64\Debug\App\UPICClone.exe
Builds\Plugin\UPICClonePlugin_artefacts\Release\VST3\UPIC Clone.vst3
```

`-Version 1.27.0` rewrites `Plugin/CMakeLists.txt`, the app's
`getApplicationVersion()` and `UPICClone.jucer` together, so the app,
plugin and Android version cannot drift apart. After a bump run
`.\scripts\fix_android.ps1` to sync the generated Android project.
`-SkipApp`, `-SkipPlugin` and `-SkipSmoke` narrow the run.

## Option 2 - build the standalone manually

With the Projucer solution (what the script does):

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
& $msbuild Builds\VisualStudio2026\UPICClone.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Or from the root CMake project (app + Android CMake entry):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel 4
.\build\UPICClone_artefacts\Release\UPIC Clone.exe
```

The Projucer file `UPICClone.jucer` is the UI/file-list source for the
VS2026 and Android exporters. A resave adds new files (for example
`Source/LivePitch.h`) and may reformat the exporter files.

## Option 3 - build and install the VST3

```powershell
# first time only
cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64
cmake --build Builds/Plugin --config Release --parallel 4

# smoke test (host-less: processor + editor + state)
cmake --build Builds/Plugin --config Release --target UPICClonePluginSmokeTest
.\Builds\Plugin\UPICClonePluginSmokeTest_artefacts\Release\UPICClonePluginSmokeTest.exe

# optional validation
Tools\pluginval\pluginval.exe --validate `
  "Builds\Plugin\UPICClonePlugin_artefacts\Release\VST3\UPIC Clone.vst3" --strictness-level 5
```

Install (an admin shell is needed for `Program Files`):

```powershell
# the script asks for admin rights (UAC prompt) and then verifies the result:
# the installed binary's SHA-256 must match the build, and the version
# resource + moduleinfo must read the expected version
.\scripts\install_plugin.ps1 -Version 1.27.0

# or by hand, from an admin shell:
Copy-Item "Builds\Plugin\UPICClonePlugin_artefacts\Release\VST3\UPIC Clone.vst3" `
          "C:\Program Files\VstPlugIns\" -Recurse -Force
```

A per-user install avoids UAC (many DAWs scan it):
`%LOCALAPPDATA%\Programs\Common\VST3`.

Then **restart the DAW or force a plugin rescan** - hosts cache the
binary. Verify the installed copy: right-click
`...\UPIC Clone.vst3\Contents\x86_64-win\UPIC Clone.vst3` ->
Properties -> Details; FileVersion and ProductVersion should match
`Plugin\CMakeLists.txt`, and `Contents\Resources\moduleinfo.json` should
agree. If a DAW still shows an older version, it is loading a cached
copy or a different scan folder.

The plugin reports its live delay to the host only when every audible
arc on the page is delayed (see README). pluginval at strictness 5
passes.

## Option 4 - build the Android app (tablet)

`Builds/Android/` is the committed Android Studio project (Projucer
export + hand tweaks). It compiles the root `CMakeLists.txt` with the
NDK and packages `libUPICClone.so` into an APK.

### Prerequisites (one-time)

- JDK 17, Android Studio.
- In the SDK Manager: **SDK Platform 35**, **Build-Tools 35**,
  **NDK 28.1.13356709**, **CMake 3.22.1** (pinned in
  `Builds/Android/app/build.gradle`).
- Let Android Studio write `Builds/Android/local.properties`
  (`sdk.dir=...`), or create it yourself.
- Both build types sign with the standard debug keystore
  (`%USERPROFILE%\.android\debug.keystore`, auto-created by Android
  Studio), so debug and release APKs are sideloadable. For a store
  build, replace `signingConfigs.juceSigning` with your own keystore.

### Build

```powershell
cd Builds\Android
.\gradlew.bat assembleDebug      # flavor debug_  + buildType debug
.\gradlew.bat assembleRelease    # flavor release_ + buildType release
```

Or open `Builds/Android` in Android Studio and press Run. The flavors
exist because the JUCE CMake build needs a different
`JUCE_BUILD_CONFIGURATION` per build type.

ABIs: `arm64-v8a` + `armeabi-v7a` for devices, `x86_64` for the
emulator (`debug_` also builds `x86`). Narrow them with `abiFilters` in
`app/build.gradle` for a smaller APK.

### On the device

- The mic permission is requested the first time it is needed - press
  **Mon**, **Capture**, or **Play** on a page with live arcs. Simply
  opening the Live tab does not ask for it and does not restart audio
  (v1.17).
- Save/Load/Render/Sample-loading use the system document pickers.
- Rotate freely: the layout reflows, the app does not restart.
- Touch: 1 finger = mouse, pinch = zoom time+pitch, two-finger drag =
  pan, long-press = context menu.

## Tests

```powershell
.\Builds\VisualStudio2026\x64\Debug\App\UPICClone.exe --selftest
Get-Content "$env:TEMP\upic_selftest.txt"
```

243 checks, no window and no audio device. They cover: log-frequency
interpolation, envelopes, the offline renderer, exact sample pitch
shift, all seven pitch modes (osc, sample varispeed/granular/vocoder,
live granular/tape/vocoder), the click guard (block-size independence
plus a second-difference limit), latency rules, JSON round-trip
(including Python-written files), layout at six window sizes with
pairwise non-overlap, tab wiring, the audio overlay, both pitch
selectors, the lock-free model snapshot (the hand-over, a bit-exact
snapshot render, a render with no timer tick, the page switch) and the
sample reverse read (the mirrored render, the unchanged pitch, the
session-only flag) and the live reverse (the mirrored capture, the
sign flip of the rendered slope, the arc list). A failure exits 1.

Snapshots written to `%TEMP%`: `upic_snapshot.png` (1280x800),
`_640x400`, `_360x640`, `_noviews`, `_audio`, `_sample`,
`_live_ratio`, `_arclist` (the arc list panel) and `_arclist_window` (the
window with the panel open).

The plugin smoke test is 15 checks: plugin-mode construct, in-place
`processBlock`, monitor passthrough from the host input bus, Play
render, state round-trip, editor lifecycle and the latency round-trip.

## Troubleshooting

The known failures - with their symptoms, causes, fixes and the scripts
that prevent them - are in **`known-issues.yaml`** at the repo root.

- **No sound (standalone)**: press **Audio** and choose the right output
  device + sample rate (44100).
- **"The operation was canceled by the user" on install**: the UAC
  prompt was dismissed. Re-run `.\scripts\install_plugin.ps1` and accept
  it, or install to `%LOCALAPPDATA%\Programs\Common\VST3` (no UAC), or
  copy the bundle from an admin shell.
- **DAW still shows the old plugin version**: restart the DAW or force a
  rescan; check the installed file's Properties -> Details and
  `moduleinfo.json`. `.\scripts\install_plugin.ps1` prints the SHA-256 and
  the versions it found - they must match the build.
- **pluginval missing**: run `.\scripts\fetch_pluginval.ps1`; the build
  still succeeds without it but reports "pluginval missing".
- **Stale VST3 version in Explorer**: the build script deletes stale
  `*_resources.rc` files automatically; do it by hand if you build with
  raw CMake.
- **JUCE fails with a syntax error in `fobject.h`**: this repo hit a
  corrupted copy of
  `JUCE/modules/juce_audio_processors/format_types/VST3_SDK/base/source/fobject.h`
  on 2026-10-16 (an external sync overwrote it). Run
  `.\scripts\check_juce.ps1` to see whether the JUCE tree is damaged, and
  restore a clean JUCE 8.0.6 copy if it is.
- **Antivirus flags the exe**: allowlist the build folder.
- **Python prototype silent**: it plays a temp WAV with `winsound`; check
  `python-upic/test_upic.wav` exists after `--test-render`.

## Repo housekeeping

- Committed sources: `Source/`, `Plugin/`, `scripts/`, `UPICClone.jucer`,
  `Builds/Android/`, docs.
- Generated and gitignored: `JUCE/`, `JuceLibraryCode/`,
  `Builds/VisualStudio2022`, `Builds/VisualStudio2026`,
  `Builds/Plugin`, `Builds/MacOSX`, `Builds/LinuxMakefile`, Gradle
  caches, `local.properties`, APK output, `Tools/pluginval/`.
- The Android `build.gradle` / `CMakeLists.txt` are Projucer-generated.
  The settings that used to be hand tweaks (SDK 35, minSdk 24, the gradle
  plugin and wrapper versions, the debug keystore) live in the
  `UPICClone.jucer` exporter now, so a resave reproduces them. The one
  value Projucer hardcodes - the NDK version - is put back by
  `.\scripts\fix_android.ps1`, which also verifies everything else
  (`docs/android-build.md`).
- The known failures are collected in `known-issues.yaml`; the JUCE tree
  can be checked with `.\scripts\check_juce.ps1`.
