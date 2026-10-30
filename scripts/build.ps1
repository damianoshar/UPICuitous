<#
    UPICuitous - one-command build + test.

    Builds the standalone app, runs its selftest, builds the VST3 plugin and
    runs the plugin smoke test - the exact sequence that used to be typed by
    hand every wave.

    Usage:
        .\scripts\build.ps1                     # app + selftest + plugin + smoke
        .\scripts\build.ps1 -Version 1.19.0     # bump the version first
        .\scripts\build.ps1 -SkipApp            # plugin only
        .\scripts\build.ps1 -SkipPlugin         # app only

    -Version writes the version into Plugin/CMakeLists.txt, Source/Main.cpp
    and UPICuitous.jucer (the .jucer version feeds the Android manifest and
    the generated IDE projects - see docs/android-build.md).

    It never touches Builds/Android/* - the Android project is Projucer-
    generated plus the NDK line that scripts/fix_android.ps1 owns (the
    Android APK is built in Android Studio).
#>
[CmdletBinding()]
param(
    [string] $Version = "",
    [switch] $SkipApp,
    [switch] $SkipPlugin,
    [switch] $SkipSmoke
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$validateSummary = "validation skipped"

function Step([string] $msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Die([string] $msg) { Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------- MSBuild ---
function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                   -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
        if ($found) { return $found }
    }
    $fallback = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $fallback) { return $fallback }
    return $null
}

# --------------------------------------------------- plugin version bump ---
# JUCE writes the Windows version resource from Info.txt into
# <Name>_resources.rc, but the custom command that does it only depends on
# the icon - so a VERSION bump never reaches the .rc and Explorer keeps
# showing the old version. Deleting the .rc makes the build regenerate it.
function Set-PluginVersion([string] $v) {
    $cm = Join-Path $root "Plugin\CMakeLists.txt"
    if (-not (Test-Path $cm)) { Die "Plugin\CMakeLists.txt not found" }
    $text = Get-Content $cm -Raw
    $text = [regex]::Replace($text, 'VERSION \d+\.\d+\.\d+', "VERSION $v")
    $parts = $v -split '\.'
    $short = "$($parts[0]).$($parts[1])"
    $text = [regex]::Replace($text, '# VST3 target for UPICuitous \(v[\d.]+\)', "# VST3 target for UPICuitous (v$short)")
    # UTF-8 without BOM, so the em dashes in the comments survive
    [IO.File]::WriteAllText($cm, $text, (New-Object System.Text.UTF8Encoding($false)))

    # keep the standalone app's version in sync (Source/Main.cpp)
    $mc = Join-Path $root "Source\Main.cpp"
    if (Test-Path $mc) {
        $mtext = [IO.File]::ReadAllText($mc, [Text.Encoding]::UTF8)
        # the version literal sits inside getApplicationVersion() - match the
        # literal itself, not the comment that follows it (the comment moved
        # once and the old pattern silently stopped matching: the app stayed
        # at 1.15.0 while the plugin said 1.17.0)
        $new = [regex]::Replace($mtext,
            '(getApplicationVersion\(\)\s*override\s*\{\s*return\s*")\d+\.\d+\.\d+(")',
            "`${1}$v`${2}")
        if ($new -eq $mtext) {
            Write-Host "    WARNING: the app version in Source/Main.cpp was not updated" -ForegroundColor Yellow
        }
        [IO.File]::WriteAllText($mc, $new, (New-Object System.Text.UTF8Encoding($false)))
    }

    # the .jucer version feeds the Android manifest (versionName) and the
    # generated IDE projects - keep it in step, or the tablet build drifts
    $jucer = Join-Path $root "UPICuitous.jucer"
    if (Test-Path $jucer) {
        $jtext = [IO.File]::ReadAllText($jucer, [Text.Encoding]::UTF8)
        $jnew = [regex]::Replace($jtext,
            '(<JUCERPROJECT\b[^>]*?\sversion=")\d+\.\d+\.\d+(")',
            "`${1}$v`${2}")
        if ($jnew -eq $jtext) {
            Write-Host "    WARNING: the project version in UPICuitous.jucer was not updated" -ForegroundColor Yellow
        }
        else {
            # a broken .jucer would cost the user their project file
            try {
                [xml]$null = $jnew
                [IO.File]::WriteAllText($jucer, $jnew, (New-Object System.Text.UTF8Encoding($false)))
            }
            catch {
                Write-Host "    WARNING: the new .jucer does not parse as XML - it was not written" -ForegroundColor Yellow
            }
        }
    }
    Write-Host "    Plugin + app version -> $v"
}

function Get-CmakeVersion {
    $cm = Join-Path $root "Plugin\CMakeLists.txt"
    $m = Select-String -Path $cm -Pattern 'VERSION (\d+\.\d+\.\d+)' | Select-Object -First 1
    if ($m) { return $m.Matches[0].Groups[1].Value }
    return "0.0.0"
}

function Remove-StaleVersionResource {
    $want = Get-CmakeVersion
    $stale = @()
    Get-ChildItem (Join-Path $root "Builds\Plugin") -Recurse -Filter "*_resources.rc" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $rc = Get-Content $_.FullName -Raw
            $m = [regex]::Match($rc, 'FILEVERSION\s+(\d+),(\d+),(\d+)')
            if ($m.Success) {
                $have = "$($m.Groups[1].Value).$($m.Groups[2].Value).$($m.Groups[3].Value)"
                if ($have -ne $want) {
                    Remove-Item $_.FullName -Force
                    $stale += "$($_.Name) ($have -> $want)"
                }
            }
        }
    if ($stale.Count -gt 0) {
        Write-Host "    stale version resource removed: $($stale -join ', ')" -ForegroundColor Yellow
    }
}

# ------------------------------------------------------- the version bump ---
# Before anything is built: the app's getApplicationVersion() and the .jucer
# (the Android manifest) must carry the new version, and the app is built
# from that source.
if ($Version) { Set-PluginVersion $Version }

# ------------------------------------------------------------------- app ---
$appOk = $true
$appSummary = "skipped"
if (-not $SkipApp) {
    Step "App build (Debug|x64)"
    Get-Process UPICuitous -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300

    $msbuild = Find-MSBuild
    if (-not $msbuild) { Die "MSBuild not found (install Visual Studio with the C++ workload)" }

    $sln = Join-Path $root "Builds\VisualStudio2026\UPICuitous.sln"
    if (-not (Test-Path $sln)) { Die "solution not found: $sln" }

    & $msbuild $sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { Die "app build failed" }

    $exe = Join-Path $root "Builds\VisualStudio2026\x64\Debug\App\UPICuitous.exe"
    if (-not (Test-Path $exe)) { Die "app exe not found: $exe" }

    Step "App selftest"
    & $exe --selftest | Out-Null
    $rc = $LASTEXITCODE
    $log = Join-Path $env:TEMP "upic_selftest.txt"
    $checks = 0; $fails = 0
    if (Test-Path $log) {
        $lines = Get-Content $log
        $checks = ($lines | Select-String -Pattern "^(PASS|FAIL)").Count
        $fails  = ($lines | Select-String -Pattern "^FAIL").Count
    }
    if ($rc -ne 0 -or $fails -gt 0) {
        $lines = Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern "^FAIL" | ForEach-Object { $_.Line }
        Die "app selftest failed ($fails of $checks) - $($lines -join ' | ')"
    }
    $appSummary = "$checks/$checks"
    Write-Host "    app selftest $appSummary" -ForegroundColor Green
}

# ---------------------------------------------------------------- plugin ---
$pluginOk = $true
$pluginSummary = "skipped"
$smokeSummary = "skipped"
$vst3Path = Join-Path $root "Builds\Plugin\UPICuitousPlugin_artefacts\Release\VST3\UPICuitous.vst3"

if (-not $SkipPlugin) {
    Step "Plugin build (Release)"
    if (-not (Test-Path (Join-Path $root "Builds\Plugin\CMakeCache.txt"))) {
        cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64
        if ($LASTEXITCODE -ne 0) { Die "cmake configure failed" }
    }
    Remove-StaleVersionResource

    cmake --build Builds/Plugin --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { Die "plugin build failed" }

    if (-not $SkipSmoke) {
        Step "Plugin smoke test"
        cmake --build Builds/Plugin --config Release --target UPICuitousPluginSmokeTest --parallel 4
        if ($LASTEXITCODE -ne 0) { Die "smoke test build failed" }
        $smoke = Join-Path $root "Builds\Plugin\UPICuitousPluginSmokeTest_artefacts\Release\UPICuitousPluginSmokeTest.exe"
        $smokeOut = & $smoke
        if ($LASTEXITCODE -ne 0) {
            ($smokeOut | Select-String -Pattern "^FAIL") | ForEach-Object { Write-Host $_.Line -ForegroundColor Red }
            Die "smoke test failed"
        }
        $smokeCount = ($smokeOut | Select-String -Pattern "^(PASS|FAIL)").Count
        $smokeSummary = "$smokeCount/$smokeCount"
        Write-Host "    smoke test $smokeSummary" -ForegroundColor Green
    }

    $mi = Join-Path $vst3Path "Contents\Resources\moduleinfo.json"
    $ver = "?"
    if (Test-Path $mi) {
        $m = Select-String -Path $mi -Pattern '"Version"\s*:\s*"([^"]+)"' | Select-Object -First 1
        if ($m) { $ver = $m.Matches[0].Groups[1].Value }
    }
    $pluginSummary = $ver
    Write-Host "    VST3 $ver -> $vst3Path" -ForegroundColor Green

    # ------------------------------------------------- pluginval validation ---
    # The industry VST3 check (bus arrangements, automation, editor, state).
    # It is not in the repo: download it once with scripts/fetch_pluginval.ps1
    $pluginval = Join-Path $root "Tools\pluginval\pluginval.exe"
    if (Test-Path $pluginval) {
        Step "pluginval (strictness 5)"
        & $pluginval --validate $vst3Path --strictness-level 5 | Select-Object -Last 2
        if ($LASTEXITCODE -ne 0) { Die "pluginval validation failed" }
        $validateSummary = "pluginval OK"
        Write-Host "    pluginval: SUCCESS" -ForegroundColor Green
    }
    else {
        Write-Host "    pluginval not installed - run scripts\fetch_pluginval.ps1 to enable the gate" -ForegroundColor Yellow
        $validateSummary = "pluginval missing"
    }
}

Write-Host ""
Write-Host "app selftest: $appSummary   |   plugin: $pluginSummary   |   smoke: $smokeSummary   |   $validateSummary" -ForegroundColor Green
Write-Host ""
