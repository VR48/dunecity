# Task: Add Advanced Windtrap Building to DuneCity

## Overview
Add a new 3x3 structure called "Advanced Windtrap" to DuneCity. This is part of the "tornie mod" extension.

## Building Specifications

| Property | Value |
|----------|-------|
| Name | Advanced Windtrap |
| Size | 3×3 tiles |
| HitPoints | 500 (same as Starport) |
| Price | 500 credits |
| Power | -300 (produces power; negative = supply) |
| ViewRange | 3 |
| BuildTime | 96 (2x Windtrap at 48) |
| InfSpawnProp | 25 |
| Builder | Construction Yard |
| TechLevel | 6 |
| Prerequisite | Windtrap, Radar, Hightech Factory |
| Available | All houses (no house-specific overrides) |

## Reference: Existing Windtrap
- Size: 2×2
- HP: 200
- Price: 300
- Power: -100
- TechLevel: 1
- Builder: Construction Yard
- Prerequisites: none

## Reference: Nuclear Plant (similar pattern)
- Size: 2×2 (single tile?)
- HP: 600
- Price: 1500
- Power: -1000
- TechLevel: 6
- Prerequisites: Windtrap

## What Needs to Change

### 1. `include/data.h` — Add the new structure ID

Add `Structure_AdvancedWindTrap` to the extended structures range. The safest approach given existing IDs:

Current extended structures:
```
Structure_Stadium = 48
Structure_Airport = 49
Structure_ExtLastID = 49
```

Current extended units:
```
Unit_AmbientAirplane = 50
Unit_AmbientHelicopter = 51
Unit_ExtLastID = 51
ItemID_LastID = 51
Num_ItemID  (= 52)
```

**Approach**: Add `Structure_AdvancedWindTrap = 52` at the end, then update:
- `Structure_ExtLastID` to include it (becomes 52 in the extended range)
- `ItemID_LastID` stays at 51... wait no, we need Num_ItemID to be 53

Actually the cleanest approach that maintains ID stability:

```c
Structure_AdvancedWindTrap = 52,  ///< DuneCity: Advanced Windtrap (500 credits, -300 power, 3x3)
```

And update:
- `ItemID_LastID = 52`
- Update `isStructure()` to include ID 52 (add another range check or extend existing)
- Update `isUnit()` if it would accidentally include 52

Look at how `isStructure()` and `isUnit()` currently handle the extended ranges and follow that exact pattern.

### 2. `config/ObjectData.ini.default` — Add stats entry

Add a new section:
```ini
[Advanced Windtrap]
Enabled = true
HitPoints = 500
Price = 500
Power = -300
ViewRange = 3
BuildTime = 96
InfSpawnProp = 25
Builder = Construction Yard
TechLevel = 6
Prerequisite = Windtrap, Radar, Hightech Factory
```

### 3. New files: `include/structures/AdvancedWindTrap.h` and `src/structures/AdvancedWindTrap.cpp`

Model these on `WindTrap.h`/`WindTrap.cpp` but with key changes:
- Structure size: 3×3 (not 2×2)
- `itemID = Structure_AdvancedWindTrap`
- Power production: same pattern as WindTrap (reads from objectData, scales with health)
- Animation: can use WindTrap animation frames for now (same turbine style) OR a placeholder
- Register with city power grid via `citySim->registerPowerSource()`

Key differences from WindTrap:
```cpp
structureSize.x = 3;
structureSize.y = 3;
graphicID = ObjPic_Windtrap;  // reuse for now; placeholder
```

### 4. `include/FileClasses/GFXManager.h` (or wherever ObjPic_Windtrap is defined)

For now, reuse the Windtrap graphic ID. The sprite asset will be provided later.
Add `ObjPic_AdvancedWindtrap` enum value if easy, otherwise reuse Windtrap graphic.

### 5. `src/ObjectData.cpp` or equivalent — Ensure the new structure loads from INI

Check how NuclearPlant was added and follow the same pattern for object data loading.

### 6. Builder logic

The Construction Yard needs to know it can build this structure. Check how the Construction Yard's build list is populated — it likely reads from the INI `Builder` field. Verify this works for the new building.

### 7. `src/CMakeLists.txt` — Add new .cpp to build

Add `AdvancedWindTrap.cpp` to the source list.

### 8. SAVEGAMEVERSION bump

Since Num_ItemID changes, update SAVEGAMEVERSION in `include/Definitions.h` so save compat is maintained.

## Build & Test

```bash
cd /Users/stefanclaw/development/dunecity
cmake --build build --target dunecity 2>&1 | tail -30
```

If tests exist:
```bash
cmake --build build --target dunelegacy_tests 2>&1 | tail -10
./build/tests/dunelegacy_tests 2>&1 | tail -30
```

## Acceptance Criteria

- [ ] DuneCity compiles without errors
- [ ] Advanced Windtrap appears in the Construction Yard build sidebar
- [ ] Placing it costs 500 credits
- [ ] It produces 300 power (negative in power bar)
- [ ] It requires Windtrap + Radar + High Tech Factory to build
- [ ] It occupies 3×3 tiles
- [ ] It has 500 HP
- [ ] Save/load still works (version bumped if Num_ItemID changed)
