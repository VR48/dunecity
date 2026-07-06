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
static const int houseToPaletteIndex[NUM_HOUSES] = { PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN, PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, PALCOLOR_REBELS };    ///< the base colors for the different houses
static const char houseChar[] = { 'H', 'A', 'O', 'F', 'S', 'M', 'N', 'R' };   ///< character for each house

/// Returns the SDL_Color for the given house at palette offset.
/// Houses 1..7 use the vanilla ibmPalette so the editor shows the
/// correct vanilla colour.  The 8th house (HOUSE_REBELS) reads from
/// the runtime 'palette' which has been overridden by Custom_IBM.pal
/// at indices 192-199 with a dark-grey/black ramp. Fremen keeps
/// using ibmPalette so the orange vanilla colour shows through.
///
/// DuneCity 1.0.408: HOUSE_FREMEN still reads from ibmPalette
/// (vanilla orange). Only HOUSE_REBELS reads from the runtime
/// 'palette' (Custom_IBM.pal values). Tornie's OOB clarification
/// 'i correct my sentence' = the getHouseSDLColor function
/// should keep Fremen on ibmPalette and Rebels on palette.
inline SDL_Color getHouseSDLColor(int house, int offset = 3) {
    const Palette& pal = (house == HOUSE_REBELS) ? palette : ibmPalette;
    return pal[houseToPaletteIndex[house] + offset];
}

#endif //GLOBALS_H
