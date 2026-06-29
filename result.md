# Test Fix + Neutral House Tests + CI — Result

## Summary

### 1. Fixed 43 failing tests

**Root cause A — missing environment variable (37 tests):**
Source-scan tests in ZoneStructureTestCase, CityStructuresTestCase,
ConstructionYardResolverTestCase, and RoadPlacementTestCase all called helper
functions that returned empty strings when `DUNE_CITY_SOURCE_DIR` was unset,
then hit `REQUIRE_FALSE(src.empty())` / `REQUIRE(file.is_open())` and failed.
The source files themselves exist at the correct paths.

**Fix:** Changed each test's internal helper function (`readSourceFile`,
`getSourceDir`) to call `SKIP(...)` instead of returning empty/failing when
the env var is absent. Tests now skip gracefully (42 skipped, 0 failed) when
run without the env var, and pass fully when run via ctest or with
`DUNE_CITY_SOURCE_DIR` set.

**Root cause B — stale expected values (6 tests in CityEffectsTestCase +
ValueModelTestCase):**

| Test | Old expected | New (correct) | Reason |
|---|---|---|---|
| `getStructureMaxLevel(Structure_Silo)` | 2 | 3 | Silo reclassified as I-high (no pollution) |
| `getPoliceCoverage(Structure_RocketTurret)` | 10 | 25 | Both turret types tuned to 25% after design review |
| `getPoliceAnnualCost(Structure_PoliceStation)` | 500 | 100 | Designer-reduced from SC's 500 to make Police Stations more accessible |
| `computeAnnualTaxRevenue(100, 7)` | 1400 | 466 | Formula changed to `pop×200×rate/(100×3)` (per-citizen contribution reduced to 200/3) |
| `computeAnnualTaxRevenue(1000, 10%)` | 20000 | 6666 | Same formula change |
| `computeAnnualTaxRevenue(1000, 15%)` | 30000 | 10000 | Same formula change |

Also updated the test name for the PoliceStation cost test and all inline
comments to reflect current code.

### 2. Added Neutral house tests (12 new test cases)

New file: `tests/NeutralHouseTestCase/NeutralHouseTestCase.cpp`

Covers:
- `HOUSE_NEUTRAL == 6`, `NUM_HOUSES == 7`
- `HOUSE_NEUTRAL == HOUSE_MERCENARY + 1`
- `SAVEGAMEVERSION == 9813` (version that added HOUSE_NEUTRAL)
- `PALCOLOR_NEUTRAL == 128`
- `houseToPaletteIndex[HOUSE_NEUTRAL] == PALCOLOR_NEUTRAL`
- Neutral mentat animation enums registered (`Anim_NeutralEyes`, etc.)
- `HouseNeutral < NUM_VOICE`, `HouseNeutral == HouseOrdos + 1`
- `ANEU.VOC` is primary Neutral voice (source scan, skips without env)
- `HNEU.VOC` / `ONEU.VOC` referenced in Harkonnen/Ordos branches (source scan)
- `HOUSE_NEUTRAL` in skirmish `houseOrder[]` (source scan)
- `Prerequisite(N) = House IX` in `[Launcher]` section of ObjectData.ini (source scan)

Source-scan tests skip gracefully when `DUNE_CITY_SOURCE_DIR` is not set.

### 3. CI test job added

Added `test-linux` job to `.github/workflows/build.yml`:
- Runs on `ubuntu-latest`, parallel to platform builds
- Installs Catch2 + SDL2 dev packages via apt (fast, no vcpkg)
- Configures with `-DDUNECITY_BUILD_TESTS=ON`
- Builds `dunelegacy_tests` target
- Runs tests with `DUNE_CITY_SOURCE_DIR` and `DUNECITY_DATADIR` set so
  source-scan tests execute (not just skip)

## Final test counts

| Run mode | Passed | Skipped | Failed |
|---|---|---|---|
| `./build/bin/dunelegacy_tests` (no env vars) | 273 | 42 | 0 |
| With `DUNE_CITY_SOURCE_DIR` + `DUNECITY_DATADIR` set | 315 | 0 | 0 |
| CI (`ctest` with env vars in test properties) | 315 | 0 | 0 |

Total test cases: **315** (303 original + 12 Neutral house)
Total assertions: **1119** (with env vars)

## Remaining issues

None. All 303 original tests pass (or skip appropriately without env vars).
All 12 new Neutral house tests pass.
