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
#include <FileClasses/Palfile.h>
#include <Network/NetworkManager.h>

// Explicit definitions of global variables (instead of relying on EXTERN macro)
// SDL stuff
SDL_Window*          window = nullptr;
SDL_Renderer*        renderer = nullptr;
SDL_Texture*         screenTexture = nullptr;
Palette              palette;
Palette              customPalette;
bool                 customPaletteLoaded = false;
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
std::array<int, NUM_HOUSES> houseToVisualHouse = {
    HOUSE_HARKONNEN,
    HOUSE_ATREIDES,
    HOUSE_ORDOS,
    HOUSE_FREMEN,
    HOUSE_SARDAUKAR,
    HOUSE_MERCENARY,
    HOUSE_NEUTRAL,
    HOUSE_REBELS
};

void loadCustomPalette() {
    customPaletteLoaded = false;

    if(pFileManager == nullptr || !pFileManager->exists("Custom_IBM.PAL")) {
        return;
    }

    try {
        customPalette = LoadPalette_RW(pFileManager->openFile("Custom_IBM.PAL").get());
        customPaletteLoaded = customPalette.getNumColors() >= 256;
    } catch(const std::exception& e) {
        SDL_Log("Warning: Could not load Custom_IBM.PAL: %s", e.what());
        customPalette = Palette();
        customPaletteLoaded = false;
    }
}

void applyCustomPaletteRuntimeHouseRamps() {
    if(palette.getNumColors() < 256) {
        return;
    }

    static const Uint8 rebelsGreyRamp[8] = { 82, 72, 62, 52, 42, 34, 27, 20 };
    for(int k = 0; k < 8; k++) {
        SDL_Color& color = palette[PALCOLOR_REBELS + k];
        color.r = rebelsGreyRamp[k];
        color.g = rebelsGreyRamp[k];
        color.b = rebelsGreyRamp[k];
        color.a = 255;
    }
}

void resetHouseVisualHouseMapping() {
    for(int house = 0; house < NUM_HOUSES; ++house) {
        houseToVisualHouse[house] = house;
    }
}

void setHouseVisualHouse(HOUSETYPE house, int visualHouse) {
    if(house < 0 || house >= NUM_HOUSES) {
        return;
    }

    if(!isValidHouseColorSlot(visualHouse)) {
        houseToVisualHouse[house] = house;
        return;
    }

    houseToVisualHouse[house] = visualHouse;
}
