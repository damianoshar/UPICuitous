<#
    UPICuitous - the dependency-boundary check (refactoring wave R8.1).

    The refactoring plan adapts the review's "enforce the dependency rules
    with CMake targets" to a source-level check in the gate first
    (docs/plans/refactoring-plan.md, section 2): the Windows gate builds
    through MSBuild on a Projucer-generated project, not through the root
    CMake file, so a CMake-only restructuring would not be verified here.

    What is checked:

      B1  the layout engines include no JUCE header
      B2  the layout engines include no app header outside the engine group
      B3  the Projucer project (.jucer) lists every source file on disk
      B4  the plugin's two CMake targets carry the same, complete source list
      B5  the generated Visual Studio project lists every source file on disk

    B3 and B5 catch the silent omission: a new .cpp that the Projucer does
    not know about is not compiled by the app project, so the gate would pass
    without ever building it. B4 catches the same omission in the plugin,
    where a new engine would otherwise be missing from the VST3.

    The check is a source-level check on purpose: it runs in a second, it
    needs no build, and it works on a tree that does not compile yet.

    Usage:
        .\scripts\check-boundaries.ps1             # the checker's tests, then the tree
        .\scripts\check-boundaries.ps1 -SelfTest   # the checker's tests only
        .\scripts\check-boundaries.ps1 -Quiet      # only the summary lines
#>
[CmdletBinding()]
param(
    [switch] $SelfTest,
    [switch] $Quiet
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# ------------------------------------------------------------------ rules ---

# B1 + B2: the JUCE-free groups. The geometry stays pure, and so does the
# domain's numeric core - the units, the validation and the lookups the
# rest of the model stands on.
$juceFreeGroups = @(
    @{
        Name    = "the layout engines"
        Message = "B1/B2 the layout engines stay JUCE-free and know only each other"
        Files   = @(
            "Source/LayoutRect.h",
            "Source/CardLayoutEngine.h",
            "Source/CardLayoutEngine.cpp",
            "Source/BottomLayoutEngine.h",
            "Source/BottomLayoutEngine.cpp",
            "Source/OverlayLayoutEngine.h",
            "Source/OverlayLayoutEngine.cpp"
        )
        # The only project headers the group may include - each other.
        Allowed = @(
            "LayoutRect.h",
            "CardLayoutEngine.h",
            "BottomLayoutEngine.h",
            "OverlayLayoutEngine.h"
        )
    },
    @{
        Name    = "the domain's numeric core"
        Message = "B1/B2 the domain's numeric core stays JUCE-free and knows only the domain"
        Files   = @(
            "Source/UpicConstants.h",
            "Source/PieceLimits.h",
            "Source/UpicMath.h",
            "Source/Ids.h"
        )
        Allowed = @(
            "UpicConstants.h",
            "PieceLimits.h",
            "UpicMath.h"
        )
    }
)

# B4: the sources the plugin needs from Source/ - the shell's own translation
# units and the engines. A new engine or a new MainComponent*.cpp has to be
# added to the plugin's CMake list in the same commit. The selftests are the
# plugin's business, not the plugin's.
$pluginSourcePattern = '^(?!SelfTest)(MainComponent.*|.*Engine)\.cpp$'

# ---------------------------------------------------------------- helpers ---

function Get-Includes([string] $text) {
    # every #include in the file, with the line it sits on
    $out = @()
    $rx = [regex] '(?m)^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]'
    foreach ($m in $rx.Matches($text)) {
        $line = 1 + ([regex]::Matches($text.Substring(0, $m.Index), "`n")).Count
        $out += [pscustomobject]@{
            Quote = $m.Groups[1].Value
            Path  = $m.Groups[2].Value
            Line  = $line
        }
    }
    return $out
}

function Test-JuceInclude([string] $path) {
    # is this a JUCE header? JuceHeader.h, a juce_*.h module header, or a
    # path into the JUCE tree
    if ($path -eq "JuceHeader.h") { return $true }
    if ($path -match 'JuceLibraryCode') { return $true }
    if ($path -match '(^|[\\/])JUCE[\\/]') { return $true }
    if ($path -match '(^|[\\/])juce_[^\\/]*\.h$') { return $true }
    return $false
}

function Find-IncludeViolations([string[]] $files, [hashtable] $contents, $rule) {
    # the include rules: no JUCE header, and no project header from outside
    # the allowed group
    $violations = @()
    foreach ($rel in $files) {
        if (-not $contents.ContainsKey($rel)) {
            $violations += "$($rule.Name): $rel is in the rule's file list but is not in the tree"
            continue
        }
        foreach ($inc in @(Get-Includes $contents[$rel])) {
            $isJuce = Test-JuceInclude $inc.Path
            if ($isJuce -and $rule.ForbidJuce) {
                $violations += "$($rule.Name): $rel($($inc.Line)) includes the JUCE header '$($inc.Path)'"
                continue
            }
            if ($inc.Quote -eq '"' -and -not $isJuce) {
                $leaf = [IO.Path]::GetFileName($inc.Path)
                if ($rule.AllowedProject -and ($rule.AllowedProject -notcontains $leaf)) {
                    $violations += "$($rule.Name): $rel($($inc.Line)) includes '$($inc.Path)', which is outside the boundary"
                }
            }
        }
    }
    return $violations
}

function Get-SourceCppFiles([string] $repoRoot) {
    $out = @()
    $dir = Join-Path $repoRoot "Source"
    if (-not (Test-Path $dir)) { return $out }
    foreach ($f in (Get-ChildItem $dir -File -Filter "*.cpp" | Sort-Object Name)) {
        $out += "Source/" + $f.Name
    }
    return $out
}

function Get-JucerSourceFilesFromText([string] $text) {
    $out = @()
    try {
        [xml] $x = $text
        foreach ($node in $x.SelectNodes("//FILE")) {
            $f = $node.GetAttribute("file")
            if ($f -like "Source/*") { $out += ($f -replace '\\', '/') }
        }
    }
    catch { }
    return @($out | Sort-Object -Unique)
}

function Get-JucerSourceFiles([string] $repoRoot) {
    $jucer = Join-Path $repoRoot "UPICuitous.jucer"
    if (-not (Test-Path $jucer)) { return @() }
    return @(Get-JucerSourceFilesFromText ([IO.File]::ReadAllText($jucer, [Text.Encoding]::UTF8)))
}

function Get-CMakeTargetSourcesFromText([string] $text, [string] $target) {
    $out = @()
    # the source list runs over several lines, so the block is matched with
    # the singleline flag
    $rx = [regex] "(?s)target_sources\(\s*$target\s+PRIVATE\s*(.*?)\)"
    $m = $rx.Match($text)
    if (-not $m.Success) { return $out }
    foreach ($mm in [regex]::Matches($m.Groups[1].Value, 'Source/([A-Za-z0-9_]+\.cpp)')) {
        $out += $mm.Groups[1].Value
    }
    return @($out | Sort-Object -Unique)
}

function Get-CMakeTargetSources([string] $repoRoot, [string] $target) {
    $cm = Join-Path $repoRoot "Plugin\CMakeLists.txt"
    if (-not (Test-Path $cm)) { return @() }
    $text = [IO.File]::ReadAllText($cm, [Text.Encoding]::UTF8)
    return @(Get-CMakeTargetSourcesFromText $text $target)
}

function Get-VisualStudioSourceFilesFromText([string] $text) {
    $out = @()
    foreach ($m in [regex]::Matches($text,
             'ClCompile Include="\.\.[\\/]\.\.[\\/]Source[\\/]([A-Za-z0-9_]+\.cpp)"')) {
        $out += $m.Groups[1].Value
    }
    return @($out | Sort-Object -Unique)
}

function Get-VisualStudioSourceFiles([string] $repoRoot) {
    # The Projucer-generated Visual Studio projects are not in the repository
    # (Builds/VisualStudio2026 is in .gitignore), so this check only runs
    # where they have been generated.
    $out = @()
    $dir = Join-Path $repoRoot "Builds\VisualStudio2026"
    if (-not (Test-Path $dir)) { return $out }
    foreach ($p in (Get-ChildItem $dir -File -Filter "*.vcxproj")) {
        $out += @(Get-VisualStudioSourceFilesFromText ([IO.File]::ReadAllText($p.FullName, [Text.Encoding]::UTF8)))
    }
    return @($out | Sort-Object -Unique)
}

function Get-TreeManifest([string] $repoRoot) {
    return @{
        SourceCpp     = @(Get-SourceCppFiles $repoRoot)
        JucerFiles    = @(Get-JucerSourceFiles $repoRoot)
        PluginTargets = @{
            "UPICuitousPlugin"          = @(Get-CMakeTargetSources $repoRoot "UPICuitousPlugin")
            "UPICuitousPluginSmokeTest" = @(Get-CMakeTargetSources $repoRoot "UPICuitousPluginSmokeTest")
        }
        VisualStudio  = @(Get-VisualStudioSourceFiles $repoRoot)
    }
}

function Find-ManifestViolations($manifest) {
    $violations = @()
    $names = @($manifest.SourceCpp | ForEach-Object { [IO.Path]::GetFileName($_) })

    # B3: the Projucer project must know every source file
    $jucerNames = @($manifest.JucerFiles | ForEach-Object { [IO.Path]::GetFileName($_) })
    foreach ($n in $names) {
        if ($jucerNames -notcontains $n) {
            $violations += "B3 the Projucer project lists every source file: Source/$n is not in UPICuitous.jucer"
        }
    }

    # B4: the plugin's two targets carry the same, complete source list
    $required = @($names | Where-Object { $_ -match $pluginSourcePattern })
    $plugin = $manifest.PluginTargets["UPICuitousPlugin"]
    $smoke  = $manifest.PluginTargets["UPICuitousPluginSmokeTest"]
    foreach ($n in $required) {
        if ($plugin -notcontains $n) {
            $violations += "B4 the plugin's source list is complete: Source/$n is not in the UPICuitousPlugin target"
        }
        if ($smoke -notcontains $n) {
            $violations += "B4 the plugin's source list is complete: Source/$n is not in the UPICuitousPluginSmokeTest target"
        }
    }
    foreach ($n in @($plugin | Where-Object { $smoke -notcontains $_ })) {
        $violations += "B4 the plugin's two targets agree: Source/$n is only in the UPICuitousPlugin target"
    }
    foreach ($n in @($smoke | Where-Object { $plugin -notcontains $_ })) {
        $violations += "B4 the plugin's two targets agree: Source/$n is only in the UPICuitousPluginSmokeTest target"
    }

    # B5: the generated Visual Studio project must know every source file
    if ($manifest.VisualStudio.Count -gt 0) {
        foreach ($n in $names) {
            if ($manifest.VisualStudio -notcontains $n) {
                $violations += "B5 the generated Visual Studio project lists every source file: Source/$n is not in the project"
            }
        }
    }
    return $violations
}

# -------------------------------------------------------------- self-test ---
# The checker's own contract: every rule must fire on a tree that breaks it,
# and must stay quiet on a tree that does not. The gate runs these before it
# trusts the checker.

function Invoke-SelfTest {
    $results = @()

    $rule = @{
        Name           = "the test rule"
        ForbidJuce     = $true
        AllowedProject = @("LayoutRect.h", "CardLayoutEngine.h")
    }

    $clean = @{ "Source/TestEngine.h" = "#pragma once`n#include `"LayoutRect.h`"`n#include <vector>`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $clean $rule)
    $results += @{ Name = "a clean engine file has no violation"; Ok = ($v.Count -eq 0) }

    $juce = @{ "Source/TestEngine.h" = "#include <JuceHeader.h>`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $juce $rule)
    $results += @{ Name = "the JUCE umbrella header is a violation"; Ok = ($v.Count -eq 1) }

    $juceQuoted = @{ "Source/TestEngine.h" = "#include `"JuceHeader.h`"`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $juceQuoted $rule)
    $results += @{ Name = "the JUCE umbrella header is a violation when quoted"; Ok = ($v.Count -eq 1) }

    $module = @{ "Source/TestEngine.h" = "#include <juce_gui_basics/juce_Button.h>`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $module $rule)
    $results += @{ Name = "a JUCE module header is a violation"; Ok = ($v.Count -eq 1) }

    $shell = @{ "Source/TestEngine.h" = "#include `"MainComponent.h`"`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $shell $rule)
    $results += @{ Name = "a shell header is outside the boundary"; Ok = ($v.Count -eq 1) }

    $sibling = @{ "Source/TestEngine.h" = "#include `"CardLayoutEngine.h`"`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $sibling $rule)
    $results += @{ Name = "a header inside the boundary is allowed"; Ok = ($v.Count -eq 0) }

    $standard = @{ "Source/TestEngine.h" = "#include <cmath>`n#include <algorithm>`n" }
    $v = @(Find-IncludeViolations @("Source/TestEngine.h") $standard $rule)
    $results += @{ Name = "the standard library is allowed"; Ok = ($v.Count -eq 0) }

    $v = @(Find-IncludeViolations @("Source/TestEngine.h") @{} $rule)
    $results += @{ Name = "a rule file that is not in the tree is a violation"; Ok = ($v.Count -eq 1) }

    # the manifest rules
    $manifest = @{
        SourceCpp     = @("Source/MainComponent.cpp", "Source/CardLayoutEngine.cpp")
        JucerFiles    = @("Source/MainComponent.cpp", "Source/CardLayoutEngine.cpp")
        PluginTargets = @{
            "UPICuitousPlugin"          = @("MainComponent.cpp", "CardLayoutEngine.cpp")
            "UPICuitousPluginSmokeTest" = @("MainComponent.cpp", "CardLayoutEngine.cpp")
        }
        VisualStudio  = @("MainComponent.cpp", "CardLayoutEngine.cpp")
    }
    $v = @(Find-ManifestViolations $manifest)
    $results += @{ Name = "a complete manifest has no violation"; Ok = ($v.Count -eq 0) }

    $broken = $manifest.Clone()
    $broken.JucerFiles = @("Source/MainComponent.cpp")
    $v = @(Find-ManifestViolations $broken)
    $results += @{ Name = "a source file missing from the .jucer is a violation"
                   Ok = (($v -join " | ") -match "not in UPICuitous\.jucer") }

    $broken = $manifest.Clone()
    $broken.PluginTargets = @{
        "UPICuitousPlugin"          = @("MainComponent.cpp")
        "UPICuitousPluginSmokeTest" = @("MainComponent.cpp", "CardLayoutEngine.cpp")
    }
    $v = @(Find-ManifestViolations $broken)
    $results += @{ Name = "a source file missing from the plugin is a violation"
                   Ok = (($v -join " | ") -match "not in the UPICuitousPlugin target") }
    $results += @{ Name = "the plugin's two targets drifting apart is a violation"
                   Ok = (($v -join " | ") -match "only in the UPICuitousPluginSmokeTest target") }

    $broken = $manifest.Clone()
    $broken.VisualStudio = @("MainComponent.cpp")
    $v = @(Find-ManifestViolations $broken)
    $results += @{ Name = "a source file missing from the Visual Studio project is a violation"
                   Ok = (($v -join " | ") -match "not in the project") }

    $broken = $manifest.Clone()
    $broken.VisualStudio = @()
    $v = @(Find-ManifestViolations $broken)
    $results += @{ Name = "a tree without a generated Visual Studio project is skipped"
                   Ok = ($v.Count -eq 0) }

    # the manifest readers: the check has to read the real manifests, not
    # just the rule engine
    $jucerText = @'
<JUCERPROJECT name="UPICuitous">
  <SOURCES>
    <GROUP>
      <FILE id="a1" name="MainComponent.cpp" file="Source/MainComponent.cpp"/>
      <FILE id="a2" name="CardLayoutEngine.h" file="Source/CardLayoutEngine.h"/>
      <FILE id="a3" name="JUCE.cpp" file="JUCE/juce_core.cpp"/>
    </GROUP>
  </SOURCES>
</JUCERPROJECT>
'@
    $v = @(Get-JucerSourceFilesFromText $jucerText)
    $results += @{ Name = "the .jucer reader finds the Source entries only"
                   Ok = ($v.Count -eq 2 -and $v -contains "Source/MainComponent.cpp") }

    $cmakeText = @'
target_sources(UPICuitousPlugin PRIVATE
    PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/MainComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/CardLayoutEngine.cpp)

target_sources(UPICuitousPluginSmokeTest PRIVATE
    PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/MainComponent.cpp)
'@
    $v = @(Get-CMakeTargetSourcesFromText $cmakeText "UPICuitousPlugin")
    $results += @{ Name = "the CMake reader finds a target's source list across the lines"
                   Ok = ($v.Count -eq 2 -and $v -contains "CardLayoutEngine.cpp") }
    $v = @(Get-CMakeTargetSourcesFromText $cmakeText "UPICuitousPluginSmokeTest")
    $results += @{ Name = "the CMake reader tells the two targets apart"
                   Ok = ($v.Count -eq 1 -and $v -contains "MainComponent.cpp") }

    $vcxText = @'
  <ItemGroup>
    <ClCompile Include="..\..\Source\MainComponent.cpp" />
    <ClCompile Include="..\..\Source\CardLayoutEngine.cpp" />
  </ItemGroup>
'@
    $v = @(Get-VisualStudioSourceFilesFromText $vcxText)
    $results += @{ Name = "the Visual Studio reader finds the Source entries"
                   Ok = ($v.Count -eq 2 -and $v -contains "MainComponent.cpp") }

    $failed = 0
    foreach ($r in $results) {
        if ($r.Ok) { if (-not $Quiet) { Write-Host "PASS  $($r.Name)" } }
        else { Write-Host "FAIL  $($r.Name)" -ForegroundColor Red; $failed++ }
    }
    Write-Host ""
    if ($failed -eq 0) {
        Write-Host "boundary check self-test: $($results.Count)/$($results.Count)" -ForegroundColor Green
    }
    else {
        Write-Host "boundary check self-test: $failed of $($results.Count) failed" -ForegroundColor Red
    }
    return $failed
}

# The checker tests itself first: a rule that cannot be shown to fire is not
# a rule the gate can rely on.
$selfTestFailures = Invoke-SelfTest
if ($SelfTest) { exit $selfTestFailures }

$manifest = Get-TreeManifest $root
$violations = @()
foreach ($g in $juceFreeGroups) {
    $contents = @{}
    foreach ($rel in $g.Files) {
        $p = Join-Path $root ($rel -replace '/', '\')
        if (Test-Path $p) {
            $contents[$rel] = [IO.File]::ReadAllText($p, [Text.Encoding]::UTF8)
        }
    }
    $rule = @{
        Name           = $g.Message
        ForbidJuce     = $true
        AllowedProject = $g.Allowed
    }
    $violations += Find-IncludeViolations $g.Files $contents $rule
}
$violations += Find-ManifestViolations $manifest

if ($violations.Count -gt 0) {
    Write-Host "dependency-boundary violations:" -ForegroundColor Red
    foreach ($v in $violations) { Write-Host "  $v" -ForegroundColor Red }
    exit 1
}

foreach ($g in $juceFreeGroups) {
    Write-Host "    B1/B2 $($g.Name) ($($g.Files.Count) files): no JUCE header, no header outside the group"
}
Write-Host "    B3 the Projucer project lists all $($manifest.SourceCpp.Count) source files"
Write-Host "    B4 the plugin's two targets carry the same $($manifest.PluginTargets['UPICuitousPlugin'].Count) source files"
if ($manifest.VisualStudio.Count -gt 0) {
    Write-Host "    B5 the generated Visual Studio project lists all $($manifest.SourceCpp.Count) source files"
}
else {
    Write-Host "    B5 the generated Visual Studio project: not generated here (skipped)"
}
Write-Host "    dependency boundaries: 5 rules, 0 violations" -ForegroundColor Green
exit 0
