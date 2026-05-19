# Bug-fix batch: City integration issues (2026-05-19)

## Issues addressed

### A. Palace UI shows only "People" label
- **Was:** PalaceInterface rendered special weapon + a single "People: N" label.
- **Fix:** Palace now shows Level, Powered/UNPOWERED, and full CityStatsBox (Role, Pop, Value, Pollution, Crime) — matching ZoneStructureInterface's layout. Role corrected from "Stadium" to "Palace (Residential)".

### B. Road rendering: brown overlay obscures sprite atlas
- **Was:** `Game::drawCityRoads` drew brown SDL rectangles on top of the properly auto-tiled `ObjPic_CityRoad` atlas sprites (rendered in `Tile::blitGround`), making roads look brown/tan instead of pavement with white centre markings.
- **Fix:** `drawCityRoads` now early-returns when the road atlas texture is available, so sprites show through. Brown rectangles remain as fallback only when atlas fails to load.

### C. Dune structures placeable on city zones
- **Was:** `Map::okayToPlaceStructure` (both overloads) did not check `hasCityZone()`, allowing factories/turrets/etc. to be placed on top of residential/commercial/industrial zones.
- **Fix:** Both overloads reject placement on tiles with active city zones. Road tiles are still allowed — `StructureBase::assignToMap` now clears the road flag when a structure is placed on a road tile.

### D. QuantBot city buildings cancel/disappear
- **Was:** QuantBot's `manageProd` and placement-search functions called the 7-parameter `okayToPlaceStructure` (no itemID), which applied rock-terrain validation to zone structures that can legitimately be placed on sand. Zone placements failed validation → cancelled.
- **Fix:** All four QuantBot placement paths (`manageProd`, `findPlaceLocation`, `findPlaceLocationWithInfluence`, `findPlaceLocationSimple`) now pass `itemID` to use the 8-parameter overload with correct zone-aware validation.

### E. Palace road transport requirement
- **Was:** Palace grew as Residential with no road adjacency gate — it could level up even without nearby roads.
- **Fix:** Added a Chebyshev-1 nearby-road check in `runZoneGrowth` for all Residential-role structures beyond L0→L1 bootstrap. Palace (and residential zones) now require an adjacent road to grow past level 1.

## Files changed

- `include/GUI/ObjectInterfaces/PalaceInterface.h` — full city stats UI
- `include/GUI/ObjectInterfaces/CityStatsBox.h` — role string fix
- `src/Game.cpp` — road rendering fix
- `src/Map.cpp` — placement collision checks
- `src/structures/StructureBase.cpp` — road flag clearing on placement
- `src/players/QuantBot.cpp` — itemID pass-through in 4 placement paths
- `src/dunecity/CityEffectsRuntime.cpp` — residential road growth gate
