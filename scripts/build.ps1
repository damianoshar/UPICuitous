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

    -Version writes the version into every place that carries one: the root
    CMakeLists, Plugin/CMakeLists.txt, Source/Main.cpp, UPICuitous.jucer
    (the .jucer version feeds the Android manifest and the generated IDE
    projects - see docs/android-build.md), the Android manifest and the
    generated Android CMakeLists, and the Projucer's own exporters. The
    .jucer is the source of truth; every build checks that the others
    still agree with it.

    It only writes the *version* into the Android project - the rest of it
    is Projucer-generated plus the NDK line that scripts/fix_android.ps1
    owns (the Android APK is built in Android Studio).
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

# ----------------------------------------------------------------- CMake ---
# JUCE 8 uses the WRITE_BASIC_CONFIG_VERSION_FILE "ExactVersion" mode that
# CMake 4.4 removed, and WinGet can put its own mingw CMake 4.4 ahead of the
# Visual Studio one on PATH. Prefer the CMake that ships with VS (the build
# project's generator), and only fall back to PATH if it is missing.
function Find-CMake {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -products * -property installationPath 2>$null |
                    Select-Object -First 1
        if ($vsRoot) {
            $cand = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $cand) { return $cand }
        }
    }
    return "cmake"
}

# ------------------------------------------------------- the version bump ---
# The version has one home: the .jucer (the project file) is the source of
# truth, and every other place that carries a version is written from it:
#
#   - the root CMakeLists (the stand-alone app's own build)
#   - Plugin/CMakeLists.txt (the VST3)
#   - Source/Main.cpp (the app's getApplicationVersion())
#   - the Android manifest and the generated Android CMakeLists
#   - the Projucer's own exporters (the VS resources, the Linux makefile,
#     the macOS plist) - they are only rewritten when the project is resaved
#     in the Projucer, so the bump patches them here
#
# A drift check runs at the start of every build, so a hand edit that breaks
# the sync is reported instead of shipping.
function Write-TextFile([string] $path, [string] $text) {
    # UTF-8 without BOM, so the em dashes in the comments survive
    [IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-VersionHex([string] $v) {
    $parts = $v -split '\.'
    if ($parts.Count -lt 3) { return "0x0" }
    # (major << 16) + (minor << 8) + patch - the form the Projucer writes
    return "0x{0:X}" -f (([int]$parts[0] -shl 16) + ([int]$parts[1] -shl 8) + [int]$parts[2])
}

function Get-ProjectVersion {
    $jucer = Join-Path $root "UPICuitous.jucer"
    if (-not (Test-Path $jucer)) { return $null }
    $m = [regex]::Match([IO.File]::ReadAllText($jucer, [Text.Encoding]::UTF8),
                         '<JUCERPROJECT\b[^>]*?\sversion="(\d+\.\d+\.\d+)"')
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

function Sync-AndroidVersion([string] $v) {
    # the Android manifest and the generated CMakeLists carry the version of
    # the project - the Projucer writes them when the project is resaved
    $hex = Get-VersionHex $v
    $manifest = Join-Path $root "Builds\Android\app\src\main\AndroidManifest.xml"
    if (Test-Path $manifest) {
        $text = [IO.File]::ReadAllText($manifest, [Text.Encoding]::UTF8)
        $new = [regex]::Replace($text, 'android:versionName="[^"]+"', "android:versionName=`"$v`"")
        if ($new -ne $text) {
            Write-TextFile $manifest $new
            Write-Host "    the Android manifest version -> $v" -ForegroundColor Cyan
        }
    }
    $cm = Join-Path $root "Builds\Android\app\CMakeLists.txt"
    if (Test-Path $cm) {
        $text = [IO.File]::ReadAllText($cm, [Text.Encoding]::UTF8)
        $new = [regex]::Replace($text, '-DJUCE_APP_VERSION=[^]]+\]\]', "-DJUCE_APP_VERSION=$v]]")
        $new = [regex]::Replace($new, '-DJUCE_APP_VERSION_HEX=[^]]+\]\]', "-DJUCE_APP_VERSION_HEX=$hex]]")
        if ($new -ne $text) {
            Write-TextFile $cm $new
            Write-Host "    the Android CMakeLists version -> $v ($hex)" -ForegroundColor Cyan
        }
    }
}

function Get-GeneratedVersionFiles {
    # the Projucer's exporters (they are only rewritten when the project is
    # resaved in the Projucer, so a version bump patches them here)
    $files = @()
    $files += Get-ChildItem (Join-Path $root "Builds") -Recurse -Filter "*resources.rc" -ErrorAction SilentlyContinue
    $files += Get-ChildItem (Join-Path $root "Builds") -Recurse -Filter "*.vcxproj" -ErrorAction SilentlyContinue
    $mk = Join-Path $root "Builds\LinuxMakefile\Makefile"
    if (Test-Path $mk) { $files += Get-Item $mk }
    $plist = Join-Path $root "Builds\MacOSX\Info-App.plist"
    if (Test-Path $plist) { $files += Get-Item $plist }
    return $files
}

function Sync-GeneratedExporters([string] $v) {
    # the Projucer's exporters (the VS version resources, the VS projects, the
    # Linux makefile and the macOS plist) - a resave in the Projucer would
    # rewrite them, but a version bump should not leave the tree with two
    # versions in it
    $parts = $v -split '\.'
    $fileVersion = "$($parts[0]),$($parts[1]),$($parts[2]),0"
    $hex = Get-VersionHex $v
    $changed = 0

    foreach ($f in (Get-GeneratedVersionFiles)) {
        $text = [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8)
        $new = $text
        # the Visual Studio version resource
        $new = [regex]::Replace($new, '(?m)^FILEVERSION\s+\d+,\d+,\d+,\d+', "FILEVERSION  $fileVersion")
        $new = [regex]::Replace($new, '(?m)^PRODUCTVERSION\s+\d+,\d+,\d+,\d+', "PRODUCTVERSION  $fileVersion")
        $new = [regex]::Replace($new, '(?m)(VALUE\s+"(?:FileVersion|ProductVersion)",\s*")\d+\.\d+\.\d+(\\0")', "`${1}$v`${2}")
        # the version every exporter puts in the preprocessor definitions
        # (the CMake ones carry the -D, the Visual Studio ones do not)
        $new = [regex]::Replace($new, '(-D)?(JUCE_APP_VERSION=)[^"\s;\]]+', '${1}${2}' + $v)
        $new = [regex]::Replace($new, '(-D)?(JUCE_APP_VERSION_HEX=)[^"\s;\]]+', '${1}${2}' + $hex)
        # the macOS plist
        $new = [regex]::Replace($new, '(?m)(<key>CFBundle(?:ShortVersionString|Version)</key>\s*<string>)\d+\.\d+\.\d+(</string>)', "`${1}$v`${2}")
        if ($new -ne $text) {
            Write-TextFile $f.FullName $new
            $changed++
        }
    }
    if ($changed -gt 0) {
        Write-Host "    the generated exporters now say $v ($changed file(s))" -ForegroundColor Cyan
    }
}

function Test-VersionSync {
    # the .jucer is the source of truth - everything else must agree with it
    $want = Get-ProjectVersion
    if (-not $want) { return }
    $checks = @(
        @{ name = "the root CMakeLists";         path = "CMakeLists.txt";
           pattern = 'project\(UPICuitous\s+VERSION\s+(\d+\.\d+\.\d+)' },
        @{ name = "the plugin's CMakeLists";     path = "Plugin\CMakeLists.txt";
           pattern = 'project\(UPICuitousPlugin\s+VERSION\s+(\d+\.\d+\.\d+)' },
        @{ name = "the app's version";           path = "Source\Main.cpp";
           pattern = 'getApplicationVersion\(\)\s*override\s*\{\s*return\s*"(\d+\.\d+\.\d+)"' },
        @{ name = "the Android manifest";        path = "Builds\Android\app\src\main\AndroidManifest.xml";
           pattern = 'android:versionName="([^"]+)"' },
        @{ name = "the Android CMakeLists";      path = "Builds\Android\app\CMakeLists.txt";
           pattern = '-DJUCE_APP_VERSION=([^]]+)\]\]' }
    )
    $drift = @()
    foreach ($c in $checks) {
        $p = Join-Path $root $c.path
        if (-not (Test-Path $p)) { continue }
        $m = [regex]::Match([IO.File]::ReadAllText($p, [Text.Encoding]::UTF8), $c.pattern)
        if (-not $m.Success) {
            $drift += "$($c.name): the version could not be read"
        }
        elseif ($m.Groups[1].Value -ne $want) {
            $drift += "$($c.name): $($m.Groups[1].Value) (the .jucer says $want)"
        }
    }
    if ($drift.Count -gt 0) {
        Write-Host "    WARNING: the version has drifted:" -ForegroundColor Yellow
        foreach ($d in $drift) { Write-Host "      $d" -ForegroundColor Yellow }
        Write-Host "      .\scripts\build.ps1 -Version $want puts it right" -ForegroundColor Yellow
    }
}

# JUCE writes the Windows version resource from Info.txt into
# <Name>_resources.rc, but the custom command that does it only depends on
# the icon - so a VERSION bump never reaches the .rc and Explorer keeps
# showing the old version. Deleting the .rc makes the build regenerate it.
function Set-ProjectVersion([string] $v) {
    $cm = Join-Path $root "Plugin\CMakeLists.txt"
    if (-not (Test-Path $cm)) { Die "Plugin\CMakeLists.txt not found" }
    $text = [IO.File]::ReadAllText($cm, [Text.Encoding]::UTF8)
    $text = [regex]::Replace($text, 'VERSION \d+\.\d+\.\d+', "VERSION $v")
    $parts = $v -split '\.'
    $short = "$($parts[0]).$($parts[1])"
    $text = [regex]::Replace($text, '# VST3 target for UPICuitous \(v[\d.]+\)', "# VST3 target for UPICuitous (v$short)")
    Write-TextFile $cm $text

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
        if ($new -eq $mtext -and -not [regex]::IsMatch($mtext,
            '(getApplicationVersion\(\)\s*override\s*\{\s*return\s*")\d+\.\d+\.\d+(")')) {
            Write-Host "    WARNING: the app version in Source/Main.cpp was not updated" -ForegroundColor Yellow
        }
        Write-TextFile $mc $new
    }

    # the root CMakeLists (the stand-alone app's own CMake build) - it had
    # drifted behind the plugin for several releases
    $rootCm = Join-Path $root "CMakeLists.txt"
    if (Test-Path $rootCm) {
        $rtext = [IO.File]::ReadAllText($rootCm, [Text.Encoding]::UTF8)
        $new = [regex]::Replace($rtext,
            '(project\(UPICuitous\s+VERSION\s+)\d+\.\d+\.\d+',
            "`${1}$v")
        if ($new -eq $rtext -and -not [regex]::IsMatch($rtext,
            '(project\(UPICuitous\s+VERSION\s+)\d+\.\d+\.\d+')) {
            Write-Host "    WARNING: the root CMakeLists version was not updated" -ForegroundColor Yellow
        }
        else {
            Write-TextFile $rootCm $new
        }
    }

    # the .jucer version feeds the Android manifest (versionName) and the
    # generated IDE projects - keep it in step, or the tablet build drifts
    $jucer = Join-Path $root "UPICuitous.jucer"
    if (Test-Path $jucer) {
        $jtext = [IO.File]::ReadAllText($jucer, [Text.Encoding]::UTF8)
        $jnew = [regex]::Replace($jtext,
            '(<JUCERPROJECT\b[^>]*?\sversion=")\d+\.\d+\.\d+(")',
            "`${1}$v`${2}")
        if ($jnew -eq $jtext -and -not [regex]::IsMatch($jtext,
            '(<JUCERPROJECT\b[^>]*?\sversion=")\d+\.\d+\.\d+(")')) {
            Write-Host "    WARNING: the project version in UPICuitous.jucer was not updated" -ForegroundColor Yellow
        }
        else {
            # a broken .jucer would cost the user their project file
            try {
                [xml]$null = $jnew
                Write-TextFile $jucer $jnew
            }
            catch {
                Write-Host "    WARNING: the new .jucer does not parse as XML - it was not written" -ForegroundColor Yellow
            }
        }
    }

    # the exporters that carry the version too
    Sync-AndroidVersion $v
    Sync-GeneratedExporters $v

    Write-Host "    version -> $v (the .jucer is the source of truth)"
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
if ($Version) { Set-ProjectVersion $Version }
Test-VersionSync

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
    $cmake = Find-CMake
    if (-not (Test-Path (Join-Path $root "Builds\Plugin\CMakeCache.txt"))) {
        & $cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64
        if ($LASTEXITCODE -ne 0) { Die "cmake configure failed" }
    }
    Remove-StaleVersionResource

    & $cmake --build Builds/Plugin --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { Die "plugin build failed" }

    if (-not $SkipSmoke) {
        Step "Plugin smoke test"
        & $cmake --build Builds/Plugin --config Release --target UPICuitousPluginSmokeTest --parallel 4
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
$versionSummary = "version " + (Get-ProjectVersion)
Write-Host "app selftest: $appSummary   |   plugin: $pluginSummary   |   smoke: $smokeSummary   |   $validateSummary   |   $versionSummary" -ForegroundColor Green
Write-Host ""
