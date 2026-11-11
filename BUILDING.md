# Building - UPICuitous

Current version: **0.28.0** (app + VST3). The build is Windows/MSVC;
JUCE 8 does not support MinGW.

## TL;DR

| Goal | Command |
|---|---|
| Everything (app + selftest + VST3 + smoke + pluginval) | `.\scripts\build.ps1` |
| Standalone only | `.\scripts\build.ps1 -SkipPlugin` |
| VST3 only | `.\scripts\build.ps1 -SkipApp` |
| Bump the version first | `.\scripts\build.ps1 -Version 0.28.0` |
| Install the VST3 (asks for admin rights) | `.\scripts\install_plugin.ps1 -Version 0.28.0` |
| Android APK | open `Builds/Android` in Android Studio, or `.\gradlew.bat assembleDebug` |
| Prototypes | `python python-upic/upicuitous.py` / open `web-upic/index.html` |

## Requirements

- **Visual Studio 2026** (v18) with *Desktop development with C++*
  (MSBuild + MSVC 14.51). The `Builds/VisualStudio2022` exporter also
  exists but the build script uses VS2026.
- **CMake 3.22+** (4.x is fine; its deprecation warning is harmless).
- **JUCE 8.0.16-security** in `JUCE/`. `python Tools/fetch_juce.py` downloads it
  without git and verifies its SHA-256 before extracting it; the root
  CMakeLists can also fetch it (with the same hash check).
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

1. kills a running `UPICuitous.exe` (a running exe locks the link),
2. builds the app `Debug|x64` from
   `Builds/VisualStudio2026/UPICuitous.sln` with MSBuild,
3. runs `UPICuitous.exe --selftest` and fails if any check fails,
4. deletes a stale `*_resources.rc` (otherwise Explorer keeps showing an
   old VST3 version), builds the plugin `Release` with CMake,
5. builds + runs the host-less plugin smoke test,
6. runs pluginval at strictness 5 if it is installed.

Outputs:

```
Builds\VisualStudio2026\x64\Debug\App\UPICuitous.exe
Builds\Plugin\UPICuitousPlugin_artefacts\Release\VST3\UPICuitous.vst3
```

`-Version 0.28.0` rewrites `Plugin/CMakeLists.txt`, the app's
`getApplicationVersion()` and `UPICuitous.jucer` together, so the app,
plugin and Android version cannot drift apart. After a bump run
`.\scripts\fix_android.ps1` to sync the generated Android project.
`-SkipApp`, `-SkipPlugin` and `-SkipSmoke` narrow the run.

## Option 2 - build the standalone manually

With the Projucer solution (what the script does):

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
& $msbuild Builds\VisualStudio2026\UPICuitous.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Or from the root CMake project (app + Android CMake entry):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel 4
.\build\UPICuitous_artefacts\Release\UPICuitous.exe
```

The Projucer file `UPICuitous.jucer` is the UI/file-list source for the
VS2026 and Android exporters. A resave adds new files (for example
`Source/LivePitch.h`) and may reformat the exporter files.

## Option 3 - build and install the VST3

```powershell
# first time only
cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64
cmake --build Builds/Plugin --config Release --parallel 4

# smoke test (host-less: processor + editor + state)
cmake --build Builds/Plugin --config Release --target UPICuitousPluginSmokeTest
.\Builds\Plugin\UPICuitousPluginSmokeTest_artefacts\Release\UPICuitousPluginSmokeTest.exe

# optional validation
Tools\pluginval\pluginval.exe --validate `
  "Builds\Plugin\UPICuitousPlugin_artefacts\Release\VST3\UPICuitous.vst3" --strictness-level 5
```

Install (an admin shell is needed for `Program Files`):

```powershell
# the script asks for admin rights (UAC prompt) and then verifies the result:
# the installed binary's SHA-256 must match the build, and the version
# resource + moduleinfo must read the expected version
.\scripts\install_plugin.ps1 -Version 0.28.0

# or by hand, from an admin shell:
Copy-Item "Builds\Plugin\UPICuitousPlugin_artefacts\Release\VST3\UPICuitous.vst3" `
          "C:\Program Files\VstPlugIns\" -Recurse -Force
```

A per-user install avoids UAC (many DAWs scan it):
`%LOCALAPPDATA%\Programs\Common\VST3`.

Then **restart the DAW or force a plugin rescan** - hosts cache the
binary. Verify the installed copy: right-click
`...\UPICuitous.vst3\Contents\x86_64-win\UPICuitous.vst3` ->
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
- The **debug** build signs with the standard debug keystore
  (`%USERPROFILE%\.android\debug.keystore`, auto-created by Android
  Studio), so it is sideloadable out of the box.
- The **release** build signs with your own keystore and **fails closed**
  without it: it never falls back to the public debug key, because a
  debug-signed release can be impersonated by anyone. Copy
  `Builds/Android/app/keystore.properties.template` to
  `Builds/Android/app/keystore.properties` and fill it in (both files are
  gitignored).

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

## ASIO (Windows stand-alone)

The root CMake build enables ASIO by default when Steinberg's ASIO SDK
headers are present. Put `asio.h`, `asiosys.h` and `iasiodrv.h` in
`JUCE/modules/juce_audio_devices/native/asio`, or point CMake at another
copy:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DUPIC_ASIO_SDK_PATH="C:\path\to\asio"
```

The Projucer VS2022/VS2026 exporters use the same convention: they add
`..\..\JUCE\modules\juce_audio_devices\native\asio` to the include path
and force-include `Source/AsioConfig.h`. That header defines `JUCE_ASIO`
only when `<asio.h>` is actually found, so a missing SDK leaves ASIO off
instead of breaking the build. If the SDK lives elsewhere, change the
header search path in the Projucer exporter.

The SDK is not committed (its Steinberg license does not allow
redistribution). Without it, configure prints a warning and the app simply
lists the other Windows device types; `-DUPIC_ENABLE_ASIO=OFF` silences it.
ASIO is never enabled for the VST3 or Android builds.

## Tests

```powershell
.\Builds\VisualStudio2026\x64\Debug\App\UPICuitous.exe --selftest
Get-Content "$env:TEMP\upic_selftest.txt"
```

353 checks, no window and no audio device. They cover: log-frequency
interpolation, envelopes, the offline renderer, exact sample pitch
shift, all seven pitch modes (osc, sample varispeed/granular/vocoder/no-stretch,
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
  restore a clean JUCE 8.0.16-security copy if it is.
- **Antivirus flags the exe**: allowlist the build folder.
- **Python prototype silent**: it plays a temp WAV with `winsound`; check
  `python-upic/test_upic.wav` exists after `--test-render`.

## Repo housekeeping

- Committed sources: `Source/`, `Plugin/`, `scripts/`, `UPICuitous.jucer`,
  `Builds/Android/`, docs.
- Generated and gitignored: `JUCE/`, `JuceLibraryCode/`,
  `Builds/VisualStudio2022`, `Builds/VisualStudio2026`,
  `Builds/Plugin`, `Builds/MacOSX`, `Builds/LinuxMakefile`, Gradle
  caches, `local.properties`, APK output, `Tools/pluginval/`.
- The Android `build.gradle` / `CMakeLists.txt` are Projucer-generated.
  The settings that used to be hand tweaks (SDK 35, minSdk 24, the gradle
  plugin and wrapper versions, the debug keystore) live in the
  `UPICuitous.jucer` exporter now, so a resave reproduces them. The one
  value Projucer hardcodes - the NDK version - is put back by
  `.\scripts\fix_android.ps1`, which also verifies everything else
  (`docs/android-build.md`).
- The known failures are collected in `known-issues.yaml`; the JUCE tree
  can be checked with `.\scripts\check_juce.ps1`.

## The release workflow

`.github/workflows/release.yml` runs on a `v*` tag and is split into four
jobs so that no single job has both the signing key and a write token:

| Job | Sees the keystore? | Can write to the repo? |
|---|---|---|
| `build-android` | no (it signs with a throwaway key it makes itself) | no |
| `sign-android` | **yes** | no |
| `windows` | no | no |
| `publish` | no | **yes** |

The signing job runs in the protected `android-signing` environment, so
the keystore is only reachable after an environment reviewer approves it.
It verifies the signature before publishing: a debug-signed APK fails the
job, and - if the `ANDROID_RELEASE_CERT_SHA256` repository variable is set
- the certificate fingerprint has to match the expected one.

The repository needs these secrets:

| Name | What it is |
|---|---|
| `ANDROID_KEYSTORE_BASE64` | the release keystore, base64-encoded |
| `ANDROID_KEY_ALIAS` | the key alias inside it |
| `ANDROID_KEY_PASSWORD` | the key password |
| `ANDROID_STORE_PASSWORD` | the store password |

and this optional variable:

| Name | What it is |
|---|---|
| `ANDROID_RELEASE_CERT_SHA256` | the SHA-256 of the release certificate, so a wrong key is caught |

Every action is pinned to a full commit SHA, and the Gradle wrapper is
verified against its `distributionSha256Sum` before it runs. The
`pr-validation.yml` workflow runs the same builds on every pull request
and also checks that no keystore is committed and that no workflow uses an
unpinned action.
