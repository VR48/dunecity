#ifndef DUNECITY_HOUSE_COLORS_H
#define DUNECITY_HOUSE_COLORS_H
#include <Colors.h>
#include <array>
#include <algorithm>
namespace DuneCity {
// House colour slots, not player identities: explicit lobby colour choices still work.
inline constexpr std::array<SDL_Color,8> houseMarkerColors{{
    {255,64,72,255}, {68,128,255,255}, {60,225,88,255}, {244,244,232,255},
    {255,80,212,255}, {255,152,40,255}, {40,235,245,255}, {172,116,255,255}
}};
// Neutral's original IBM palette ramp is grey, which disappears into the rock
// colour on the radar. This is radar-only: Neutral keeps its existing sprite,
// UI and lobby colour treatment.
inline constexpr Uint32 neutralRadarColor = COLOR_RGB(40,235,245);
inline SDL_Color houseColorShade(int slot, int shade) {
    if (slot < 0 || slot >= 8) return {0,0,0,255};
    constexpr int levels[8] = {100,90,80,70,58,46,34,23};
    const int scale=levels[std::clamp(shade,0,7)];
    const auto c=houseMarkerColors[slot];
    return {static_cast<Uint8>(c.r*scale/100),static_cast<Uint8>(c.g*scale/100),
            static_cast<Uint8>(c.b*scale/100),255};
}
inline Uint32 radarTerrainColor(Uint32 color) {
    // Ownership markers keep full brightness; only terrain is subdued.
    return COLOR_RGB(((color&RMASK)>>RSHIFT)*55/100,
                     ((color&GMASK)>>GSHIFT)*55/100,
                     ((color&BMASK)>>BSHIFT)*55/100);
}
}
#endif
