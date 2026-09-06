# macOS release runner

DuneCity's macOS DMG is built by a repository-level self-hosted GitHub Actions
runner. Both the MacBook and Mac mini can use the `dunecity-builder` label; an
online, idle runner claims each job.

The workflow deliberately excludes pull requests. A self-hosted runner executes
repository code on the host, so only pushes by maintainers, version tags, and
manual workflow runs may use it.

## One-time host setup

1. Install the native tools:

   ```bash
   xcode-select --install
   brew install autoconf autoconf-archive automake cmake libtool pkg-config nasm ninja
   ```

2. In GitHub, open **Settings → Actions → Runners → New self-hosted runner** for
   `VR48/dunecity`. Install the ARM64 macOS runner outside the repository and use
   these configuration values:

   ```text
   Runner group: Default
   Name: dunecity-macbook-air or dunecity-mac-mini
   Extra label: dunecity-builder
   Work folder: _work
   ```

3. Install and start its LaunchAgent from the runner directory:

   ```bash
   ./svc.sh install
   ./svc.sh start
   ./svc.sh status
   ```

4. Set the repository Actions variable `ENABLE_MACOS_BUILD` to `true`. Set it to
   `false` before taking every Mac runner offline for an extended period, or
   release workflows will wait for the Mac job until its timeout.

The service runs as the user that configured it. Keep that account logged in
after reboot and disable automatic sleep while it is acting as a runner.

## What the job produces

The runner builds the ARM64 app with vcpkg, stages non-system dylibs inside
`dunecity.app/Contents/Frameworks`, applies an ad-hoc signature, and creates
`DuneCity-X.Y.Z-macOS.dmg`. The verification step mounts the DMG and checks:

- app metadata and version match the source version;
- the executable has an ARM64 slice;
- no Homebrew, user-directory, or vcpkg build paths remain;
- the app bundle has a valid code signature and required game data;
- the drag-to-Applications link exists.

The DMG is uploaded as the `DuneCity-macOS-DMG` workflow artifact. On a `vX.Y.Z`
tag, the existing release job attaches it to the GitHub Release and updates the
dunelegacy.com download links after publishing.

## Release sequence

Run a manual workflow on the release commit before tagging it. Once its macOS
job passes:

```bash
scripts/bump-version.sh X.Y.Z
git add CMakeLists.txt include/config.h vcpkg.json
git commit -m "release: prepare DuneCity X.Y.Z"
git push origin main
git tag vX.Y.Z
git push origin vX.Y.Z
```

The source version must be committed before the tag. The tag workflow rejects a
version mismatch and publishes the website only after the GitHub Release exists.
