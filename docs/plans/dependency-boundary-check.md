# The dependency-boundary check

The architecture note for the boundary R8.1 introduced. It describes the
check as it is after that phase, not as it is meant to be at the end of the
refactoring.

## Why a source-level check

The review's R079–R082 want the dependency rules enforced by CMake targets
(`upic_domain`, `upic_application`, `upic_audio`, …). The plan adapts that:
the Windows gate builds the app through MSBuild on a Projucer-generated
project and builds the plugin through CMake in `Builds/Plugin`; the root
`CMakeLists.txt` - the one a target split would touch - serves CI and
Android only. A CMake-only restructuring would therefore not be verified by
the gate that guards this tree.

So the rule is enforced first where it can be seen: a source-level check
that reads the tree and fails when a boundary is broken. The CMake targets
can come later, and then the check is the thing that says the two agree.

## What is checked

`scripts/check-boundaries.ps1` runs five rules.

| Rule | Says | Why |
|---|---|---|
| **B1** | the JUCE-free groups include no JUCE header | the geometry is pure: a rectangle in, a rectangle out. The domain's core is pure numbers. A JUCE header would drag the GUI back into them. |
| **B2** | the JUCE-free groups include no app header outside their group | the engines must not know the shell. `MainComponent.h` is 2,900 lines of GUI; an engine that includes it can never be tested without it. |
| **B3** | `UPICuitous.jucer` lists every `Source/*.cpp` on disk | the Projucer project is the committed source of truth for the file list. A new `.cpp` it does not know is not compiled by the app project - and the gate would pass without ever building it. |
| **B4** | the plugin's two CMake targets carry the same, complete source list | the VST3 and its smoke test build the same sources. A new engine or a new `MainComponent*.cpp` that is missing from one of them is a plugin that does not have the code it needs. |
| **B5** | the generated Visual Studio project lists every `Source/*.cpp` | the same trap as B3, on the machine that runs the gate. `Builds/VisualStudio2026` is generated and not in the repository, so this rule only runs where the project has been generated. |

B1 and B2 cover three groups, each with its own permitted project headers:

| Group | Files | May include |
|---|---|---|
| the layout engines | `LayoutRect.h`, `CardLayoutEngine.{h,cpp}`, `BottomLayoutEngine.{h,cpp}`, `OverlayLayoutEngine.{h,cpp}` | each other |
| the domain's numeric core | `UpicConstants.h`, `PieceLimits.h`, `UpicMath.h`, `Ids.h` | the units, the validation and the math |
| the arc | `Arc.h` | the units, the validation and the math |

The codec (`Source/ArcCodec.h`) is deliberately **not** in a group: it is
the edge where the JUCE value types live. The plan keeps them there (R2),
and the check says so by leaving the edge out.

B4's required set is every `Source/*.cpp` that is not a selftest and whose
name says it belongs to the shell or to an engine (`MainComponent*.cpp`,
`*Engine.cpp`).

## The checker's own contract

A rule that cannot be shown to fire is not a rule the gate can rely on. The
checker therefore tests itself before it checks the tree - 18 cases, in
`scripts/check-boundaries.ps1`:

- each forbidden include fires (the JUCE umbrella header, quoted and not; a
  JUCE module header; a shell header), and the permitted ones do not (a
  header inside the group, the standard library);
- a rule file that is not in the tree is a violation, so a renamed engine
  cannot silently fall out of the boundary;
- each manifest rule fires on a broken manifest and stays quiet on a
  complete one;
- each manifest *reader* finds what it should: the `.jucer` entries under
  `Source/`, the right CMake target's source list across the lines, the
  Visual Studio project's `Source/` entries.

The self-test runs on every gate run, so a checker that stops detecting
anything fails the gate instead of passing it.

## How it fails

The check runs before the build - a boundary violation must not cost a
build. The gate's step is:

```
==> Dependency boundaries
    B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
    B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
    B1/B2 the arc (1 file): no JUCE header, no header outside the group
    B3 the Projucer project lists all 37 source files
    B4 the plugin's two targets carry the same 19 source files
    B5 the generated Visual Studio project lists all 37 source files
    dependency boundaries: 5 rules, 0 violations
```

A violation names the file, the line and the rule, and the gate stops:

```
dependency-boundary violations:
  B1/B2 the layout engines stay JUCE-free and know only each other: Source/OverlayLayoutEngine.cpp(124) includes the JUCE header 'JuceHeader.h'
FAILED: the dependency-boundary check failed
```

## What is not covered yet

- **The plugin still depends on the desktop shell.** `PluginProcessor.cpp`
  includes `MainComponent.h`. That is R7's job (the review's R075), and it
  is the highest-risk change in the list - the check does not pretend it is
  done.
- **The CMake targets do not exist yet.** R079–R082 still to come; the check
  is what they will have to agree with.
- **The test suites are not split yet.** R086–R089 (the 4,822-line model
  test, the audio tests, the WP7 tests) are still to come, and they will add
  their own boundaries - the selftests may keep using the shell's header,
  but the domain-only headless target may not.
- **The Android generated list.** `Builds/Android/app/CMakeLists.txt` lists
  the sources explicitly and is only rewritten by a Projucer resave, which
  the H5 wave owns. It is not checked here; B3 checks the `.jucer` it is
  generated from.

## Extending it

A new rule is a new entry in the rule table at the top of
`scripts/check-boundaries.ps1`, and a case in the self-test that shows it
firing. The rules that read the tree are written as a pure function over
the file's text (`Get-…FromText`) with a thin wrapper that reads the file,
so a rule can be tested without a tree.
