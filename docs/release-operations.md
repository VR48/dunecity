# Release and hosting operations

Verified 2026-09-10. This is the entry point for Codex, Claude, Hermes and human
maintainers. Check current branches, workflows and release state before acting;
version numbers and run IDs below are verification examples, not next-version defaults.

## Repositories and destinations

| Purpose | Repository / local checkout on Stefan's MacBook Air | Destination |
| --- | --- | --- |
| Game and release automation | `VR48/dunecity`; `~/Documents/projects/dunecity` | GitHub Releases; SourceForge Files and dedicated source refs |
| Main website | `VR48/dunelegacy.com`; `~/Documents/projects/dunelegacy.com` | https://dunelegacy.com via **Deploy to Droplet** |
| Historical SourceForge repository | `ssh://svan058@git.code.sf.net/p/dunelegacy/code`; `~/Documents/projects/dunelegacy-code` | `master` holds Legacy history/old website; `dunecity` holds latest released game source |

The legacy website files are `sourceforge_website/` in both game checkouts.
Main website files are `website/` in the separate website repository. Do not
confuse pushing website source with publishing it to SourceForge web hosting.

## An authorized desktop release

1. Inspect dirty files and current tags; preserve other work. Follow `AGENTS.md`
   and `CLAUDE.md` version/build rules. Use `scripts/bump-version.sh` and its
   `--check` mode; commit all intended source before tagging.
2. If a local build was requested, build and verify it separately. Remote CI
   success does not update the app on Stefan's Mac. Use dependency audits and
   CTest as described in `AGENTS.md`; do not launch a game merely to check version.
3. Push the authorized release and its `vX.Y.Z` tag. **Build Dune Legacy** in
   `.github/workflows/build.yml` gates publication on tests and Windows, Linux
   and macOS success. Verify all six assets: ZIP, DMG, AppImage, DEB, RPM, tar.gz.
4. The release job updates version/download links in the separate website repo's
   `website/index.html` and `website/dune-city.html`, using `WEBSITE_DEPLOY_KEY`.
   Watch **Deploy to Droplet** there and check the live pages. Release prose is
   not guaranteed to be rewritten by version replacement; review it explicitly.
5. A successful stable-tag build triggers **Sync SourceForge release** in
   `.github/workflows/sourceforge.yml`. Verify its success separately. It copies
   the six existing assets without rebuilding, adds README and SHA256SUMS,
   reads uploads back to verify hashes, publishes `dunecity-vX.Y.Z`, advances
   SourceForge's `dunecity` branch and changes the three OS defaults.
6. Report only destinations actually verified. Checksum/upload success, source
   refs, download defaults, main website deployment and local app are distinct.

**Source policy:** no source archive in SourceForge Files. README links to the
matching GitHub tag; source remains available in Git. Never force-push Legacy
master or mirror-delete historic releases. Website-only updates to Legacy master
are separate deliberate changes, such as `dc69c5a`.

## Retry without rebuilding

```sh
gh workflow run sourceforge.yml --repo VR48/dunecity --ref main -f tag=vX.Y.Z
gh run list --repo VR48/dunecity --workflow sourceforge.yml --limit 5
gh run watch RUN_ID --repo VR48/dunecity --exit-status
```

Dispatch only an existing published stable tag. Historical backfills do not
change current download defaults or the source branch. A failed SourceForge run
does not roll back a GitHub release. Fix its specific failure and rerun the mirror;
do not create a new game version merely to retry an upload. Strict host checking,
missing secrets, conflicting tags or non-fast-forward refs must be investigated.
Infrastructure/docs-only pushes can use `[skip ci]` to avoid starting a game build;
do not skip required CI for game changes. SourceForge workflow_run dispatch occurs
only after successful stable-tag builds, not such documentation pushes.

## SourceForge presentation and web hosting

See [sourceforge-releases.md](sourceforge-releases.md) for credential **names**,
SSH transport, metadata, paths and evidence. The project description in
Admin → Metadata feeds both the project overview and download landing page.
Project display name is **Dune Legacy & Dune City**; shortname remains `dunelegacy`.

The old URL `https://dunelegacy.sourceforge.net/website/downloads.html` is a static
HTML page served separately from the main website. Its evergreen download link
uses SourceForge's platform default, so a normal release needs no version edit.
For content changes: edit/commit both tracked copies, push the intended repository
branches, back up the current remote file, SFTP only the changed file to a temporary
name under `/home/project-web/dunelegacy/htdocs/website/`, rename it into place,
read back and compare bytes, then inspect the public page. CDN caching can briefly
show old HTML; a cache-busting query helps distinguish that from a failed upload.
Keep old manuals/maps and other files. No PHP upgrade is needed for static HTML.

## Knowledge maintenance

Keep this runbook and SourceForge guide authoritative in Git; entry links exist
in both `AGENTS.md` and `CLAUDE.md`. Put dated outcomes in `HANDOVER.md`, correcting
stale current-state claims. When available, retain verified outcomes in Codex
context-memory and deliberately promote shared engineering knowledge; memory is
an index to evidence, not a replacement for repository documentation. Never put
private keys, API tokens, raw secrets or browser session data into docs or memory.
