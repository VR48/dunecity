# Stability Notes - Tortoise Going Backwards

## Current state (v1.0.475)

The tint function matches the v1.0.392/v1.0.393 algorithm:
```cpp
objPic[id][house][z] = mapSurfaceColorRange(
    objPic[id][HOUSE_HARKONNEN][z].get(),
    PALCOLOR_HARKONNEN, houseToPaletteIndex[house]);

if(house == HOUSE_REBELS && objPic[id][house][z] && objPic[id][house][z]->format->palette) {
    SDL_Color rebelsOverride[8];
    const int rebelsBase = houseToPaletteIndex[HOUSE_REBELS];  // = 52
    for(int k = 0; k < 8; k++) {
        rebelsOverride[k] = palette[rebelsBase + k];
    }
    SDL_SetPaletteColors(objPic[id][house][z]->format->palette,
                          rebelsOverride, rebelsBase, 8);
}
```

## Outstanding issues (per Tornie's OOBs)

1. **Ordos corruption**: v1.0.471/473 surface palette write for non-REBELS houses
   may have introduced Ordos tint artifacts. Untangled but Tornie says "things
   got worse" after my fix attempts. STATUS: needs proper diagnosis, not
   another well-intentioned patch.

2. **Multi-color ghost**: was fixed in v1.0.464+v1.0.465+v1.0.471 via surface
   palette write + cache invalidation. May have re-introduced Ordos issue.

3. **Missing units**: fixed in v1.0.468 via generic unit fallback.

4. **Savegame compatibility**: fixed in v1.0.467 (colorIndex default = 52).

## Sorry about the churn

Per Tornie's OOB "j'ai comme l'impression qu'on est sur
une planète qui recule" = I feel like we're going
backwards. The v1.0.460-v1.0.474 cycle was a pendulum
between tint corruption (with surface write) and
tint absence (without). Each fix introduced a new
side effect that required another fix.

v1.0.475 freezes the v1.0.473 code in place. The next
patch should be smaller and more targeted once the
underlying Ordos bug is properly diagnosed.

