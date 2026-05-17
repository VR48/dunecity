# DuneCity Architecture Entry Point

This is not a new full spec. It is the repo-root entry point for coding agents so they find the existing architecture docs and follow the current settled constraints.

## Authoritative docs

Read in this order:

1. `docs/dunecity-current-architecture.md` — code-verified current implementation state. Treat this as the best snapshot of what the repo actually does.
2. `README.md` — project overview, repo conventions, build commands.
3. `analysis/delivery-summary.md` — historical delivery summary of the initial integration; useful background, but may be more optimistic/stale than current code.
4. `analysis/architecture-comparison.md` — Dune Legacy vs Micropolis architecture comparison.
5. `analysis/dunelegacy-architecture.md` — Dune Legacy architecture details.
6. `analysis/simcity-architecture.md` — Micropolis architecture details.
7. `scripts/SPRITE-IMPORT.md` — sprite import/adaptation notes, if working on art/rendering.

## Project shape

DuneCity is a Dune Legacy C++17/SDL2 RTS fork with Micropolis-style city-building concepts being integrated.

Primary repo: `~/development/dunecity`
Micropolis reference: `~/development/simcity/MicropolisCore/MicropolisEngine/src/`

The VSCode workspace includes both folders so agents can inspect Micropolis as read-only reference while editing DuneCity only.

## Settled constraints

1. R/C/I gameplay zone footprints stay **2x2**.
   - Do not convert gameplay placement or simulation rules to 3x3.
   - Full SimCity/Micropolis 3x3 art may be used as source/reference, but it must be adapted/rendered cleanly inside the 2x2 gameplay footprint.

2. Tiles remain the current implementation substrate.
   - Tile fields carry city zone type, density, powered/conductive state, and city tile IDs.
   - Overlay layers live in `CitySimulation` via `CityMapLayer<DATA, BLKSIZE>` where wired.

3. Longer-term direction: lot objects may become authoritative later.
   - If richer zoning is needed, introduce explicit zone/lot objects as the authoritative source of zone state and density.
   - Keep tiles as substrate for roads/slabs, power, pollution, land value, crime, traffic, occupancy, overlays, and rendering.
   - Do not start this migration opportunistically while fixing rendering or placement bugs.

4. Roads and classic power lines are no longer part of the player build flow.
   - Concrete slabs provide transport/electricity substrate.
   - Dune-style power remains, with WindTraps feeding city power.

5. Determinism matters.
   - City actions should route through `CommandManager` where they affect game state.
   - Avoid floating point in saved simulation state.
   - Preserve save/load compatibility gates.

## Important code areas

- `include/dunecity/` — city simulation headers
- `src/dunecity/` — city simulation implementation
- `include/Tile.h`, `src/Tile.cpp` — per-tile city state
- `include/Game.h`, `src/Game.cpp` — city sim lifecycle/game-loop integration
- `include/Command.h`, `src/Command.cpp` — city command routing
- `src/structures/ZoneStructure.cpp` — placed zone structures/rendering behaviour
- `src/structures/ConstructionYard.cpp` and `src/structures/BuilderBase.cpp` — build placement and construction UI flow
- `src/FileClasses/GFXManager.cpp` / `include/FileClasses/GFXManager.h` — sprite lookup/import plumbing
- `scripts/import-micropolis.py`, `scripts/import-sprites.py` — sprite import/adaptation pipeline
- `imported_sprites/` — generated/imported sprite assets
- `tests/` — CppUnit tests

## Build/test baseline

Prefer targeted builds/tests over broad rewrites:

```bash
cmake --build build --target dunelegacy
cmake --build build --target dunelegacy_tests
./build/tests/dunelegacy_tests
```

If build/test targets differ on the current machine, inspect `README.md`, `BUILD.md`, `CMakeLists.txt`, and `tests/CMakeLists.txt` before changing build files.

## Working rules for agents

- Edit DuneCity only unless explicitly asked otherwise.
- Treat `../simcity` as read-only reference.
- Prefer small, compileable changes.
- Do not rewrite the city simulation architecture to solve a rendering bug.
- Preserve the 2x2 zone footprint unless Stefan explicitly changes that decision.
- When unsure, leave a short note describing the architectural tradeoff instead of silently inventing a new layer.
