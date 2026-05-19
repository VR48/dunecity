# RCI Growth Fidelity — Implementation Notes

Date: 2026-05-19
Job: Second pass (after residential-shrink-unemployment fix from job 230)

## What was implemented

### Slice 1: Demand and decline correctness (already done by job 230)
- Pre-growth valve computation via `computeDemandValves()` — Micropolis-shaped
- Zscore-gated growth (`zscore = valve + localEval`)
- Probabilistic residential decline when zscore is negative
- **Enhancement this pass:** Bootstrap clause for empty/near-empty cities (totalPop < 100 returns small positive demand)
- **Enhancement this pass:** Valve multiplier raised from 600 to 1500 since DuneCity computes fresh each tick rather than accumulating like Micropolis. Ensures extreme imbalance (10k R vs 200 C+I) produces a valve strong enough to overcome max local eval.

### Slice 2: Connectivity traffic (already done by job 230)
- BFS traffic connectivity via `TrafficSimulation` class
- R→C, C→I, I→R pathfinding
- Traffic result feeds into local evaluation and growth gates
- **Enhancement this pass:** Traffic density now stamped along ALL BFS-visited road tiles (+50 per tile, cap 240), matching SC's `addToTrafficDensityMap()` pattern. Previously only +20 was added at the zone's own block.

### Slice 3: Local evaluation parity
- `computeLocalEval()` handles all three zone types with role-appropriate weights
- Residential: land value, pollution, crime, traffic all consumed
- Commercial: land value, crime, pollution penalties, traffic penalties
- Industrial: traffic dominates, pollution is not a self-blocker
- **New this pass:** `computeCommercialRate()` helper adds supply-side bonuses — nearby residential (+up to 200), nearby industrial (+up to 50), airport presence (+100). Applied to commercial zones during growth evaluation.

### Slice 4: Pollution/crime/value consumption
- All three zone types consume land value, pollution, crime, and traffic via `computeLocalEval()`
- **New this pass:** Traffic pollution. Blocks with traffic density ≥64 contribute +2 pollution; density ≥150 contributes +6. Applied after zone growth BFS trips stamp the traffic density map. Closes the SC feedback loop where busy roads degrade nearby residential value.
- StarPort special case: marked as clean (no pollution) despite Industrial role, per spec override.

### Slice 5: Civic caps (already done by job 230)
- Stadium/Palace cap residential demand above population threshold
- Airport caps commercial demand
- Starport caps industrial demand
- Soft cap at 200 (not hard stop)

### Slice 6: Regression harness
- Fixed StarPort test inconsistency (was testing CityRole::None, should be Industrial)
- Added 14 new tests:
  - Traffic pollution thresholds (3 tests)
  - Commercial rate calculation (3 tests)
  - Scenario regressions: R=-2000 shrink, connected growth, pollution blocking, crime impact, Airport/Starport/Stadium civic caps, Palace spec (8 tests)
- Updated local eval test expectations to match current /4 scaling formula
- Total: 69 city-effects tests passing (244 assertions)

### Growth rate tracking (already done by job 230)
- `growthRateMap_` incremented/decremented on growth/decline events
- Decay toward zero each scan tick

## What was NOT implemented

- **Commercial rate map overlay** (`commercialRateMap_`): Used inline helper `computeCommercialRate()` instead of a full CityMapLayer. The helper is called per-node during growth evaluation and doesn't persist between scans. If a UI overlay for "commercial desirability" is needed later, promote to a map layer.
- **Full traffic density path reconstruction**: BFS records all visited tiles for density stamping, but doesn't reconstruct the actual shortest path. This is correct for SC's model (SC stamps density on all explored tiles too), but means the overlay shows "explored area" rather than a precise commute route.

## Files changed

- `include/dunecity/CityEffects.h` — StarPort clean override, traffic pollution constants + helper, commercial rate helper, bootstrap clause in demand valves, valve multiplier 600→1500
- `include/dunecity/TrafficSimulation.h` — Pos struct public, getLastPath() accessor, pathTiles_ member
- `src/dunecity/TrafficSimulation.cpp` — BFS records visited tiles in pathTiles_, clears on failure
- `src/dunecity/CityEffectsRuntime.cpp` — Traffic density stamped along full BFS path, traffic pollution pass after zone growth, commercial rate adjustment in growth evaluation
- `tests/CityEffectsTestCase/CityEffectsTestCase.cpp` — Fixed StarPort role test, updated local eval expectations, added 14 regression tests
- `tests/CMakeLists.txt` — Excluded TrafficSimulation.cpp from test build (it pulls in Tile/Map/StructureBase which the test stubs don't provide)

## Verification

- 69 city-effects tests pass (244 assertions)
- 249/287 total tests pass; 37 failures are pre-existing source-file scanning tests unrelated to city sim
- App built at `build/bin/dunecity.app` (May 19 19:16)
