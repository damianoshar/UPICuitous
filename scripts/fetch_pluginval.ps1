<#
    Downloads pluginval (Tracktion's VST3/AU/CLAP validator) into
    Tools/pluginval/ - the binary is not in the repo, this script fetches it
    once so scripts/build.ps1 can use it as a validation gate.

    SECURITY (S-05): the release is pinned to a version and the archive is
    verified against a SHA-256 before it is extracted. The old script
    downloaded whatever the "latest" release happened to be and ran it
    without a check - a hijacked release (or a tampered asset) would have
    run arbitrary code on the build machine.

    Usage:
        .\scripts\fetch_pluginval.ps1
        .\scripts\fetch_pluginval.ps1 -Force   # re-download even if present
#>
[CmdletBinding()]
param(
    [switch] $Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst = Join-Path $root "Tools\pluginval"
$exe = Join-Path $dst "pluginval.exe"

# The pinned pluginval release and the SHA-256 of its Windows archive.
# Update both together (the hash is of the .zip, not of the exe).
$Version = "v1.0.4"
$ArchiveName = "pluginval_Windows.zip"
$Sha256 = "c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab"
$Url = "https://github.com/Tracktion/pluginval/releases/download/$Version/$ArchiveName"

if ((Test-Path $exe) -and -not $Force) {
    Write-Host "pluginval is already installed: $exe" -ForegroundColor Green
    exit 0
}

$ProgressPreference = "SilentlyContinue"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
$zip = Join-Path $env:TEMP "pluginval_Windows.zip"

Write-Host "==> downloading $ArchiveName ($Version)"
Invoke-WebRequest -Uri $Url -OutFile $zip -TimeoutSec 300

Write-Host "==> verifying the SHA-256"
$hash = (Get-FileHash -Path $zip -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $Sha256) {
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    Write-Host "FAILED: the archive's SHA-256 is $hash, expected $Sha256" -ForegroundColor Red
    Write-Host "The download was discarded - nothing was extracted." -ForegroundColor Red
    exit 1
}
Write-Host "  ok: $hash"

Write-Host "==> extracting"
Expand-Archive -Path $zip -DestinationPath $dst -Force
Remove-Item $zip -Force

if (-not (Test-Path $exe)) { Write-Host "FAILED: pluginval.exe not found after the extract" -ForegroundColor Red; exit 1 }
Write-Host "pluginval $Version installed: $exe" -ForegroundColor Green
& $exe --version
