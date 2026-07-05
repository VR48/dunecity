# v1.0.362 — Tracked Items from Tornie's Review (2026-07-05)

This release is a verification-only patch. None of the seven items below
land in v1.0.362; the build bumps the version to 1.0.362 to make the
testing round easier to discuss without confounding with prior tags.

## Items requested by Tornie

| # | Status | Item |
|---|---|---|
| 1 | ✅ verified (no code change) | Mentat MENTATM.CPS for Rebels + Neutral — already in PAK ENTRIES list (offset 298947, 77-entry Tornie.PAK). MENTATA/H/M/O.CPS all packed. |
| 2 | ⏸ awaiting file | Reset Advanced Windtrap tiles + animations. Tornie: *"j'envoie un fichier avec des instructions sous peu"*. |
| 3 | ⏸ awaiting design | Troopers + Infantry in Tornie Mods only. Which approach? (gate via `mods/Tornie/ObjectData.ini`? new mod-only unit entry in build menu? field override?) |
| 4 | ⏸ awaiting zip | Fremen campaign replace. No Fremen campaign ZIP currently in `/root/.hermes/cache/`. Last campaign-related ZIP was `doc_0becb8ff8231_campagne_Rebels.zip` (already integrated as v1.0.352). Need fresh upload. |
| 5 | ⏸ awaiting design | Spectator mode for multiplayer + AI-vs-AI solo. v1 minimum: observer joins as a no-command slot in `ObjectManager`, can't send commands but sees all teams. |
| 6 | ⏸ awaiting design | Color swap for one-player custom maps. v1 minimum: dropdown in `CustomGamePlayers` lets a player pick a foreign house color. Save format version bump required. |
| 7 | ✅ verified (no code change) | Rebels palette = Fremen positions 192-199, Custom_IBM.pal applies the dark-grey ramp. Verified: `houseToPaletteIndex[HOUSE_REBELS = 7]` = `PALCOLOR_REBELS = 192 = PALCOLOR_FREMEN`. Custom_IBM.pal writes 96,80,64,48,32,16,8,0 at indices 192-199. |
| 8 | 🐛 investigated (repro needed) | Editor crashes on open. Pre-existing bug — repro needed locally to identify the nullptr / OOB access. v1.0.359's greyPlace-removal in MapEditor.cpp is not the cause; the code at line 2035 is uniform for every house. |

## What was actually shipped in v1.0.362

- Version bump 1.0.361 → 1.0.362 in `CMakeLists.txt`, `include/config.h`, `vcpkg.json`.
- This roadmap document added to `.github/` so the seven items are tracked.

## What needs to happen for each item

**#2 Windtrap reset**: wait for Tornie's instructions ZIP.

**#3 Troopers in Tornie Mods only**: pick one of:
  - (a) Hard gate via `ModManager.getActiveModName() == "Tornie"` — Troopers only spawnable when Tornie is active.
  - (b) Add new unit entry `TornieTrooper` to `mods/Tornie/ObjectData.ini` with custom properties.
  - (c) Hide Troopers from foreign mods by setting `Enabled != true` in vanilla `ObjectData.ini` and override to `Enabled = true` in Tornie.

**#4 Fremen campaign**: upload the modified `campagne_Fremen.zip` to Discord. Replaces `mods/Tornie/campaign/REGIONF.INI` and 22 `scenf00X.ini`.

**#5 Spectator mode**: design choice needed:
  - (a) 9th slot in `NUM_HOUSES`, can be AI-only with no UI.
  - (b) Separate `SpectatorPlayer` class extending `Player` with no commands.
  - (c) Hotkey toggle that toggles `pLocalHouse` to `nullptr` and shows all tiles.

**#6 Color swap**: design choice needed:
  - (a) Single-instance runtime only (no save integration), gate off when 8 active players.
  - (b) Save-integrated, with custom field in `HouseInfo`.
  - (c) Dropdown in `CustomGamePlayers` per player.

**#8 Editor crash**: needs a local repro. Suggested test:
  - Open editor in v1.0.362
  - Switch to terrain mode
  - Switch to structure mode
  - Switch to unit mode
  - Capture log Tornie around the crash
