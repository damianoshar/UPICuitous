<#
    Downloads pluginval (Tracktion's VST3/AU/CLAP validator) into
    Tools/pluginval/ - the binary is not in the repo, this script fetches it
    once so scripts/build.ps1 can use it as a validation gate.

    Usage:
        .\scripts\fetch_pluginval.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst = Join-Path $root "Tools\pluginval"
$exe = Join-Path $dst "pluginval.exe"

if (Test-Path $exe) {
    Write-Host "pluginval is already installed: $exe" -ForegroundColor Green
    exit 0
}

$ProgressPreference = "SilentlyContinue"
Write-Host "==> looking up the latest pluginval release"
$rel = Invoke-RestMethod -Uri "https://api.github.com/repos/Tracktion/pluginval/releases/latest" `
        -Headers @{ "User-Agent" = "upicuitous-build" } -TimeoutSec 60
$asset = $rel.assets | Where-Object { $_.name -eq "pluginval_Windows.zip" } | Select-Object -First 1
if (-not $asset) { Write-Host "FAILED: no Windows asset in $($rel.tag_name)" -ForegroundColor Red; exit 1 }

New-Item -ItemType Directory -Force -Path $dst | Out-Null
$zip = Join-Path $env:TEMP "pluginval_Windows.zip"
Write-Host "==> downloading $($asset.name) ($($asset.size) bytes)"
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -TimeoutSec 300
Expand-Archive -Path $zip -DestinationPath $dst -Force
Remove-Item $zip -Force

if (-not (Test-Path $exe)) { Write-Host "FAILED: pluginval.exe not found after the extract" -ForegroundColor Red; exit 1 }
Write-Host "pluginval $($rel.tag_name) installed: $exe" -ForegroundColor Green
& $exe --version
