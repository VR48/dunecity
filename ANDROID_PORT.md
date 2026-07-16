# Android Port Notes

Dune2R/DuneCity is SDL2-based. The Android path uses the SDL Android activity
wrapper and builds the native game as the app's `libmain.so` shared library.

Known external references:

- Official Dune Legacy releases currently list Windows and macOS packages, but
  no Android APK release.
- `caiiiycuk/dunedroid` is an older SDL 1.2/1.3 Android porting tree for
  legacy games. It is useful background, but should not be used directly for
  this SDL2 codebase.
- SDL2 supports Android through its Android project skeleton / SDLActivity
  flow.

## Touch Mapping

The game remains mouse-driven internally. `TouchInput` translates touch events
into mouse events before normal UI/game handling:

```text
single tap                            -> left click
single drag                           -> left mouse drag
hold one finger + tap a second finger -> right click at the first finger
Android Back button                   -> Escape
```

This gives Android the existing desktop semantics:

```text
left click / tap      select, press UI buttons, place buildings
left drag             box-select units
right click / press   action command, move/attack/cancel contextual cursor
```

The next control layer should add explicit map panning. A safe default is
two-finger drag for camera pan, leaving one-finger drag for selection boxes.

## Required Tooling

The PC needs these discoverable by Android Studio or environment variables:

```text
Android SDK
Android NDK
CMake for Android
JDK
Gradle or Android Gradle Plugin wrapper
```

Expected variables:

```text
ANDROID_HOME or ANDROID_SDK_ROOT
ANDROID_NDK_HOME, or an NDK installed under the SDK's ndk/ directory
JAVA_HOME
```

Load these paths in PowerShell with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-env.ps1
```

Or in `cmd.exe` without changing PowerShell execution policy:

```bat
call scripts\android-env.cmd
```

Dune2R builds a native Android shared library and debug APK with this toolchain.
Runtime was verified on an Android 13 Ulefone Armor 21 over ADB.

## Verified Build Status

Native library:

```text
build-android-arm64-ndk/lib/libmain.so
```

APK:

```text
GitHub Release asset: DuneLegacy.apk
```

Data/config/mod payload:

```text
build-android-payload/
```

The debug APK also embeds this payload under `assets/dune2r_payload/` and the
generated `Dune2RActivity` copies it to Android app storage before launching
the native SDL game. The payload marker uses the Android release version, so
installing a newer APK refreshes bundled game and mod files while preserving
the user's `config/Dune City.ini` and any additional custom files.

Android package versions are maintained independently in
`android-version.json`. Increment `versionCode` for every distributed APK and
use `versionName` for the public Android release number; do not change the
DuneCity project version merely to publish an Android update.

## Upstream Payload Policy

Stefan's `svan058/dunecity` `tornie` branch is the source of truth for the
DuneCity base, Tornie mod, configuration, and data files. Before an Android
release, merge that branch into `feature/dune2r-mod`; the packager then copies
`data/`, `config/`, and `mods/` from the merged checkout. Dune2R and the Android
runtime adaptations are the only layers maintained by this fork. Do not copy
payload files from a separate desktop installation or keep a second Android-
specific copy of Stefan's files.

Verified runtime behavior:

```text
landscape launch and scaling
single-finger tap / click alignment
Android Back as Escape
two-finger right-click gesture
```

## Build Commands

Configure and build the native Android library:

```bat
call scripts\android-env.cmd
cmake -S . -B build-android-arm64-ndk -G Ninja -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=%ANDROID_NDK_HOME%/build/cmake/android.toolchain.cmake -DVCPKG_TARGET_TRIPLET=arm64-android -DVCPKG_HOST_TRIPLET=x64-windows -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-arm64-ndk --target dunecity -- -j 4
```

Stage and build the debug APK:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-android-apk.ps1 -BuildApk
```

Install and push the staged payload after a device is attached:

```bat
adb install -r "DuneLegacy.apk"
```

The generated APK stages the bundled payload automatically on first launch.
For debugging, the payload can still be pushed manually:

```bat
adb push "build-android-payload\." /sdcard/Android/data/net.dunecity.dune2r/files/
```

The native code searches Android app storage and its `data/` subdirectory. The
payload contains:

```text
config/
data/
mods/
imported_sprites/
```

## Remaining Work

1. Add touch camera panning and possibly a small command overlay for mobile.
2. Keep the original Dune II data licensing boundary explicit. The APK should
   not include copyrighted data unless the distributor has rights to bundle it.
