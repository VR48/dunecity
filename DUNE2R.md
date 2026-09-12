# Dune2R

Mod author: Vuk Ristic / VR48

Base project credits:

- DuneCity is built on `svan058/dunecity`, a hybrid RTS/city-builder fork of
  Dune Legacy with Micropolis-style city simulation integrated into the game
  loop, built by Stefan van der Wel (svan058)!
- Dune Legacy credits include Stefan van der Wel, Anthony Cole, Richard
  Schaller, Olaf van der Spek, Raal Goff, Stefen Hendriks, Felix Medrano, and
  other contributors listed in the upstream `AUTHORS` file.
- Dune2R is intended as a separate graphics/mod layer on top of that work, not
  a replacement of the DuneCity or Dune Legacy project identity.

Dune2R is a visual modernization mod for DuneCity. The goal is to preserve the
classic Dune II / Dune Legacy + Dune City gameplay model while adding optional replacement
graphics that can be selected as a normal in-game mod.

The project has two graphics targets:

- Compact graphics: Dune II-compatible sprite strips produced by the
  `~dune2config` / Dune2 Compact pipeline. These are intended as safe drop-in
  replacements for classic unit and structure art.
- Enhanced graphics: high-resolution animated assets with explicit anchors,
  direction metadata, and state-specific rendering for a "Resurrected" style
  presentation layer.

Dune2R should remain gameplay-compatible with the base game wherever possible.
Unit identity, pathing, targeting, economy, save data, and simulation behavior
should stay intact; the mod should primarily change how the world is presented.
Commander units will be added later with ability to generate custom sprites.

The long-term vision is a toggleable graphics layer: classic art, compact
replacement art, or enhanced animated art, with graceful fallback when a given
unit or direction has not yet been remastered.

Enhanced delivery uses independently downloadable packs under
`mods/Dune2R/graphics_hd/units/**`. A pack may contain `unit.ini`,
`building.ini`, or `tile.ini`: units map directional states, buildings map
footprint-anchored phase states, and terrain maps all 16 cardinal-neighbor
topologies. The engine never replaces base-game asset files. Missing states,
animations, or complete packs fall through to an authored still and then to
the classic renderer.

Oathkeeper keeps editable source sprites, animations, alignment, and timing in
its own `dune2/units/<unit>` authoring cache. `~dune2mount <UnitName>` converts
that material into the runtime manifests and atlases above. It prefers authored
full-unit products and otherwise composes completed chassis and turret layers,
so adding or refreshing unit art is normally data-only and needs no game
rebuild. Runtime mounts are committed to Git for collaborators and release-pack
generation, but the large atlases are excluded from the base Windows, Linux,
and Android packages. Raw authoring material is not shipped.

When Dune2R is active, the in-game `Classic` / `Dune2R` control crossfades the
classic world and the enhanced world. The preference and its fade timing are
local presentation state only; they do not enter saves or the multiplayer
protocol. The main menu also exposes `Dune2R EditoR`. It lists only
units and directional motion slots that have packaged Dune2R assets. Each slot
can use the independent layered renderer, its packaged complete-unit animation,
or a deterministic random choice refreshed on movement, facing, stop, and
firing transitions. These are local presentation preferences stored in the
user's `Dune City.ini`; they do not enter saves, simulation state, or the
multiplayer protocol. The editor and its preferences are ignored by Vanilla,
DuneCity, Tornie, and every mod whose exact name is not `Dune2R`.

The editor's `ASSETS` screen installs remastered packs per unit or as an `ALL`
queue. Downloads resume from `.part` files, are verified against the pinned
size and SHA-256 catalog, and are installed atomically into the user's managed
Dune2R mount. A game or managed-mod refresh preserves those installed packs.
Developers can regenerate the version-pinned catalog with
`scripts/generate-dune2r-asset-catalog.py` after committing a new asset set.
