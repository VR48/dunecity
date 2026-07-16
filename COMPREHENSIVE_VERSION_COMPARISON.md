# DuneCity Comprehensive Version Comparison

**Per Tornie's OOB**: "faire le tri dans tout, prendre ce qui fonctionne, comparer pour le prochain, sans rien oublier, tout est une question d'assemblage" = sort through everything, take what works, compare for the next one, without forgetting anything, it's all about assembly.

This document inventories EVERY change between the v1.0.305 baseline (last known-good before the v1.0.460+ pendulum) and the current v1.0.481, so we can pick the right pieces for the next release.

## 1. Tint function changes (getZoomedObjPic)

| Version | Remap call | Surface palette write | REBELS special | Tornie assessment |
|---|---|---|---|---|
| v1.0.173 | `mapSurfaceColorRange(src, 144, houseToPaletteIndex[house])` | None | None | **✓ stable, no glitch** |
| v1.0.305 | `mapSurfaceColorRange(src, 144, destSlot)` (uses variable) | None | None (4-ID hardcoded list) | **✓ last good baseline** |
| v1.0.393 | Same as v1.0.305 | None | **REBELS-only** `SDL_SetPaletteColors(surface, palette[PALCOLOR_FREMEN..+7], 192, 8)` | ID 70 crashes |
| v1.0.460 | + houseToPaletteIndex[REBELS] = 52 (was 192) | None | (REBELS surface write still targets 192, doesn't match) | bug |
| v1.0.464 | Same | **ALL houses** `palette[destSlot..+7] → surface` | None (REBELS covered by all-houses) | multi-color ghost fixed |
| v1.0.465 | Same | + invalidateAllSpriteTextures on game init | None | clear cache |
| v1.0.466 | Same | + runtime palette write at every game init | None | runtime palette fresh |
| v1.0.467 | Same | Same | + REBELS savegame colorIndex default = 52 | savegame compat |
| v1.0.468 | Same | + generic unit fallback (any ID, not 4-ID list) | None | ID 70 fixed |
| v1.0.470 | Same | None (reverted v1.0.464) | None | only HARKONNEN correct |
| v1.0.471 | Same | **ALL houses** re-added (v1.0.464 restore) | None | non-HARKONNEN fixed |
| v1.0.473 | Same | None (v1.0.393-like) | REBELS-only via `houseToPaletteIndex[HOUSE_REBELS]` = 52 | Ordos corruption |
| v1.0.475 | Same as v1.0.305 | None | None | **✓ clean baseline, no editor visual bug** |
| v1.0.476 | Same | None | + `applyRebelsTint()` (Rec. 601 luminance + 75% darker) | visual bug returned |
| v1.0.480 | Same | None | + 100% desaturation (simple RGB average) | **v1.0.475+ desaturation** |
| v1.0.481 | Same | None | + applyRebelsTint in getUIGraphicSurface | editor preview desaturation |

**WORKING:**
- v1.0.173 pure remap pattern (no surface write): single `mapSurfaceColorRange` call ✓
- REBELS desaturation: `(R+G+B)/3 * 0.25` for full greyscale + darken ✓
- v1.0.477 generic unit fallback (any ID) ✓

**BUGS:**
- v1.0.473-481: visual bug returned (editor) - the `applyRebelsTint` breaks something
- v1.0.475 had no desaturation (REBELS showed vanilla orange)

## 2. REBELS color slot changes

| Version | houseToPaletteIndex[REBELS] | Custom_IBM.PAL[REBELS slot] | runtime palette write |
|---|---|---|---|
| v1.0.173 | 192 (FREMEN) | n/a | no |
| v1.0.305 | 192 (FREMEN) | n/a | no |
| v1.0.460 | **52** (new slot) | sat0/dark75 dark grey | at first init |
| v1.0.461 | 52 | same | at first init |
| v1.0.466 | 52 | same | at every game init |
| v1.0.467 | 52 | same | + savegame default 52 |
| v1.0.473 | 52 | same | none (REBELS surface only) |
| v1.0.475 | 192 (reverted to FREMEN) | n/a | no |
| v1.0.476 | 192 | n/a | no (uses applyRebelsTint) |
| v1.0.480 | 192 | n/a | no (100% desaturation) |
| v1.0.481 | 192 | n/a | no (applyRebelsTint both paths) |

**WORKING:** 
- v1.0.173-305: REBELS at 192, reads as FREMEN (vanilla orange)
- v1.0.460+ with applyRebelsTint: REBELS gets the dark grey via in-surface desaturation (no runtime override needed)

**CURRENT STATE:** REBELS at 192 (vanilla) + applyRebelsTint does the desaturation locally

## 3. Unit fallback changes

| Version | Fallback for missing units |
|---|---|
| v1.0.305 | 4-ID hardcoded list: Deviator, Flame, Sonic, EliteSiege → Siege tank |
| v1.0.468 | **Any missing unit ID** → Siege tank |
| v1.0.477 | **Any missing unit ID** → Siege tank (re-applied) |
| v1.0.478 | Same, with better comments |

**WORKING:** v1.0.477 generic fallback covers ID 70 + any other missing sprite

## 4. Cache invalidation changes

| Version | invalidateAllSpriteTextures behavior |
|---|---|
| v1.0.465 | clear objPicTex + objPic + uiGraphic (too aggressive) |
| v1.0.468 | clear objPicTex + objPic only (preserve uiGraphic) |
| v1.0.473+ | **NOT CALLED** in Game::initGame() (removed by v1.0.476 reset) |

**STATUS:** Cache invalidation is currently DISABLED (v1.0.475 onwards). Tornie reports the v1.0.480 first-launch invisibility bug is related — texture cache from previous game state isn't being cleared.

## 5. ObjectData.ini / PAK / Sprite changes

| Version | Change | Status |
|---|---|---|
| v1.0.345 | Units.zip sprites (EliteSiegeTank + FlameTank) | ✓ works |
| v1.0.345 | vanilla MENTAT*.CPS in PAK | ✓ works |
| v1.0.345 | HeraldRebels.png from upload | ✓ works |
| v1.0.345 | Flame Tank self-damage fix in Bullet.cpp | ✓ works |
| v1.0.358 | scenr*.ini re-routed Mercenary → Rebels | ✓ works |
| v1.0.450 | Custom_IBM.PAL[192..199] = yellow (spectator) | ✓ works (but unused) |
| v1.0.463 | Custom_IBM.PAL[52..59] = true greyscale (R==G==B) | ✓ works (but unused after v1.0.476 reset) |
| v1.0.469 | Custom_IBM.PAL[52..59] = greyscale preserving v1.0.305 luminance | ✓ works (but unused) |

## 6. Mentat / UI graphics

| Version | Change | Status |
|---|---|---|
| v1.0.374 | Mentat portrait ibmPalette re-tint for REBELS+NEUTRAL | ✓ works (v1.0.475+ has it) |
| v1.0.481 | applyRebelsTint added to getUIGraphicSurface | ✓ REBELS desaturation in editor |

## 7. Campaign / savegame

| Version | Change | Status |
|---|---|---|
| v1.0.358 | scenr001-022 [STRUCTURES] re-routed to REBELS | ✓ works |
| v1.0.403 | REGION*.INI regenerated | ✓ works |
| v1.0.467 | REBELS savegame colorIndex default = 52 | ✓ works (but house 205 bug) |

## 8. Custom game color swap

| Version | Change | Status |
|---|---|---|
| v1.0.457 | Remove duplicate Original entry | ✓ works |
| v1.0.458 | Add Bright Yellow entry (data=-14) | ✗ might cause "house 205 not found" |
| v1.0.459 | Teal at ORDOS slot 176 not REBELS slot 192 | ✓ works |

**BUG SUSPECT:** v1.0.458 Bright Yellow data=-14 might be the source of "house 205 not found" in Neutral campaign (colorIndex 205 internally?)

## 9. Map editor

| Version | Change | Status |
|---|---|---|
| v1.0.359 | Remove greyPlace from MapEditor.cpp | ✓ works |
| v1.0.391 | MapEditorPen{1,3,5}x{1,3,5}.png brush icons | ✓ works |

## 10. Bug summary (current v1.0.481)

| Bug | Likely cause | Reference version |
|---|---|---|
| Units/buildings invisible at first launch | No cache invalidation on game init | v1.0.465 → reverted by v1.0.476 reset |
| DUNE1 ID 70 + Siegetank_Gun missing | v1.0.477 fallback not reached? | v1.0.477 |
| "house 205 not found" Neutral campaign | v1.0.458 Bright Yellow data=-14 or colorIndex bug | v1.0.458 |
| Sonic/Flame Tank wrong sidebar icon | icon path differs from getZoomedObjPic | needs investigation |
| FREMEN has REBELS tint in editor | v1.0.480 desaturation accidentally affects FREMEN | needs investigation |
| Editor visual bug returned (vs v1.0.475) | applyRebelsTint breaks something | v1.0.476-481 |

## 11. Recommended next release (Tornie's choice)

**OPTION A — Return to v1.0.475 baseline + add only the desaturation:**
- v1.0.475 was clean (no visual bugs)
- Add only the `applyRebelsTint` call to `getZoomedObjPic` (not getUIGraphicSurface)
- This gives REBELS desaturation without the editor visual bug
- May still have the v1.0.475 missing unit fallback (ID 70 issue)

**OPTION B — Fix all known bugs + add desaturation:**
- v1.0.475 baseline + applyRebelsTint
- Re-add v1.0.477 generic unit fallback
- Re-add v1.0.465 cache invalidation (objPic only, not uiGraphic)
- Fix "house 205" by removing v1.0.458 Bright Yellow entry (or fixing it)
- Fix Sonic/Flame Tank sidebar icon
- Fix FREMEN tint contamination

**OPTION C — Full v1.0.305 reset + selective re-applies:**
- Start from v1.0.305 commit hash
- Re-apply only the changes Tornie confirms are needed
- No surface palette writes, no runtime palette overrides
- Just vanilla remap + REBELS desaturation

## 12. Files in each state

- `src/FileClasses/GFXManager.cpp`: v1.0.481 has applyRebelsTint in both getZoomedObjPic and getUIGraphicSurface
- `include/FileClasses/GFXManager.h`: v1.0.481 has invalidateAllSpriteTextures (unused since v1.0.475)
- `src/Game.cpp`: v1.0.466 has cache invalidation + REBELS runtime palette write (unused since v1.0.475)
- `src/Tile.cpp`: v1.0.476+ reverted to v1.0.173 baseline (no per-house sprite)
- `include/globals.h`: v1.0.476+ reverted to v1.0.173 (REBELS at 192)
- `include/GameInitSettings.h`: v1.0.476+ reverted to v1.0.173 (no colorIndex field)
- `src/Menu/CustomGamePlayers.cpp`: v1.0.476+ reverted to v1.0.173 (vanilla color dropdown)
- `src/main.cpp`: v1.0.476+ reverted to v1.0.173 (no runtime palette override)
