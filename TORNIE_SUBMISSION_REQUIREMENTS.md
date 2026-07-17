# Tornie Mod Submission Requirements

Status: mandatory for all Tornie Mod submissions after version 1.0.522.

Repository of record: <https://github.com/VR48/dunecity>

This document is an acceptance contract for Tornie Mod updates. Give this file
to the Codex agent preparing the update and instruct it to satisfy every
requirement before producing a pull request or package.

## Goal

Tornie must remain a selectable, self-contained mod. Enabling Tornie may change
Tornie gameplay and content. Disabling it must restore the selected base mode
without leaving Tornie assets, rules, campaigns, or configuration active.

A Tornie update must not damage or replace:

- Dune Legacy behavior;
- DuneCity behavior or save compatibility;
- Dune2R content or graphics;
- Windows packaging;
- Android packaging, touch controls, storage, TLS, or save behavior;
- another selectable mod.

## Repository Workflow

1. Use GitHub's **Fork** action on `VR48/dunecity`. Do not create a disconnected
   copy of the repository for integration work.
2. Configure `VR48/dunecity` as the `upstream` remote.
3. Start every update from the latest accepted `upstream/main`.
4. Create a short-lived branch named `mod/tornie-<version>`.
5. Rebase or merge the latest `upstream/main` before final testing.
6. Submit a pull request to `VR48/dunecity:main`.
7. State the exact upstream commit SHA used as the base.

Example:

```bash
git remote add upstream https://github.com/VR48/dunecity.git
git fetch upstream
git switch -c mod/tornie-1.0.523 upstream/main
```

A whole-game installer or portable game ZIP is a test artifact, not an
acceptable source submission. A disconnected repository snapshot is also not
an acceptable integration submission because it hides the true diff and makes
upstream synchronization unreliable.

## Allowed Content-Only Paths

A normal Tornie content update may change only:

```text
mods/Tornie/**
```

The complete mod must be present under that directory, including every asset
needed by the submitted version. Do not assume that an asset from an earlier
submission remains installed.

Expected layout:

```text
mods/Tornie/
  mod.ini
  ObjectData.ini
  GameOptions.ini                 # when used
  QuantBotConfig.ini              # when used
  campaign/
  data/
    Tornie.PAK                    # when the mod uses a PAK
    <required loose mod assets>   # when the engine loads them by filename
  licenses/
  ASSET-MANIFEST.sha256
  CHANGELOG.md
```

Folder and filename case must match the source references exactly. This matters
on Android and Linux even when a mismatched name happens to work on Windows.

## Prohibited Changes

A content-only Tornie submission must not modify or replace:

```text
src/**
include/**
tests/**
data/**
config/**
mods/Dune2R/**
mods/DuneCity/**
CMakeLists.txt
src/CMakeLists.txt
android-version.json
ANDROID_PORT.md
scripts/package-android-apk.ps1
.github/workflows/**
```

It must not place Tornie files in the executable directory, root `data/`,
Android application root, user configuration directory, or another mod.

It must not:

- make Tornie the default mod;
- change global defaults to emulate Tornie;
- alter save or network formats without an approved engine change;
- overwrite a user's selected mod or configuration;
- depend on absolute paths or a developer's local files;
- include generated build directories, credentials, logs, save files, or crash
  dumps;
- silently depend on files present only in a previous Tornie release;
- bundle Dune II proprietary game data unless redistribution permission is
  documented.

## Engine Changes

If Tornie needs behavior that the current mod API cannot express, stop the
content-only submission and prepare two separate pull requests:

1. **Engine/API pull request:** a generic, optional capability based on current
   `VR48/dunecity:main`, with focused tests and no forced Tornie behavior.
2. **Tornie content pull request:** files under `mods/Tornie/**` that use the
   accepted engine capability.

An engine change must preserve Vanilla, DuneCity, Dune2R, Windows, Android,
existing saves, and multiplayer compatibility unless the maintainers explicitly
approve a documented format or protocol migration.

Do not hard-code a Tornie asset as mandatory during global graphics
initialization. Missing optional mod content must produce a clear mod validation
error or use an intentional base-game fallback; it must never dereference a null
surface or crash another mode.

## Asset Completeness

The submission must contain:

- every campaign and region file referenced by the mod;
- every sprite, icon, palette, sound, and PAK referenced by Tornie code or data;
- all source-generated output required at runtime;
- an `ASSET-MANIFEST.sha256` covering every runtime file under
  `mods/Tornie/`;
- a short provenance/license entry for each original or third-party asset set;
- a changelog listing added, changed, renamed, and removed files.

If `Tornie.PAK` is generated, include the deterministic source inputs and use
the repository's accepted packer. Verify that the PAK contains every expected
entry. Loose files required by the loader must also be present in
`mods/Tornie/data/`; a whole-game ZIP containing only campaigns is not a
complete mod submission.

Do not duplicate base assets merely to make a full game package. Reference
base-game assets through supported fallbacks, and package only Tornie-owned or
properly licensed additions.

## Required Diff Audit

Before submission, run:

```bash
git fetch upstream
git diff --name-status upstream/main...HEAD
git diff --check upstream/main...HEAD
```

For a content-only release, the first command must list only
`mods/Tornie/**`. Any other path makes the submission fail review until it is
removed or moved into a separate approved engine pull request.

Also inspect for local paths and secrets:

```bash
git grep -n -I -E 'C:\\Users\\|/Users/|/home/|BEGIN .*PRIVATE KEY|token=' -- mods/Tornie
```

## Required Test Matrix

All checks must use a clean checkout or clean staging directory. Passing a test
against an old installation does not prove that the package is complete.

### Mode Isolation

Test each selectable mode after launching, returning to the menu, switching
modes, and relaunching:

- Vanilla/Dune Legacy;
- DuneCity;
- Dune2R;
- Tornie.

Each mode must reach its main menu and start a game without missing-file,
graphics, sound, or null-surface errors. Tornie content must appear only when
Tornie is selected.

### Windows x64

- Configure and build Release from the repository root.
- Stage a fresh portable directory using the repository packaging rules.
- Confirm the staged directory contains the complete `mods/Tornie` tree.
- Launch with Tornie active.
- Start one campaign and one skirmish.
- Open the map editor and display every Tornie unit/structure palette.
- Save, exit, relaunch, and load the save.
- Repeat a smoke launch with DuneCity and Dune2R active.

### Android ARM64

- Build with the repository's Android packaging script.
- Install as a clean or version-upgrade installation on Android 13 or newer.
- Confirm the payload contains the complete `mods/Tornie` tree with exact case.
- Launch Tornie and start one campaign and one skirmish.
- Verify touch, simulated right-click, Android Back/Escape, soft keyboard,
  save/load, and landscape orientation.
- Switch to DuneCity and Dune2R and confirm both still launch.

### Automated Tests

- Run the repository unit-test target.
- Run package-content validation for Windows and Android.
- Fail the build when a required manifest file is absent, has the wrong case,
  has the wrong hash, or is missing from either package.
- Fail the build if a content-only diff touches a prohibited path.

## Submission Deliverables

Every submission must provide:

1. A pull request based on current `VR48/dunecity:main`.
2. The upstream base commit SHA and Tornie commit SHA.
3. A content-only ZIP whose archive root is `mods/Tornie/`.
4. `ASSET-MANIFEST.sha256`.
5. `CHANGELOG.md`.
6. Build and test results for Windows x64 and Android ARM64.
7. SHA-256 hashes for supplied archives.
8. A list of any known limitations.
9. Confirmation that Vanilla, DuneCity, and Dune2R were tested after Tornie.

Release executables may be supplied for testing, but they do not replace the
pull request or mod-only package.

## Acceptance Rule

The maintainers will reject or return a Tornie submission when any mandatory
item above is missing. The maintainers will not reconstruct a complete mod from
a whole-game binary release, infer hidden engine changes, recover omitted
assets from old releases, or manually separate Tornie changes from DuneCity,
Dune2R, or Dune Legacy.

No Tornie update is merged or released until the clean Windows and Android
packages pass the full test matrix.

## Licensing and Attribution

This project is GPL-2.0 licensed. A separate public repository is not
automatically unlawful merely because GitHub does not label it as a fork.
However, anyone distributing modified binaries must satisfy the applicable
license obligations, preserve required notices and attribution, and provide the
corresponding source in the manner required by the license.

GitHub fork lineage is required here as a project contribution policy because
it preserves history and makes review and synchronization reliable. It is not,
by itself, the legal test for GPL compliance.

This document is a project acceptance policy, not legal advice.

