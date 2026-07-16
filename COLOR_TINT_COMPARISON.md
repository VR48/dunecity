# DuneCity Color Tint Function: v1.0.173 vs v1.0.305 vs v1.0.471

## Stable baselines

### v1.0.173 (vanilla Dune Legacy tint)
```cpp
objPic[id][house][z] = mapSurfaceColorRange(
    objPic[id][HOUSE_HARKONNEN][z].get(),
    PALCOLOR_HARKONNEN,
    houseToPaletteIndex[house]);
```
**NO surface palette write.**
Software renderer (Dune Legacy) reads from the runtime palette via SDL surface paletted textures.

### v1.0.305 (vanilla+ Tornie mod, no override)
Same as v1.0.173:
```cpp
objPic[id][house][z] = mapSurfaceColorRange(
    objPic[id][HOUSE_HARKONNEN][z].get(),
    PALCOLOR_HARKONNEN,
    houseToPaletteIndex[house]);
```
**NO surface palette write.** The engine picked up the runtime palette read at SDL hardware renderer (via the screen texture palette).

### v1.0.471 (current DuneCity+Tornie)
```cpp
int destSlot = houseToPaletteIndex[house];
int swapSlot = getHouseColorSwap(house);
if(swapSlot >= 0) {
    destSlot = swapSlot;
}
objPic[id][house][z] = mapSurfaceColorRange(
    objPic[id][HOUSE_HARKONNEN][z].get(),
    PALCOLOR_HARKONNEN,
    destSlot);

// Surface palette write (v1.0.464+v1.0.471):
if(objPic[id][house][z] && objPic[id][house][z]->format->palette) {
    for(int k = 0; k < 8; k++) {
        objPic[id][house][z]->format->palette->colors[destSlot + k] =
            palette[destSlot + k];
    }
}
```
**WITH surface palette write** (required for SDL2 hardware renderer).

## Difference summary

| Element | v1.0.173 | v1.0.305 | v1.0.471 |
|---|---|---|---|
| Remap call signature | ✓ same | ✓ same | ✓ same |
| Surface palette write | ✗ | ✗ | ✓ (added v1.0.464) |
| Civic sprite fallback | ✓ | ✓ | ✓ |
| Tornie unit fallback | ✗ | ✓ | ✗ (replaced w/ generic v1.0.468) |
| Generic unit fallback | ✗ | ✗ | ✓ |
| Windtrap animation | ✓ | ✓ | ✓ |
| Truecolor blend mode | ✓ | ✓ | ✓ |
| Pixel shift direction | 144→destSlot | 144→destSlot | 144→destSlot |
| House index mapping | vanilla | vanilla | +REBELS=52 (v1.0.460) |
| Runtime palette REBELS | vanilla | vanilla | +greyscale dark (v1.0.461+v1.0.466) |

## Why v1.0.471 differs

1. **REBELS uses slot 52** (was 192 in v1.0.173/305). Per Tornie's OOB "nouvel indice est 52" (v1.0.460).
2. **Runtime palette[52..59] = greyscale dark** (Custom_IBM.PAL sat0/dark75 v1.0.460).
3. **Surface palette write** added in v1.0.464 to fix the v1.0.470 regression where SDL2 hardware renderer only read HARKONNEN's color from the surface palette (Tornie's "toutes les team bizarres sauf harkonnen" OOB).
4. **Generic unit fallback** in v1.0.468 replaces the v1.0.305 4-ID hardcoded list (Tornie's ID 70 crash).
