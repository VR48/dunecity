# Local Build Guide

This guide reproduces the local Windows and Android test builds made from the
Dune2R checkout on the PREDATOR laptop. It is intentionally separate from the
release workflow: these commands do not commit, push, tag, upload, or publish
anything.

The working checkout used here is:

```text
D:\BotServer\GitHub\Dune2R
```

Run the commands from PowerShell. Administrator access is not normally needed.

## Keep The Warm Caches

The first build is expensive because vcpkg must obtain and compile every native
dependency. Do not delete these directories after a successful build:

```text
build-windows-local
build-android-refinery-fix
D:\BotServer\External-4TB-Cloud\VcpkgCache\archives
```

Normal source edits only require incremental compilation. Avoid `git clean
-xfd`, deleting all `build-*` directories, or moving the checkout unless a
toolchain cache is genuinely broken.

## One-Time Tooling

Install or retain the following:

- Git for Windows
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer
- Ninja
- vcpkg
- JDK 17
- Android SDK platform 34 and Build Tools 34 or newer
- Android NDK 26 or newer; NDK 28.2 is the tested version
- Android Platform Tools (`adb`)

Verify the command-line tools:

```powershell
git --version
cmake --version
ninja --version
java -version
adb version
```

## Start A Build Shell

Open PowerShell and initialize the paths used on this laptop:

```powershell
Set-Location 'D:\BotServer\GitHub\Dune2R'
$env:VCPKG_ROOT = 'D:\BotServer\Tools\vcpkg'
$env:JAVA_HOME = 'C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot'
$env:ANDROID_HOME = 'D:\BotServer\External-4TB-Cloud\AndroidSDK'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:ANDROID_NDK_HOME = 'D:\BotServer\External-4TB-Cloud\AndroidSDK\ndk\28.2.13676358'
```

The `D:` volume is exFAT. Current MSYS packages contain link entries that exFAT
cannot extract, so Android dependency downloads and tool extraction must use an
NTFS directory on `C:`. OpenSSL installation can also race while renaming its
Makefile on exFAT, so keep vcpkg dependency concurrency at one:

```powershell
$env:VCPKG_DOWNLOADS = "$env:LOCALAPPDATA\vcpkg-downloads"
$env:VCPKG_MAX_CONCURRENCY = '1'
```

Only temporary downloads/tools use `C:`. The source, large build trees, Android
SDK, NDK, and binary package cache remain on `D:`.

Confirm the important paths before building:

```powershell
Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
Test-Path "$env:ANDROID_NDK_HOME\build\cmake\android.toolchain.cmake"
Test-Path "$env:JAVA_HOME\bin\java.exe"
```

All three commands should print `True`.

## Windows Test Build

### Configure Once

Run this only when `build-windows-local` does not exist, CMake files changed, or
the compiler/toolchain changed:

```powershell
cmake -S . -B build-windows-local -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DDUNECITY_BUILD_TESTS=ON
```

The first configure may spend several minutes restoring or building vcpkg
dependencies.

### Compile

```powershell
cmake --build build-windows-local --config Release --target dunecity --parallel 6
```

The runnable development output is:

```text
build-windows-local\bin\Release\dunecity.exe
```

The build target also copies the required DLLs, configuration, data, and mods
beside the executable.

### Run Tests

```powershell
ctest --test-dir build-windows-local -C Release --output-on-failure
```

Do not package a test build when this reports a failure.

### Create The Portable ZIP

Refresh the install tree, then run CPack:

```powershell
cmake --install build-windows-local --prefix build-windows-local/install --config Release
Push-Location build-windows-local
cpack -G ZIP -C Release
Pop-Location
```

The filename contains the current project version. At the time this guide was
written, the output is:

```text
build-windows-local\DuneCity-1.0.533-Windows-x64.zip
```

For an ordinary code change, repeat only **Compile**, **Run Tests**, and
**Create The Portable ZIP**. Do not reconfigure every time.

## Android Test APK

The repository script builds the native arm64 library, stages the payload,
creates the SDL Android project, strips the APK copies of native libraries, and
assembles a debug-signed APK.

### Full Or Incremental Build

Keep using the same native build directory so CMake and vcpkg remain warm:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-android-apk.ps1 -NativeBuildDir build-android-refinery-fix -BuildNative -NativeBuildJobs 4 -BuildApk
```

`VCPKG_MAX_CONCURRENCY=1` applies to dependency builds. `NativeBuildJobs 4`
still lets the Dune2R source itself compile with four workers.

On the first run, vcpkg may take 30 to 90 minutes and may remain quiet for
several minutes while compiling OpenSSL or SDL2. Subsequent builds should
restore dependencies from the binary cache and compile only changed files.

The packager records a toolchain fingerprint. If it prints this message, the
script found a real toolchain or CMake input change and safely recreated only
the selected native build directory:

```text
Android toolchain requirements changed; recreating ...
```

### APK-Only Restaging

Use this only when `libmain.so` is already current and only payload or packaging
inputs changed:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-android-apk.ps1 -NativeBuildDir build-android-refinery-fix -BuildApk
```

If C++ code changed, always include `-BuildNative`.

### Outputs

```text
Native library:
build-android-refinery-fix\lib\libmain.so

APK:
build-android-apk\app\build\outputs\apk\debug\DuneLegacy.apk

Staged payload:
build-android-payload
```

Optional Dune2R remastered asset packs are not embedded in the APK. They remain
downloadable through the in-game asset workflow, which keeps the APK smaller.

### Verify And Install

Check that a device is visible, then replace the installed test APK:

```powershell
adb devices
adb install -r '.\build-android-apk\app\build\outputs\apk\debug\DuneLegacy.apk'
```

Verify the local debug signature:

```powershell
$buildTools = Get-ChildItem "$env:ANDROID_HOME\build-tools" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
& "$($buildTools.FullName)\apksigner.bat" verify --verbose '.\build-android-apk\app\build\outputs\apk\debug\DuneLegacy.apk'
```

For a clean runtime log during testing:

```powershell
adb logcat -c
adb logcat | Select-String 'Dune2RActivity|SDL|Dune City|dunecity'
```

Press `Ctrl+C` to stop log capture.

## Quick Daily Sequence

For Windows after editing code:

```powershell
cmake --build build-windows-local --config Release --target dunecity --parallel 6
ctest --test-dir build-windows-local -C Release --output-on-failure
```

For Android after editing code:

```powershell
$env:VCPKG_DOWNLOADS = "$env:LOCALAPPDATA\vcpkg-downloads"
$env:VCPKG_MAX_CONCURRENCY = '1'
powershell -ExecutionPolicy Bypass -File .\scripts\package-android-apk.ps1 -NativeBuildDir build-android-refinery-fix -BuildNative -NativeBuildJobs 4 -BuildApk
```

Package Windows only when it is ready to copy elsewhere. Build Android only
when the change needs device-specific verification; most C++ compile errors are
cheaper to catch with the Windows build and test suite first.

## Troubleshooting

### `Can't create ... gawk.exe` Or `ld.exe`

The vcpkg downloads/tools directory is on exFAT. Set the NTFS override in the
same PowerShell window and rerun:

```powershell
$env:VCPKG_DOWNLOADS = "$env:LOCALAPPDATA\vcpkg-downloads"
```

### `Trying to rename Makefile ... Permission denied`

OpenSSL's install jobs raced on exFAT. Serialize vcpkg and rerun the same build:

```powershell
$env:VCPKG_MAX_CONCURRENCY = '1'
```

Once OpenSSL has been stored in the binary cache, future native directories
can restore it instead of rebuilding it.

### The Build Is Quiet For Several Minutes

OpenSSL, SDL2, payload copying, and ZIP compression can be quiet. Check whether
the process is still active before cancelling it:

```powershell
Get-Process cmake,ninja,make,clang,java -ErrorAction SilentlyContinue | Select-Object ProcessName,Id,CPU,StartTime
```

### CMake Cache Refers To An Old Checkout

Use a new build-directory name and configure there. Do not delete source files
or reset the repository:

```powershell
cmake -S . -B build-windows-local-2 -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DDUNECITY_BUILD_TESTS=ON
```

For Android, pass a new `-NativeBuildDir` value to the packaging script.

### APK Installs But Old Data Appears

Confirm that the APK version in `android-version.json` changed when appropriate.
The app preserves user configuration and custom files while refreshing managed
payload files. For debugging only, the staged payload can be pushed manually:

```powershell
adb push '.\build-android-payload\.' /sdcard/Android/data/net.dunecity.dune2r/files/
```

## Before Publishing

Local test builds do not require a version bump. Before an actual release:

1. Confirm the intended desktop and Android versions.
2. Run the Windows tests.
3. Test the Windows ZIP and Android APK locally.
4. Review `git status` and `git diff --check`.
5. Commit only the intended source and documentation.
6. Publish only after explicit approval.

The broader cross-platform and release notes remain in `BUILD.md`,
`ANDROID_PORT.md`, and the `releases/` directory.
