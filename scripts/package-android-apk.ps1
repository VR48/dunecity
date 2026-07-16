[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$AndroidSdk = "",
    [string]$AndroidNdk = "",
    [string]$VcpkgRoot = "",
    [string]$NativeBuildDir = "build-android-arm64-ndk",
    [switch]$BuildApk
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
if ([string]::IsNullOrWhiteSpace($AndroidSdk)) {
    $AndroidSdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
}
if ([string]::IsNullOrWhiteSpace($AndroidSdk)) {
    throw "Set ANDROID_HOME or ANDROID_SDK_ROOT, or pass -AndroidSdk."
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    $AndroidNdk = $env:ANDROID_NDK_HOME
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    $ndkRoot = Join-Path $AndroidSdk "ndk"
    if (Test-Path -LiteralPath $ndkRoot) {
        $latestNdk = Get-ChildItem -LiteralPath $ndkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $latestNdk) {
            $AndroidNdk = $latestNdk.FullName
        }
    }
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    throw "Set ANDROID_NDK_HOME, install an NDK under the Android SDK ndk directory, or pass -AndroidNdk."
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = $env:VCPKG_ROOT
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "Set VCPKG_ROOT or pass -VcpkgRoot."
}

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-UnderRoot([string]$Path, [string]$Root) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside repo root: $fullPath"
    }
}

function Reset-Directory([string]$Path, [string]$Root) {
    Assert-UnderRoot $Path $Root
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Convert-ToPropertiesPath([string]$Path) {
    return $Path.Replace("\", "\\").Replace(":", "\:")
}

$RepoRoot = Get-FullPath $RepoRoot
$cmakeProject = Select-String -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Pattern '^project\(DuneCity VERSION ([0-9]+)\.([0-9]+)\.([0-9]+) '
if ($null -eq $cmakeProject) {
    throw "Could not read the DuneCity version from CMakeLists.txt."
}
$projectVersion = $cmakeProject.Matches[0].Groups[1..3].Value -join "."
$androidVersionFile = Join-Path $RepoRoot "android-version.json"
if (-not (Test-Path -LiteralPath $androidVersionFile)) {
    throw "Missing Android version metadata: $androidVersionFile"
}
$androidVersion = Get-Content -LiteralPath $androidVersionFile -Raw | ConvertFrom-Json
$androidVersionName = [string]$androidVersion.versionName
$androidVersionCode = [int]$androidVersion.versionCode
if ($androidVersionName -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Invalid Android versionName '$androidVersionName'."
}
if ($androidVersionCode -le 0 -or $androidVersionCode -gt 2100000000) {
    throw "Android versionCode must be between 1 and 2100000000."
}
$payloadVersion = $androidVersionName -replace '[^0-9A-Za-z._-]', '_'

$nativeLib = Join-Path $RepoRoot "$NativeBuildDir\lib\libmain.so"
if (-not (Test-Path -LiteralPath $nativeLib)) {
    throw "Missing native library: $nativeLib. Build target 'dunecity' in $NativeBuildDir first."
}

$sdlSourceRoot = Join-Path $VcpkgRoot "buildtrees\sdl2\src"
$sdlSource = Get-ChildItem -LiteralPath $sdlSourceRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "android-project\app\src\main\java\org\libsdl\app\SDLActivity.java") } |
    Select-Object -First 1
if ($null -eq $sdlSource) {
    throw "Could not find SDL android-project under $sdlSourceRoot"
}

$stageDir = Join-Path $RepoRoot "build-android-apk"
$payloadDir = Join-Path $RepoRoot "build-android-payload"
Reset-Directory $stageDir $RepoRoot
Reset-Directory $payloadDir $RepoRoot

$sdlAndroidProject = Join-Path $sdlSource.FullName "android-project"
Copy-Item -Path (Join-Path $sdlAndroidProject "*") -Destination $stageDir -Recurse -Force

$jniDir = Join-Path $stageDir "app\jni"
if (Test-Path -LiteralPath $jniDir) {
    Remove-Item -LiteralPath $jniDir -Recurse -Force
}

$wrapperProperties = Join-Path $stageDir "gradle\wrapper\gradle-wrapper.properties"
(Get-Content -LiteralPath $wrapperProperties) |
    ForEach-Object {
        if ($_ -like "distributionUrl=*") {
            "distributionUrl=https\://services.gradle.org/distributions/gradle-8.9-bin.zip"
        } else {
            $_
        }
    } |
    Set-Content -LiteralPath $wrapperProperties -Encoding ASCII

$rootBuildGradle = @'
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.7.3'
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

tasks.register('clean', Delete) {
    delete rootProject.buildDir
}
'@
Set-Content -LiteralPath (Join-Path $stageDir "build.gradle") -Value $rootBuildGradle -Encoding ASCII

$appBuildGradle = @'
apply plugin: 'com.android.application'

android {
    namespace "net.dunecity.dune2r"
    compileSdkVersion 34

    defaultConfig {
        applicationId "net.dunecity.dune2r"
        minSdkVersion 23
        targetSdkVersion 34
        versionCode __VERSION_CODE__
        versionName "__VERSION_NAME__"

        ndk {
            abiFilters "arm64-v8a"
        }
    }

    sourceSets {
        main {
            jniLibs.srcDirs = ["src/main/jniLibs"]
        }
    }

    packagingOptions {
        jniLibs {
            useLegacyPackaging true
        }
    }

    lint {
        abortOnError false
    }
}
'@
$appBuildGradle = $appBuildGradle.Replace("__VERSION_CODE__", [string]$androidVersionCode).Replace("__VERSION_NAME__", $androidVersionName)
Set-Content -LiteralPath (Join-Path $stageDir "app\build.gradle") -Value $appBuildGradle -Encoding ASCII

$manifest = @'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    android:installLocation="auto">

    <uses-feature android:glEsVersion="0x00020000" />
    <uses-feature android:name="android.hardware.touchscreen" android:required="false" />
    <uses-feature android:name="android.hardware.bluetooth" android:required="false" />
    <uses-feature android:name="android.hardware.gamepad" android:required="false" />
    <uses-feature android:name="android.hardware.usb.host" android:required="false" />
    <uses-feature android:name="android.hardware.type.pc" android:required="false" />

    <uses-permission android:name="android.permission.VIBRATE" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <application
        android:label="@string/app_name"
        android:icon="@mipmap/ic_launcher"
        android:allowBackup="true"
        android:theme="@style/AppTheme"
        android:hardwareAccelerated="true"
        android:extractNativeLibs="true">

        <activity
            android:name="net.dunecity.dune2r.Dune2RActivity"
            android:label="@string/app_name"
            android:alwaysRetainTaskState="true"
            android:launchMode="singleInstance"
            android:screenOrientation="landscape"
            android:configChanges="layoutDirection|locale|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|keyboard|keyboardHidden|navigation"
            android:preferMinimalPostProcessing="true"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
'@
Set-Content -LiteralPath (Join-Path $stageDir "app\src\main\AndroidManifest.xml") -Value $manifest -Encoding ASCII

$activityDir = Join-Path $stageDir "app\src\main\java\net\dunecity\dune2r"
New-Item -ItemType Directory -Force -Path $activityDir | Out-Null
$activity = @'
package net.dunecity.dune2r;

import android.content.res.AssetManager;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class Dune2RActivity extends SDLActivity {
    private static final String PAYLOAD_ROOT = "dune2r_payload";
    private static final String PAYLOAD_MARKER = ".dune2r_payload___PAYLOAD_VERSION__";

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        copyBundledPayload();
        super.onCreate(savedInstanceState);
    }

    private void copyBundledPayload() {
        File outputRoot = getExternalFilesDir(null);
        if (outputRoot == null) {
            outputRoot = getFilesDir();
        }

        File marker = new File(outputRoot, PAYLOAD_MARKER);
        if (marker.exists()) {
            return;
        }

        try {
            copyAssetTree(getAssets(), PAYLOAD_ROOT, outputRoot);
            marker.createNewFile();
        } catch (IOException e) {
            throw new RuntimeException("Failed to stage Dune2R payload", e);
        }
    }

    private void copyAssetTree(AssetManager assets, String assetPath, File outputRoot) throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, outputRoot);
            }
            return;
        }

        String relativePath = assetPath.substring(PAYLOAD_ROOT.length() + 1);
        File outputFile = new File(outputRoot, relativePath);
        if (outputFile.exists() && outputFile.length() > 0 && relativePath.equals("config/Dune City.ini")) {
            return;
        }

        File parent = outputFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Could not create directory: " + parent);
        }

        try (InputStream in = assets.open(assetPath);
             OutputStream out = new FileOutputStream(outputFile)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        }
    }
}
'@
$activity = $activity.Replace("__PAYLOAD_VERSION__", $payloadVersion)
Set-Content -LiteralPath (Join-Path $activityDir "Dune2RActivity.java") -Value $activity -Encoding ASCII

$stringsPath = Join-Path $stageDir "app\src\main\res\values\strings.xml"
$strings = @'
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">Dune Legacy</string>
</resources>
'@
Set-Content -LiteralPath $stringsPath -Value $strings -Encoding ASCII

$iconSource = Join-Path $RepoRoot "dunecity-128x128.png"
if (Test-Path -LiteralPath $iconSource) {
    foreach ($density in @("mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi", "mipmap-xxhdpi", "mipmap-xxxhdpi")) {
        $iconDir = Join-Path $stageDir "app\src\main\res\$density"
        New-Item -ItemType Directory -Force -Path $iconDir | Out-Null
        Copy-Item -LiteralPath $iconSource -Destination (Join-Path $iconDir "ic_launcher.png") -Force
    }
}

$jniLibsArm64 = Join-Path $stageDir "app\src\main\jniLibs\arm64-v8a"
New-Item -ItemType Directory -Force -Path $jniLibsArm64 | Out-Null
Copy-Item -LiteralPath $nativeLib -Destination (Join-Path $jniLibsArm64 "libmain.so") -Force

$libcxx = Join-Path $AndroidNdk "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so"
if (-not (Test-Path -LiteralPath $libcxx)) {
    throw "Missing libc++_shared.so: $libcxx"
}
Copy-Item -LiteralPath $libcxx -Destination (Join-Path $jniLibsArm64 "libc++_shared.so") -Force

Set-Content -LiteralPath (Join-Path $stageDir "local.properties") -Value ("sdk.dir=" + (Convert-ToPropertiesPath $AndroidSdk)) -Encoding ASCII

Copy-Item -LiteralPath (Join-Path $RepoRoot "data") -Destination (Join-Path $payloadDir "data") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "config") -Destination (Join-Path $payloadDir "config") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "mods") -Destination (Join-Path $payloadDir "mods") -Recurse -Force
if (Test-Path -LiteralPath (Join-Path $RepoRoot "imported_sprites")) {
    Copy-Item -LiteralPath (Join-Path $RepoRoot "imported_sprites") -Destination (Join-Path $payloadDir "imported_sprites") -Recurse -Force
}

$assetPayloadDir = Join-Path $stageDir "app\src\main\assets\dune2r_payload"
New-Item -ItemType Directory -Force -Path (Split-Path $assetPayloadDir -Parent) | Out-Null
Copy-Item -LiteralPath $payloadDir -Destination $assetPayloadDir -Recurse -Force

if ($BuildApk) {
    if ([string]::IsNullOrWhiteSpace($env:JAVA_HOME)) {
        throw "Set JAVA_HOME before building the APK."
    }
    $env:ANDROID_HOME = $AndroidSdk
    $env:ANDROID_SDK_ROOT = $AndroidSdk
    $env:ANDROID_NDK_HOME = $AndroidNdk
    $env:Path = "$env:JAVA_HOME\bin;$AndroidSdk\platform-tools;$env:Path"
    Push-Location $stageDir
    try {
        & .\gradlew.bat assembleDebug --no-daemon
        if ($LASTEXITCODE -ne 0) {
            throw "Gradle assembleDebug failed with exit code $LASTEXITCODE"
        }

        $debugApk = Join-Path $stageDir "app\build\outputs\apk\debug\app-debug.apk"
        $namedApk = Join-Path $stageDir "app\build\outputs\apk\debug\DuneLegacy.apk"
        if (Test-Path -LiteralPath $debugApk) {
            Copy-Item -LiteralPath $debugApk -Destination $namedApk -Force
        }
    } finally {
        Pop-Location
    }
}

Write-Host "APK project staged: $stageDir"
Write-Host "APK version: $androidVersionName ($androidVersionCode)"
Write-Host "DuneCity payload version: $projectVersion"
Write-Host "Data payload staged: $payloadDir"
Write-Host "Native libs staged: $jniLibsArm64"
Write-Host "Install APK: adb install -r `"$stageDir\app\build\outputs\apk\debug\DuneLegacy.apk`""
Write-Host "The APK contains the payload and stages it into Android app storage on first launch."
Write-Host "Manual payload push, if needed for debugging: adb push `"$payloadDir\.`" /sdcard/Android/data/net.dunecity.dune2r/files/"
