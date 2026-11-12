# UPICuitous Security Review

- Date: 2026-10-31
- Scope: UPICuitous 0.28.0, current `main` at `34e91f8`
- Also reviewed: Android manifest/Gradle/keystore setup, GitHub Actions workflows, build/fetch scripts, Python and web prototypes, and the `full-history` branch for accidental secrets
- Review type: static code and configuration review
- Not performed: dynamic exploit, signed APK install, or release workflow execution

## Executive summary

No committed keystore, private key, or real password was found in the current tree or the `full-history` branch. The only password-like strings are the workflow secret references and the `keystore.properties.template` placeholders. I also did not find an obvious remote-code-execution path in the application's own code.

The realistic risks are:

1. Release-signing and supply-chain compromise.
2. Untrusted project/plugin-state parsing causing crashes, freezes, undefined behavior, or non-finite audio.
3. The pinned JUCE version missing security patches.
4. Android permissions and backup settings being broader than needed.
5. Release-process and GitHub-account protections.

The app itself has no network or browser code. `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, and `JUCE_REPORT_APP_USAGE=0`; the `INTERNET` permission is unnecessary.

## Findings

| ID | Severity | Finding |
| --- | --- | --- |
| S-01 | High | Android signing key is exposed to the entire build job |
| S-02 | High | Release APK can silently be debug-signed |
| S-03 | High | Untrusted project/plugin state is not hardened |
| S-04 | High | JUCE 8.0.6 is missing security patches |
| S-05 | Medium | Build inputs are downloaded or executed without integrity checks |
| S-06 | Medium | Android permissions and backup are broader than needed |
| S-07 | Low | Python prototype uses `tempfile.mktemp` |
| S-08 | Low | Elevated install script interpolates local paths |
| S-09 | Low | Windows artifacts are unsigned and releases have no checksums |
| S-10 | Low | `.gitignore` does not cover several secret/build-output patterns |

## S-01 - High - Android signing key is exposed to the entire build job

### Evidence

`.github/workflows/release.yml`:

- Lines 9-10 give the entire workflow `contents: write`.
- Line 21 uses `actions/checkout@v4` with the default `persist-credentials: true`.
- Lines 56-64 write `Builds/Android/app/app-release.keystore` and `keystore.properties` into the workspace.
- Line 70 runs Gradle with the fetched JUCE tree, Gradle wrapper, and all build dependencies.
- Line 86 publishes with `softprops/action-gh-release@v2`.

Any compromised Gradle plugin, wrapper JAR, JUCE archive, or build dependency can read the signing key and the persisted git token. The token can push to the repository because the job has `contents: write`.

The workflow-level `contents: write` is broader than necessary. The checkout credentials are available in `.git/config` by default. Actions are pinned to mutable tags such as `@v4` and `@v2`, not full commit SHAs.

The release secrets are repository-level and the workflow triggers on any `v*` tag. A compromised write-access GitHub account can push a tag and run the workflow. There is no environment approval.

GitHub-hosted runners are ephemeral, but the key is still exposed to every line of code that runs in that job.

### Impact

An attacker who compromises any build dependency can:

- copy the release keystore and passwords,
- sign malicious APKs that Android accepts as updates,
- use the persisted token to modify the repository or release assets.

### Remediation

- Build an unsigned APK in a `contents: read` job with `persist-credentials: false`.
- Sign in a minimal, separate job using a GitHub Environment with required reviewers and restricted tags.
- Publish in a separate `contents: write` job that never sees the keystore.
- Pin every action to a full commit SHA.
- Write the keystore with `umask 077`.
- Use `env:` and `printf` instead of direct `${{ secrets.* }}` interpolation.
- Delete the keystore and `keystore.properties` in an `if: always()` cleanup step.
- Add `apksigner verify --print-certs` and compare the certificate SHA-256 to the expected release certificate.
- Enable 2FA, tag protection, secret scanning/push protection, and branch protection.
- Keep the release keystore outside cloud-synced folders, restrict its ACL, and keep an offline backup.

## S-02 - High - Release APK can silently be debug-signed

### Evidence

`Builds/Android/app/build.gradle:62-66`:

```gradle
if (keystorePropertiesFile.exists()) {
    signingConfig signingConfigs.releaseSigning
} else {
    signingConfig signingConfigs.juceSigning
}
```

`UPICuitous.jucer:127-129` points at `~/.android/debug.keystore` with the public `android` password and `androiddebugkey` alias.

CI normally creates `keystore.properties`, so the CI path is probably release-signed. However, a local or manual release build, a future workflow change, or a missing properties file produces a release APK signed with the public debug key.

The release workflow does not run `apksigner verify`, so a misconfiguration could be published silently.

### Impact

The debug key is public. Anyone can sign an APK with the same package name and a higher version code. Users who installed a debug-signed build are exposed to a malicious "update."

### Remediation

- Fail closed for release builds: if `keystore.properties` is missing, stop; never fall back to the debug key.
- Remove the fallback from `build.gradle` and the debug keystore from the release path.
- Verify the APK certificate in CI before upload/publish.
- Never distribute debug builds.
- Document that changing the signing key requires users to uninstall and reinstall.

## S-03 - High - Untrusted project/plugin state is not hardened

### Evidence

Relevant paths:

- `Source/MainComponent.cpp:3710` - `loadFileAsString()` reads the entire project file.
- `Source/MainComponent.cpp:3555-3690` - `loadFromVar()`.
- `Source/UpicModel.h:828-851` - `SampleSlot::fromVar()`.
- `Source/UpicModel.h` - `Arc::fromVar()`, `Envelope::fromVar()`, `SieveZone::fromVar()`, `Page::fromVar()`.
- `Source/MainComponent.cpp:3740-3745` - `loadSampleIntoSlot()`.
- `Source/UpicEngine.h:397-407` - `readMaterial()`.
- `Source/LivePitch.h` - grain and vocoder index calculations.
- `Plugin/PluginProcessor.cpp:87-93` - `setStateInformation()`.

Concrete issues:

- No maximum project/state size. A huge `.upic.json` or VST3 state can exhaust memory.
- `SampleSlot::fromVar()` decodes the base64 string before checking the declared frame count. A huge base64 payload can be decoded before rejection.
- The declared `frames` value is not capped at 60 seconds. The app can allocate very large sample buffers.
- The existing 60-second cap in `loadSampleIntoSlot()` trusts the file's sample rate and `lengthInSamples`. A crafted audio header can bypass the intended cap and cause a very large allocation.
- Arrays of samples, waveforms, envelopes, pages, arcs, points, sieve remainders, and global sieve remainders are unbounded. A small JSON file can create enough objects or points to freeze the UI or make the audio thread do enormous work.
- `Arc::freqAt()` and `Envelope::valueAt()` are linear scans called per sample. Many points become a CPU denial of service.
- Numeric fields are not consistently checked with `std::isfinite`. `juce::jlimit()` returns `NaN` unchanged, so `NaN` can reach `(int)` buffer indices in `Waveform::valueAt()`, `readMaterial()`, `GrainCore`, and `VocoderCore`. That is undefined behavior and can become an out-of-bounds index on some targets.
- `readMaterial()` uses `while (pos >= matDur) pos -= matDur;`. A zero or negative duration, for example from a huge or infinite sample rate, can spin.
- `masterGain` is loaded without clamping to the slider's 0..1.5 range. `intensity` and PM values are also not all clamped. `tanh()` bounds the signal before the master multiply, so this is not "1e9 times louder," but it can force full-scale/clipped or `NaN` output.
- `pmImportBtn` uses `std::stod`, which accepts `nan` and `inf`; the engine's `jlimit` does not sanitize `NaN`.
- `PluginProcessor::setStateInformation()` has no null check, no size limit, no exception handling, and directly calls `loadFromVar()`, which touches UI components. A malicious or buggy host can pass null/oversized state, trigger `bad_alloc`, or call it off the message thread.

This is primarily a crash/DoS/audio-safety and memory-safety issue, not a proven remote code execution issue.

### Remediation

- Enforce a maximum project/state size before reading or parsing.
- Parse and validate outside `dataLock`; swap the validated model under the lock.
- Cap declared frames at 60 seconds multiplied by a clamped sample rate, plus a total decoded-byte budget.
- Check base64 length against the expected decoded size before decoding.
- Cap samples, waveforms, envelopes, pages, arcs, points, and sieve remainders.
- Reject non-finite values. Clamp `masterGain` to 0..1.5, intensity to 0..1, and PM fields to their UI ranges.
- Clamp `reader->sampleRate` in `loadSampleIntoSlot()` and cap `lengthInSamples` before allocating.
- Use binary search for sorted arc and envelope points.
- Wrap parsing and allocation in `try/catch` so `std::bad_alloc` cannot cross the plugin boundary.
- In `setStateInformation()`, reject null, negative, and oversized data, and marshal UI updates to the message thread.
- Add tests for truncated, oversized, `NaN`/`Infinity`, duplicate-ID, and extreme-count files.

## S-04 - High - JUCE 8.0.6 is missing security patches

### Evidence

`CMakeLists.txt` and `Tools/fetch_juce.py` pin JUCE `8.0.6`.

JUCE released `8.0.16-security` on 2026-10-13, fixing malformed String input. The 9.0.x changelog also lists "Added protection against malformed AudioFormat input" in 9.0.1; 9.0.4 is the latest stable.

The app parses untrusted JSON strings and WAV/FLAC/Ogg/AIFF files. These are exactly the areas where the security fixes matter.

### Remediation

- Upgrade to at least `8.0.16-security`.
- Evaluate `9.0.4` if the API migration is manageable.
- Update the hashes in `scripts/check_juce.ps1`.
- Test standalone, VST3, and Android after the upgrade.
- Re-run the Android CMake normalization with `scripts/fix_android.ps1` after any Projucer resave.

## S-05 - Medium - Build inputs are downloaded or executed without integrity checks

### Evidence

- `CMakeLists.txt` uses `FetchContent` with no `URL_HASH`.
- `Tools/fetch_juce.py` downloads JUCE without a SHA-256 check.
- `scripts/check_juce.ps1` only verifies a few VST3 SDK headers. A modified `juce_JSON.cpp` or other framework file would pass.
- `Builds/Android/gradle/wrapper/gradle-wrapper.properties` has no `distributionSha256Sum`.
- The Android workflow does not run Gradle wrapper validation.
- `scripts/fetch_pluginval.ps1` downloads the latest pluginval release and executes it without a hash.
- GitHub Actions are pinned to tags, not commit SHAs.

### Remediation

- Pin JUCE to a commit or immutable tag plus a SHA-256 hash.
- Add `URL_HASH` to `FetchContent`.
- Verify the entire JUCE archive, not only selected headers.
- Add `distributionSha256Sum` for the Gradle distribution.
- Add `gradle/actions/wrapper-validation` before the Android build.
- Pin pluginval to a version and SHA-256.
- Pin all GitHub Actions to full commit SHAs.
- Use Dependabot or a similar updater to keep pinned versions current.

## S-06 - Medium - Android permissions and backup are broader than needed

### Evidence

`Builds/Android/app/src/main/AndroidManifest.xml:6-14` requests:

- `INTERNET`
- `CHANGE_WIFI_MULTICAST_STATE`
- `READ_MEDIA_VIDEO`
- `READ_MEDIA_IMAGES`
- `POST_NOTIFICATIONS`
- legacy external-storage permissions
- `READ_MEDIA_AUDIO`
- `RECORD_AUDIO`

The app has no network, image, video, or push-notification code. `JUCE_PUSH_NOTIFICATIONS=1` is still enabled in the generated Android CMake.

`allowBackup` is not set, so it defaults to true.

### Remediation

- Remove `INTERNET`, `CHANGE_WIFI_MULTICAST_STATE`, `READ_MEDIA_VIDEO`, `READ_MEDIA_IMAGES`, and `POST_NOTIFICATIONS` unless actually used.
- Remove `JUCE_PUSH_NOTIFICATIONS` unless actually used.
- Keep `RECORD_AUDIO`.
- Review whether `READ_MEDIA_AUDIO` and the legacy storage permissions are needed if the file chooser uses the Storage Access Framework.
- Set `android:allowBackup="false"` or add explicit backup rules.
- Update both the `.jucer` and the generated manifest/CMake.
- Extend `scripts/fix_android.ps1` so a Projucer resave does not reintroduce the removed permissions.

## S-07 - Low - Python prototype uses `tempfile.mktemp`

### Evidence

`python-upic/upicuitous.py:478` uses `tempfile.mktemp`, which has a predictable-name race. A local attacker could pre-create or replace the file.

### Remediation

Use `NamedTemporaryFile(delete=False)` or `TemporaryDirectory` and keep the file handle open while writing.

## S-08 - Low - Elevated install script interpolates local paths

### Evidence

`scripts/install_plugin.ps1` interpolates `$src` and `$dst` into an elevated PowerShell here-string. A repository path containing a single quote can break out of the string and execute additional elevated commands.

### Remediation

Pass paths as arguments or use an encoded command. Avoid string interpolation inside the elevated script.

## S-09 - Low - Windows artifacts are unsigned and releases have no checksums

### Evidence

The Android APK is signed, but the Windows `.exe` and VST3 bundle are not Authenticode-signed. The GitHub release does not include a `SHA256SUMS` file.

### Remediation

- Code-sign the Windows binaries if distributing them.
- Publish SHA-256 checksums for all release assets.
- Publish the APK signing certificate fingerprint and verification command.

## S-10 - Low - `.gitignore` does not cover several secret/build-output patterns

### Evidence

The current `.gitignore` covers the known keystore and build paths, but not all common secret or build-output patterns.

### Remediation

Add at least:

- `*.jks`
- `*.p12`
- `*.pfx`
- `*.apk`
- `dist/`
- `juce-*.zip`
- `_juce_tmp/`

## What is already good

- No keystore, private key, or real password is tracked in the current tree or the `full-history` branch. Only `keystore.properties.template` and workflow secret references exist.
- The release keystore is outside the repository, and the workflow does not upload it.
- The app has no network or browser code. `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, and `JUCE_REPORT_APP_USAGE=0`; the `INTERNET` permission is unnecessary.
- The launcher activity is exported, which is required. The receiver is not exported, and there are no custom intent filters or URL schemes.
- The web demo and Python prototype have no network calls, `eval`, or obvious XSS sinks.
- Using a dedicated Android keystore and GitHub secrets is the right direction. The remaining problem is isolating and protecting that key.

## Remediation checklist

- [ ] Split the Android workflow into build, sign, and release jobs.
- [ ] Give the build job `contents: read` and `persist-credentials: false`.
- [ ] Put signing secrets in a protected GitHub Environment with required reviewers.
- [ ] Pin all GitHub Actions to full commit SHAs.
- [ ] Remove the debug signing fallback and fail closed when the release keystore is missing.
- [ ] Add `apksigner verify --print-certs` to CI.
- [ ] Upgrade JUCE to at least `8.0.16-security`; evaluate `9.0.4`.
- [ ] Add SHA-256 verification for JUCE downloads and `FetchContent`.
- [ ] Add `distributionSha256Sum` and Gradle wrapper validation.
- [ ] Pin pluginval to a version and verify its SHA-256.
- [ ] Add project/state size limits and sample/count limits.
- [ ] Reject non-finite numbers and clamp audio parameters.
- [ ] Add `try/catch` around parsing and allocation.
- [ ] Harden `PluginProcessor::setStateInformation()`.
- [ ] Remove unused Android permissions and push-notification support.
- [ ] Set `android:allowBackup="false"` or add backup rules.
- [ ] Replace `tempfile.mktemp`.
- [ ] Fix the elevated install script's path interpolation.
- [ ] Add Windows code signing and/or release checksums.
- [ ] Extend `.gitignore`.
- [ ] Enable 2FA, tag protection, secret scanning, and branch protection.

## Not security issues, but noticed

- `.github/workflows/windows.yml` uploads the Windows `.exe` and VST3 as workflow artifacts, not as GitHub release assets. If the release is intended to contain Windows binaries, add a release job.
- The root `CMakeLists.txt` declares `project(UPICuitous VERSION 1.2.0 ...)`, while the app and plugin are at 0.28.0. This is release metadata, not a security issue.
- `androidVersionCode` is fixed at `1`; bump it for future releases.
- If 0.28.0 is the first release signed with the dedicated release key, users of any earlier debug-signed build must uninstall and reinstall.

## Sources

- JUCE Security: https://juce.com/security/
- JUCE 8.0.16-security: https://github.com/juce-framework/JUCE/releases/tag/8.0.16-security
- JUCE 9.0.4: https://github.com/juce-framework/JUCE/releases/tag/9.0.4
- GitHub Actions security hardening: https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions
- OWASP GitHub Actions Security Cheat Sheet: https://cheatsheetseries.owasp.org/cheatsheets/GitHub_Actions_Security_Cheat_Sheet.html
- Android app signing: https://developer.android.com/studio/publish/app-signing

## Verification

This report is based on a static code and configuration review of `34e91f8` and the Android/CI files. No dynamic exploit, signed APK install, or release workflow run was performed.

VERIFIED: yes for the code/configuration findings (static review); no dynamic exploit was run.
