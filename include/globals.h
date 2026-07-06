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
EXTERN Palette              palette;                    ///< the palette for the screen (may include mod overrides)
EXTERN Palette              ibmPalette;                 ///< vanilla IBM.PAL before any mod overrides (used for Fremen colours)
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


// constants
// DuneCity 1.0.445: REBELS uses palette index 30 (start of
// the per-house ramp). The 8 colors at 30-37 are overridden
// at GFX init from the specific IBM.PAL indices Tornie picked:
// 28, 29, 30, 31, 122, 175, 12 (7 colors) + the closest match
// from 30-37 for the 8th slot to keep the ramp size at 8.
//
// Tornie's OOB: 'Rebels colors index is 28,29,30,31,122,175,12
// from IBM.PAL' = pick those 7 specific dark grey colors from
// vanilla IBM.PAL as the REBELS tint. We pad to 8 entries by
// using 30-37 in vanilla order for the missing slot.
//
// All 8 houses read from the SAME palette via houseToPaletteIndex[].
// REBELS starts at index 30 (which already holds one of Tornie's
// picked colors at index 30). The other 7 picked colors populate
// 31-37. No custom overrides elsewhere.
static const int houseToPaletteIndex[NUM_HOUSES] = { PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN, PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, 30 };    ///< the base colors for the different houses (REBELS = 30)

// DuneCity 1.0.445: Tornie's picked REBELS tint indices
// from vanilla IBM.PAL. Tornie specified the 8 indices to
// use for the REBELS tint ramp - 28, 29, 30, 31, 122, 175,
// 12, 12 (the 8th slot repeats 12 per Tornie's OOB 'last
// is 12 again'). These 7 unique dark grey colors give
// REBELS a dark grey tint matching the Tornie mod aesthetic.
static const int REBELS_TINT_INDICES[8] = { 28, 29, 30, 31, 122, 175, 12, 12 };    ///< IBM.PAL indices for REBELS tint ramp
static const char houseChar[] = { 'H', 'A', 'O', 'F', 'S', 'M', 'N', 'R' };   ///< character for each house

/// Returns the SDL_Color for the given house at palette offset.
/// All 8 houses read from the same runtime palette via
/// houseToPaletteIndex[house]. REBELS reads from index 30 which
/// was populated at GFX init from the Tornie-picked IBM.PAL indices.
inline SDL_Color getHouseSDLColor(int house, int offset = 3) {
    return palette[houseToPaletteIndex[house] + offset];
}

#endif //GLOBALS_H
