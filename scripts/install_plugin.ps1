<#
    Install the built VST3 into the system VST3 folder.

    "C:\Program Files\VstPlugIns" needs administrator rights, so the copy
    runs elevated (UAC prompt). The script verifies the result: the
    installed binary's SHA-256 must match the build output, and the
    version resource / moduleinfo must read the expected version.

    Usage:
        .\scripts\install_plugin.ps1                 # install (UAC prompt)
        .\scripts\install_plugin.ps1 -Version 1.18.0 # also check the version
#>
[CmdletBinding()]
param(
    [string] $Version = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "Builds\Plugin\UPICuitousPlugin_artefacts\Release\VST3\UPICuitous.vst3"
$dst = "C:\Program Files\VstPlugIns\UPICuitous.vst3"

if (-not (Test-Path $src)) {
    Write-Host "The plugin was not built: $src is missing." -ForegroundColor Red
    Write-Host "Run .\scripts\build.ps1 first." -ForegroundColor Yellow
    exit 1
}

# The VST3 is a bundle: the binary with the version resource is inside.
function Get-PluginBinary([string] $bundle) {
    $inner = Get-ChildItem -LiteralPath (Join-Path $bundle "Contents") -Recurse -File `
                  -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($inner) { return $inner.FullName }
    return $bundle
}

$srcBin = Get-PluginBinary $src
$srcHash = (Get-FileHash -Path $srcBin -Algorithm SHA256).Hash

# The copy needs admin rights -> run it elevated and wait for it.
$script = @"
`$ErrorActionPreference = 'Stop'
if (Test-Path '$dst') { Remove-Item -LiteralPath '$dst' -Recurse -Force }
Copy-Item -LiteralPath '$src' -Destination '$dst' -Recurse -Force
"@

$bytes = [Text.Encoding]::Unicode.GetBytes($script)
$encoded = [Convert]::ToBase64String($bytes)

Write-Host "Installing to $dst (UAC prompt)..." -ForegroundColor Cyan
try {
    $proc = Start-Process powershell -Verb RunAs -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $encoded
    ) -Wait -PassThru -ErrorAction Stop
}
catch {
    # the UAC prompt was declined - nothing was changed
    Write-Host "The elevation was declined - the install was cancelled." -ForegroundColor Yellow
    Write-Host "The plugin in $dst was not touched." -ForegroundColor Yellow
    exit 1
}

if ($proc.ExitCode -ne 0) {
    Write-Host "The elevated install failed (exit $($proc.ExitCode))." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $dst)) {
    Write-Host "The install did not produce $dst." -ForegroundColor Red
    exit 1
}

$dstBin = Get-PluginBinary $dst
$dstHash = (Get-FileHash -Path $dstBin -Algorithm SHA256).Hash
if ($srcHash -ne $dstHash) {
    Write-Host "SHA-256 mismatch!" -ForegroundColor Red
    Write-Host "  build:     $srcBin`n            $srcHash"
    Write-Host "  installed: $dstBin`n            $dstHash"
    exit 1
}
Write-Host "  SHA-256 matches: $srcHash" -ForegroundColor Green

$info = (Get-Item $dstBin).VersionInfo
Write-Host "  FileVersion:    $($info.FileVersion)"
Write-Host "  ProductVersion: $($info.ProductVersion)"

$mi = Join-Path $dst "Contents\Resources\moduleinfo.json"
if (Test-Path $mi) {
    $m = Select-String -Path $mi -Pattern '"Version"\s*:\s*"([^"]+)"' | Select-Object -First 1
    if ($m) { Write-Host "  moduleinfo:     $($m.Matches[0].Groups[1].Value)" }
}

if ($Version) {
    if ($info.ProductVersion -ne $Version) {
        Write-Host "Expected version $Version, found '$($info.ProductVersion)'." -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "Installed." -ForegroundColor Green
Write-Host "Restart the DAW (it caches the plugin list), then check the plugin's version." -ForegroundColor Cyan
