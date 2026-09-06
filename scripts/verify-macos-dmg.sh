#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <DuneCity-X.Y.Z-macOS.dmg>" >&2
    exit 2
fi

DMG_PATH="$1"
if [[ ! -f "$DMG_PATH" ]]; then
    echo "DMG does not exist: $DMG_PATH" >&2
    exit 1
fi

SOURCE_VERSION=$(sed -n 's/^project(DuneCity VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt)
EXPECTED_NAME="DuneCity-${SOURCE_VERSION}-macOS.dmg"
if [[ "$(basename "$DMG_PATH")" != "$EXPECTED_NAME" ]]; then
    echo "Expected $EXPECTED_NAME, got $(basename "$DMG_PATH")" >&2
    exit 1
fi

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/dunecity-dmg-verify.XXXXXX")
MOUNT_POINT="$WORK_DIR/mount"
mkdir -p "$MOUNT_POINT"
MOUNTED=0

cleanup() {
    if [[ "$MOUNTED" -eq 1 ]]; then
        hdiutil detach "$MOUNT_POINT" -quiet || hdiutil detach "$MOUNT_POINT" -force -quiet || true
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

# CPack embeds the repository license as a DMG software license agreement.
# Accept it non-interactively so verification works in GitHub Actions.
if ! printf 'Y\n' | PAGER=cat hdiutil attach "$DMG_PATH" -nobrowse -readonly -mountpoint "$MOUNT_POINT" \
        >"$WORK_DIR/hdiutil-attach.log" 2>&1; then
    tail -n 30 "$WORK_DIR/hdiutil-attach.log" >&2
    exit 1
fi
MOUNTED=1

APP_PATH="$MOUNT_POINT/dunecity.app"
INFO_PLIST="$APP_PATH/Contents/Info.plist"
EXECUTABLE="$APP_PATH/Contents/MacOS/dunecity"

[[ -d "$APP_PATH" ]] || { echo "dunecity.app is missing from the DMG" >&2; exit 1; }
[[ -L "$MOUNT_POINT/Applications" ]] || { echo "Applications link is missing from the DMG" >&2; exit 1; }
[[ -x "$EXECUTABLE" ]] || { echo "App executable is missing or not executable" >&2; exit 1; }
[[ -f "$APP_PATH/Contents/Resources/DUNE.PAK" ]] || { echo "DUNE.PAK is missing from app resources" >&2; exit 1; }

PLIST_EXECUTABLE=$(plutil -extract CFBundleExecutable raw "$INFO_PLIST")
PLIST_IDENTIFIER=$(plutil -extract CFBundleIdentifier raw "$INFO_PLIST")
PLIST_SHORT_VERSION=$(plutil -extract CFBundleShortVersionString raw "$INFO_PLIST")
PLIST_BUILD_VERSION=$(plutil -extract CFBundleVersion raw "$INFO_PLIST")

[[ "$PLIST_EXECUTABLE" == "dunecity" ]] || { echo "Unexpected CFBundleExecutable: $PLIST_EXECUTABLE" >&2; exit 1; }
[[ "$PLIST_IDENTIFIER" == "net.dunecity.DuneCity" ]] || { echo "Unexpected CFBundleIdentifier: $PLIST_IDENTIFIER" >&2; exit 1; }
[[ "$PLIST_SHORT_VERSION" == "$SOURCE_VERSION" ]] || { echo "Short version $PLIST_SHORT_VERSION does not match $SOURCE_VERSION" >&2; exit 1; }
[[ "$PLIST_BUILD_VERSION" == "$SOURCE_VERSION" ]] || { echo "Build version $PLIST_BUILD_VERSION does not match $SOURCE_VERSION" >&2; exit 1; }

codesign --verify --deep --strict "$APP_PATH"

ARCHITECTURES=$(lipo -archs "$EXECUTABLE")
[[ " $ARCHITECTURES " == *" arm64 "* ]] || { echo "arm64 slice is missing: $ARCHITECTURES" >&2; exit 1; }

NON_PORTABLE=""
while IFS= read -r BINARY; do
    if ! file "$BINARY" | grep -q 'Mach-O'; then
        continue
    fi

    BAD_PATHS=$(otool -L "$BINARY" | tail -n +2 | awk '{print $1}' | \
        grep -E '^(/Users/|/opt/homebrew/|/usr/local/)|vcpkg_installed' || true)
    if [[ -n "$BAD_PATHS" ]]; then
        NON_PORTABLE+="$BINARY:"$'\n'"$BAD_PATHS"$'\n'
    fi
done < <(printf '%s\n' "$EXECUTABLE"; find "$APP_PATH/Contents/Frameworks" -type f -print)

if [[ -n "$NON_PORTABLE" ]]; then
    echo "Bundle contains non-portable library paths:" >&2
    printf '%s' "$NON_PORTABLE" >&2
    exit 1
fi

echo "Verified $EXPECTED_NAME: arm64, portable dylibs, valid bundle metadata and code signature."
