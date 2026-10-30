<#
    Check the JUCE tree against the known-good checksums.

    The JUCE tree is gitignored, so git cannot repair it - and an external
    sync once dropped a damaged copy of the VST3 SDK header fobject.h into
    it (see known-issues.yaml, KI-001). This script compares the files that
    matter against the SHA-256 list in known-issues.yaml and says which one
    is wrong.

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
$issues = Join-Path $root "known-issues.yaml"

if (-not (Test-Path $juce)) {
    Write-Host "No JUCE tree at $juce - run `python Tools\fetch_juce.py`." -ForegroundColor Yellow
    exit 1
}

# The VST3 SDK headers: this is the folder that broke. The expected hashes
# are the JUCE 8.0.6 ones (also written down in known-issues.yaml, KI-001).
$expected = @{
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fobject.h"             = "E548318C88285EB66BDB0F13637C91BC839B4356C8FEB2C9E508CD5F8BEDD43A"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fstring.h"              = "7854C87CF715CFA7D716C8021F5291F4E1F3476365D43F5AE717A6E316BA071F"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\classfactoryhelpers.h" = "9E62E90B4C4374ED3DC7D4B637609A775D77715256841E3BBDC113AD2954C5C1"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fbuffer.h"              = "9AF081BC29C2451ABE0075F0B99D89A012FFDCFA3214393F37F0E61B435A22BB"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fcommandline.h"         = "7FC62CE9900F2D35FF77549EF6DB79069D1950E64ACBFC9E994321F73B280FEC"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fdebug.h"               = "02EC781553CA4EDBA305064DE4A619901C116457270603CBC3D781CE7BB80954"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\fstreamer.h"            = "341FF07999A790CB52500C62F484AD527F5E207AA268E368C265351EABED6EBA"
    "modules\juce_audio_processors\format_types\VST3_SDK\base\source\updatehandler.h"        = "75DBBE97EB8D0B9B7F72ED4B35423360E3A4D808559250782785EAE290D24575"
}

$broken = @()
$missing = @()

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
    Write-Host "The JUCE VST3 SDK headers are the JUCE 8.0.6 ones." -ForegroundColor Green
    exit 0
}

foreach ($m in $missing) { Write-Host "  MISSING: $m" -ForegroundColor Yellow }
foreach ($b in $broken)  { Write-Host "  DAMAGED: $b" -ForegroundColor Yellow }

if ($broken.Count -eq 0) {
    Write-Host ""
    Write-Host "Some SDK headers are missing - the tree may be incomplete." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "The JUCE tree is damaged (see known-issues.yaml, KI-001)." -ForegroundColor Red

if ($Fix) {
    Write-Host "Re-fetching JUCE 8.0.6 ..." -ForegroundColor Cyan
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
    Write-Host "Restore a clean JUCE 8.0.6 tree (or run this script with -Fix)." -ForegroundColor Cyan
}
exit 1
