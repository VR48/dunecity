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

#include <globals.h>

#include <SoundPlayer.h>
#include <FileClasses/music/MusicPlayer.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <Network/NetworkManager.h>

// Explicit definitions of global variables (instead of relying on EXTERN macro)
// SDL stuff
SDL_Window*          window = nullptr;
SDL_Renderer*        renderer = nullptr;
SDL_Texture*         screenTexture = nullptr;
Palette              palette;
int                  drawnMouseX = 0;
int                  drawnMouseY = 0;
int                  currentZoomlevel = 0;

// abstraction layers
std::unique_ptr<SoundPlayer>         soundPlayer;
std::unique_ptr<MusicPlayer>         musicPlayer;
std::unique_ptr<FileManager>         pFileManager;
std::unique_ptr<GFXManager>          pGFXManager;
std::unique_ptr<SFXManager>          pSFXManager;
std::unique_ptr<FontManager>         pFontManager;
std::unique_ptr<TextManager>         pTextManager;
std::unique_ptr<NetworkManager>      pNetworkManager;

// game stuff
Game*                currentGame = nullptr;
ScreenBorder*        screenborder = nullptr;
Map*                 currentGameMap = nullptr;
House*               pLocalHouse = nullptr;
HumanPlayer*         pLocalPlayer = nullptr;

RobustList<UnitBase*>       unitList;
RobustList<StructureBase*>  structureList;
RobustList<Bullet*>         bulletList;

// misc
SettingsClass    settings;
SettingsClass::GameOptionsClass effectiveGameOptions;
bool debug = false;
