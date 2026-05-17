# DuneCity C++ Migration Analysis

**Date:** 2026-03-29 00:30  
**Analyzer:** hermes (cron job)  
**Goal:** Migrate SimCity/Micropolis city-building into Dune Legacy (C++)

---

## 1. Specific Files to Port from MicropolisCore

### Completed Ports (~2,247 LOC)

| Source File | LOC | Purpose | Ported To |
|------------|-----|---------|-----------|
| `simulate.cpp` | 1,729 | Main simulation loop, 16-phase cycle | `CitySimulation.cpp` (518 LOC) ✓ |
| `budget.cpp` | ~800 | Tax, funding, economy | `CityBudget.cpp` (87 LOC) ✓ |
| `evaluate.cpp` | ~900 | City evaluation, ratings | `CityEvaluation.cpp` (132 LOC) ✓ |
| `power.cpp` | 195 | Power grid calculation | `PowerGrid.cpp` (113 LOC) ✓ |
| `scan.cpp` | 600 | Map scanning for effects | `CityScanner.cpp` (264 LOC) ✓ |
| `connect.cpp` | ~1,200 | Connectivity/road tracing | `TrafficSimulation.cpp` (134 LOC) ✓ |
| `tool.cpp` | 1,617 | Building tools (res/com/ind zones) | `ZoneSimulation.cpp` (239 LOC) ✓ |

**Headers:** CitySimulation.h, ZoneSimulation.h, TrafficSimulation.h, PowerGrid.h, CityBudget.h, CityScanner.h, CityEvaluation.h, CityConstants.h, CityMapLayer.h

### Remaining MicropolisCore Files (Priority Order)

| File | LOC | Purpose | Priority |
|------|-----|---------|----------|
| `traffic.cpp` | 519 | Vehicle routing, pathfinding | **HIGH** - Vehicle AI not integrated |
| `sprite.cpp` | 2,039 | City sprites (cars, planes) | Medium |
| `disasters.cpp` | 418 | Disaster effects | Low - Dune has own disasters |
| `animate.cpp` | ~700 | Animation state | Low |

---

## 2. Integration Points with Dune Legacy

### Current Architecture

```
Dune Legacy src/
├── dunecity/                    # Ported modules (~2,247 LOC)
│   ├── CitySimulation.cpp/h     ✓ Core orchestration (16-phase) - INTEGRATED
│   ├── ZoneSimulation.cpp/h    ✓ Zone growth - EXISTS (NO BUILDING SPAWNING)
│   ├── TrafficSimulation.cpp/h ✓ Road traffic - EXISTS
│   ├── PowerGrid.cpp/h         ✓ Power grid - INTEGRATED
│   ├── CityScanner.cpp/h       ✓ Map scanning - INTEGRATED
│   ├── CityBudget.cpp/h        ✓ Economy - INTEGRATED
│   └── CityEvaluation.cpp/h    ✓ Ratings - EXISTS
├── structures/
│   └── WindTrap.cpp            ✓ Power source - CONNECTED
├── Game.cpp                    ✓ Main game loop - INTEGRATED
└── Tile.cpp                    ✓ Zone fields - EXISTS
```

### Verified Integration Connections

| Dune Legacy File | Integration | Status |
|------------------|-------------|--------|
| `src/structures/WindTrap.cpp` | `citySim->registerPowerSource()` | ✓ CONNECTED |
| `src/Game.cpp` (line 2388) | `citySimulation_->advancePhase()` | ✓ CONNECTED |
| `src/Game.cpp` (line 3060) | `citySimulation_->load()` | ✓ CONNECTED |
| `src/Game.cpp` (line 3167) | `citySimulation_->save()` | ✓ CONNECTED |
| `src/Command.cpp` | City command handlers | ✓ CONNECTED |

---

## 3. What's Changed Since Last Run

### Changes from 2026-03-25
- **No code changes** in either codebase since last analysis
- Blockers remain unchanged

### Last Analysis State
- Core simulation loop ported and integrated
- Power grid connected to WindTraps
- Zone placement functional (density updates)
- **CRITICAL GAP:** Zones never spawn actual buildings

---

## 4. Testing Strategy

### Test Framework: Catch2 (Dune Legacy standard)

```bash
# Build tests
cd ~/development/dune/dunelegacy
mkdir -p build_test && cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Debug
make dunelegacy_tests

# Run all tests
./bin/dunelegacy_tests

# Run DuneCity-specific tests
./bin/dunelegacy_tests "[dunecity]"
```

### Existing DuneCity Tests
- Location: `tests/DuneCityTestCase/`
- File: `DuneCityTestCase.cpp`
- Tests: ZoneType, DisasterType, CityConstants validation

---

## 5. Actionable Next Steps

### Immediate (Phase 1: Building Spawning)

| Step | File to Modify | Action |
|------|----------------|--------|
| 1 | `include/data.h` | Add `Structure_Res1` through `Structure_Ind4` (Structure_20-34) |
| 2 | `include/structures/StructureBase.h` | Add city building type detection |
| 3 | `src/dunecity/ZoneSimulation.cpp` | Add `constructCityBuilding(ZoneType, density)` |
| 4 | `src/dunecity/ZoneSimulation.cpp` | Call building constructor when density >= threshold |

### Building Spawning Implementation

In `ZoneSimulation.cpp::doResIn()`, add building spawn logic:

```cpp
void ZoneSimulation::doResIn(int x, int y, uint8_t pop, int /*landVal*/) {
    if (pop < 8) {
        Tile* tile = currentGameMap->getTile(x, y);
        tile->setCityZoneDensity(pop + 1);
        incRateOfGrowth(x, y, 1);
    }
    
    // NEW: Spawn building when density peaks
    if (pop >= 7) {
        int landValue = sim_->getLandValueMap().worldGet(x, y);
        int buildingType = (landValue > 150) ? Structure_Res4 
                          : (landValue > 100) ? Structure_Res3
                          : (landValue > 50)  ? Structure_Res2
                          : Structure_Res1;
        // Call ObjectManager::addObject() with buildingType
    }
}
```

---

## 6. Blockers and Decisions Needed

### Blocker: City Building Enum Values Not Defined
- **Status:** `data.h` Structure_LastID = 19, no city buildings defined
- **Impact:** Cannot instantiate city buildings via ObjectManager
- **Decision Required:** Define all 12 building types:
  - `Structure_Res1` (20), `Structure_Res2` (21), `Structure_Res3` (22), `Structure_Res4` (23)
  - `Structure_Com1` (24), `Structure_Com2` (25), `Structure_Com3` (26), `Structure_Com4` (27)
  - `Structure_Ind1` (28), `Structure_Ind2` (29), `Structure_Ind3` (30), `Structure_Ind4` (31)

### Blocker: No Building Spawning in ZoneSimulation
- **Status:** `doResidential/commercial/Industrial()` update density only
- **Impact:** Zones never grow into actual buildings
- **Solution:** Add threshold check at max density to call `ObjectManager::addObject()`

### Decision: Building Rendering
- **Option A:** Use existing Dune structure sprites
- **Option B:** Add new city-specific sprites
- **Recommendation:** Start with Option A - reuse WindTrap or ConstructionYard sprites

---

## Summary

| Metric | Value |
|--------|-------|
| MicropolisCore source | 22,649 LOC |
| Ported to DuneCity | ~2,247 LOC (9.9%) |
| Integration status | Core simulation integrated |
| Working features | Zone placement, power grid, scanning, budget |
| **BLOCKER** | No actual building spawning |

---

*Analysis generated from: ~/development/simcity/MicropolisCore/MicropolisEngine/src and ~/development/dune/dunelegacy/src/dunecity*
