# Bug Fix Report: Cursor + Neutral Herald

## Bug 1: Cursor disappearing on campaign start (ALL houses)

### Root Cause

Three compounding issues in `src/CursorManager.cpp`:

**1. Static cache not cleared on cleanup.**
`CursorManager::cleanup()` nulled the instance pointers and reset `initialized = false`, but left the static `CursorCache` holding the old `SDL_Cursor*` objects. On the next `initialize()` call (new game), `cache.normal != nullptr` caused cursor *creation* to be skipped — the old SDL cursor pointers were reused without being recreated from fresh surfaces. On platforms where SDL cursor objects are backed by platform-specific resources (Windows D3D11), these stale pointers can become invisible after a renderer or window lifecycle event.

**2. `SDL_ShowCursor(SDL_ENABLE)` was inside `if (normalCursor)`.**
If `SDL_CreateColorCursor` failed (e.g. on a platform that can't handle a paletted cursor surface), `normalCursor` would be null and `SDL_ShowCursor(SDL_ENABLE)` would never be called. The cursor would stay hidden even though the OS default cursor was usable.

**3. No factored-out `clear()` on `CursorCache`.**
The destructor freed cursors correctly, but there was no way to reset the cache mid-session from `cleanup()`. Adding `clear()` lets `cleanup()` invalidate the cache without requiring program exit.

### Files Changed

`src/CursorManager.cpp`

- Added `CursorCache::clear()` method that frees all SDL cursor objects and nulls pointers.
- `CursorManager::cleanup()` now calls `getCursorCache().clear()` so `initialize()` always recreates cursors from current GFXManager surfaces on the next game start. Prevents stale SDL_Cursor* reuse.
- Moved `SDL_ShowCursor(SDL_ENABLE)` outside the `if (normalCursor)` block in `initialize()` so it is called unconditionally — if cursor image creation fails, the OS default cursor is still shown.

---

## Bug 2: Neutral herald/banner missing in campaign house-choice info screen

### Root Cause

Two related gaps in HOUSE_NEUTRAL support:

**1. `createMentatHouseChoiceQuestion()` had no HOUSE_NEUTRAL case.**
The switch in `PictureFactory::createMentatHouseChoiceQuestion()` covered HARKONNEN, ATREIDES, ORDOS, FREMEN, SARDAUKAR, MERCENARY but fell through to `default: break` for HOUSE_NEUTRAL. This left `pQuestionPart2` as `nullptr`. The very next line unconditionally called `SDL_SetColorKey(pQuestionPart2.get(), SDL_TRUE, 0)` — a null-pointer dereference and crash.

**2. `GFXManager::loadUIGraphics()` never populated `UI_MentatHouseChoiceInfoQuestion[HOUSE_NEUTRAL]`.**
Without the entry, `getUIGraphic(UI_MentatHouseChoiceInfoQuestion, HOUSE_NEUTRAL)` fell back to a colour-remapped copy of the HOUSE_HARKONNEN banner (via the generic remap path in `getUIGraphicSurface`), showing the wrong emblem with wrong colours.

### Files Changed

**`src/FileClasses/PictureFactory.cpp`**
- Added `case HOUSE_NEUTRAL` in `createMentatHouseChoiceQuestion()`: reuses the Ordos banner slot from the sprite sheet, then remaps Ordos green palette entries to the neutral grey palette range via `mapSurfaceColorRange(pOrdosPart.get(), PALCOLOR_ORDOS, PALCOLOR_NEUTRAL)`. Matches the same colour-remap pattern used by `createHeraldNeu()`. No new PNG asset required.
- Added a null-guard after the switch: if `pQuestionPart2` is still null (future-proofing for unknown house IDs), a blank 208×48 transparent surface is created rather than crashing.

**`src/FileClasses/GFXManager.cpp`**
- Added `uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_NEUTRAL] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_NEUTRAL, benePalette);` immediately after the MERCENARY entry, so the texture is properly cached and `getUIGraphic` returns it directly without falling back to the remapped-Harkonnen path.

---

## Test Results

```
All tests passed (1119 assertions in 315 test cases)
```

315 passing, 0 failing.

---

## Remaining Concerns

**Cursor (Windows):** The fix addresses static-cache invalidation by always recreating cursors on game start. This adds a small one-time cost per game session (a handful of `SDL_CreateColorCursor` calls). The root cause — SDL cursor objects becoming invalid after renderer teardown on Windows D3D backends — is resolved by forcing recreation, but cannot be verified without a Windows build.

**Neutral herald appearance:** The grey-remapped Ordos banner is a functional stand-in matching the pattern used elsewhere in the codebase. If a designer wants a distinct "Neutral" house name graphic (like Fremen/Sardaukar/Mercenary have their custom PNGs), placing a `Neutral.png` (104×24 px, same format as the others) in the game data directory is the upgrade path. `createMentatHouseChoiceQuestion` can then be extended to load it.
