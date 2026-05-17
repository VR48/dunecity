# DuneCity

A hybrid RTS + city-builder: the Dune Legacy engine with Micropolis (SimCity) city simulation integrated into the game loop.

**Status:** App renamed to DuneCity. City simulation compiles and runs. No rendering yet - see [GitHub issues](https://github.com/svan058/dunecity/issues) for roadmap.

---

## Development Workflow

**Trunk-based development:**
- `main` = trunk, always deployable
- Feature branches: short-lived, PRs for multi-agent visibility
- **Gate:** Must compile to merge back

```bash
# Create feature branch
git checkout -b feature/your-feature

# Work, commit, push
git add . && git commit -m "feat: description" && git push -u origin feature/your-feature

# Open PR at: https://github.com/svan058/dunecity/compare

# After review + CI passes → squash merge to main
```

---

## Multi-Agent Collaboration

Hermes (Skroderider) and Cursor (RealClaw) both work on this repo.

**Rules:**
1. **Trunk-based** — work on `main` or short-lived branches (hours, not days)
2. **Claim phases/features** — check FEATURES.md before starting, claim your work
3. **Small PRs, squash merge** — fast review, less merge conflict risk
4. **Must compile** — no broken builds pushed
5. **Coordinate on high-traffic files** — CMakeLists.txt, Game.cpp, GameInterface.cpp

**Keybinds:** See FEATURES.md for current hotkey assignments. Don't override existing gameplay keys.

---

## Build

```bash
# Clone
git clone git@github.com:svan058/dunecity.git ~/development/dunecity

# Configure (uses vcpkg from original dune/dunelegacy)
cd ~/development/dunecity
cmake -B build \
  -DCMAKE_PREFIX_PATH=/Users/stefanclaw/dune/dunelegacy/build/vcpkg_installed/arm64-osx \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_MANIFEST_INSTALL=OFF

# Build
cmake --build build -j$(sysctl -n hw.ncpu)

# Run
open build/bin/dunecity.app
```

---

## For Agents: Quick Context

DuneCity is a fork of [Dune Legacy](https://github.com/svan058/dunelegacy) (a C++17/SDL2 open-source Dune II engine) with the [Micropolis](https://github.com/SimHacker/MicropolisCore) city-building simulation ported into it. Both simulations run in the same fixed-timestep game loop — Dune Legacy processes combat objects every tick, then advances one phase of a 16-phase Micropolis city cycle.

**Read `analysis/delivery-summary.md` for the full technical summary** — every file, every subsystem, every integration point.

### Repository Layout

```
dunecity/
├── include/dunecity/           # City simulation headers (9 files)
│   ├── CitySimulation.h        #   Main orchestrator (225 lines)
│   ├── CityConstants.h         #   Zone types, disaster types, constants
│   ├── CityMapLayer.h          #   Multi-resolution overlay map template
│   ├── PowerGrid.h             #   Power flood-fill
│   ├── ZoneSimulation.h        #   RCI zone growth/decline
│   ├── TrafficSimulation.h     #   Traffic pathfinding
│   ├── CityScanner.h           #   Analysis passes (pollution, crime, etc.)
│   ├── CityBudget.h            #   Tax/budget
│   └── CityEvaluation.h        #   City scoring, valves, census
├── src/dunecity/               # City simulation implementations (7 files, ~1,500 LOC)
├── include/                    # Dune Legacy headers (modified: Game.h, Tile.h, Map.h, Command.h, etc.)
├── src/                        # Dune Legacy sources (modified: Game.cpp, Tile.cpp, Command.cpp, etc.)
├── analysis/                   # Architecture docs, porting notes, delivery summary
│   ├── delivery-summary.md     #   FULL technical summary of what was built
│   ├── architecture-comparison.md
│   ├── dunelegacy-architecture.md
│   └── simcity-architecture.md
├── data/                       # Game assets (LEGACY.PAK, GFXHD.PAK, OPENSD2.PAK bundled)
├── config/                     # Default config templates
├── CMakeLists.txt              # Build config (C++17, vcpkg)
├── vcpkg.json                  # Dependencies: SDL2, SDL2_mixer, SDL2_ttf, curl, miniupnpc
└── BUILD.md                    # Detailed build instructions for all platforms
```

### Key Modified Dune Legacy Files

| File | What Changed |
|------|-------------|
| `include/Game.h` / `src/Game.cpp` | City sim lifecycle, save/load (v9807), economic victory check |
| `include/Tile.h` / `src/Tile.cpp` | Per-tile city fields (zone type, density, powered, conductive) + save/load |
| `include/Command.h` / `src/Command.cpp` | 4 new command types for city actions |
| `src/structures/WindTrap.cpp` | Registers as power source with city grid |
| `include/players/QuantBot.h` / `src/players/QuantBot.cpp` | AI city-building logic |
| `include/Definitions.h` | `SAVEGAMEVERSION` 9807, `WINLOSEFLAGS_ECONOMIC` |
| `src/CMakeLists.txt` | All 7 `dunecity/*.cpp` added to build |

---

## Getting the Code

```bash
# Clone the repo
git clone git@github.com:svan058/dunecity.git ~/development/dunecity
cd ~/development/dunecity

# Verify
git log --oneline -5
```

### Upstream Dune Legacy

The original (unmodified) Dune Legacy repo is at `~/development/dune/dunelegacy/` with remote `ssh://sfgit/p/dunelegacy/code`. DuneCity changes also exist there as uncommitted modifications, but **`~/development/dunecity/` is the source of truth**.

### Micropolis Reference

The Micropolis source used for porting is at `~/development/simcity/MicropolisCore/MicropolisEngine/src/`. It was read-only reference — no files were copied verbatim, algorithms were ported and adapted.

---

## Building

### Prerequisites

- CMake 3.15+
- C++17 compiler (Clang on macOS, GCC on Linux, MSVC on Windows)
- Pre-built vcpkg dependencies (see below)

### macOS (Tested Configuration)

The project was developed and tested on macOS (arm64). Pre-built vcpkg dependencies exist at `~/dune/dunelegacy/build/vcpkg_installed/`. To build using those:

```bash
cd ~/development/dunecity

cmake -B build \
  -DVCPKG_INSTALLED_DIR=/Users/stefanclaw/dune/dunelegacy/build/vcpkg_installed \
  -DVCPKG_MANIFEST_MODE=OFF \
  -DCMAKE_PREFIX_PATH=/Users/stefanclaw/dune/dunelegacy/build/vcpkg_installed/arm64-osx \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build --target dunelegacy
```

### Fresh Build (Any Platform)

If you don't have pre-built deps, use vcpkg from scratch:

```bash
# One-time: clone and bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg && ./bootstrap-vcpkg.sh

# Configure and build
cd ~/development/dunecity
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build --target dunelegacy
```

First build takes ~5 minutes (vcpkg compiles SDL2, curl, etc.).

### Running

```bash
# macOS
open build/bin/dunelegacy.app

# Linux
./build/bin/dunelegacy

# Windows
build\bin\Debug\dunelegacy.exe
```

Requires original Dune II PAK files in the data directory. See `BUILD.md` for details.

---

## Architecture Summary

### Game Loop

```
Game::updateGameState()  [runs every tick at ~62.5 Hz]
  ├── cmdManager.executeCommands()    // city commands dispatched here
  ├── house[i]->update()              // AI city-building runs here
  ├── processObjects()                // WindTrap registers power sources
  ├── citySimulation_->advancePhase() // one of 16 city phases per tick
  └── economic victory check
```

### City Simulation Phases

Each tick advances one phase. A full city-time unit takes 16 ticks.

| Phase | What Happens |
|-------|-------------|
| 0 | Increment city time, clear census, reset power flags |
| 1-8 | Map scan — process 1/8 of zones per phase (growth, roads) |
| 9 | Census, tax collection, growth valve calculation |
| 10 | Decay rate-of-growth, decay traffic density |
| 11 | Power flood-fill from WindTrap seeds |
| 12 | Pollution, terrain, land value analysis |
| 13 | Crime scan |
| 14 | Population density scan |
| 15 | Fire analysis, Arrakis disasters |

### Subsystems

| Module | Source | Purpose |
|--------|--------|---------|
| `CitySimulation` | `simulate.cpp` | Orchestrator, 16-phase scheduler, command handler |
| `PowerGrid` | `power.cpp` | Flood-fill power distribution |
| `ZoneSimulation` | `zone.cpp` | RCI zone growth/decline based on desirability |
| `TrafficSimulation` | `traffic.cpp` | Stack-based DFS pathfinding, density |
| `CityScanner` | `scan.cpp` | 5 analysis passes with map smoothing |
| `CityBudget` | `budget.cpp` | Tax collection, service fund allocation |
| `CityEvaluation` | `evaluate.cpp` | City scoring, valve adjustment, census |

### Determinism

All city state is deterministic for multiplayer:
- Dedicated `Random cityRng_` seeded from game seed
- All city actions via `CommandManager` (4 command types)
- No floating-point in simulation state
- RNG seed persisted in saves

---

## What Works

- Full 16-phase city simulation running inside Dune Legacy's game loop
- Power grid fed by WindTrap structures
- RCI zone growth/decline with desirability checks
- Traffic pathfinding connecting zones via roads
- 5 analysis scans (pollution, crime, land value, population, fire)
- Tax/budget system
- City scoring and growth valve calculation
- Arrakis-themed disasters (sandstorm, sandworm, sabotage, spice blow)
- Save/load with backward compatibility (v9807)
- AI city-building in QuantBot
- Economic victory condition
- Feature flag `citySimEnabled_` to disable entirely

## What Doesn't Work Yet

- **No rendering** — city zones, overlays, heatmaps have no visual representation
- **No UI** — no toolbar for zone placement, no tax/budget panels
- **No tests** — no CppUnit test files for any DuneCity subsystem
- **PoliceStation/FireStation** not connected to city sim
- **Multiplayer lobby** doesn't show city info
- **Simulation fidelity ~25%** — edge cases, multi-tile zones, sprite animations simplified
- **AI is basic** — grid-walk zone placement, no strategic optimization

---

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| `SAVEGAMEVERSION` | 9807 | `include/Definitions.h` |
| `WINLOSEFLAGS_ECONOMIC` | 0x10 | `include/Definitions.h` |
| `NUM_CITY_PHASES` | 16 | `include/dunecity/CityConstants.h` |
| `CITY_PHASE_INTERVAL` | 1 | `include/dunecity/CityConstants.h` |
| `MAX_TAX_RATE` | 20 | `include/dunecity/CityConstants.h` |
| Default city tax | 7% | `CitySimulation.h` (`cityTax_ = 7`) |
| Economic victory threshold | 500 pop | `CitySimulation.h` (`economicVictoryThreshold_ = 500`) |

---

## Related Files

| Path | What It Is |
|------|-----------|
| `ARCHITECTURE.md` | Repo-root architecture entry point and settled constraints |
| `CLAUDE.md` | Claude CLI / coding-agent instructions for this repo |
| `docs/dunecity-current-architecture.md` | Code-verified current implementation state |
| `analysis/delivery-summary.md` | Historical technical delivery summary |
| `analysis/architecture-comparison.md` | Dune Legacy vs Micropolis deep comparison |
| `analysis/dunelegacy-architecture.md` | Dune Legacy architecture deep dive |
| `analysis/simcity-architecture.md` | Micropolis architecture deep dive |
| `~/.cursor/plans/dunecity_integration_plan_c90603b0.plan.md` | Original 9-phase implementation plan |
| `~/development/simcity/` | Micropolis source (read-only reference) |
