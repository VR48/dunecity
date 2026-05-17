# CLAUDE.md — DuneCity

You are working in `~/development/dunecity`, a Dune Legacy C++17/SDL2 fork with Micropolis-style city simulation integrated into the RTS game loop.

## First reads

Before making non-trivial changes, read:

1. `ARCHITECTURE.md` — repo-root entry point and settled constraints
2. `docs/dunecity-current-architecture.md` — code-verified current implementation state
3. `README.md` — project overview/build commands
4. Relevant source files for the task

For deeper background only when needed:

- `analysis/delivery-summary.md` — historical integration summary; useful but may be stale versus current code
- `analysis/architecture-comparison.md`
- `analysis/dunelegacy-architecture.md`
- `analysis/simcity-architecture.md`
- `scripts/SPRITE-IMPORT.md` for sprite/art pipeline work

## Hard architecture constraints

- R/C/I zones stay **2x2 in gameplay footprint**.
- Do **not** convert simulation, placement, or zoning rules to 3x3.
- Micropolis/SimCity 3x3 art may be referenced/imported, but adapt/render it cleanly inside the 2x2 gameplay footprint.
- Tiles remain the current substrate for city state and overlays.
- Lot objects are a possible future architecture, not an opportunistic refactor.
- **Roads are player-built structures**, alongside but separate from concrete slabs. Like Slab1, a Road is special-cased in `House::placeStructure` to mutate a tile flag (`Tile::isRoad_`) rather than spawning a `StructureBase` — so roads aren't selectable. Roads only appear in the build menu when city-sim mode is active (gated in `BuilderBase::updateBuildList`). Cost defaults to 10 credits; build is instant in city mode.
- Concrete slabs (Slab1/Slab4) and Roads are **independent tile states**. Concrete renders as concrete; roads render as an auto-tiled overlay sprite drawn on top of the underlying rock terrain. They do not share rendering or placement logic.
- **Power flows globally** based on `producedPower >= powerRequirement` on the structure's owner. There is no per-tile electrical grid and no power lines. `Tile::cityPowered_` exists in save format but is unused; the legacy `cityConductive_` was renamed to `isRoad_`.
- Dune-style power remains; WindTraps and the city-mode Nuclear Plant feed the same global house-power pool.
- Preserve deterministic simulation and save/load compatibility.

## Workspace boundaries

- Edit files under `~/development/dunecity` only unless explicitly instructed.
- `../simcity` is read-only Micropolis reference.
- Do not hand-edit generated/imported assets unless the task is specifically about the asset pipeline.

## Likely touch points

City simulation:

- `include/dunecity/`
- `src/dunecity/`
- `include/Tile.h`, `src/Tile.cpp`
- `include/Game.h`, `src/Game.cpp`
- `include/Command.h`, `src/Command.cpp`

Zone/build/render work:

- `src/structures/ZoneStructure.cpp`
- `include/structures/ZoneStructure.h`
- `src/structures/ConstructionYard.cpp`
- `src/structures/BuilderBase.cpp`
- `src/FileClasses/GFXManager.cpp`
- `include/FileClasses/GFXManager.h`
- `scripts/import-micropolis.py`
- `scripts/import-sprites.py`
- `imported_sprites/`

Tests:

- `tests/`
- `tests/CMakeLists.txt`

## Build/test commands

Use targeted verification. Start with:

```bash
cmake --build build --target dunelegacy
cmake --build build --target dunelegacy_tests
./build/tests/dunelegacy_tests
```

If those fail because the local build tree is stale or target names differ, inspect `README.md`, `BUILD.md`, `CMakeLists.txt`, and `tests/CMakeLists.txt` before changing build configuration.

## Style of work

- Prefer small, compileable changes.
- Fix the direct cause before broad refactors.
- Add/update tests when touching placement, rendering lookup, command routing, save/load, or city sim behaviour.
- Keep DuneCity-specific logic clearly named and isolated where possible.
- If a task tempts you to redesign zoning, stop and write the tradeoff down first.

The main trap: solving a 3x3 art/rendering problem by accidentally changing the 2x2 gameplay architecture. Don’t step on the rake. It has teeth.
