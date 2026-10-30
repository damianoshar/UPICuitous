# Wave 16 - the housekeeping (v1.18)

**Status:** shipped in v1.18.
**Kanso:** cards 47, 265, 266 (Done).
**Goal:** no behaviour change - the repo should stop losing things.

## Why

Three things kept coming back:

- a Projucer resave undid the Android hand edits (SDK 35, NDK 28.1,
  minSdk 24, the debug keystore) - every resave was a small risk of
  breaking the APK build;
- the version was written in three places by hand and had already drifted
  (the app said 1.15.0, the plugin 1.17.0, the Android manifest 1.2.0);
- the failures that keep coming back were written down in a Kanso card
  and in a chat, not in the repo.

## What was decided

- **The Android settings live in the `.jucer`.** `androidMinimumSDK`,
  `androidTargetSDK`, `androidPluginVersion`, `gradleVersion` and the
  `androidKeyStore*` values are exporter settings now, so a resave
  reproduces the same files. The one value Projucer hardcodes - the NDK
  version - is put back by `scripts/fix_android.ps1`, which also
  verifies every other value against the `.jucer` (`-Verify` checks
  without changing anything). `docs/android-build.md` has the ownership
  table.
- **The version is written in one place.** `build.ps1 -Version` writes it
  into the plugin, the app *and* the `.jucer`, and it warns when it
  cannot. The bump also moved **before** the app build: it used to sit
  inside the plugin block, so `-SkipPlugin` skipped it and the app was
  built with the old version.
- **`known-issues.yaml`** collects the recurring failures in the format
  APC's troubleshooting knowledge base asks for: id / title / category /
  severity / symptoms / error_patterns / root_cause / resolution /
  prevention. Nine entries, and the accepted ones are accepted on
  purpose.
- **`scripts/check_juce.ps1`** is the prevention for the damaged JUCE
  header: the JUCE tree is gitignored, so git cannot repair it - the
  script compares the VST3 SDK headers against their SHA-256 and names
  the damaged file.
- **`Source/ModelSnapshot.h`** (v1.18) was missing from the `.jucer`
  file list. It compiled (the `#include` resolves), but a resave would
  have dropped it from the generated project listing.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The `.jucer` edit would corrupt the user's project file | The build script parses the result as XML before writing it, and writes nothing if it does not parse |
| The script would rewrite the `.jucer` in a way Projucer rejects | The edit is one attribute on the `JUCERPROJECT` element - the same shape Projucer itself writes |
| The NDK line would drift silently | `fix_android.ps1 -Verify` fails loudly, and the build docs point at it |
| The root `CMakeLists.txt` still says 1.2.0 | Left alone on purpose: that file is the user's (the app/APK build) and is deliberately not touched. It only affects the version resource of the app built through that path (KI-008) |

## What was verified

- The version chain: the `.jucer`, `Source/Main.cpp` and
  `Plugin/CMakeLists.txt` all read the same version, and the bump
  reaches all three.
- `fix_android.ps1` was tested by breaking the NDK line and the manifest
  version on purpose and fixing them again.
- `check_juce.ps1` was tested by damaging a header on purpose: it named
  the file and the hashes.
- The full gate: app selftest 211/211, plugin smoke 15/15, pluginval
  strictness 5 SUCCESS.

## What came out of it

- The next wave is not a feature: the remaining backlog is Scala import
  and the per-slot Ref (both real features), and the future ideas stay
  flagged until they are pulled forward on purpose.
- The housekeeping is now part of the build: the version chain, the
  Android check and the JUCE check are all scripts that fail loudly.
