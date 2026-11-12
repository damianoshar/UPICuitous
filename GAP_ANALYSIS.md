# UPICuitous - Gap Analysis & Improvement Plan

## Project Overview
UPICuitous is a JUCE-based UPIC clone (v0.28.0) that implements Iannis Xenakis' UPIC concept: drawing pitch curves on a time-vs-pitch page, assigning wavetables and envelopes to arcs, and rendering them as oscillator, sample-based, live audio, and phase-modulation voices. It ships as:
- Windows standalone app
- VST3 plugin
- Android tablet app

---

## Documentation Gaps

### 1. Missing Documentation Files Referenced in README
The README references these files that **do not exist in the release version** but **DO EXIST in the older version** at `C:\Users\harha\Downloads\upic-clone-juce`:
- `FUTURE_IDEAS.md` - Roadmap: MIDI scrub/reverse, FM/PM, FM8 bridge, DAW sync (exists in older version)
- `TOOLKITS.md` - Agent toolkits: what is used and why (exists in older version)
- `known-issues.yaml` - Recurring failures: symptom, cause, fix, prevention (exists in older version)
- `PROGRESS.md` - Wave log, features, decisions, test inventory (exists in older version)
- `BUILDING.md` - Manual CMake commands, Android project, troubleshooting (exists in older version)
- `docs/waves/README.md` - Design notes per wave (directory doesn't exist in either version)

### 2. Incomplete docs/ Structure
- No `docs/waves/` directory exists (referenced in README layout)
- No architecture/design documentation
- No API documentation for the core classes
- No contribution guide
- No changelog/history file

### 3. Android Documentation Gaps
- `docs/android-build.md` exists but lacks troubleshooting section for common build issues
- No guide for setting up the release keystore
- No CI/CD documentation for the release workflow

### 4. Conflicting Directory
**There are TWO versions of the project in adjacent directories:**
| Directory | Version | Brand | Key Files |
|-----------|---------|-------|-----------|
| `upic-clone-juce-release-v1.27.0` | v0.28.0 | UPICuitous | Current working dir, newer features (353 tests, 4 sample pitch modes) |
| `upic-clone-juce` | v1.27.0 | UPIC Clone | Has all missing docs, older features (323 tests, 3 sample pitch modes) |

This causes confusion - the README in the release version references files that only exist in the older version.

---

## Codebase Gaps & Technical Debt

### 1. Security Issues (from SECURITY-REVIEW.md - HIGH PRIORITY)

| ID | Severity | Issue |
|----|----------|-------|
| S-01 | High | Android signing key exposed to entire build job |
| S-02 | High | Release APK can silently be debug-signed |
| S-03 | High | Untrusted project/plugin state not hardened |
| S-04 | High | JUCE 8.0.6 missing security patches (8.0.16-security available) |
| S-05 | Medium | Build inputs downloaded without integrity checks |
| S-06 | Medium | Android permissions broader than needed |
| S-07 | Low | Python prototype uses `tempfile.mktemp` |
| S-08 | Low | Elevated install script interpolates local paths |
| S-09 | Low | Windows artifacts unsigned, no checksums |
| S-10 | Low | `.gitignore` missing secret/build patterns |

### 2. JUCE Version
- **Current**: 8.0.6 (pinned in CMakeLists.txt and .jucer)
- **Security patch**: 8.0.16-security (released 2026-10-13)
- **Latest stable**: 9.0.4
- **Impact**: Missing fixes for malformed String/AudioFormat input

### 3. Build System Issues
- `FetchContent` for JUCE has no `URL_HASH` (supply chain risk)
- `Tools/fetch_juce.py` downloads without SHA-256 verification
- Gradle wrapper lacks `distributionSha256Sum`
- GitHub Actions pinned to tags (`@v4`, `@v2`) not commit SHAs
- pluginval downloaded/executed without hash verification
- CMakeLists.txt version (1.2.0) differs from app/plugin version (0.28.0)

### 4. Android Build Issues
- Debug keystore fallback in build.gradle allows accidental debug-signed releases
- `androidVersionCode` fixed at 1 (must increment for Play Store)
- NDK version hardcoded in Projucer (26.2) vs actual (28.1.13356709) - requires `fix_android.ps1`
- Generated `JuceLibraryCode/` paths must be redirected by script
- Unused permissions: INTERNET, CHANGE_WIFI_MULTICAST_STATE, READ_MEDIA_VIDEO, READ_MEDIA_IMAGES, POST_NOTIFICATIONS
- `JUCE_PUSH_NOTIFICATIONS=1` enabled but unused
- `allowBackup` not explicitly set (defaults to true)

### 5. Code Hardening Gaps (from SECURITY-REVIEW.md S-03)

**Missing validation/bounds checking:**
- No maximum project/state size limit
- `SampleSlot::fromVar()` decodes base64 before checking frame count
- No cap on declared frames (60-second limit only in `loadSampleIntoSlot()`)
- Unbounded arrays: samples, waveforms, envelopes, pages, arcs, points, sieve remainders
- Linear scans in `Arc::freqAt()` and `Envelope::valueAt()` (O(n) per sample)
- No `std::isfinite` checks on numeric fields
- `juce::jlimit()` returns NaN unchanged
- `readMaterial()` while loop can spin on zero/negative duration
- `masterGain` loaded without clamping to 0..1.5
- PM values not clamped (uses `std::stod` which accepts nan/inf)
- `PluginProcessor::setStateInformation()` has no null/size checks, no exception handling

**Performance issues:**
- No binary search for sorted arc/envelope points
- No try/catch around parsing/allocation (bad_alloc can cross plugin boundary)

### 6. Missing Features (from README FUTURE_IDEAS.md reference)
- MIDI scrub/reverse
- Full FM engine
- FM8 bridge
- DAW sync (tempo/map)
- MPE support
- AU plugin format
- macOS/Linux builds
- WebAudio prototype completion
- Python prototype completion

### 7. Testing Gaps
- No CI for Android (only in release.yml, not PR validation)
- No macOS/Linux CI
- pluginval optional (only runs if pre-downloaded)
- No fuzz testing for JSON/audio parsing
- No performance benchmarks

### 8. Code Organization Issues
- Single massive `MainComponent.cpp` (201K lines) - violates SRP
- Many UI components inline in MainComponent.h
- No separation of concerns (model/view/controller)
- Header-only implementation for most classes

---

## Missing Prototype Implementations

### python-upic/
- Only contains `test_upic.wav` (441KB) - no actual Python code
- README claims: `python python-upic/upicuitous.py`

### web-upic/
- Directory doesn't exist
- README claims: Open `web-upic/index.html`

---

## Platform Support Gaps

| Platform | Status |
|----------|--------|
| Windows (standalone) | ✅ Supported |
| Windows (VST3) | ✅ Supported |
| Android | ✅ Supported (debug keystore only) |
| macOS | ❌ Not built (Xcode config exists but untested) |
| Linux | ❌ Not built (LinuxMakefile config exists but untested) |
| iOS | ❌ Not supported |
| AU plugin | ❌ Not supported |
| AAX plugin | ❌ Not supported |
| CLAP plugin | ❌ Not supported |

---

## CI/CD Gaps

### Current Workflows
1. `windows.yml` - Reusable, builds Windows app + VST3, runs selftest + smoke test
2. `release.yml` - Triggered on tags, builds Android + Windows, publishes release

### Missing
- PR validation workflow (runs on every PR)
- Android build validation in PRs
- macOS/Linux build validation
- Automated dependency updates (Dependabot/Renovate)
- Release signing verification (apksigner verify)
- Code signing for Windows binaries

---

## Architecture & Design Gaps

### 1. Lock-Free Model Handover (ModelSnapshot.h)
- Well-designed for audio thread safety
- But: snapshots copy entire model (waveforms, envelopes, pages, slots)
- Could be optimized with copy-on-write or shared pointers for immutable data

### 2. Audio Engine (UpicEngine.h)
- Supports 4 pitch modes for samples, 3 for live
- Phase vocoder implementation present
- Granular with stretch/no-stretch variants
- Good separation of concerns with LivePitch.h

### 3. Score Editor (ScoreEditor.h)
- 1611 lines - large but well-organized
- Supports touch, mouse, keyboard, wheel
- Magnetic sieves, zones, procedural generators
- Multi-select with keyboard shortcuts

### 4. UI Architecture
- Everything in MainComponent (god class)
- Overlays for settings (audio, MIDI/OSC, clear warning)
- Tab strip for materials (Osc/Sample/Live/PM)
- ArcListPanel for arc inventory

---

## Priority Matrix

### 🔴 CRITICAL (Do First)
1. **Fix Android debug keystore fallback** (S-02) - can ship debug-signed releases
2. **Upgrade JUCE to 8.0.16-security** (S-04) - security patches
3. **Add project/state size limits** (S-03) - DoS prevention
4. **Add NaN/finite checks** (S-03) - audio thread safety
5. **Harden PluginProcessor::setStateInformation()** (S-03) - plugin crash prevention

### 🟠 HIGH (Do Soon)
6. **Split Android workflow** (S-01) - isolate signing key
7. **Pin GitHub Actions to commit SHAs** (S-01, S-05)
8. **Add SHA-256 for JUCE downloads** (S-05)
9. **Add Gradle wrapper validation** (S-05)
10. **Remove unused Android permissions** (S-06)
11. **Set `allowBackup=false`** (S-06)
12. **Create missing documentation files** (FUTURE_IDEAS.md, TOOLKITS.md, etc.)

### 🟡 MEDIUM (Plan For)
13. **Evaluate JUCE 9.0.4 migration**
14. **Add binary search for arc/envelope points** (performance)
15. **Add try/catch around parsing** (S-03)
16. **Fix python-upic and web-upic prototypes**
17. **Add PR validation workflow**
18. **Add macOS/Linux CI**
19. **Fix CMake version mismatch** (1.2.0 vs 0.28.0)
20. **Increment androidVersionCode**

### 🟢 LOW (Nice to Have)
21. **Code sign Windows binaries** (S-09)
22. **Publish SHA-256 checksums** (S-09)
23. **Fix `tempfile.mktemp` in Python** (S-07)
24. **Fix install_plugin.ps1 path interpolation** (S-08)
25. **Extend .gitignore** (S-10)
26. **Refactor MainComponent into smaller classes**
27. **Add AU/CLAP plugin formats**
28. **Add MPE support**
29. **Implement DAW sync**

---

## Suggested Implementation Order

### Phase 1: Security Hardening (Week 1-2)
1. Fix Android debug keystore fallback in build.gradle
2. Upgrade JUCE to 8.0.16-security, update hashes
3. Add project/state size limits and validation
4. Add finite checks and clamping
5. Harden PluginProcessor::setStateInformation()

### Phase 2: CI/CD & Supply Chain (Week 2-3)
6. Split release.yml into build/sign/publish jobs
7. Pin all GitHub Actions to commit SHAs
8. Add URL_HASH to FetchContent, verify JUCE download
9. Add Gradle wrapper validation
10. Add apksigner verify step

### Phase 3: Android & Platform (Week 3-4)
11. Remove unused Android permissions
12. Set allowBackup=false
13. Fix androidVersionCode increment
14. Create PR validation workflow
15. Test macOS/Linux builds

### Phase 4: Documentation & Prototypes (Week 4-5)
16. Create all missing documentation files
17. Implement python-upic prototype
18. Implement web-upic prototype
19. Add architecture documentation

### Phase 5: Performance & Refactoring (Week 5-6)
20. Add binary search for arc/envelope points
21. Add try/catch around parsing
22. Refactor MainComponent
23. Add benchmarking

### Phase 6: Features (Ongoing)
24. Implement missing features from FUTURE_IDEAS.md
25. Add AU/CLAP support
26. Add MPE support

---

## Effort Estimates

| Task | Effort | Risk |
|------|--------|------|
| Android debug keystore fix | 1 day | Low |
| JUCE upgrade to 8.0.16-security | 2-3 days | Medium |
| Project size limits & validation | 2-3 days | Low |
| NaN/finite checks | 2 days | Low |
| PluginProcessor hardening | 2 days | Medium |
| Workflow split | 3 days | Medium |
| Action pinning | 1 day | Low |
| Supply chain verification | 2 days | Low |
| Android permissions cleanup | 1 day | Low |
| PR validation workflow | 2 days | Low |
| macOS/Linux CI | 3-5 days | Medium |
| Missing docs creation | 3-5 days | Low |
| Python prototype | 5-7 days | Medium |
| Web prototype | 5-7 days | Medium |
| Binary search optimization | 2 days | Low |
| MainComponent refactor | 10-15 days | High |

---

## Dependencies Between Tasks

```
JUCE Upgrade (8.0.16) ──┐
                        ├──→ All builds must pass after upgrade
Android keystore fix ───┤
                        │
Project size limits ────┼──→ Can be done in parallel
NaN checks ─────────────┤
                        │
PluginProcessor hardening ──────────→ Depends on JUCE upgrade
                        │
Workflow split ─────────┼──→ Can be done in parallel
Action pinning ─────────┤
Supply chain verify ────┤
                        │
Android permissions ────┼──→ Can be done in parallel
allowBackup ────────────┤
                        │
PR validation workflow ─┼──→ Needs working builds
macOS/Linux CI ─────────┘
                        │
Missing docs ───────────┼──→ Independent
Python prototype ───────┤
Web prototype ──────────┤
                        │
Binary search ──────────┼──→ After validation in place
try/catch parsing ──────┤
MainComponent refactor ─┘    → After all hardening complete
```

---

## Verification Checklist

After each phase, verify:
- [ ] `.\scripts\build.ps1` passes (app + selftest + plugin + smoke + pluginval)
- [ ] Android APK builds and installs
- [ ] `UPICuitous.exe --selftest` shows 353/353 PASS
- [ ] pluginval strictness 5 passes
- [ ] No debug keystore in release APK
- [ ] Release workflow publishes with checksums
- [ ] PR workflow validates all platforms