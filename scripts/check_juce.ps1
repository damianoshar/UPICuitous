<#
    Check the JUCE tree against the known-good checksums.

    The JUCE tree is gitignored, so git cannot repair it - and an external
    sync once dropped a damaged copy of the VST3 SDK header fobject.h into
    it (see known-issues.yaml, KI-001). This script compares the files that
    matter against a pinned SHA-256 list and says which one is wrong.

    It also checks the JUCE version, so a tree from an older release is
    caught before it produces a confusing build error.

    Usage:
        .\scripts\check_juce.ps1           # check
        .\scripts\check_juce.ps1 -Fix      # check and re-fetch JUCE if it is broken
#>
[CmdletBinding()]
param(
    [switch] $Fix
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$juce = Join-Path $root "JUCE"

# The JUCE release this repo builds against (v1.30: 8.0.16-security - the
# release that carries the malformed-input fixes).
$expectedVersion = "8.0.16"

if (-not (Test-Path $juce)) {
    Write-Host "No JUCE tree at $juce - run `python Tools\fetch_juce.py`." -ForegroundColor Yellow
    exit 1
}

# The VST3 SDK headers: this is the folder that broke. The expected hashes
# are the JUCE 8.0.16-security ones (also written down in known-issues.yaml,
# KI-001). JUCE 8.0.16 moved the SDK into juce_audio_processors_headless.
#
# juce_JSON.cpp and juce_String.cpp are in the list too: they carry the
# malformed-input fixes the security review pointed at, so a tampered copy
# must not pass the check.
$expected = @{
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fobject.h"             = "FA913CBCB426A24F6301E22A9505ED9529EF4E84DD343FCD4F2F2807E35D71AB"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fstring.h"              = "1590392D1C6D18DD64933682EBAD47399CB7694AAAF61750F67956807D6E885B"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\classfactoryhelpers.h" = "76C430B058B182B6205A0B3155E18E18BFB5E7082092AF6768B581EC2BCD174F"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fbuffer.h"              = "6C6C1568A364484CD4078E3A754F203A7A50BFA51C17E889CE2B230E726603D1"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fcommandline.h"         = "3B2514A75AB1394E5C2260AEE7A2644321B0719588F3BE7B7E2060901927A91F"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fdebug.h"               = "65C2A8D1C8F725BAF16F77B010D1D35B5FAAD1815FD29851323385C23847D511"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\fstreamer.h"            = "97ABEE7FAE05B56361E64CA60543C7A49AE6E5C577895248BF10825C95B63D0C"
    "modules\juce_audio_processors_headless\format_types\VST3_SDK\base\source\updatehandler.h"        = "F0980800C43EAA92FBE93A6F3D4F139650DFADB8989C693F197384BDF20AFDB7"
    "modules\juce_core\json\juce_JSON.cpp"                                                         = "D9F958281C55D9D0D3770B8500803AF622D307A83F8CCB340602C9B9AAA1C86F"
    "modules\juce_core\text\juce_String.cpp"                                                       = "32B1AA4A963D4B40061DC0B46E7A3EE7AD9FDF06723971D76EA5F73FC6EFB3E3"
    "modules\juce_core\juce_core.h"                                                                = "81EDB3454574C7FBBC46A8AEAED497ECFC7EF819FC5A44A4DB54745A4197AB7B"
}

$broken = @()
$missing = @()

# the version check first: a tree from another release has different paths
$versionFile = Join-Path $juce "modules\juce_core\juce_core.h"
$haveVersion = "?"
if (Test-Path $versionFile) {
    $m = [regex]::Match((Get-Content $versionFile -Raw), 'version:\s*([0-9.]+)')
    if ($m.Success) { $haveVersion = $m.Groups[1].Value }
}

foreach ($rel in $expected.Keys) {
    $path = Join-Path $juce $rel
    if (-not (Test-Path $path)) {
        $missing += $rel
        continue
    }
    $hash = (Get-FileHash -Path $path -Algorithm SHA256).Hash
    $want = $expected[$rel]
    if ($hash -ne $want) {
        $broken += "$rel`n      have $hash`n      want $want"
    }
}

Write-Host ""
if ($missing.Count -eq 0 -and $broken.Count -eq 0) {
    if ($haveVersion -ne $expectedVersion) {
        Write-Host "The JUCE tree is intact but is version $haveVersion, not $expectedVersion." -ForegroundColor Yellow
        Write-Host "This repo builds against JUCE $expectedVersion - re-fetch it." -ForegroundColor Cyan
        exit 1
    }
    Write-Host "The JUCE tree is the JUCE $expectedVersion one." -ForegroundColor Green
    exit 0
}

foreach ($m in $missing) { Write-Host "  MISSING: $m" -ForegroundColor Yellow }
foreach ($b in $broken)  { Write-Host "  DAMAGED: $b" -ForegroundColor Yellow }

if ($broken.Count -eq 0) {
    Write-Host ""
    Write-Host "Some files are missing - the tree may be incomplete or from another JUCE version." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "The JUCE tree is damaged (see known-issues.yaml, KI-001)." -ForegroundColor Red

if ($Fix) {
    Write-Host "Re-fetching JUCE $expectedVersion ..." -ForegroundColor Cyan
    Push-Location $root
    try {
        & python "Tools\fetch_juce.py"
    }
    finally {
        Pop-Location
    }
    Write-Host "Fetched. Run this script again to check the result." -ForegroundColor Cyan
}
else {
    Write-Host "Restore a clean JUCE $expectedVersion tree (or run this script with -Fix)." -ForegroundColor Cyan
}
exit 1
