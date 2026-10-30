<#
    Keep the Android project in sync with the .jucer.

    Almost everything the Android build needs is now an exporter setting in
    UPICuitous.jucer, so a Projucer resave reproduces it:

        androidMinimumSDK="24"      -> minSdkVersion
        androidTargetSDK="35"       -> compileSdk + targetSdkVersion
        androidPluginVersion="8.10.0" -> the Android Gradle plugin
        gradleVersion="8.11.1"      -> the gradle wrapper
        androidKeyStore*            -> the debug keystore (signingConfig)
        the project version         -> the manifest's versionName

    TWO things cannot be expressed: Projucer hardcodes the NDK version
    ("26.2.11394342") and writes paths into JuceLibraryCode/ (a generated
    tree that is not committed). This repo builds with NDK 28.1.13356709 and
    fetches JUCE/ at the repo root instead, so after a resave both have to be
    put back. That is what this script does - and then it verifies every value
    against the .jucer, so a drift is reported instead of silently breaking
    the APK build.

    Usage:
        .\scripts\fix_android.ps1            # fix the NDK line + verify
        .\scripts\fix_android.ps1 -Verify    # only verify, change nothing
#>
[CmdletBinding()]
param(
    [switch] $Verify,
    [string] $NdkVersion = "28.1.13356709"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$jucer = Join-Path $root "UPICuitous.jucer"
$android = Join-Path $root "Builds\Android"
$appGradle = Join-Path $android "app\build.gradle"
$rootGradle = Join-Path $android "build.gradle"
$wrapper = Join-Path $android "gradle\wrapper\gradle-wrapper.properties"
$manifest = Join-Path $android "app\src\main\AndroidManifest.xml"
$appCMake = Join-Path $android "app\CMakeLists.txt"

if (-not (Test-Path $jucer)) { throw "UPICuitous.jucer not found" }
if (-not (Test-Path $appGradle)) { throw "Builds/Android/app/build.gradle not found" }

# ---------------------------------------------------------------- the .jucer
[xml]$jx = Get-Content $jucer -Raw
$exporter = $jx.JUCERPROJECT.EXPORTFORMATS.ANDROIDSTUDIO
if ($null -eq $exporter) { throw "the .jucer has no ANDROIDSTUDIO exporter" }

$want = @{
    "androidMinimumSDK"    = [string] $exporter.androidMinimumSDK
    "androidTargetSDK"     = [string] $exporter.androidTargetSDK
    "androidVersionCode"   = [string] $exporter.androidVersionCode
    # NB: the Android Gradle plugin version is stored under the legacy id
    # "gradleWrapperVersion" in the .jucer (Projucer's Ids::androidPluginVersion
    # has that string value - see jucer_PresetIDs.h). The gradle *wrapper*
    # version is "gradleVersion".
    "gradleWrapperVersion"  = [string] $exporter.gradleWrapperVersion
    "gradleVersion"        = [string] $exporter.gradleVersion
}
$projectVersion = [string] $jx.JUCERPROJECT.version

$problems = @()

function Get-Setting([string] $text, [string] $pattern, [string] $name) {
    $m = [regex]::Match($text, $pattern)
    if (-not $m.Success) {
        $script:problems += "$name not found in the file"
        return $null
    }
    return $m.Groups[1].Value.Trim()
}

# ------------------------------------------- the generated CMakeLists
# Projucer writes module paths and the umbrella header into JuceLibraryCode/,
# which is not committed. Point them at the fetched JUCE/ tree and create the
# two generated pieces in the CMake build directory instead.
function Repair-AndroidCmake([string] $text) {
    $prelude = @'
# UPICuitous: this file is Projucer-generated and normally points at
# JuceLibraryCode/. That generated tree is not committed; the Android build
# fetches JUCE/ at the repo root instead. Create the small unity wrappers and
# the umbrella header in the build tree so no generated JUCE copy is needed.
set(UPIC_JUCE_WRAPPER_DIR "${CMAKE_CURRENT_BINARY_DIR}/juce_wrappers")
file(MAKE_DIRECTORY "${UPIC_JUCE_WRAPPER_DIR}")
function(upic_juce_wrapper name source)
    file(WRITE "${UPIC_JUCE_WRAPPER_DIR}/${name}" "#include <${source}>\n")
endfunction()
upic_juce_wrapper(include_juce_audio_basics.cpp juce_audio_basics/juce_audio_basics.cpp)
upic_juce_wrapper(include_juce_audio_devices.cpp juce_audio_devices/juce_audio_devices.cpp)
upic_juce_wrapper(include_juce_audio_formats.cpp juce_audio_formats/juce_audio_formats.cpp)
upic_juce_wrapper(include_juce_audio_processors.cpp juce_audio_processors/juce_audio_processors.cpp)
upic_juce_wrapper(include_juce_audio_processors_ara.cpp juce_audio_processors/juce_audio_processors_ara.cpp)
upic_juce_wrapper(include_juce_audio_processors_lv2_libs.cpp juce_audio_processors/juce_audio_processors_lv2_libs.cpp)
upic_juce_wrapper(include_juce_audio_utils.cpp juce_audio_utils/juce_audio_utils.cpp)
upic_juce_wrapper(include_juce_core.cpp juce_core/juce_core.cpp)
upic_juce_wrapper(include_juce_core_CompilationTime.cpp juce_core/juce_core_CompilationTime.cpp)
upic_juce_wrapper(include_juce_data_structures.cpp juce_data_structures/juce_data_structures.cpp)
upic_juce_wrapper(include_juce_dsp.cpp juce_dsp/juce_dsp.cpp)
upic_juce_wrapper(include_juce_events.cpp juce_events/juce_events.cpp)
upic_juce_wrapper(include_juce_graphics.cpp juce_graphics/juce_graphics.cpp)
upic_juce_wrapper(include_juce_graphics_Harfbuzz.cpp juce_graphics/juce_graphics_Harfbuzz.cpp)
upic_juce_wrapper(include_juce_graphics_Sheenbidi.c juce_graphics/juce_graphics_Sheenbidi.c)
upic_juce_wrapper(include_juce_gui_basics.cpp juce_gui_basics/juce_gui_basics.cpp)
upic_juce_wrapper(include_juce_gui_extra.cpp juce_gui_extra/juce_gui_extra.cpp)

set(UPIC_JUCE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/juce_include")
file(MAKE_DIRECTORY "${UPIC_JUCE_INCLUDE_DIR}")
file(WRITE "${UPIC_JUCE_INCLUDE_DIR}/JuceHeader.h" [=[
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>
#if ! DONT_SET_USING_JUCE_NAMESPACE
using namespace juce;
#endif
]=])
'@

    if ($text -notmatch 'UPIC_JUCE_WRAPPER_DIR') {
        $needle = 'project(juce_jni_project)'
        $lineEnd = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
        $text = $text.Replace($needle, $needle + $lineEnd + $lineEnd + $prelude)
    }

    $text = $text.Replace('../../../JuceLibraryCode/modules/', '../../../JUCE/modules/')
    $text = $text.Replace('"../../../JuceLibraryCode/modules"', '"../../../JUCE/modules"')
    $text = $text.Replace('"../../../JuceLibraryCode"', '"${UPIC_JUCE_INCLUDE_DIR}"')
    $text = [regex]::Replace($text,
        '"\.\./\.\./\.\./JuceLibraryCode/(include_juce_[^"]+)"',
        '"$${UPIC_JUCE_WRAPPER_DIR}/$1"')
    $text = $text.Replace('"../../../JuceLibraryCode/JuceHeader.h"',
                          '"${UPIC_JUCE_INCLUDE_DIR}/JuceHeader.h"')
    return $text
}

# --------------------------------------------- the NDK (Projucer hardcodes it)
$appText = [IO.File]::ReadAllText($appGradle, [Text.Encoding]::UTF8)
$ndkLine = 'def ndkVersionString = "{0}"' -f $NdkVersion
if (-not $Verify) {
    $patched = [regex]::Replace($appText, 'def ndkVersionString = "[^"]+"', $ndkLine)
    if ($patched -ne $appText) {
        [IO.File]::WriteAllText($appGradle, $patched, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "  NDK version set to $NdkVersion" -ForegroundColor Cyan
    }
    $appText = $patched
}
$ndkFound = Get-Setting $appText 'def ndkVersionString = "([^"]+)"' "the NDK version"
if ($ndkFound -ne $NdkVersion) {
    $problems += "the NDK is '$ndkFound' but this repo builds with '$NdkVersion' (run this script without -Verify to fix it)"
}

# --------------------------------------------------------------- the checks
$compileSdk = Get-Setting $appText 'compileSdk\s+(\d+)' "compileSdk"
if ($compileSdk -ne $want["androidTargetSDK"]) {
    $problems += "compileSdk is '$compileSdk', the .jucer says androidTargetSDK=$($want['androidTargetSDK'])"
}

$minSdk = Get-Setting $appText 'minSdkVersion\s+(\d+)' "minSdkVersion"
if ($minSdk -ne $want["androidMinimumSDK"]) {
    $problems += "minSdkVersion is '$minSdk', the .jucer says androidMinimumSDK=$($want['androidMinimumSDK'])"
}

$targetSdk = Get-Setting $appText 'targetSdkVersion\s+(\d+)' "targetSdkVersion"
if ($targetSdk -ne $want["androidTargetSDK"]) {
    $problems += "targetSdkVersion is '$targetSdk', the .jucer says androidTargetSDK=$($want['androidTargetSDK'])"
}

$rootText = [IO.File]::ReadAllText($rootGradle, [Text.Encoding]::UTF8)
$agp = Get-Setting $rootText "com\.android\.tools\.build:gradle:([0-9.]+)" "the Android Gradle plugin"
if ($agp -ne $want["gradleWrapperVersion"]) {
    $problems += "the Android Gradle plugin is '$agp', the .jucer says gradleWrapperVersion=$($want['gradleWrapperVersion'])"
}

$wrapperText = [IO.File]::ReadAllText($wrapper, [Text.Encoding]::UTF8)
$gradle = Get-Setting $wrapperText 'gradle-([0-9.]+)-all\.zip' "the gradle wrapper"
if ($gradle -ne $want["gradleVersion"]) {
    $problems += "the gradle wrapper is '$gradle', the .jucer says gradleVersion=$($want['gradleVersion'])"
}

$manifestText = [IO.File]::ReadAllText($manifest, [Text.Encoding]::UTF8)
if (-not $Verify) {
    # Projucer writes both from the .jucer (the project version and
    # androidVersionCode), so a resave would put them back
    $patched = [regex]::Replace($manifestText, 'android:versionCode="[^"]+"',
                                'android:versionCode="{0}"' -f $want["androidVersionCode"])
    $patched = [regex]::Replace($patched, 'android:versionName="[^"]+"',
                                'android:versionName="{0}"' -f $projectVersion)
    if ($patched -ne $manifestText) {
        [IO.File]::WriteAllText($manifest, $patched, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "  the manifest version set to $projectVersion (code $($want['androidVersionCode']))" -ForegroundColor Cyan
        $manifestText = $patched
    }
}
$versionName = Get-Setting $manifestText 'android:versionName="([^"]+)"' "the manifest versionName"
if ($versionName -ne $projectVersion) {
    $problems += "the manifest versionName is '$versionName', the .jucer project version is '$projectVersion'"
}
$versionCode = Get-Setting $manifestText 'android:versionCode="([^"]+)"' "the manifest versionCode"
if ($versionCode -ne $want["androidVersionCode"]) {
    $problems += "the manifest versionCode is '$versionCode', the .jucer says androidVersionCode=$($want['androidVersionCode'])"
}

# the debug keystore + the two flavours are what the tablet build needs
if ($appText -notmatch "juceSigning") { $problems += "the juceSigning config is missing" }
if ($appText -notmatch "debug_") { $problems += "the debug_ product flavour is missing" }
if ($appText -notmatch "release_") { $problems += "the release_ product flavour is missing" }

# the generated CMakeLists carries the version too (JUCE_APP_VERSION and
# its hex form: (major << 16) + (minor << 8) + patch)
if (Test-Path $appCMake) {
    $cmText = [IO.File]::ReadAllText($appCMake, [Text.Encoding]::UTF8)
    if (-not $Verify) {
        $repaired = Repair-AndroidCmake $cmText
        if ($repaired -ne $cmText) {
            [IO.File]::WriteAllText($appCMake, $repaired, (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "  the generated CMakeLists now uses the fetched JUCE tree" -ForegroundColor Cyan
            $cmText = $repaired
        }
    }
    elseif ($cmText -match 'JuceLibraryCode/(modules|include_juce|JuceHeader)') {
        $problems += "the generated CMakeLists still points at JuceLibraryCode (run this script without -Verify to fix it)"
    }
    $parts = $projectVersion.Split('.')
    $value = ([int]$parts[0] -shl 16) + ([int]$parts[1] -shl 8) + [int]$parts[2]
    $hex = "0x{0:X}" -f $value   # Projucer: "0x" + toHexString, no padding
    if (-not $Verify) {
        $patched = [regex]::Replace($cmText, '-DJUCE_APP_VERSION=[^]]+\]\]', "-DJUCE_APP_VERSION=$projectVersion]]")
        $patched = [regex]::Replace($patched, '-DJUCE_APP_VERSION_HEX=[^]]+\]\]', "-DJUCE_APP_VERSION_HEX=$hex]]")
        if ($patched -ne $cmText) {
            [IO.File]::WriteAllText($appCMake, $patched, (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "  the generated CMakeLists version set to $projectVersion ($hex)" -ForegroundColor Cyan
            $cmText = $patched
        }
    }
    $cmakeVersion = Get-Setting $cmText '-DJUCE_APP_VERSION=([^]]+)\]\]' "the CMakeLists version"
    if ($cmakeVersion -ne $projectVersion) {
        $problems += "the generated CMakeLists says JUCE_APP_VERSION=$cmakeVersion, the .jucer project version is '$projectVersion'"
    }
    $cmakeHex = Get-Setting $cmText '-DJUCE_APP_VERSION_HEX=([^]]+)\]\]' "the CMakeLists version hex"
    if ($cmakeHex -ne $hex) {
        $problems += "the generated CMakeLists says JUCE_APP_VERSION_HEX=$cmakeHex, the .jucer project version is '$projectVersion' ($hex)"
    }
}

# ---------------------------------------------------------------- the report
Write-Host ""
Write-Host "  .jucer  minSdk $($want['androidMinimumSDK']) / target+compileSdk $($want['androidTargetSDK'])" -ForegroundColor Cyan
Write-Host "  .jucer  AGP $($want['gradleWrapperVersion']) / gradle $($want['gradleVersion'])" -ForegroundColor Cyan
Write-Host "  .jucer  project version $projectVersion" -ForegroundColor Cyan
Write-Host "  build   NDK $ndkFound" -ForegroundColor Cyan

if ($problems.Count -gt 0) {
    Write-Host ""
    foreach ($p in $problems) { Write-Host "  PROBLEM: $p" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "The Android project does not match the .jucer. A Projucer resave would" -ForegroundColor Yellow
    Write-Host "change the listed values (or the script could not fix the NDK line)." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "The Android project matches the .jucer." -ForegroundColor Green
exit 0
