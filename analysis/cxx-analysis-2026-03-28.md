# DuneCity C++ Migration Analysis

**Date:** 2026-03-28 23:50  
**Analyzer:** hermes (cron job)  
**Goal:** Migrate SimCity/Micropolis city-building into Dune Legacy (C++)

---

## 1. Specific Files to Port from MicropolisCore

### Completed Ports (9 files, ~2,247 LOC in dunecity module)

| MicropolisCore File | LOC | Purpose | DuneCity Port |
|---------------------|-----|---------|---------------|
| `simulate.cpp` | 1,729 | Main simulation loop, 16-phase cycle | `CitySimulation.cpp` ✓ |
| `budget.cpp` | ~800 | Tax, funding, economy | `CityBudget.cpp` ✓ |
| `evaluate.cpp` | ~900 | City evaluation, ratings | `CityEvaluation.cpp` ✓ |
| `power.cpp` | 195 | Power grid calculation | `PowerGrid.cpp` ✓ |
| `scan.cpp` | 600 | Map scanning for effects | `CityScanner.cpp` ✓ |
| `connect.cpp` | ~1,200 | Connectivity/road tracing | `TrafficSimulation.cpp` ✓ |
| `tool.cpp` | 1,617 | Building tools (res/com/ind zones) | `ZoneSimulation.cpp` ✓ |
| `traffic.cpp` | 519 | Vehicle pathfinding, traffic density | `TrafficSimulation.cpp` ✓ |
| `map.cpp` | 1,025 | Grid management | `CityMapLayer.cpp` ✓ |

### Remaining Unported Files (Low Priority)

| File | LOC | Purpose | Priority |
|------|-----|---------|----------|
| `sprite.cpp` | 2,039 | City sprites (cars, planes) | Low - Use Dune's vehicles |
| `disasters.cpp` | 418 | Disaster effects | Low - Dune has own disasters |
| `animate.cpp` | ~700 | Animation state | Low |
| `fileio.cpp` | ~800 | Save/load format | Skip - Use Dune's save system |
| `generate.cpp` | ~800 | Terrain generation | Skip - Use Dune's terrain |

---

## 2. Integration Points with Dune Legacy

### Architecture

```
Dune Legacy src/
├── include/dunecity/              # Headers (~887 LOC)
│   ├── CitySimulation.h         ✓ Core orchestration (16-phase)
│   ├── ZoneSimulation.h         ✓ Zone growth
│   ├── TrafficSimulation.h      ✓ Road traffic
│   ├── PowerGrid.h              ✓ Power grid
│   ├── CityScanner.h            ✓ Map scanning
│   ├── CityBudget.h             ✓ Economy
│   ├── CityEvaluation.h         ✓ Ratings
│   ├── CityConstants.h           ✓ Zone/Power constants
│   └── CityMapLayer.h            ✓ Data layers
├── src/dunecity/                  # Implementation (~1,487 LOC)
│   ├── CitySimulation.cpp       ✓
│   ├── ZoneSimulation.cpp       ✓
│   ├── TrafficSimulation.cpp    ✓
│   ├── PowerGrid.cpp            ✓
│   ├── CityScanner.cpp           ✓
│   ├── CityBudget.cpp            ✓
│   ├── CityEvaluation.cpp        ✓
│   └── CityMapLayer.cpp           ✓
└── src/structures/
    └── WindTrap.cpp             ✓ Power source
```

### Integration Status

| Dune Legacy File | Integration | Status |
|------------------|-------------|--------|
| `src/Game.cpp` | CitySimulation init/advancePhase/load/save | ✓ INTEGRATED |
| `include/Game.h` | citySimulation_ member + getCitySimulation() | ✓ INTEGRATED |
| `include/Tile.h` | Zone fields (zoneType, population, etc.) | ✓ INTEGRATED |
| `src/CMakeLists.txt` | dunecity module | ✓ INTEGRATED |
| `src/structures/WindTrap.cpp` | registerPowerSource() | ✓ INTEGRATED |
| `tests/CMakeLists.txt` | DuneCityTestCase Catch2 | ✓ INTEGRATED |

### Git Status
```
M include/Command.h
M include/Definitions.h
M include/Game.h
M include/Map.h
M include/Tile.h
M include/players/QuantBot.h
M src/CMakeLists.txt
M src/Command.cpp
M src/Game.cpp
M src/Tile.cpp
M src/players/QuantBot.cpp
M src/structures/WindTrap.cpp
M tests/CMakeLists.txt
M vcpkg.json
?? AI_BOTS_GUIDE.md
?? build_test/
?? include/dunecity/    # UNTRACKED - 5+ weeks
?? src/dunecity/        # UNTRACKED - 5+ weeks
?? tests/DuneCityTestCase/  # UNTRACKED
```
**BLOCKER: All dunecity code remains uncommitted to the Dune Legacy repo.**

---

## 3. What's Changed Since Last Run (2026-03-28 23:42)

- **No significant changes detected** - state remains stable
- dunecity files still untracked (continues to be the primary blocker)

### Change Summary
| Change | Files | Impact |
|--------|-------|--------|
| None | - | Stable state - waiting on commit |

### Current Status
1. **9/9 core MicropolisCore files ported** - All critical simulation systems complete
2. **Integration verified** - CitySimulation hooked into Game.cpp game loop
3. **Testing infrastructure exists** - DuneCityTestCase uses Catch2
4. **BLOCKER: Git untracked** - All dunecity files remain uncommitted (5+ weeks)

---

## 4. Testing Strategy (Catch2 via CMake)

### Dune Legacy Test Setup
```bash
cd ~/development/dune/dunelegacy
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/dunelegacy_tests "[dunecity]"
```

### Existing DuneCity Test Cases
- `tests/DuneCityTestCase/DuneCityTestCase.cpp` - Uses **Catch2** (not gtest)
- Tests ZoneType, DisasterType, CityConstants validation
- Run with: `./dunelegacy_tests "[dunecity]"`

### Recommended Test Plan (Prioritized)
1. **CitySimulation unit tests** (HIGH PRIORITY)
   - Test 16-phase cycle ordering
   - Test map scanning bounds
   - Test census accumulation

2. **PowerGrid unit tests** (HIGH PRIORITY)
   - Test power calculation
   - Test power source registration from WindTrap

3. **ZoneSimulation unit tests** (MEDIUM)
   - Test zone growth from RCI
   - Test building placement validation

4. **Integration tests** (MEDIUM)
   - Load/save roundtrip
   - Game cycle integration

---

## 5. Blockers and Decisions Needed

### Critical Blockers

| Blocker | Impact | Resolution |
|---------|--------|------------|
| **Untracked dunecity files** | Code not versioned, 5+ weeks | Commit to dune/dunelegacy repo |

### Decisions Needed

| Decision | Options | Recommendation |
|----------|---------|----------------|
| Traffic/vehicle integration | Port traffic.cpp OR use existing Dune vehicle system | **Use existing Dune vehicles** - Harvester, Trike, Tank already exist |
| Sprite rendering | Port sprite.cpp OR use Dune's unit rendering | **Use Dune's unit rendering** - consistent with game art |
| Commit strategy | Commit dunecity as a branch OR commit directly to main | **Branch + PR** - allows review |
| Test framework | Continue with Catch2 OR migrate to gtest | **Keep Catch2** - already integrated |

### Immediate Next Steps
1. **Commit dunecity code** to Dune Legacy repo (pending user action)
2. **Add more Catch2 test cases** to DuneCityTestCase/ for simulation coverage
3. **Verify WindTrap power** integration works in actual gameplay
4. **Decide on vehicle system** - integrate Dune's existing vehicles or use Micropolis-style traffic density

---

## 6. Porting Notes (Reference)

### CitySimulation Phase Order (from micropolis.cpp)
```
Phase 0:  Clear + cycle reset
Phase 1-8: mapScan (8 chunks)
Phase 9:   Census + Tax
Phase 10:  Decay + Messages
Phase 11:  Power scan
Phase 12:  Pollution/Terrain/LandValue
Phase 13:  Crime scan
Phase 14:  Population density
Phase 15:  Fire analysis + disasters
```

### Key Data Structures to Map

| MicropolisCore | DuneLegacy | Notes |
|----------------|------------|-------|
| `rateOfGrowthMap` | `rateOfGrowthMap_` | int16_t per tile |
| `powerGrid` | `powerGrid_` | PowerGrid class |
| `trafficDensityMap` | `trafficDensityMap_` | uint8_t per tile |
| `pollutionMap` | `pollutionDensityMap_` | uint8_t per tile |
| `population` | Tile population | Per-zone |
