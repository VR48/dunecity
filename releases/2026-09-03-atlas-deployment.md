# Dune2R Atlas Deployment

- Add Oathkeeper's `~dune2atlas [target]` browser with class, house and coverage
  filters, paginated original/remastered previews, and explicit source binding.
- Decode vanilla references from local game data. Building slab diagrams use
  engine footprint definitions; reference art is not included in published packs.
- Save bound building footprints and concrete-slab guides into dune2config's
  authoring profile. Chimneys and smoke can extend above the ground footprint.
- Install visual packs into the local Windows mod without changing the source
  checkout. Validate PNGs, atlas geometry, target identity and building footprint.
- Retain prior local revisions for rollback. Replacing a source keeps one pack
  owning that target, rather than registering competing replacements.
- Require Manage Server/Administrator, a configured Dune2 channel, and separate
  confirmation for publishing the exact locally installed revision.
- Publish only the selected pack and refreshed catalog, using an isolated Git
  index. Never stage unrelated work, move the local branch, or force push.
- Build catalogs from committed Git trees, not potentially dirty local folders.
- Add REFRESH to the game's ASSETS menu. Fetch only the official catalog, require
  immutable official asset URLs, enforce file/type/size limits, and preserve the
  previous catalog on a failed update or while offline.
- Persist the online catalog and local rollback packs across managed-mod updates.
- Keep gameplay data, QuantBot configuration, multiplayer rules, other mods,
  and the previously verified Refinery scaling unchanged.

## Workflow

1. Open `~dune2atlas`, or `~dune2atlas Refinery`, in the Dune2 config channel.
2. Select a target and house. For terrain, the binding is shared across houses.
3. Select the source asset, then Connect / Replace. This does not publish.
4. Install Locally and test the game. Rollback restores the previous local pack.
5. Publish only after testing, and confirm the separate publication dialog.
6. Players use Dune2R EditoR > ASSETS > REFRESH, then DOWNLOAD the selected pack.

The first catalog-refresh build is required once. Later visual assets supported
by the existing renderer do not require new binaries. Specialized mod renderers
are listed but not writable until their rendering/binding support is verified.
The Discord browser reports the bot computer's local installation, not the
contents of every player's device. Original assets and raw generation inputs
are not uploaded by publishing. Publishing occurs only from the confirmation
button; installing this feature does not publish a new art revision.

Android build: 0.2.20-atlas-test (1000540).
