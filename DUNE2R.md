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
