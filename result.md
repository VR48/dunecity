# HOUSE_REBELS — Implementation Result

## Build
**Success** — clean compile, zero warnings, zero errors.

```
cmake --build build --config Release -j8
[100%] Built target dunecity
```

## Files Changed

### Already committed (66901dc, linter-applied)
| File | Change |
|---|---|
| `include/DataTypes.h` | Added `HOUSE_REBELS = 7`, `NUM_HOUSES` = 8 |
| `include/Colors.h` | Added `PALCOLOR_REBELS 192` |
| `include/globals.h` | Extended `houseToPaletteIndex[8]` + `houseChar[]` with `'R'` |
| `include/structures/Palace.h` | Added `bool spawnRebelsUnits()` declaration |
| `src/sand.cpp` | Added "Rebels" to `getHouseByName()`, `getHouseNameByNumber()`, weakness function |
| `src/FileClasses/SFXManager.cpp` | Added `HOUSE_REBELS` switch case (Harkonnen voice prefix) |
| `src/FileClasses/GFXManager.cpp` | Herald (PNG + fallback), mentat background, MentatHouseChoiceInfoQuestion, Custom_IBM.pal loading |
| `src/FileClasses/TextManager.cpp` | Added `mentatStrings[HOUSE_REBELS]`, briefing text, mentat entries |
| `src/structures/Palace.cpp` | Added `HOUSE_REBELS` in `doSpecialWeapon()`, `spawnRebelsUnits()` (1-2 sonic tanks, HUNT) |
| `src/INIMap/INIMapLoader.cpp` | Added `HOUSE_REBELS` to Unit_Special alternating Sonic/Devastator logic |
| `src/Menu/CustomGamePlayers.cpp` | Rebels bound house from map, `Player8` support, AI-only skip in dropdown |
| `src/Menu/BriefingMenu.cpp` | Added `HOUSE_REBELS` to Harkonnen music groups |
| `src/Menu/MentatMenu.cpp` | Added `HOUSE_REBELS` to drawSpecificStuff shoulder position |
| `src/Menu/HouseChoiceInfoMenu.cpp` | Added `HOUSE_REBELS` planet animation (Harkonnen) |
| `src/Menu/MapChoice.cpp` | Added `HOUSE_REBELS` → "REB" key |
| `src/MapEditor/MapEditorInterface.cpp` | Rebels visible in editor |
| `src/ObjectBase.cpp` | House-related changes |
| `src/main.cpp` | Custom_IBM.pal loading for Tornie mod (palette 192-199 grey ramp) |
| `mods/Tornie/ObjectData.ini` | `Builder(R)` / `TechLevel(R)` for Deviator (disabled), Sonic Tank, EliteSiegeTank, FlameTank |

### Uncommitted (fixes for crash/UB/visual gaps)
| File | Change |
|---|---|
| `include/dunecity/CitySimulation.h` | `kMaxCityHouses` 7 → 8 (prevents city state corruption) |
| `src/FileClasses/GFXManager.cpp` | `houseAnchor[8]` dark grey entry, `GameStatsBackground` + `MapChoiceScreen` for Rebels |
| `src/FileClasses/PictureFactory.cpp` | `HOUSE_REBELS` cases in `createGameStatsBackground`, `createMentatHouseChoiceQuestion`, `createMapChoiceScreen` (prevents THROW crash) |
| `src/Tile.cpp` | Radar color for Rebels (was black) |

## What Works
- HOUSE_REBELS = 7 compiles and links cleanly
- Palette index 192 (PALCOLOR_REBELS) — same as FREMEN in standard palette; in Tornie mod, Custom_IBM.pal overrides entries 192-199 to dark grey
- Voice: Harkonnen voice lines for all Rebels units
- Herald: loads HeraldRebels.png (Tornie mod), falls back to remapped Neutral herald
- Palace special weapon: spawns 1-2 Sonic Tanks in HUNT mode
- ObjectData.ini: Deviator disabled, Sonic Tank enabled (TL6), FlameTank (TL8), EliteSiegeTank (TL9)
- AI-only: excluded from player house dropdown in CustomGamePlayers
- Radar: dark grey blip (palette 192)
- City simulation: kMaxCityHouses = 8, Rebels city state tracked independently
- All switch statements with `default: THROW(...)` now handle HOUSE_REBELS

## What Is Untested
- Actual Tornie mod gameplay with Rebels AI opponent on a map
- Custom_IBM.pal grey ramp visual appearance in-game
- HeraldRebels.png rendering at both 1x and 2x scales
- Palace sonic tank spawn positioning under combat conditions
- Map editor: placing Rebels units/structures and saving/loading .ini maps
- Multiplayer: 8-house maps with Rebels as AI opponent

## PUSH_REQUIRED
Next version bump + tag needed before push.
