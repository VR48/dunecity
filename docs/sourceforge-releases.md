# SourceForge release mirror

GitHub remains the source of truth and builds desktop releases. The **Sync
SourceForge release** workflow runs after a successful stable-tag **Build Dune
Legacy** workflow. It can also be dispatched manually with a published tag, such
as `v1.0.612`, to backfill or retry without rebuilding the game.

## Verified deployment

Configured and first published on 2026-09-10. [Run 34416472472](https://github.com/VR48/dunecity/actions/runs/34416472472)
successfully mirrored [1.0.612](https://sourceforge.net/projects/dunelegacy/files/dunecity/1.0.612/),
initially verified nine files, pushed the source refs and confirmed all three OS defaults.
The setup below is retained for credential rotation or migration.

On 2026-09-10 Stefan requested removal of the source archive. Current policy is
**eight files: six packages, README with a tagged Git source link, SHA256SUMS**.
The archive is excluded from future runs; source branch/tag mirroring continues.
See [release-operations.md](release-operations.md) for the cross-repository checklist.

## Published layout

- Files: `dunelegacy` project, `dunecity/<version>/` directory.
- Six unchanged GitHub desktop packages, `README.md`
  release notes and `SHA256SUMS`.
- Source: existing `ssh://USER@git.code.sf.net/p/dunelegacy/code` repository,
  dedicated `dunecity` branch and `dunecity-vX.Y.Z` tags.
- The release workflow never force-pushes, deletes old releases or changes Legacy master.
  Separately authorized website edits can advance Legacy master.

Every uploaded file is downloaded through authenticated rsync and SHA256 checked
before source refs or download defaults are changed. Only the current GitHub
latest stable release advances the branch and Windows/macOS/Linux defaults.
Historical backfills publish files and a namespaced source tag only. Runs are
serialized. Failed verification leaves download defaults unchanged.

## One-time setup

A SourceForge account needs file release and Git write access to the Dune Legacy
project. Create the `dunecity` parent directory in the project's Files interface.
Create a dedicated SSH key for this automation and add its public key to that
SourceForge account. Do not reuse unrelated deployment keys.

In GitHub repository Settings → Secrets and variables → Actions, configure:

| Kind | Name | Value |
| --- | --- | --- |
| Variable | `SOURCEFORGE_USER` | SourceForge username |
| Secret | `SOURCEFORGE_SSH_KEY` | Dedicated SSH private key |
| Secret | `SOURCEFORGE_KNOWN_HOSTS` | Verified SSH host entries for `frs.sourceforge.net` and `git.code.sf.net` |
| Secret | `SOURCEFORGE_API_KEY` | Account's Releases API Key |

Check SSH fingerprints against SourceForge's published host keys before trusting
host entries. The workflow enforces strict host-key checking. Enter secrets
directly into GitHub settings or via `gh secret set`; never paste them into chat,
commit them, or include them in logs. Account login alone does not provide CI
credentials.

Then run **Sync SourceForge release**, tag `v1.0.612`. A missing credential fails
explicitly and does not affect the completed GitHub release or website deploy.
After success, inspect the SourceForge Files page and platform download defaults.
GitHub Actions logs include checksum verification and confirmed default filenames.

## Local preparation and tests

Run from the repository with authenticated GitHub CLI:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_sourceforge_release.py'
python3 scripts/sourceforge-release.py v1.0.612 --directory /tmp/sourceforge-1.0.612
```

The output directory must not already exist. Without `--publish`, this only
prepares a bundle. Publication additionally needs the environment variables above
and `GIT_SSH_COMMAND` pointing to the dedicated key and pinned known-hosts file.
Prefer the workflow for publication so concurrent uploads are serialized.

Retry by dispatching the same tag. rsync checks content and resumes the mirror;
it never removes historical releases. An existing conflicting source tag or a
non-fast-forward branch stops publication for investigation rather than forcing it.

## References

- [SourceForge file releases](https://sourceforge.net/p/forge/documentation/Release%20Files%20for%20Download/)
- [SourceForge release API](https://sourceforge.net/p/forge/documentation/Using%20the%20Release%20API/)

## SourceForge project presentation and legacy website

On 2026-09-10 the project display name became **Dune Legacy & Dune City**
(shortname stays `dunelegacy`). Metadata now describes Dune City and Vanilla,
links to dunelegacy.com and lists current city/RTS features. SourceForge reuses
this description on its download overview page; both were verified in-browser.

The legacy website is also in the old SourceForge `master` checkout at
`/Users/stefan/Documents/projects/dunelegacy-code/sourceforge_website`.
Commit `dc69c5a` replaces the stale downloads page with a Dune City introduction,
a version-independent latest-download link, platform selection on the main site,
installation help and classic-release archive links. The same page is retained
in this GitHub repository's `sourceforge_website` directory.

Web hosting uses SFTP `svan058@web.sourceforge.net`, path
`/home/project-web/dunelegacy/htdocs/website/`. The dedicated deployment key works;
the web host ED25519 fingerprint was verified against SourceForge documentation.
Upload only intended files, then read back and compare bytes. This deployment
used a temporary filename and rename; the prior HTML is backed up locally at
`/tmp/dunecity-sf-web-backup/downloads.html`. No PHP settings changed.

The CDN may briefly show the old page at the plain URL. The deployed HTML was
verified by SFTP readback and browser rendering with `?updated=20260910`.
