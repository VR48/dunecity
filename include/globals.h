/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <DataTypes.h>
#include <Definitions.h>
#include <Colors.h>
#include <FileClasses/Palette.h>
#include <data.h>
#include <misc/RobustList.h>
#include <misc/DrawingRectHelper.h>

#include <misc/SDL2pp.h>

#include <array>
#include <memory>

#define _(msgid) pTextManager->getLocalized(msgid)

// forward declarations
class SoundPlayer;
class MusicPlayer;

class FileManager;
class GFXManager;
class SFXManager;
class FontManager;
class TextManager;
class NetworkManager;

class Game;
class Map;
class ScreenBorder;
class House;
class HumanPlayer;
class UnitBase;
class StructureBase;
class Bullet;

#ifndef SKIP_EXTERN_DEFINITION
 #define EXTERN extern
#else
 #define EXTERN
#endif


// SDL stuff
EXTERN SDL_Window*          window;                     ///< the window
EXTERN SDL_Renderer*        renderer;                   ///< the renderer
EXTERN SDL_Texture*         screenTexture;              ///< the texture
EXTERN Palette              palette;                    ///< the palette for the screen
EXTERN Palette              customPalette;              ///< optional Custom_IBM.PAL for extra visual color ramps
EXTERN bool                 customPaletteLoaded;        ///< true when Custom_IBM.PAL is available
EXTERN int                  drawnMouseX;                ///< the current mouse position (x coordinate)
EXTERN int                  drawnMouseY;                ///< the current mouse position (y coordinate)
EXTERN int                  currentZoomlevel;           ///< 0 = the smallest zoom level, 1 = medium zoom level, 2 = maximum zoom level


// abstraction layers
EXTERN std::unique_ptr<SoundPlayer>         soundPlayer;                ///< manager for playing sfx and voice
EXTERN std::unique_ptr<MusicPlayer>         musicPlayer;                ///< manager for playing background music

EXTERN std::unique_ptr<FileManager>         pFileManager;               ///< manager for loading files from PAKs
EXTERN std::unique_ptr<GFXManager>          pGFXManager;                ///< manager for loading and managing graphics
EXTERN std::unique_ptr<SFXManager>          pSFXManager;                ///< manager for loading and managing sounds
EXTERN std::unique_ptr<FontManager>         pFontManager;               ///< manager for loading and managing fonts
EXTERN std::unique_ptr<TextManager>         pTextManager;               ///< manager for loading and managing texts and providing localization
EXTERN std::unique_ptr<NetworkManager>      pNetworkManager;            ///< manager for all network events (nullptr if not in multiplayer game)

// game stuff
EXTERN Game*                currentGame;                ///< the current running game
EXTERN ScreenBorder*        screenborder;               ///< the screen border for the current running game
EXTERN Map*                 currentGameMap;             ///< the map for the current running game
EXTERN House*               pLocalHouse;                ///< the house of the human player that is playing the current running game on this computer
EXTERN HumanPlayer*         pLocalPlayer;               ///< the player that is playing the current running game on this computer

EXTERN RobustList<UnitBase*>       unitList;            ///< the list of all units
EXTERN RobustList<StructureBase*>  structureList;       ///< the list of all structures
EXTERN RobustList<Bullet*>         bulletList;          ///< the list of all bullets


// misc
EXTERN SettingsClass    settings;                       ///< the settings read from the settings file
EXTERN SettingsClass::GameOptionsClass effectiveGameOptions;  ///< effective game options (settings + mod overrides)

EXTERN bool debug;                                      ///< is set for debugging purposes
EXTERN std::array<int, NUM_HOUSES> houseToVisualHouse;  ///< runtime visual color slot for each house


// constants
static const int houseToPaletteIndex[NUM_HOUSES] = { PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN, PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, PALCOLOR_REBELS };    ///< the base colors for the different houses
static const int houseColorToPaletteIndex[NUM_HOUSE_COLOR_SLOTS] = {
    PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN,
    PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, PALCOLOR_REBELS,
    PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN,
    PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY
};    ///< palette ramp used by house colors, including custom visual-only colors
static const char houseChar[] = { 'H', 'A', 'O', 'F', 'S', 'M', 'N', 'R' };   ///< character for each house

inline bool isValidHouseColorSlot(int colorSlot) {
    return colorSlot >= 0 && colorSlot < NUM_HOUSE_COLOR_SLOTS;
}

inline bool isCustomHouseColorSlot(int colorSlot) {
    return colorSlot >= NUM_HOUSES && colorSlot < NUM_HOUSE_COLOR_SLOTS;
}

inline int getHouseVisualHouse(int house) {
    if(house >= 0 && house < NUM_HOUSES) {
        const int visualHouse = houseToVisualHouse[house];
        if(isValidHouseColorSlot(visualHouse)) {
            return visualHouse;
        }
    }

    return house;
}

inline int getHouseColorPaletteIndex(int house) {
    const int visualHouse = getHouseVisualHouse(house);
    if(isValidHouseColorSlot(visualHouse)) {
        return houseColorToPaletteIndex[visualHouse];
    }

    return PALCOLOR_HARKONNEN;
}

inline int getHouseColorPaletteIndexFromSlot(int colorSlot) {
    if(isValidHouseColorSlot(colorSlot)) {
        return houseColorToPaletteIndex[colorSlot];
    }

    return PALCOLOR_HARKONNEN;
}

inline const Palette& getPaletteForHouseColorSlot(int colorSlot) {
    return isCustomHouseColorSlot(colorSlot) && customPaletteLoaded ? customPalette : palette;
}

inline SDL_Color getHouseColorSDL(int colorSlot, int shadeOffset = 3) {
    const Palette& sourcePalette = getPaletteForHouseColorSlot(colorSlot);
    const int paletteIndex = getHouseColorPaletteIndexFromSlot(colorSlot) + shadeOffset;
    if(paletteIndex >= 0 && paletteIndex < sourcePalette.getNumColors()) {
        return sourcePalette[paletteIndex];
    }

    return SDL_Color{ 0, 0, 0, 255 };
}

inline Uint32 getHouseColorRGB(int colorSlot, int shadeOffset = 3) {
    return SDL2RGB(getHouseColorSDL(colorSlot, shadeOffset));
}

void loadCustomPalette();
void applyCustomPaletteRuntimeHouseRamps();
void resetHouseVisualHouseMapping();
void setHouseVisualHouse(HOUSETYPE house, int visualHouse);

#endif //GLOBALS_H
