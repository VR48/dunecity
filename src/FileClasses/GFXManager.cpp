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

#include <FileClasses/GFXManager.h>

#include <globals.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/INIFile.h>
#include <mod/ModManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/PictureFactory.h>
#include <FileClasses/LoadSavePNG.h>
#include <FileClasses/Shpfile.h>
#include <FileClasses/Cpsfile.h>
#include <FileClasses/Icnfile.h>
#include <FileClasses/Wsafile.h>
#include <FileClasses/Palfile.h>

#include <Colors.h>

#include <misc/FileSystem.h>

#include <misc/draw_util.h>
#include <misc/Scaler.h>
#include <misc/exceptions.h>
#include <mod/ModManager.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

/**
    Number of columns and rows each obj pic has
*/
static const Coord objPicTiles[] {
    { 8, 1 },   // ObjPic_Tank_Base
    { 8, 1 },   // ObjPic_Tank_Gun
    { 8, 1 },   // ObjPic_Siegetank_Base
    { 8, 1 },   // ObjPic_Siegetank_Gun
    { 8, 1 },   // ObjPic_Devastator_Base
    { 8, 1 },   // ObjPic_Devastator_Gun
    { 8, 1 },   // ObjPic_Sonictank_Gun
    { 8, 1 },   // ObjPic_Launcher_Gun
    { 8, 1 },   // ObjPic_DeviatorGunTornie
    { 8, 1 },   // ObjPic_RocketTrike (Tornie — derived from RocketTrike.png sprite sheet)
    { 8, 1 },   // ObjPic_FlameTank (Tornie — derived from FlameTank.png sprite sheet)
    { 8, 1 },   // ObjPic_EliteSiegeTankCustom (Tornie — derived from EliteSiegeTank.png sprite sheet)
    { 8, 1 },   // ObjPic_Quad
    { 8, 1 },   // ObjPic_Trike
    { 8, 1 },   // ObjPic_Harvester
    { 8, 3 },   // ObjPic_Harvester_Sand
    { 8, 1 },   // ObjPic_MCV
    { 8, 2 },   // ObjPic_Carryall
    { 8, 2 },   // ObjPic_CarryallShadow
    { 8, 1 },   // ObjPic_Frigate
    { 8, 1 },   // ObjPic_FrigateShadow
    { 8, 3 },   // ObjPic_Ornithopter
    { 8, 3 },   // ObjPic_OrnithopterShadow
    { 4, 3 },   // ObjPic_Trooper
    { 4, 3 },   // ObjPic_Troopers
    { 4, 3 },   // ObjPic_Soldier
    { 4, 3 },   // ObjPic_Infantry
    { 4, 3 },   // ObjPic_Saboteur
    { 1, 9 },   // ObjPic_Sandworm
    { 4, 1 },   // ObjPic_ConstructionYard
    { 4, 1 },   // ObjPic_Windtrap
    { 4, 1 },   // ObjPic_AdvancedWindTrap
    { 4, 1 },   // ObjPic_AdvancedWindTrap2x3
    { 4, 1 },   // ObjPic_AdvancedWindTrap3x2
    { 10, 1 },  // ObjPic_Refinery
    { 4, 1 },   // ObjPic_Barracks
    { 4, 1 },   // ObjPic_WOR
    { 4, 1 },   // ObjPic_Radar
    { 6, 1 },   // ObjPic_LightFactory
    { 4, 1 },   // ObjPic_Silo
    { 8, 1 },   // ObjPic_HeavyFactory
    { 8, 1 },   // ObjPic_HighTechFactory
    { 4, 1 },   // ObjPic_IX
    { 4, 1 },   // ObjPic_Palace
    { 10, 1 },  // ObjPic_RepairYard
    { 10, 1 },  // ObjPic_Starport
    { 10, 1 },  // ObjPic_GunTurret
    { 10, 1 },  // ObjPic_RocketTurret
    { 25, 3 },  // ObjPic_Wall
    { 16, 1 },  // ObjPic_Bullet_SmallRocket
    { 16, 1 },  // ObjPic_Bullet_MediumRocket
    { 16, 1 },  // ObjPic_Bullet_LargeRocket
    { 1, 1 },   // ObjPic_Bullet_Small
    { 1, 1 },   // ObjPic_Bullet_Medium
    { 1, 1 },   // ObjPic_Bullet_Large
    { 1, 1 },   // ObjPic_Bullet_Sonic
    { 1, 1 },   // ObjPic_Bullet_SonicTemp
    { 5, 1 },   // ObjPic_Hit_Gas
    { 1, 1 },   // ObjPic_Hit_ShellSmall
    { 1, 1 },   // ObjPic_Hit_ShellMedium
    { 1, 1 },   // ObjPic_Hit_ShellLarge
    { 5, 1 },   // ObjPic_ExplosionSmall
    { 5, 1 },   // ObjPic_ExplosionMedium1
    { 5, 1 },   // ObjPic_ExplosionMedium2
    { 5, 1 },   // ObjPic_ExplosionLarge1
    { 5, 1 },   // ObjPic_ExplosionLarge2
    { 2, 1 },   // ObjPic_ExplosionSmallUnit
    { 21, 1 },  // ObjPic_ExplosionFlames
    { 3, 1 },   // ObjPic_ExplosionSpiceBloom
    { 6, 1 },   // ObjPic_DeadInfantry
    { 6, 1 },   // ObjPic_DeadAirUnit
    { 3, 1 },   // ObjPic_Smoke
    { 1, 1 },   // ObjPic_SandwormShimmerMask
    { 1, 1 },   // ObjPic_SandwormShimmerTemp
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain_GreenSpice
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain_RedSpice
    { 14, 1 },  // ObjPic_DestroyedStructure
    { 6, 1 },   // ObjPic_RockDamage
    { 3, 1 },   // ObjPic_SandDamage
    { 16, 1 },  // ObjPic_Terrain_Hidden
    { 16, 1 },  // ObjPic_Terrain_HiddenFog
    { 8, 1 },   // ObjPic_Terrain_Tracks
    { 1, 1 },   // ObjPic_Star
    { 8, 1 },   // ObjPic_RebelHarvester
    { 4, 1 },   // ObjPic_Worfinery
    { 4, 1 },   // ObjPic_TechCenter
    { 4, 1 },   // ObjPic_Scoutpost
    { 4, 4 },   // ObjPic_ZoneResidential (4 density × 4 value-tier variants)
    { 4, 4 },   // ObjPic_ZoneCommercial  (4 density × 4 value-tier variants)
    { 4, 2 },   // ObjPic_ZoneIndustrial  (4 density × 2 value-tier variants)
    { 16, 1 },  // ObjPic_CityRoad (16 connection variants, indexed by neighbor mask)
    { 8, 1 },   // ObjPic_NuclearPlant (8 frame slots for build-animation parity; all identical)
    { 4, 1 },   // ObjPic_PoliceStation (4 frame slots, all identical; 2x2 footprint)
    { 4, 1 },   // ObjPic_Stadium (4 frame slots, all identical; 3x3 footprint)
    { 4, 1 },   // ObjPic_Airport (4 frame slots, all identical; 3x3 footprint)
    { 1, 1 },   // ObjPic_Hospital (single cell, 2x2 footprint, auto-placed on residential)
    { 1, 1 },   // ObjPic_Church   (single cell, 2x2 footprint, auto-placed on residential)
    { 8, 1 },   // ObjPic_SonicTrike
    { 8, 1 },   // ObjPic_EliteLauncherGunTornie
    { 8, 1 },   // ObjPic_RebelSonicTankGun
    { 8, 1 },   // ObjPic_HarvestankGunTornie
};
static_assert(sizeof(objPicTiles) / sizeof(objPicTiles[0]) == NUM_OBJPICS,
              "objPicTiles must have one entry per ObjPic enum value");

static void applyRebelsTint(SDL_Surface* surface);
static void applyCustomVisualColorRamp(SDL_Surface* surface, int colorSlot);
static void preserveOpaqueBlackIndex(SDL_Surface* surface);
static void normalizeTransparentPaletteIndexes(SDL_Surface* surface);
static sdl2::surface_ptr convertTornieIndexedSurfaceToRGBA(SDL_Surface* source, const char* label, int house, unsigned int zoom, bool useTextureMask = false);
static void logTornieStructureSurfaceDiagnostics(const char* stage, const char* label, SDL_Surface* surface, int frameWidth, int frameHeight);
static bool isTornieStructureObjPic(unsigned int id);
static const char* getTornieStructureObjPicName(unsigned int id);
static sdl2::surface_ptr remapIndexedSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette);
static sdl2::surface_ptr convertTruecolorSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette);
static void normalizeHouseColorRangesToHarkonnen(SDL_Surface* surface);
static void normalizeHarkonnenTeamRed(SDL_Surface* surface);
static void normalizeLooseTeamPaintToHarkonnen(SDL_Surface* surface);
static sdl2::surface_ptr createTintedTerrainSpiceSurface(SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint);
static sdl2::surface_ptr createTintedMapEditorIcon(SDL_Surface* source, SDL_Surface* sand, SDL_Color tint);
static sdl2::surface_ptr createCustomMapEditorStar(SDL_Surface* source);
static sdl2::surface_ptr resizeSurfaceNearest(SDL_Surface* source, int width, int height);
static sdl2::surface_ptr scaleSurfaceNearest(SDL_Surface* source, int factor);
static std::unique_ptr<Animation> loadPngStripAnimation(const std::string& filename, int frameCount, double frameRate, bool bDoublePic = true, int transparentColorKey = -1);


GFXManager::GFXManager() {

    // open all shp files
    std::unique_ptr<Shpfile> units = loadShpfile("UNITS.SHP");
    std::unique_ptr<Shpfile> units1 = loadShpfile("UNITS1.SHP");
    std::unique_ptr<Shpfile> units2 = loadShpfile("UNITS2.SHP");
    std::unique_ptr<Shpfile> mouse = loadShpfile("MOUSE.SHP");
    std::unique_ptr<Shpfile> shapes = loadShpfile("SHAPES.SHP");
    std::unique_ptr<Shpfile> menshpa = loadShpfile("MENSHPA.SHP");
    std::unique_ptr<Shpfile> menshph = loadShpfile("MENSHPH.SHP");
    std::unique_ptr<Shpfile> menshpo = loadShpfile("MENSHPO.SHP");
    std::unique_ptr<Shpfile> menshpm = loadShpfile("MENSHPM.SHP");

    std::unique_ptr<Shpfile> choam;
    if(pFileManager->exists("CHOAM." + _("LanguageFileExtension"))) {
        choam = loadShpfile("CHOAM." + _("LanguageFileExtension"));
    } else if(pFileManager->exists("CHOAMSHP.SHP")) {
        choam = loadShpfile("CHOAMSHP.SHP");
    } else {
        THROW(std::runtime_error, "GFXManager::GFXManager(): Cannot open CHOAMSHP.SHP or CHOAM."+_("LanguageFileExtension")+"!");
    }

    std::unique_ptr<Shpfile> bttn;
    if(pFileManager->exists("BTTN." + _("LanguageFileExtension"))) {
        bttn = loadShpfile("BTTN." + _("LanguageFileExtension"));
    } else {
        // The US-Version has the buttons in SHAPES.SHP
        // => bttn == nullptr
    }

    std::unique_ptr<Shpfile> mentat;
    if(pFileManager->exists("MENTAT." + _("LanguageFileExtension"))) {
        mentat = loadShpfile("MENTAT." + _("LanguageFileExtension"));
    } else {
        mentat = loadShpfile("MENTAT.SHP");
    }

    std::unique_ptr<Shpfile> pieces = loadShpfile("PIECES.SHP");
    std::unique_ptr<Shpfile> arrows = loadShpfile("ARROWS.SHP");

    // Load icon file
    std::unique_ptr<Icnfile> icon = std::make_unique<Icnfile>(  pFileManager->openFile("ICON.ICN").get(),
                                                                pFileManager->openFile("ICON.MAP").get());

    // Load radar static
    std::unique_ptr<Wsafile> radar = loadWsafile("STATIC.WSA");

    // open bene palette
    Palette benePalette = LoadPalette_RW(pFileManager->openFile("BENE.PAL").get());

    //create PictureFactory
    std::unique_ptr<PictureFactory> PicFactory = std::make_unique<PictureFactory>();



    // load object pics in the original resolution
    objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(0));
    objPic[ObjPic_Tank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(10));
    objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(15));
    objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(20));
    objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(25));
    objPic[ObjPic_Sonictank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(30));
    objPic[ObjPic_Launcher_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(35));
    objPic[ObjPic_Quad][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(0));
    objPic[ObjPic_Trike][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(10));
    objPic[ObjPic_Harvester_Sand][HOUSE_HARKONNEN][0] = units1->getPictureArray(8,3,HARVESTERSAND_ROW(72),HARVESTERSAND_ROW(73),HARVESTERSAND_ROW(74));
    objPic[ObjPic_MCV][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(15));
    objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0] = units->getPictureArray(8,2,AIRUNIT_ROW(45),AIRUNIT_ROW(48));
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][0] = nullptr;    // create shadow after scaling
    objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,AIRUNIT_ROW(60));
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][0] = nullptr;     // create shadow after scaling
    objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0] = units->getPictureArray(8,3,ORNITHOPTER_ROW(51),ORNITHOPTER_ROW(52),ORNITHOPTER_ROW(53));
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][0] = nullptr; // create shadow after scaling
    objPic[ObjPic_Trooper][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(82),INFANTRY_ROW(83),INFANTRY_ROW(84));
    objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0] = units->getPictureArray(4,4,MULTIINFANTRY_ROW(103),MULTIINFANTRY_ROW(104),MULTIINFANTRY_ROW(105),MULTIINFANTRY_ROW(106));
    objPic[ObjPic_Soldier][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(73),INFANTRY_ROW(74),INFANTRY_ROW(75));
    objPic[ObjPic_Infantry][HOUSE_HARKONNEN][0] = units->getPictureArray(4,4,MULTIINFANTRY_ROW(91),MULTIINFANTRY_ROW(92),MULTIINFANTRY_ROW(93),MULTIINFANTRY_ROW(94));
    objPic[ObjPic_Saboteur][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(63),INFANTRY_ROW(64),INFANTRY_ROW(65));
    objPic[ObjPic_Sandworm][HOUSE_HARKONNEN][0] = units1->getPictureArray(1,9,71|TILE_NORMAL,70|TILE_NORMAL,69|TILE_NORMAL,68|TILE_NORMAL,67|TILE_NORMAL,68|TILE_NORMAL,69|TILE_NORMAL,70|TILE_NORMAL,71|TILE_NORMAL);
    objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][0] = icon->getPictureArray(17);
    objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0] = icon->getPictureArray(19);
    objPic[ObjPic_Refinery][HOUSE_HARKONNEN][0] = icon->getPictureArray(21);
    objPic[ObjPic_Barracks][HOUSE_HARKONNEN][0] = icon->getPictureArray(18);
    objPic[ObjPic_WOR][HOUSE_HARKONNEN][0] = icon->getPictureArray(16);
    objPic[ObjPic_Radar][HOUSE_HARKONNEN][0] = icon->getPictureArray(26);
    objPic[ObjPic_LightFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(12);
    objPic[ObjPic_Silo][HOUSE_HARKONNEN][0] = icon->getPictureArray(25);
    objPic[ObjPic_HeavyFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(13);
    objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(14);
    objPic[ObjPic_IX][HOUSE_HARKONNEN][0] = icon->getPictureArray(15);
    objPic[ObjPic_Palace][HOUSE_HARKONNEN][0] = icon->getPictureArray(11);
    objPic[ObjPic_RepairYard][HOUSE_HARKONNEN][0] = icon->getPictureArray(22);
    objPic[ObjPic_Starport][HOUSE_HARKONNEN][0] = icon->getPictureArray(20);
    objPic[ObjPic_GunTurret][HOUSE_HARKONNEN][0] = icon->getPictureArray(23);
    objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0] = icon->getPictureArray(24);
    objPic[ObjPic_Wall][HOUSE_HARKONNEN][0] = icon->getPictureArray(6,25,3,1);

    // DuneCity zone sprites — load the full 3×3 Micropolis composites and
    // downscale them to the 2×2 gameplay footprint (32×32 at base zoom).
    // This shows the entire building art rather than a 2×2 center crop.
    // Fall back to colored placeholder surfaces when the imported PNGs
    // are absent (e.g. fresh checkout without running the import script).
    //
    // These surfaces are 32-bit RGBA (either from LoadPNG_RW or the
    // placeholder helper).  The legacy Scaler functions assume 8-bit
    // paletted surfaces and would segfault on them, so we pre-generate
    // all three zoom levels here using format-agnostic SDL_BlitScaled,
    // mirroring how ObjPic_Star bypasses the scaler.
    {
        auto makeZonePlaceholder = [](Uint8 r, Uint8 g, Uint8 b) -> sdl2::surface_ptr {
            const int sz = 2 * D2_TILESIZE;  // 32x32
            sdl2::surface_ptr s{ SDL_CreateRGBSurface(0, sz, sz, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            SDL_FillRect(s.get(), nullptr, SDL_MapRGBA(s->format, r, g, b, 255));
            SDL_Rect top{0,0,sz,1}, bot{0,sz-1,sz,1}, lft{0,0,1,sz}, rgt{sz-1,0,1,sz};
            Uint32 border = SDL_MapRGBA(s->format, r/2, g/2, b/2, 255);
            SDL_FillRect(s.get(), &top, border);
            SDL_FillRect(s.get(), &bot, border);
            SDL_FillRect(s.get(), &lft, border);
            SDL_FillRect(s.get(), &rgt, border);
            return s;
        };

        // Scale a 32-bit surface by an integer factor using SDL_BlitScaled
        // (nearest-neighbour).  This avoids the legacy 8-bit Scaler path.
        auto scaleRGBASurface = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                src->w * factor, src->h * factor,
                src->format->BitsPerPixel,
                src->format->Rmask, src->format->Gmask,
                src->format->Bmask, src->format->Amask) };
            if (dst) {
                SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
            }
            return dst;
        };

        // Try loading imported zone sprites (default: variant 0, density 2).
        // Search order: installed data dir, then source-tree-relative
        // paths for dev builds (binary in build/bin/ or app bundle).
        char* sdlBasePath = SDL_GetBasePath();
        std::string binDir = sdlBasePath ? sdlBasePath : "./";
        if (sdlBasePath) SDL_free(sdlBasePath);

        std::vector<std::string> searchDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
            binDir + "imported_sprites/micropolis/composites_2x2/",               // CMake-copied next to binary
            binDir + "../../imported_sprites/micropolis/composites_2x2/",          // build/bin -> root
            binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",  // .app/Contents/MacOS -> root
        };
        // Dev-friendly fallback: DUNE_CITY_SOURCE_DIR env points at the
        // source tree so imported sprites are found without installing.
        const char* srcDirEnv = SDL_getenv("DUNE_CITY_SOURCE_DIR");
        if (srcDirEnv && srcDirEnv[0]) {
            std::string srcDir = srcDirEnv;
            if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
            searchDirs.push_back(srcDir + "imported_sprites/micropolis/composites_2x2/");
        }
        // Build a per-zone-type atlas containing all (value-tier × density)
        // variants. Atlas layout: columns = density (0..numDensity-1), rows
        // = value tier (0..numValue-1). ZoneStructure picks the cell at
        // (density, valueTier) each tick so a growing residential zone walks
        // from "empty lot" through "house" to "tall apartment" sprites, with
        // value-tier variants adding richer-looking buildings where the land
        // value is high. Sprite files come from the Micropolis import in
        // imported_sprites/micropolis/composites_2x2/.
        struct ZoneAtlasSpec {
            int objPicID;
            const char* prefix;     // "res", "com", or "ind"
            int numDensity;         // columns
            int numValue;           // rows
            int emptyBaseTile;      // top-left Micropolis tile of the 3x3 empty-zone graphic
            Uint8 r, g, b;          // fallback color if every variant is missing
        };
        // Micropolis empty-zone graphic IDs (3x3 each, with the R/C/I letter
        // and dotted border baked into the center tile):
        //   Residential RZB = 240 (range 240-248)
        //   Commercial  CZB = 423 (range 423-431)
        //   Industrial  IZB = 612 (range 612-620)
        const ZoneAtlasSpec zoneAtlases[] = {
            { ObjPic_ZoneResidential, "res", 4, 4, 240,  80, 160,  80 },
            { ObjPic_ZoneCommercial,  "com", 4, 4, 423,  80,  80, 200 },
            { ObjPic_ZoneIndustrial,  "ind", 4, 2, 612, 200, 160,  50 },
        };

        const int cellSize = 2 * D2_TILESIZE;  // 32 px per zone cell at zoom 0

        // Build the empty-zone (d=0) sprite for a zone type by compositing
        // the Micropolis 3x3 empty-zone tile group into a 48x48 image and
        // downscaling to 32x32. The center tile of each group carries the
        // R/C/I letter glyph, the surrounding 8 tiles form the dotted
        // border the player recognises from SimCity Classic.
        auto buildEmptyZoneCell = [&](int baseTile) -> sdl2::surface_ptr {
            std::vector<std::string> rawDirs = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/raw_tiles/",
                binDir + "imported_sprites/micropolis/raw_tiles/",
                binDir + "../../imported_sprites/micropolis/raw_tiles/",
                binDir + "../../../../../imported_sprites/micropolis/raw_tiles/",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string sd = srcDirEnv;
                if (sd.back() != '/' && sd.back() != '\\') sd += '/';
                rawDirs.push_back(sd + "imported_sprites/micropolis/raw_tiles/");
            }
            auto loadTile = [&](int n) -> sdl2::surface_ptr {
                char fn[32];
                std::snprintf(fn, sizeof(fn), "tile_%03d.png", n);
                for (const auto& dir : rawDirs) {
                    auto rw = sdl2::RWops_ptr{ SDL_RWFromFile((dir + fn).c_str(), "rb") };
                    if (rw) {
                        auto img = LoadPNG_RW(rw.get());
                        if (img) return img;
                    }
                }
                return sdl2::surface_ptr{};
            };

            const int srcTileSize = 16;  // Micropolis tile pixel size
            const int srcW = 3 * srcTileSize;
            const int srcH = 3 * srcTileSize;
            sdl2::surface_ptr big{ SDL_CreateRGBSurface(0, srcW, srcH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!big) return sdl2::surface_ptr{};
            SDL_FillRect(big.get(), nullptr,
                         SDL_MapRGBA(big->format, 0, 0, 0, 0));
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    auto t = loadTile(baseTile + row * 3 + col);
                    if (!t) continue;
                    SDL_SetSurfaceBlendMode(t.get(), SDL_BLENDMODE_NONE);
                    SDL_Rect d{ col * srcTileSize, row * srcTileSize,
                                srcTileSize, srcTileSize };
                    SDL_BlitSurface(t.get(), nullptr, big.get(), &d);
                }
            }
            // Downscale 48x48 → 32x32 to fit our 2x2 zone footprint.
            sdl2::surface_ptr down{ SDL_CreateRGBSurface(0, cellSize, cellSize,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (down) {
                SDL_SetSurfaceBlendMode(big.get(), SDL_BLENDMODE_NONE);
                SDL_BlitScaled(big.get(), nullptr, down.get(), nullptr);
            }
            return down;
        };

        for (const auto& spec : zoneAtlases) {
            const int atlasW = spec.numDensity * cellSize;
            const int atlasH = spec.numValue   * cellSize;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0,
                atlasW, atlasH, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!atlas) continue;
            // Transparent atlas so any frame we fail to load falls back to
            // showing terrain rather than a stale neighbour cell.
            SDL_FillRect(atlas.get(), nullptr,
                         SDL_MapRGBA(atlas->format, 0, 0, 0, 0));

            // Pre-build the empty-zone (d=0) cell from Micropolis raw tiles
            // so every row of column 0 shares the same proper SC-style
            // "freshly zoned, R/C/I letter visible, dotted border" look.
            sdl2::surface_ptr emptyCell = buildEmptyZoneCell(spec.emptyBaseTile);

            for (int v = 0; v < spec.numValue; ++v) {
                for (int d = 0; d < spec.numDensity; ++d) {
                    if (d == 0) {
                        if (emptyCell) {
                            SDL_SetSurfaceBlendMode(emptyCell.get(), SDL_BLENDMODE_NONE);
                            SDL_Rect dst{ d * cellSize, v * cellSize, cellSize, cellSize };
                            SDL_BlitSurface(emptyCell.get(), nullptr, atlas.get(), &dst);
                        }
                        continue;
                    }

                    char fileName[64];
                    std::snprintf(fileName, sizeof(fileName),
                                  "%s_v%d_d%d_2x2.png", spec.prefix, v, d);

                    sdl2::surface_ptr cell;
                    for (const auto& dir : searchDirs) {
                        std::string path = dir + fileName;
                        auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                        if (rwops) {
                            cell = LoadPNG_RW(rwops.get());
                            if (cell) break;
                        }
                    }
                    if (!cell) {
                        SDL_Log("Zone sprite missing, placeholder for %s", fileName);
                        cell = makeZonePlaceholder(spec.r, spec.g, spec.b);
                    }
                    // Composites are 32×32 already; resize defensively in
                    // case the import pipeline produced a different size.
                    if (cell->w != cellSize || cell->h != cellSize) {
                        sdl2::surface_ptr scaled{ SDL_CreateRGBSurface(0,
                            cellSize, cellSize,
                            cell->format->BitsPerPixel,
                            cell->format->Rmask, cell->format->Gmask,
                            cell->format->Bmask, cell->format->Amask) };
                        if (scaled) {
                            SDL_BlitScaled(cell.get(), nullptr, scaled.get(), nullptr);
                            cell = std::move(scaled);
                        }
                    }
                    SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
                    SDL_Rect dst{ d * cellSize, v * cellSize, cellSize, cellSize };
                    SDL_BlitSurface(cell.get(), nullptr, atlas.get(), &dst);
                }
            }

            // Pre-generate all zoom levels so the 8-bit scaler loop never
            // runs on these truecolor RGBA surfaces.
            objPic[spec.objPicID][HOUSE_HARKONNEN][0] = std::move(atlas);
            objPic[spec.objPicID][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[spec.objPicID][HOUSE_HARKONNEN][0].get(), 2);
            objPic[spec.objPicID][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[spec.objPicID][HOUSE_HARKONNEN][0].get(), 3);

            // Zone sprites are house-independent — pre-fill every house slot
            // so getZoomedObjPic() never tries to palette-remap RGBA data.
            for (int h = 1; h < NUM_HOUSES; h++) {
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    objPic[spec.objPicID][h][z] = sdl2::surface_ptr{
                        SDL_ConvertSurface(objPic[spec.objPicID][HOUSE_HARKONNEN][z].get(),
                                           objPic[spec.objPicID][HOUSE_HARKONNEN][z]->format, 0)
                    };
                }
            }
        }

        // ----- DuneCity city-mode road atlas -----
        //
        // 16 connection variants indexed by Dune-Legacy neighbor mask
        // (bit 0 up, 1 right, 2 down, 3 left).  We pull the artwork from
        // Micropolis road tiles and use Micropolis's _RoadTable[] to map
        // each mask to the right Micropolis tile number — the bit ordering
        // matches Dune Legacy's exactly.  Result is a single 16×1 atlas
        // (16*TILESIZE × TILESIZE) consumed by Tile::blitGround() when a
        // slab is rendered in city-sim mode.
        {
            std::vector<std::string> roadDirs = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/categories/roads/",
                binDir + "imported_sprites/micropolis/categories/roads/",
                binDir + "../../imported_sprites/micropolis/categories/roads/",
                binDir + "../../../../../imported_sprites/micropolis/categories/roads/",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string srcDir = srcDirEnv;
                if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
                roadDirs.push_back(srcDir + "imported_sprites/micropolis/categories/roads/");
            }

            // Micropolis _RoadTable[16] from src/sim/w_con.c — indexed by
            // neighbor mask, value is the Micropolis tile number.
            static const int kRoadTileForMask[16] = {
                66, 67, 66, 68,   // 0=none, 1=U,    2=R,    3=UR
                67, 67, 69, 73,   // 4=D,    5=UD,   6=RD,   7=URD
                66, 71, 66, 72,   // 8=L,    9=UL,   10=LR,  11=ULR
                70, 75, 74, 76    // 12=LD,  13=ULD, 14=LRD, 15=cross
            };

            // Build the 16-tile atlas at zoom-0 native pixel size (each tile
            // is D2_TILESIZE square = 16px, matching the source PNGs and what
            // the in-game renderer samples via world2zoomedWorld(TILESIZE)).
            // TILESIZE (64) is a world-coord scaling constant, NOT a pixel
            // size — using it here makes the atlas 4× too big in every
            // dimension and the renderer ends up sampling only the corner of
            // each tile (where curbs live, not the road markings).
            const int atlasW = 16 * D2_TILESIZE;
            const int atlasH = D2_TILESIZE;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, atlasW, atlasH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };

            // Asphalt-color background — matches the recolored Micropolis
            // road interior so a missing source PNG looks like plain road
            // rather than a black square.
            if (atlas) {
                SDL_FillRect(atlas.get(), nullptr,
                             SDL_MapRGBA(atlas->format, 90, 90, 90, 255));
            }

            int roadsLoaded = 0;
            for (int mask = 0; mask < 16 && atlas; ++mask) {
                char filename[32];
                snprintf(filename, sizeof(filename), "tile_%03d.png", kRoadTileForMask[mask]);

                sdl2::surface_ptr src;
                for (const auto& dir : roadDirs) {
                    std::string path = dir + filename;
                    auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                    if (rwops) {
                        src = LoadPNG_RW(rwops.get());
                        if (src) break;
                    }
                }

                if (!src) {
                    // Leave the black fallback for this slot — surrounding
                    // tiles still composite into a navigable road surface.
                    continue;
                }

                // Repaint the source tile so it reads cleanly on Dune rock:
                //   - peach curbs (204,127,102) → asphalt (the road has no
                //     real-world curb context on bare rock)
                //   - grass corners (0,230,0) → asphalt (same reason)
                //   - light-gray edge marks (191,191,191) → white (boost
                //     contrast so they're visible against dark asphalt)
                // Then procedurally stamp a 2×2 white center dot on every
                // tile — the Micropolis intersection art (mask 15) has no
                // center marking, and the dashed center on straight tiles
                // is small enough to disappear once asphalt-on-rock becomes
                // the dominant visual. The center dot guarantees every road
                // tile reads as "a road" regardless of connectivity.
                {
                    sdl2::surface_ptr rgba{ SDL_ConvertSurfaceFormat(src.get(), SDL_PIXELFORMAT_RGBA32, 0) };
                    if (rgba) {
                        SDL_LockSurface(rgba.get());
                        Uint8* pixels = static_cast<Uint8*>(rgba->pixels);
                        for (int y = 0; y < rgba->h; ++y) {
                            Uint8* row = pixels + y * rgba->pitch;
                            for (int x = 0; x < rgba->w; ++x) {
                                Uint32* px = reinterpret_cast<Uint32*>(row + x * 4);
                                Uint8 cr, cg, cb, ca;
                                SDL_GetRGBA(*px, rgba->format, &cr, &cg, &cb, &ca);
                                const bool isPeachCurb = (cr == 204 && cg == 127 && cb == 102);
                                const bool isGrass     = (cr == 0 && cg == 230 && cb == 0);
                                const bool isLightMark = (cr == 191 && cg == 191 && cb == 191);
                                const bool isBlack     = (cr == 0 && cg == 0 && cb == 0);
                                const bool isBlueCond  = (cr == 102 && cg == 102 && cb == 230);
                                const bool isBluePure  = (cr == 0 && cg == 0 && cb == 230);
                                const bool isGray      = (cr == 127 && cg == 127 && cb == 127);
                                const bool isRed       = (cr == 255 && cg == 0 && cb == 0);
                                // Also catch brownish terrain pixels that leak
                                // through on some Micropolis road tiles.
                                const bool isBrown = (cr > 120 && cg < cr && cb < cg);
                                if (isPeachCurb || isGrass || isBlueCond || isBluePure || isGray || isRed || isBrown) {
                                    *px = SDL_MapRGBA(rgba->format, 90, 90, 90, 255);
                                } else if (isLightMark) {
                                    *px = SDL_MapRGBA(rgba->format, 255, 255, 255, 255);
                                } else if (isBlack) {
                                    *px = SDL_MapRGBA(rgba->format, 255, 255, 255, 255);
                                }
                            }
                        }
                        // Center 2×2 white dot to guarantee visibility.
                        const Uint32 white = SDL_MapRGBA(rgba->format, 255, 255, 255, 255);
                        for (int dy = 0; dy < 2; ++dy) {
                            for (int dx = 0; dx < 2; ++dx) {
                                int cx = rgba->w / 2 - 1 + dx;
                                int cy = rgba->h / 2 - 1 + dy;
                                Uint32* p = reinterpret_cast<Uint32*>(static_cast<Uint8*>(rgba->pixels) + cy * rgba->pitch + cx * 4);
                                *p = white;
                            }
                        }
                        SDL_UnlockSurface(rgba.get());
                        src = std::move(rgba);
                    }
                }

                SDL_Rect dst{ mask * D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE };
                if (src->w == D2_TILESIZE && src->h == D2_TILESIZE) {
                    SDL_BlitSurface(src.get(), nullptr, atlas.get(), &dst);
                } else {
                    SDL_BlitScaled(src.get(), nullptr, atlas.get(), &dst);
                }
                roadsLoaded++;
            }
            SDL_Log("Loaded %d/16 city-road tiles from Micropolis road set", roadsLoaded);

            objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][0] = std::move(atlas);
            // Nearest-neighbor scale road atlas to avoid bilinear blur on pixel art.
            auto scaleRGBA_NN = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
                sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                    src->w * factor, src->h * factor,
                    src->format->BitsPerPixel,
                    src->format->Rmask, src->format->Gmask,
                    src->format->Bmask, src->format->Amask) };
                if (!dst) return dst;
                SDL_LockSurface(src);
                SDL_LockSurface(dst.get());
                const int bpp = src->format->BytesPerPixel;
                for (int y = 0; y < src->h; ++y) {
                    const Uint8* srcRow = static_cast<const Uint8*>(src->pixels) + y * src->pitch;
                    for (int x = 0; x < src->w; ++x) {
                        Uint32 pixel;
                        memcpy(&pixel, srcRow + x * bpp, bpp);
                        for (int dy = 0; dy < factor; ++dy) {
                            Uint8* dstRow = static_cast<Uint8*>(dst->pixels) + (y * factor + dy) * dst->pitch;
                            for (int dx = 0; dx < factor; ++dx) {
                                memcpy(dstRow + (x * factor + dx) * bpp, &pixel, bpp);
                            }
                        }
                    }
                }
                SDL_UnlockSurface(dst.get());
                SDL_UnlockSurface(src);
                return dst;
            };
            objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][1] = scaleRGBA_NN(objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][0].get(), 2);
            objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][2] = scaleRGBA_NN(objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][0].get(), 3);

            for (int h = 1; h < NUM_HOUSES; h++) {
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    if (objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][z]) {
                        objPic[ObjPic_CityRoad][h][z] = sdl2::surface_ptr{
                            SDL_ConvertSurface(objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][z].get(),
                                               objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][z]->format, 0)
                        };
                    }
                }
            }
        }

        // ----- DuneCity nuclear-plant sprite -----
        //
        // The nuclear plant has a 3x3 gameplay footprint and StructureBase
        // animates by stepping through 8 horizontal frames. We prefer the
        // 4x4 (64×64) Micropolis source — downscaling 64→48 with BlitScaled
        // gives a cleaner result than upscaling the 2x2 (32×32) variant.
        {
            std::vector<std::string> nuclearDirs4x4 = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_special/",
                binDir + "imported_sprites/micropolis/composites_special/",
                binDir + "../../imported_sprites/micropolis/composites_special/",
                binDir + "../../../../../imported_sprites/micropolis/composites_special/",
            };
            std::vector<std::string> nuclearDirs2x2 = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_special_2x2/",
                binDir + "imported_sprites/micropolis/composites_special_2x2/",
                binDir + "../../imported_sprites/micropolis/composites_special_2x2/",
                binDir + "../../../../../imported_sprites/micropolis/composites_special_2x2/",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string srcDir = srcDirEnv;
                if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
                nuclearDirs4x4.push_back(srcDir + "imported_sprites/micropolis/composites_special/");
                nuclearDirs2x2.push_back(srcDir + "imported_sprites/micropolis/composites_special_2x2/");
            }

            // Atlas frame dimensions at zoom-0 pixel size. TILESIZE (64) is
            // the world-coord constant; D2_TILESIZE (16) is the actual zoom-0
            // pixel size. Using TILESIZE here would put the sprite far outside
            // the rectangle the renderer samples.
            const int frameW = 3 * D2_TILESIZE;
            const int frameH = 3 * D2_TILESIZE;
            const int numFrames = 8;
            const int atlasW = numFrames * frameW;
            const int atlasH = frameH;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, atlasW, atlasH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (atlas) {
                SDL_FillRect(atlas.get(), nullptr, SDL_MapRGBA(atlas->format, 0, 0, 0, 0));
            }

            sdl2::surface_ptr nuclearSrc;
            for (const auto& dir : nuclearDirs4x4) {
                std::string path = dir + "nuclear_power_plant_4x4.png";
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                if (rwops) {
                    nuclearSrc = LoadPNG_RW(rwops.get());
                    if (nuclearSrc) {
                        SDL_Log("Loaded nuclear plant sprite (4x4) from: %s", path.c_str());
                        break;
                    }
                }
            }
            if (!nuclearSrc) {
                for (const auto& dir : nuclearDirs2x2) {
                    std::string path = dir + "nuclear_power_plant_2x2.png";
                    auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                    if (rwops) {
                        nuclearSrc = LoadPNG_RW(rwops.get());
                        if (nuclearSrc) {
                            SDL_Log("Loaded nuclear plant sprite (2x2 fallback) from: %s", path.c_str());
                            break;
                        }
                    }
                }
            }

            if (atlas && nuclearSrc) {
                // Scale the source to fill the full 48×48 frame so the
                // nuclear plant occupies its entire 3×3 gameplay footprint.
                SDL_SetSurfaceBlendMode(nuclearSrc.get(), SDL_BLENDMODE_NONE);
                for (int f = 0; f < numFrames; ++f) {
                    SDL_Rect frameDst = { f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(nuclearSrc.get(), nullptr, atlas.get(), &frameDst);
                }

                objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 3);

                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_NuclearPlant][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            } else {
                SDL_Log("Nuclear plant sprite not found; NuclearPlant will fall back to HighTechFactory art");
                SDL_Surface* fallback = objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get();
                if (atlas && fallback) {
                    const int fallbackFrames = objPicTiles[ObjPic_HighTechFactory].x;
                    const int fallbackFrameW = fallback->w / fallbackFrames;
                    const int fallbackFrameH = fallback->h / objPicTiles[ObjPic_HighTechFactory].y;

                    SDL_SetSurfaceBlendMode(fallback, SDL_BLENDMODE_NONE);
                    for (int f = 0; f < numFrames; ++f) {
                        const int sourceFrame = f % fallbackFrames;
                        SDL_Rect src{ sourceFrame * fallbackFrameW, 0, fallbackFrameW, fallbackFrameH };
                        SDL_Rect dst{ f * frameW, 0, frameW, frameH };
                        SDL_BlitScaled(fallback, &src, atlas.get(), &dst);
                    }

                    objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0] = std::move(atlas);
                    objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 3);

                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_NuclearPlant][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
                // Last resort: if fallback sprite was also null, fill atlas
                // with a debug placeholder so objPic is never null.
                if (!objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0]) {
                    sdl2::surface_ptr ph{ SDL_CreateRGBSurface(0, atlasW, atlasH,
                        SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                    if (ph) {
                        SDL_FillRect(ph.get(), nullptr, SDL_MapRGBA(ph->format, 180, 100, 100, 255));
                        objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0] = std::move(ph);
                        objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 3);
                        for (int h = 1; h < NUM_HOUSES; h++) {
                            for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                if (objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_NuclearPlant][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][z]->format, 0)
                                    };
                                }
                            }
                        }
                    }
                }
            }
        }

        // ----- DuneCity police-station sprite -----
        //
        // 2x2 footprint, no animation. Like the nuclear plant we still
        // build a multi-frame atlas (4 horizontal copies) so StructureBase
        // animation indexing has somewhere to land — all frames are the
        // same image, so the sprite never appears to "animate".
        {
            std::vector<std::string> policeDirs = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
                binDir + "imported_sprites/micropolis/composites_2x2/",
                binDir + "../../imported_sprites/micropolis/composites_2x2/",
                binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string srcDir = srcDirEnv;
                if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
                policeDirs.push_back(srcDir + "imported_sprites/micropolis/composites_2x2/");
            }

            const int frameW    = 2 * D2_TILESIZE;   // 32 px at zoom 0
            const int frameH    = 2 * D2_TILESIZE;
            const int numFrames = 4;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0,
                numFrames * frameW, frameH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (atlas) {
                SDL_FillRect(atlas.get(), nullptr,
                             SDL_MapRGBA(atlas->format, 0, 0, 0, 0));
            }

            sdl2::surface_ptr policeSrc;
            for (const auto& dir : policeDirs) {
                std::string path = dir + "police_station_2x2.png";
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                if (rwops) {
                    policeSrc = LoadPNG_RW(rwops.get());
                    if (policeSrc) {
                        SDL_Log("Loaded police station sprite from: %s", path.c_str());
                        break;
                    }
                }
            }

            if (atlas && policeSrc) {
                SDL_SetSurfaceBlendMode(policeSrc.get(), SDL_BLENDMODE_NONE);
                for (int f = 0; f < numFrames; ++f) {
                    SDL_Rect dst{ f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(policeSrc.get(), nullptr, atlas.get(), &dst);
                }

                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][1] =
                    scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][2] =
                    scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 3);

                // House-independent sprite — clone for every house slot
                // so getZoomedObjPic() never attempts a palette remap on
                // RGBA data.
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_PoliceStation][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            } else {
                SDL_Log("Police station sprite not found; using placeholder");
                // Fill the already-cleared atlas with a visible debug color so
                // objPic is never null and getZoomedObjPic won't crash.
                if (atlas) {
                    SDL_FillRect(atlas.get(), nullptr,
                                 SDL_MapRGBA(atlas->format, 100, 120, 180, 255));
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0] = std::move(atlas);
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][1] =
                        scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][2] =
                        scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 3);
                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_PoliceStation][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
            }
        }

    // ----- DuneCity stadium sprite -----
    // Stadium is a 3x3 footprint civic building. Source art is the
    // Micropolis 4x4 stadium composite (64×64) scaled to 48×48.
    {
        std::vector<std::string> stadiumDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_special/",
            binDir + "imported_sprites/micropolis/composites_special/",
            binDir + "../../imported_sprites/micropolis/composites_special/",
            binDir + "../../../../../imported_sprites/micropolis/composites_special/",
        };
        if (srcDirEnv && srcDirEnv[0]) {
            std::string srcDir = srcDirEnv;
            if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
            stadiumDirs.push_back(srcDir + "imported_sprites/micropolis/composites_special/");
        }

        const int frameW = 3 * D2_TILESIZE;
        const int frameH = 3 * D2_TILESIZE;
        const int numFrames = 4;
        sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, numFrames * frameW, frameH,
            SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
        if (atlas) SDL_FillRect(atlas.get(), nullptr, SDL_MapRGBA(atlas->format, 0, 0, 0, 0));

        sdl2::surface_ptr stadiumSrc;
        for (const auto& dir : stadiumDirs) {
            std::string path = dir + "stadium_4x4.png";
            auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
            if (rwops) {
                stadiumSrc = LoadPNG_RW(rwops.get());
                if (stadiumSrc) { SDL_Log("Loaded stadium sprite from: %s", path.c_str()); break; }
            }
        }

        if (atlas && stadiumSrc) {
            SDL_SetSurfaceBlendMode(stadiumSrc.get(), SDL_BLENDMODE_NONE);
            for (int f = 0; f < numFrames; ++f) {
                SDL_Rect dst = { f * frameW, 0, frameW, frameH };
                SDL_BlitScaled(stadiumSrc.get(), nullptr, atlas.get(), &dst);
            }
            objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0] = std::move(atlas);
            objPic[ObjPic_Stadium][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 2);
            objPic[ObjPic_Stadium][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 3);
            for (int h = 1; h < NUM_HOUSES; h++) {
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    if (objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]) {
                        objPic[ObjPic_Stadium][h][z] = sdl2::surface_ptr{
                            SDL_ConvertSurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z].get(),
                                               objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]->format, 0)
                        };
                    }
                }
            }
        } else {
            SDL_Log("Stadium sprite not found; Stadium will fall back to Palace art");
            SDL_Surface* fallback = objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get();
            if (atlas && fallback) {
                const int fbFrames = objPicTiles[ObjPic_Palace].x;
                const int fbFrameW = fallback->w / fbFrames;
                const int fbFrameH = fallback->h / objPicTiles[ObjPic_Palace].y;
                for (int f = 0; f < numFrames; ++f) {
                    int sourceFrame = f % fbFrames;
                    SDL_Rect src = { sourceFrame * fbFrameW, 0, fbFrameW, fbFrameH };
                    SDL_Rect dst = { f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(fallback, &src, atlas.get(), &dst);
                }
                objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_Stadium][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_Stadium][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 3);
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_Stadium][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
            if (!objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0]) {
                sdl2::surface_ptr ph{ SDL_CreateRGBSurface(0, numFrames * frameW, frameH,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (ph) {
                    SDL_FillRect(ph.get(), nullptr, SDL_MapRGBA(ph->format, 120, 180, 100, 255));
                    objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0] = std::move(ph);
                    objPic[ObjPic_Stadium][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_Stadium][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][0].get(), 3);
                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_Stadium][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_Stadium][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
            }
        }
    }

    // ----- DuneCity airport sprite -----
    // Airport is a 3x3 footprint economic building. Source art is the
    // Micropolis 6x6 airport composite (96×96) scaled to 48×48.
    {
        std::vector<std::string> airportDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_special/",
            binDir + "imported_sprites/micropolis/composites_special/",
            binDir + "../../imported_sprites/micropolis/composites_special/",
            binDir + "../../../../../imported_sprites/micropolis/composites_special/",
        };
        if (srcDirEnv && srcDirEnv[0]) {
            std::string srcDir = srcDirEnv;
            if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
            airportDirs.push_back(srcDir + "imported_sprites/micropolis/composites_special/");
        }

        const int frameW = 3 * D2_TILESIZE;
        const int frameH = 3 * D2_TILESIZE;
        const int numFrames = 4;
        sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, numFrames * frameW, frameH,
            SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
        if (atlas) SDL_FillRect(atlas.get(), nullptr, SDL_MapRGBA(atlas->format, 0, 0, 0, 0));

        sdl2::surface_ptr airportSrc;
        for (const auto& dir : airportDirs) {
            std::string path = dir + "airport_6x6.png";
            auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
            if (rwops) {
                airportSrc = LoadPNG_RW(rwops.get());
                if (airportSrc) { SDL_Log("Loaded airport sprite from: %s", path.c_str()); break; }
            }
        }

        if (atlas && airportSrc) {
            SDL_SetSurfaceBlendMode(airportSrc.get(), SDL_BLENDMODE_NONE);
            for (int f = 0; f < numFrames; ++f) {
                SDL_Rect dst = { f * frameW, 0, frameW, frameH };
                SDL_BlitScaled(airportSrc.get(), nullptr, atlas.get(), &dst);
            }
            objPic[ObjPic_Airport][HOUSE_HARKONNEN][0] = std::move(atlas);
            objPic[ObjPic_Airport][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 2);
            objPic[ObjPic_Airport][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 3);
            for (int h = 1; h < NUM_HOUSES; h++) {
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    if (objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]) {
                        objPic[ObjPic_Airport][h][z] = sdl2::surface_ptr{
                            SDL_ConvertSurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][z].get(),
                                               objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]->format, 0)
                        };
                    }
                }
            }
        } else {
            SDL_Log("Airport sprite not found; Airport will fall back to Starport art");
            SDL_Surface* fallback = objPic[ObjPic_Starport][HOUSE_HARKONNEN][0].get();
            if (atlas && fallback) {
                const int fbFrames = objPicTiles[ObjPic_Starport].x;
                const int fbFrameW = fallback->w / fbFrames;
                const int fbFrameH = fallback->h / objPicTiles[ObjPic_Starport].y;
                for (int f = 0; f < numFrames; ++f) {
                    int sourceFrame = f % fbFrames;
                    SDL_Rect src = { sourceFrame * fbFrameW, 0, fbFrameW, fbFrameH };
                    SDL_Rect dst = { f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(fallback, &src, atlas.get(), &dst);
                }
                objPic[ObjPic_Airport][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_Airport][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_Airport][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 3);
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_Airport][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
            if (!objPic[ObjPic_Airport][HOUSE_HARKONNEN][0]) {
                sdl2::surface_ptr ph{ SDL_CreateRGBSurface(0, numFrames * frameW, frameH,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (ph) {
                    SDL_FillRect(ph.get(), nullptr, SDL_MapRGBA(ph->format, 180, 160, 100, 255));
                    objPic[ObjPic_Airport][HOUSE_HARKONNEN][0] = std::move(ph);
                    objPic[ObjPic_Airport][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_Airport][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][0].get(), 3);
                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_Airport][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_Airport][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_Airport][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
            }
        }
    }
    // ----- DuneCity hospital & church sprites -----
    // Auto-placed on residential zones by the game (SC Classic behavior).
    // Source: Micropolis 2x2 composites (32×32), matching zone cell size.
    {
        struct CivicSpec { int objPicID; const char* fileName; };
        const CivicSpec civics[] = {
            { ObjPic_Hospital, "hospital_2x2.png" },
            { ObjPic_Church,   "church_2x2.png"   },
        };
        std::vector<std::string> civicDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
            binDir + "imported_sprites/micropolis/composites_2x2/",
            binDir + "../../imported_sprites/micropolis/composites_2x2/",
            binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",
        };
        if (srcDirEnv && srcDirEnv[0]) {
            std::string sd = srcDirEnv;
            if (sd.back() != '/' && sd.back() != '\\') sd += '/';
            civicDirs.push_back(sd + "imported_sprites/micropolis/composites_2x2/");
        }
        for (const auto& cs : civics) {
            sdl2::surface_ptr cell;
            for (const auto& dir : civicDirs) {
                auto rw = sdl2::RWops_ptr{ SDL_RWFromFile((dir + cs.fileName).c_str(), "rb") };
                if (rw) {
                    cell = LoadPNG_RW(rw.get());
                    if (cell) { SDL_Log("Loaded civic sprite: %s from %s", cs.fileName, dir.c_str()); break; }
                }
            }
            const int cellSize = 2 * D2_TILESIZE;  // 32
            if (!cell) {
                SDL_Log("Civic sprite %s not found; using placeholder", cs.fileName);
                cell = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, cellSize, cellSize,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (cell) SDL_FillRect(cell.get(), nullptr, SDL_MapRGBA(cell->format, 200, 200, 200, 255));
            }
            if (cell) {
                if (cell->w != cellSize || cell->h != cellSize) {
                    sdl2::surface_ptr scaled{ SDL_CreateRGBSurface(0, cellSize, cellSize,
                        SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                    if (scaled) {
                        SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
                        SDL_BlitScaled(cell.get(), nullptr, scaled.get(), nullptr);
                        cell = std::move(scaled);
                    }
                }
                objPic[cs.objPicID][HOUSE_HARKONNEN][0] = std::move(cell);
                objPic[cs.objPicID][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[cs.objPicID][HOUSE_HARKONNEN][0].get(), 2);
                objPic[cs.objPicID][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[cs.objPicID][HOUSE_HARKONNEN][0].get(), 3);
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[cs.objPicID][HOUSE_HARKONNEN][z]) {
                            objPic[cs.objPicID][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[cs.objPicID][HOUSE_HARKONNEN][z].get(),
                                                   objPic[cs.objPicID][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
        }
    }
    } // end city-sprite loading scope (binDir, srcDirEnv, scaleRGBASurface)

    // Final safety net: ensure every DuneCity civic sprite has a populated
    // objPic for HOUSE_HARKONNEN at all zoom levels, and cloned for every
    // house.  If ANY of the per-sprite load blocks above failed to populate
    // (e.g. SDL_CreateRGBSurface returned null, or an unexpected code path),
    // clone from ConstructionYard so getObjPic/getZoomedObjPic never throws.
    {
        static const unsigned int civicIds[] = {
            ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
            ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
        };
        for (auto cid : civicIds) {
            if (!objPic[cid][HOUSE_HARKONNEN][0]) {
                SDL_Log("GFXManager: civic sprite ID %u still null after load — cloning ConstructionYard as fallback", cid);
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    if (objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]) {
                        objPic[cid][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                            SDL_ConvertSurface(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z].get(),
                                               objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]->format, 0)
                        };
                    }
                }
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[cid][HOUSE_HARKONNEN][z]) {
                            objPic[cid][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[cid][HOUSE_HARKONNEN][z].get(),
                                                   objPic[cid][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
        }
    }

    objPic[ObjPic_Bullet_SmallRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(35));
    objPic[ObjPic_Bullet_MediumRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(20));
    objPic[ObjPic_Bullet_LargeRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(40));
    objPic[ObjPic_Bullet_Small][HOUSE_HARKONNEN][0] = units1->getPicture(23);
    objPic[ObjPic_Bullet_Medium][HOUSE_HARKONNEN][0] = units1->getPicture(24);
    objPic[ObjPic_Bullet_Large][HOUSE_HARKONNEN][0] = units1->getPicture(25);
    objPic[ObjPic_Bullet_Sonic][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    replaceColor(objPic[ObjPic_Bullet_Sonic][HOUSE_HARKONNEN][0].get(), PALCOLOR_WHITE, PALCOLOR_BLACK);
    objPic[ObjPic_Bullet_SonicTemp][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    objPic[ObjPic_Hit_Gas][HOUSE_ORDOS][0] = units1->getPictureArray(5,1,57|TILE_NORMAL,58|TILE_NORMAL,59|TILE_NORMAL,60|TILE_NORMAL,61|TILE_NORMAL);
    objPic[ObjPic_Hit_Gas][HOUSE_HARKONNEN][0] = mapSurfaceColorRange(objPic[ObjPic_Hit_Gas][HOUSE_ORDOS][0].get(), PALCOLOR_ORDOS, PALCOLOR_HARKONNEN);
    objPic[ObjPic_Hit_ShellSmall][HOUSE_HARKONNEN][0] = units1->getPicture(2);
    objPic[ObjPic_Hit_ShellMedium][HOUSE_HARKONNEN][0] = units1->getPicture(3);
    objPic[ObjPic_Hit_ShellLarge][HOUSE_HARKONNEN][0] = units1->getPicture(4);
    objPic[ObjPic_ExplosionSmall][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,32|TILE_NORMAL,33|TILE_NORMAL,34|TILE_NORMAL,35|TILE_NORMAL,36|TILE_NORMAL);
    objPic[ObjPic_ExplosionMedium1][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,47|TILE_NORMAL,48|TILE_NORMAL,49|TILE_NORMAL,50|TILE_NORMAL,51|TILE_NORMAL);
    objPic[ObjPic_ExplosionMedium2][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,52|TILE_NORMAL,53|TILE_NORMAL,54|TILE_NORMAL,55|TILE_NORMAL,56|TILE_NORMAL);
    objPic[ObjPic_ExplosionLarge1][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,37|TILE_NORMAL,38|TILE_NORMAL,39|TILE_NORMAL,40|TILE_NORMAL,41|TILE_NORMAL);
    objPic[ObjPic_ExplosionLarge2][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,42|TILE_NORMAL,43|TILE_NORMAL,44|TILE_NORMAL,45|TILE_NORMAL,46|TILE_NORMAL);
    objPic[ObjPic_ExplosionSmallUnit][HOUSE_HARKONNEN][0] = units1->getPictureArray(2,1,0|TILE_NORMAL,1|TILE_NORMAL);
    objPic[ObjPic_ExplosionFlames][HOUSE_HARKONNEN][0] = units1->getPictureArray(21,1,  11|TILE_NORMAL,12|TILE_NORMAL,13|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,
                                                                                    18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,
                                                                                    19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,20|TILE_NORMAL,21|TILE_NORMAL,22|TILE_NORMAL);
    objPic[ObjPic_ExplosionSpiceBloom][HOUSE_HARKONNEN][0] = units1->getPictureArray(3,1,7|TILE_NORMAL,6|TILE_NORMAL,5|TILE_NORMAL);
    objPic[ObjPic_DeadInfantry][HOUSE_HARKONNEN][0] = icon->getPictureArray(4,1,1,6);
    objPic[ObjPic_DeadAirUnit][HOUSE_HARKONNEN][0] = icon->getPictureArray(3,1,1,6);
    objPic[ObjPic_Smoke][HOUSE_HARKONNEN][0] = units1->getPictureArray(3,1,29|TILE_NORMAL,30|TILE_NORMAL,31|TILE_NORMAL);
    objPic[ObjPic_SandwormShimmerMask][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    replaceColor(objPic[ObjPic_SandwormShimmerMask][HOUSE_HARKONNEN][0].get(), PALCOLOR_WHITE, PALCOLOR_BLACK);
    objPic[ObjPic_SandwormShimmerTemp][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0] = icon->getPictureRow(124,209,NUM_TERRAIN_TILES_X);
    objPic[ObjPic_Terrain_GreenSpice][HOUSE_HARKONNEN][0] =
        createTintedTerrainSpiceSurface(objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0].get(),
                                        SDL_Color{ 24, 112, 48, 255 },
                                        SDL_Color{ 20, 84, 42, 255 });
    objPic[ObjPic_Terrain_RedSpice][HOUSE_HARKONNEN][0] =
        createTintedTerrainSpiceSurface(objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0].get(),
                                        SDL_Color{ 136, 48, 40, 255 },
                                        SDL_Color{ 96, 32, 30, 255 });
    objPic[ObjPic_DestroyedStructure][HOUSE_HARKONNEN][0] = icon->getPictureRow2(14, 33, 125, 213, 214, 215, 223, 224, 225, 232, 233, 234, 240, 246, 247);
    objPic[ObjPic_RockDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(1,6);
    objPic[ObjPic_SandDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(7,12);
    objPic[ObjPic_Terrain_Hidden][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_Tracks][HOUSE_HARKONNEN][0] = icon->getPictureRow(25,32);
    objPic[ObjPic_Star][HOUSE_HARKONNEN][0] = LoadPNG_RW(pFileManager->openFile("Star5x5.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][1] = LoadPNG_RW(pFileManager->openFile("Star7x7.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][2] = LoadPNG_RW(pFileManager->openFile("Star11x11.png").get());

    // Load IBM.PAL early so Tornie's 8-bit sprites use the standard sprite
    // palette before house-color remapping.
    // Custom_IBM.PAL is reserved for extra house-color ramps and is not used
    // to recolor sprite assets.
    Palette ibmPalette(256);
    bool ibmPaletteLoaded = false;
    try {
        if(pFileManager->exists("IBM.PAL")) {
            ibmPalette = LoadPalette_RW(pFileManager->openFile("IBM.PAL").get());
            ibmPaletteLoaded = true;
        }
        SDL_Log("GFXManager: ibmPalette loaded (%d colors, source=%s)",
                ibmPalette.getNumColors(),
                ibmPaletteLoaded ? "IBM.PAL" : "PNG palette");
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: ibmPalette load failed (%s) — Tornie sprite tinting disabled", e.what());
    }

    auto openTornieAsset = [&](const char* filename, const char* label) -> sdl2::RWops_ptr {
        const bool tornieActive = ModManager::instance().isInitialized()
            && ModManager::instance().getActiveModName() == "Tornie";
        if(!tornieActive) {
            return nullptr;
        }

        if(pFileManager->exists(filename)) {
            SDL_Log("GFXManager: %s asset '%s' loaded through active Tornie lookup", label, filename);
            return pFileManager->openFile(filename);
        }

        if(auto packedAsset = pFileManager->openFileFromNamedPak(filename, "Tornie.PAK")) {
            SDL_Log("GFXManager: %s asset '%s' loaded directly from Tornie.PAK", label, filename);
            return packedAsset;
        }

        return nullptr;
    };

    auto getTornieFrameCount = [](SDL_Surface* surface, int frameWidth, int frameHeight) -> int {
        if(!surface || frameWidth <= 0 || frameHeight <= 0) {
            return 0;
        }

        if(surface->w >= 2 * frameWidth && surface->h >= frameHeight && surface->h < 2 * frameHeight) {
            return std::max(1, surface->w / frameWidth);
        }

        if(surface->h >= 2 * frameHeight) {
            return std::max(1, surface->h / frameHeight);
        }

        return 1;
    };

    auto getTornieFrameRect = [&](SDL_Surface* surface, int frameWidth, int frameHeight, int frame) -> SDL_Rect {
        if(!surface || frameWidth <= 0 || frameHeight <= 0) {
            return SDL_Rect{0, 0, 0, 0};
        }

        const bool horizontal =
            surface->w >= 2 * frameWidth
            && surface->h >= frameHeight
            && surface->h < 2 * frameHeight;
        const int frameCount = getTornieFrameCount(surface, frameWidth, frameHeight);
        const int clampedFrame = std::max(0, std::min(frame, std::max(0, frameCount - 1)));

        if(horizontal) {
            const int sourceX = clampedFrame * frameWidth;
            return SDL_Rect{
                sourceX,
                0,
                std::min(frameWidth, std::max(0, surface->w - sourceX)),
                std::min(frameHeight, surface->h)
            };
        }

        const int sourceY = (frameCount > 1) ? clampedFrame * frameHeight : 0;
        return SDL_Rect{
            0,
            sourceY,
            std::min(frameWidth, surface->w),
            std::min(frameHeight, std::max(0, surface->h - sourceY))
        };
    };

    auto loadTorniePalettedSprite = [&](unsigned int objPicEnum,
                                        const char* pngName,
                                        const char* label) {
        try {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing", label, pngName);
                return;
            }

            auto raw = LoadPNG_RW(rwop.get());
            if(!raw) {
                SDL_Log("GFXManager: %s sprite '%s' failed to decode", label, pngName);
                return;
            }

            if(raw->format->BitsPerPixel != 8 || !raw->format->palette) {
                SDL_Log("GFXManager: %s sprite '%s' is not 8-bit indexed, refusing it", label, pngName);
                return;
            }
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                    raw = std::move(remapped);
                } else {
                    ibmPalette.applyToSurface(raw.get());
                }
                normalizeTransparentPaletteIndexes(raw.get());
            }

            objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(raw);
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0]) {
                objPic[objPicEnum][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][0].get());
                if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
                    objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: %s sprite '%s' loaded", label, pngName);
        } catch(std::exception& e) {
            SDL_Log("GFXManager: %s sprite load failed (%s)", label, e.what());
        }
    };

    auto loadTornieIndexedSheet = [&](const char* pngName, const char* label) -> sdl2::surface_ptr {
        auto rwop = openTornieAsset(pngName, label);
        if(!rwop) {
            SDL_Log("GFXManager: %s sheet '%s' missing", label, pngName);
            return nullptr;
        }

        auto sheet = LoadPNG_RW(rwop.get());
        if(!sheet || sheet->format->BitsPerPixel != 8 || !sheet->format->palette) {
            SDL_Log("GFXManager: %s sheet '%s' is not 8-bit indexed", label, pngName);
            return nullptr;
        }

        normalizeTransparentPaletteIndexes(sheet.get());
        if(ibmPaletteLoaded) {
            ibmPalette.applyToSurface(sheet.get());
        }
        SDL_SetColorKey(sheet.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
        return sheet;
    };

    auto createGroundUnitAtlas = [&](SDL_Surface* sheet, int firstFrame,
                                      const char* label) -> sdl2::surface_ptr {
        constexpr int frameSize = D2_TILESIZE;
        constexpr int sourceColumns = 10;
        if(!sheet || sheet->w < sourceColumns * frameSize || firstFrame < 0
                || (firstFrame + 4) / sourceColumns * frameSize + frameSize > sheet->h) {
            SDL_Log("GFXManager: %s has an invalid ground-unit sheet layout", label);
            return nullptr;
        }

        auto atlas = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, NUM_ANGLES * frameSize,
                                                              frameSize, 8, 0, 0, 0, 0) };
        if(!atlas || !atlas->format->palette) {
            return nullptr;
        }

        SDL_SetPaletteColors(atlas->format->palette, sheet->format->palette->colors,
                             0, sheet->format->palette->ncolors);
        SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
        SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

        static const int sourceFrames[NUM_ANGLES] = { 2, 1, 0, 1, 2, 3, 4, 3 };
        static const bool mirrorFrames[NUM_ANGLES] = { false, false, false, true,
                                                       true, true, false, false };
        for(int angle = 0; angle < NUM_ANGLES; ++angle) {
            const int sourceIndex = firstFrame + sourceFrames[angle];
            const int sourceX = (sourceIndex % sourceColumns) * frameSize;
            const int sourceY = (sourceIndex / sourceColumns) * frameSize;
            auto frame = getSubPicture(sheet, sourceX, sourceY, frameSize, frameSize);
            if(mirrorFrames[angle]) {
                frame = flipVSurface(frame.get());
            }
            SDL_Rect destination{ angle * frameSize, 0, frameSize, frameSize };
            SDL_BlitSurface(frame.get(), nullptr, atlas.get(), &destination);
        }

        return atlas;
    };

    auto installGroundUnitAtlas = [&](unsigned int objPicEnum, SDL_Surface* sheet,
                                      int firstFrame, const char* label) {
        auto atlas = createGroundUnitAtlas(sheet, firstFrame, label);
        if(!atlas) {
            return false;
        }

        objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(atlas);
        objPic[objPicEnum][HOUSE_HARKONNEN][1] =
            Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][0].get());
        if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
            objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][1].get());
        }
        SDL_Log("GFXManager: installed Tornie %s ground-unit atlas", label);
        return true;
    };

    loadTorniePalettedSprite(ObjPic_DeviatorGunTornie,
                             "DeviatorGun.png",
                             "Deviator turret");
    loadTorniePalettedSprite(ObjPic_FlameTankGunTornie,
                             "FlameTankGun.png",
                             "Flame Tank turret");
    loadTorniePalettedSprite(ObjPic_EliteLauncherGunTornie,
                             "EliteLauncherGun.png",
                             "Elite Launcher turret");
    loadTorniePalettedSprite(ObjPic_HarvestankGunTornie,
                             "HarvestankGun.png",
                             "Harvestank turret");

    try {
        auto setAdvancedWindtrapAtlas = [&](int objPicEnum, sdl2::surface_ptr atlas, const char* label) {
            if(!atlas) {
                return false;
            }

            objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(atlas);
            objPic[objPicEnum][HOUSE_HARKONNEN][1] =
                scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 2);
            if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
                objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                    scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 3);
            }
            SDL_Log("GFXManager: Advanced Windtrap %s sprite loaded", label);
            return true;
        };

        auto loadIndexedTornieAtlasSource = [&](const char* pngName, const char* label) -> sdl2::surface_ptr {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing", label, pngName);
                return nullptr;
            }

            auto raw = LoadPNG_RW(rwop.get());
            if(!raw || raw->format->BitsPerPixel != 8 || !raw->format->palette) {
                SDL_Log("GFXManager: %s sprite '%s' is not 8-bit indexed, refusing it", label, pngName);
                return nullptr;
            }

            preserveOpaqueBlackIndex(raw.get());
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                    raw = std::move(remapped);
                } else {
                    ibmPalette.applyToSurface(raw.get());
                }
                normalizeTransparentPaletteIndexes(raw.get());
            }
            normalizeHouseColorRangesToHarkonnen(raw.get());
            normalizeHarkonnenTeamRed(raw.get());

            return raw;
        };

        auto loadAdvancedWindtrapVariant = [&](int objPicEnum,
                                               const char* pngName,
                                               Coord footprint,
                                               const char* label,
                                               const char* buildSiteName = nullptr) {
            auto raw = loadIndexedTornieAtlasSource(pngName, std::string("Advanced Windtrap ").append(label).c_str());
            if(!raw) {
                return false;
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            const int rawFrameCount = getTornieFrameCount(raw.get(), frameWidth, frameHeight);
            const bool rawHasFullHorizontalAtlas =
                raw->w >= 4 * frameWidth
                && raw->h >= frameHeight
                && raw->h < 2 * frameHeight;
            if(rawFrameCount <= 0) {
                SDL_Log("GFXManager: Advanced Windtrap %s sprite '%s' has an unsupported size", label, pngName);
                return false;
            }
            logTornieStructureSurfaceDiagnostics("raw-normalized", label, raw.get(), frameWidth, frameHeight);

            sdl2::surface_ptr buildSite;
            if(buildSiteName != nullptr && pFileManager->exists(buildSiteName)) {
                buildSite = loadIndexedTornieAtlasSource(buildSiteName, std::string("Advanced Windtrap build site ").append(label).c_str());
                if(buildSite) {
                    logTornieStructureSurfaceDiagnostics("build-normalized", label, buildSite.get(), frameWidth, frameHeight);
                }
            }
            SDL_Surface* buildSource = buildSite ? buildSite.get() : raw.get();
            const int buildFrameCount = getTornieFrameCount(buildSource, frameWidth, frameHeight);

            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, 4 * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            if(!atlas || !atlas->format->palette) {
                return false;
            }

            SDL_SetPaletteColors(atlas->format->palette,
                                 raw->format->palette->colors,
                                 0,
                                 raw->format->palette->ncolors);
            SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(buildSource, SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE);
            SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(buildSource, SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            SDL_Rect srcTop = getTornieFrameRect(raw.get(), frameWidth, frameHeight, rawHasFullHorizontalAtlas ? 2 : 0);
            SDL_Rect srcBottom = getTornieFrameRect(raw.get(), frameWidth, frameHeight,
                                                    rawHasFullHorizontalAtlas ? 3 : (rawFrameCount > 1 ? 1 : 0));
            SDL_Rect buildTop = getTornieFrameRect(buildSource, frameWidth, frameHeight, 0);
            SDL_Rect buildBottom = getTornieFrameRect(buildSource, frameWidth, frameHeight, buildFrameCount > 1 ? 1 : 0);

            const bool protectOpaqueBlack =
                raw->format->palette->ncolors > PALCOLOR_BLACK
                && buildSource->format->palette->ncolors > PALCOLOR_BLACK
                && atlas->format->palette->ncolors > PALCOLOR_BLACK;
            const SDL_Color rawBlack = protectOpaqueBlack ? raw->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color buildBlack = protectOpaqueBlack ? buildSource->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color atlasBlack = protectOpaqueBlack ? atlas->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK].g = 1;
                buildSource->format->palette->colors[PALCOLOR_BLACK].g = 1;
                atlas->format->palette->colors[PALCOLOR_BLACK].g = 1;
            }

            auto blitFrame = [&](SDL_Surface* source, SDL_Rect* src, int frame) {
                if(!source || src->w <= 0 || src->h <= 0) {
                    return;
                }
                SDL_Rect dst{frame * frameWidth + (frameWidth - src->w) / 2, frameHeight - src->h, src->w, src->h};
                SDL_BlitSurface(source, src, atlas.get(), &dst);
            };

            blitFrame(buildSource, &buildTop, 0);
            blitFrame(buildSource, &buildBottom, 1);
            blitFrame(raw.get(), &srcTop, 2);
            blitFrame(raw.get(), &srcBottom, 3);
            logTornieStructureSurfaceDiagnostics("atlas-built", label, atlas.get(), frameWidth, frameHeight);

            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK] = rawBlack;
                buildSource->format->palette->colors[PALCOLOR_BLACK] = buildBlack;
                atlas->format->palette->colors[PALCOLOR_BLACK] = atlasBlack;
            }

            normalizeTransparentPaletteIndexes(atlas.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, atlas.get(), frameWidth, frameHeight);

            return setAdvancedWindtrapAtlas(objPicEnum, std::move(atlas), label);
        };

        auto createAdvancedWindtrapPlaceholder = [&](int objPicEnum, Coord footprint, const char* label) {
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0] != nullptr) {
                return;
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            sdl2::surface_ptr placeholder{ SDL_CreateRGBSurface(0, 4 * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            SDL_Surface* windtrap = objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get();
            if(!placeholder || !placeholder->format->palette || !windtrap || !windtrap->format->palette) {
                return;
            }

            SDL_SetPaletteColors(placeholder->format->palette,
                                 windtrap->format->palette->colors,
                                 0,
                                 windtrap->format->palette->ncolors);
            SDL_SetSurfaceBlendMode(placeholder.get(), SDL_BLENDMODE_NONE);
            SDL_FillRect(placeholder.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(placeholder.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            const int windtrapFrameSize = 2 * D2_TILESIZE;
            SDL_Rect src{2 * windtrapFrameSize, 0, windtrapFrameSize, windtrapFrameSize};
            src.w = std::min(src.w, frameWidth);
            src.h = std::min(src.h, frameHeight);
            for(int frame = 0; frame < 4; frame++) {
                SDL_Rect dst{frame * frameWidth + (frameWidth - src.w) / 2, frameHeight - src.h, src.w, src.h};
                SDL_BlitSurface(windtrap, &src, placeholder.get(), &dst);
            }

            normalizeTransparentPaletteIndexes(placeholder.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, placeholder.get(), frameWidth, frameHeight);

            setAdvancedWindtrapAtlas(objPicEnum, std::move(placeholder), label);
        };

        if(!loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap, "Tornie_AdvancedWindtrap_gfx.png", Coord(3,3), "3x3", "BUILDING_3x3_prebuild.png")) {
            loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap, "super_power_plant.png", Coord(3,3), "3x3 fallback");
        }
        loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap2x3, "advanced_power_2x3.png", Coord(2,3), "2x3", "BuildSite_2x3.png");
        loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap3x2, "Advanced_Power_Plant.png", Coord(3,2), "3x2", "BUILDING_3x2_prebuild.png");

        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap, Coord(3,3), "3x3 placeholder");
        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap2x3, Coord(2,3), "2x3 placeholder");
        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap3x2, Coord(3,2), "3x2 placeholder");
    } catch(std::exception& e) {
        SDL_Log("GFXManager: Advanced Windtrap sprite load failed (%s)", e.what());
    }

    // DuneCity 1.0.503: Rocket Trike uses its own dedicated sprite (8-frame,
    // 1-tile-tall strip, 128x16) with a separate RocketTrikeMask.png that holds
    // the per-house colour tint via the benePalette remap path. Restored from
    // v1.0.250 (commit 4265a5f in v1.0.251 dropped this; Tornie confirms the
    // mask carries the intended red-tint variant).
    //
    // Preferred path: RocketTrikeMask.png (8-bit palette-indexed) — load into
    // HOUSE_HARKONNEN only; getZoomedObjPic remaps indices 144-150 per house.
    // Fallback: RocketTrike.png (RGBA) — pre-build all zoom/house slots.
    // Final fallback: vanilla Trike sprite.
    objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    {
        bool usedPaletteIndexed = false;
        try {
            if(pFileManager->exists("RocketTrikeMask.png")) {
                auto rtMask = LoadPNG_RW(pFileManager->openFile("RocketTrikeMask.png").get());
                if(rtMask && rtMask->format->BitsPerPixel == 8 && rtMask->format->palette) {
                    // v1.0.509 (Tornie OOB): switch from benePalette to ibmPalette so
                    // the RocketTrike participates in the per-house color remap that
                    // mapSurfaceColorRange drives from PALCOLOR_HARKONNEN. benePalette
                    // was the original v1.0.250 fix but gave the RocketTrike a fixed
                    // red tint that didn't shift with the owning house — that was a
                    // mistake per the user.
                    if(ibmPaletteLoaded) {
                        ibmPalette.applyToSurface(rtMask.get());
                    }
                    objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = std::move(rtMask);
                    usedPaletteIndexed = true;
                    SDL_Log("GFXManager: Loaded RocketTrikeMask.png (palette-indexed, per-house remap)");
                } else if(rtMask) {
                    SDL_Log("GFXManager: RocketTrikeMask.png is not 8-bit palette-indexed (%d bpp), falling back to RGBA path",
                            rtMask->format->BitsPerPixel);
                }
            }
            if(!usedPaletteIndexed && pFileManager->exists("RocketTrike.png")) {
                auto rtRaw = LoadPNG_RW(pFileManager->openFile("RocketTrike.png").get());
                if(rtRaw) {
                    sdl2::surface_ptr rtSurf{ SDL_ConvertSurfaceFormat(rtRaw.get(), SCREEN_FORMAT, 0) };
                    if(rtSurf) {
                        auto scaleRT = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
                            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                                src->w * factor, src->h * factor,
                                src->format->BitsPerPixel,
                                src->format->Rmask, src->format->Gmask,
                                src->format->Bmask, src->format->Amask) };
                            if(dst) SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
                            return dst;
                        };
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = std::move(rtSurf);
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][1] = scaleRT(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][2] = scaleRT(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(), 3);
                        for(int h = 1; h < (int)NUM_HOUSES; h++) {
                            for(int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                if(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_RocketTrike][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z]->format, 0) };
                                }
                            }
                        }
                    }
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: RocketTrike sprite load failed (%s) — falling back to vanilla Trike", e.what());
        }
    }

    // Sonic Trike follows the same simple indexed eight-frame loading path as
    // Rocket Trike. The supplied PNG already contains all eight directions, so
    // it must not be normalized or rebuilt as a five-frame source strip.
    objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = units->getPictureArray(8, 1, GROUNDUNIT_ROW(5));
    {
        bool usedPaletteIndexed = false;
        try {
            if(pFileManager->exists("SonicTrikeMask.png")) {
                auto stMask = LoadPNG_RW(pFileManager->openFile("SonicTrikeMask.png").get());
                if(stMask && stMask->format->BitsPerPixel == 8 && stMask->format->palette
                   && stMask->h > 0 && stMask->w == stMask->h * NUM_ANGLES) {
                    if(ibmPaletteLoaded) {
                        ibmPalette.applyToSurface(stMask.get());
                    }
                    objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = std::move(stMask);
                    usedPaletteIndexed = true;
                    SDL_Log("GFXManager: Loaded SonicTrikeMask.png (palette-indexed, per-house remap)");
                } else if(stMask) {
                    SDL_Log("GFXManager: SonicTrikeMask.png is not an indexed 8-frame strip; falling back to SonicTrike.png");
                }
            }

            if(!usedPaletteIndexed && pFileManager->exists("SonicTrike.png")) {
                auto stRaw = LoadPNG_RW(pFileManager->openFile("SonicTrike.png").get());
                if(stRaw && stRaw->h > 0 && stRaw->w == stRaw->h * NUM_ANGLES) {
                    sdl2::surface_ptr stSurf{ SDL_ConvertSurfaceFormat(stRaw.get(), SCREEN_FORMAT, 0) };
                    if(stSurf) {
                        auto scaleST = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
                            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                                src->w * factor, src->h * factor,
                                src->format->BitsPerPixel,
                                src->format->Rmask, src->format->Gmask,
                                src->format->Bmask, src->format->Amask) };
                            if(dst) {
                                SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
                            }
                            return dst;
                        };

                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = std::move(stSurf);
                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][1] =
                            scaleST(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][2] =
                            scaleST(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(), 3);

                        for(int h = 1; h < static_cast<int>(NUM_HOUSES); ++h) {
                            for(int z = 0; z < NUM_ZOOMLEVEL; ++z) {
                                if(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_SonicTrike][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z]->format, 0) };
                                }
                            }
                        }
                        SDL_Log("GFXManager: Loaded SonicTrike.png (RGBA eight-direction fallback)");
                    }
                } else if(stRaw) {
                    SDL_Log("GFXManager: SonicTrike.png is not an 8-frame strip; using vanilla Trike fallback");
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: SonicTrike sprite load failed (%s), falling back to vanilla Trike", e.what());
        }
    }

#if 0 // Replaced by the native 80x10 FlameTankGun.png turret strip above.
    SDL_Log("GFXManager: Loading FlameTank.png...");
    try {
        SDL_Log("GFXManager: FlameTank step 1: openFile");
        auto ftRaw = LoadPNG_RW(pFileManager->openFile("FlameTank.png").get());
        SDL_Log("GFXManager: FlameTank step 2: LoadPNG_RW returned (%s)",
                ftRaw ? "valid surface" : "null surface");
        if(ftRaw) {
            SDL_Log("GFXManager: FlameTank step 3: BitsPerPixel=%d palette=%s",
                    ftRaw->format->BitsPerPixel,
                    ftRaw->format->palette ? "yes" : "no");
            // Apply palette: 8-bit palette-indexed sprites use ibmPalette (authored
            // against IBM.PAL), not benePalette.
            if(ibmPaletteLoaded && ftRaw->format->BitsPerPixel == 8 && ftRaw->format->palette) {
                SDL_Log("GFXManager: FlameTank step 3a: apply ibmPalette");
                ibmPalette.applyToSurface(ftRaw.get());
                SDL_Log("GFXManager: FlameTank step 3b: ibmPalette applied");
            }
            SDL_Log("GFXManager: FlameTank step 4: std::move to objPic");
            objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0] = std::move(ftRaw);
            // Generate zoom levels 1 and 2 so getZoomedObjPic never throws on a
            // null HOUSE_HARKONNEN[z>0] entry. Same pattern as v1.0.240 EliteSiegeTank fix.
            SDL_Log("GFXManager: FlameTank step 5: generate zoom 1");
            if(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0]) {
                objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0].get());
                SDL_Log("GFXManager: FlameTank step 6: generate zoom 2");
                if(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1]) {
                    objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: FlameTank.png loaded (all zoom levels)");
        } else {
            SDL_Log("GFXManager: FlameTank.png: surface is null, skipping");
        }
    } catch(std::exception& e) {
        SDL_Log("GFXManager: %s — FlameTank sprite missing, units will fall back to placeholder", e.what());
    }

#endif

    SDL_Log("GFXManager: Loading EliteSiegeTank.png...");
    try {
        auto estRaw = LoadPNG_RW(pFileManager->openFile("EliteSiegeTank.png").get());
        if(estRaw) {
            // Use ibmPalette (not benePalette) — sprite is authored against IBM.PAL.
            if(estRaw->format->BitsPerPixel != 8 || !estRaw->format->palette) {
                SDL_Log("GFXManager: EliteSiegeTank.png is not 8-bit indexed, refusing it");
                estRaw.reset();
            } else if(ibmPaletteLoaded) {
                ibmPalette.applyToSurface(estRaw.get());
            }
        }

        if(estRaw) {
            objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0] = std::move(estRaw);
            // Generate zoom levels 1 and 2 so getZoomedObjPic never throws on a
            // null HOUSE_HARKONNEN[z>0] entry. Fix from v1.0.240 EliteSiegeTank crash.
            if(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0]) {
                objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0].get());
                if(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1]) {
                    objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: EliteSiegeTank.png loaded (all zoom levels)");
        }
    } catch(std::exception& e) {
        SDL_Log("GFXManager: %s — EliteSiegeTank sprite missing, units will fall back to placeholder", e.what());
    }

    // Keep the validated Elite Siege Tank turret from Tornie's indexed UNITS2
    // reconversion. The other experimental atlas replacements are deliberately
    // left on their previous graphics until their frame layouts are corrected.
    {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                objPic[ObjPic_EliteSiegeTankGunTornie][colorSlot][zoom].reset();
                objPicTex[ObjPic_EliteSiegeTankGunTornie][colorSlot][zoom].reset();
            }
        }

        auto units2Sp = loadTornieIndexedSheet("TornieUnits2.png", "Tornie UNITS2");
        if(units2Sp) {
            installGroundUnitAtlas(ObjPic_EliteSiegeTankGunTornie, units2Sp.get(), 15,
                                   "Elite Siege Tank turret");
        }

        if(!objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0]) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                if(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][zoom]) {
                    objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][zoom] =
                        copySurface(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][zoom].get());
                }
            }
        }
    }

    // DuneCity 1.0.509: Tornie Worfinery + Tech Center dedicated sprites.
    // 48x64 PNG, 8-bit palette-indexed, 2 vertical frames (3 tiles wide × 2 tiles tall
    // per frame). Animation runs at ConstructionYard speed (handled by the
    // structure itself via frame index based on game cycle).
    auto loadTornieStructureSprite = [&](unsigned int objPicEnum,
                                          const char* pngName,
                                          Coord footprint,
                                          const char* buildSiteName,
                                          const char* label,
                                          bool normalizeTeamRed = false,
                                          bool normalizeLooseTeamPaint = false) {
        try {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing — using vanilla fallback", label, pngName);
                return;
            }
            auto raw = LoadPNG_RW(rwop.get());
            if(!raw) {
                SDL_Log("GFXManager: %s sprite '%s' failed to decode — using vanilla fallback", label, pngName);
                return;
            }
            if(raw->format->BitsPerPixel != 8 || !raw->format->palette) {
                SDL_Log("GFXManager: %s sprite '%s' is not 8-bit indexed, refusing it", label, pngName);
                return;
            }
            preserveOpaqueBlackIndex(raw.get());
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                    raw = std::move(remapped);
                } else {
                    ibmPalette.applyToSurface(raw.get());
                }
                normalizeTransparentPaletteIndexes(raw.get());
            }
            normalizeHouseColorRangesToHarkonnen(raw.get());
            if(normalizeTeamRed) {
                normalizeHarkonnenTeamRed(raw.get());
            }
            if(normalizeLooseTeamPaint) {
                normalizeLooseTeamPaintToHarkonnen(raw.get());
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            const int rawFrameCount = getTornieFrameCount(raw.get(), frameWidth, frameHeight);
            const bool rawHasFullHorizontalAtlas =
                raw->w >= 4 * frameWidth
                && raw->h >= frameHeight
                && raw->h < 2 * frameHeight;
            if(rawFrameCount <= 0) {
                SDL_Log("GFXManager: %s sprite '%s' has an unsupported size", label, pngName);
                return;
            }
            logTornieStructureSurfaceDiagnostics("raw-normalized", label, raw.get(), frameWidth, frameHeight);

            sdl2::surface_ptr buildSite;
            if(buildSiteName != nullptr) {
                auto buildRwop = openTornieAsset(buildSiteName, label);
                buildSite = buildRwop ? LoadPNG_RW(buildRwop.get()) : nullptr;
                if(buildSite && buildSite->format->BitsPerPixel == 8 && buildSite->format->palette) {
                    preserveOpaqueBlackIndex(buildSite.get());
                    normalizeTransparentPaletteIndexes(buildSite.get());
                    if(ibmPaletteLoaded) {
                        if(auto remapped = remapIndexedSurfaceToPalette(buildSite.get(), ibmPalette.getSDLPalette())) {
                            buildSite = std::move(remapped);
                        } else {
                            ibmPalette.applyToSurface(buildSite.get());
                        }
                        normalizeTransparentPaletteIndexes(buildSite.get());
                    }
                    normalizeHouseColorRangesToHarkonnen(buildSite.get());
                    if(normalizeTeamRed) {
                        normalizeHarkonnenTeamRed(buildSite.get());
                    }
                    if(normalizeLooseTeamPaint) {
                        normalizeLooseTeamPaintToHarkonnen(buildSite.get());
                    }
                    logTornieStructureSurfaceDiagnostics("build-normalized", label, buildSite.get(), frameWidth, frameHeight);
                } else {
                    SDL_Log("GFXManager: %s build-site sprite '%s' is not 8-bit indexed, using active frame", label, buildSiteName);
                    buildSite.reset();
                }
            }

            SDL_Surface* buildSource = buildSite ? buildSite.get() : raw.get();
            const int buildFrameCount = getTornieFrameCount(buildSource, frameWidth, frameHeight);

            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, 4 * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            if(!atlas || !atlas->format->palette) {
                return;
            }
            SDL_SetPaletteColors(atlas->format->palette,
                                 raw->format->palette->colors,
                                 0,
                                 raw->format->palette->ncolors);

            SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(buildSource, SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE);
            SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(buildSource, SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            SDL_Rect srcTop = getTornieFrameRect(raw.get(), frameWidth, frameHeight, rawHasFullHorizontalAtlas ? 2 : 0);
            SDL_Rect srcBottom = getTornieFrameRect(raw.get(), frameWidth, frameHeight,
                                                    rawHasFullHorizontalAtlas ? 3 : (rawFrameCount > 1 ? 1 : 0));
            SDL_Rect buildTop = getTornieFrameRect(buildSource, frameWidth, frameHeight, 0);
            SDL_Rect buildBottom = getTornieFrameRect(buildSource, frameWidth, frameHeight, buildFrameCount > 1 ? 1 : 0);

            const bool protectOpaqueBlack =
                raw->format->palette->ncolors > PALCOLOR_BLACK
                && buildSource->format->palette->ncolors > PALCOLOR_BLACK
                && atlas->format->palette->ncolors > PALCOLOR_BLACK;
            const SDL_Color rawBlack = protectOpaqueBlack ? raw->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color buildBlack = protectOpaqueBlack ? buildSource->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color atlasBlack = protectOpaqueBlack ? atlas->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK].g = 1;
                buildSource->format->palette->colors[PALCOLOR_BLACK].g = 1;
                atlas->format->palette->colors[PALCOLOR_BLACK].g = 1;
            }

            auto blitFrame = [&](SDL_Surface* source, SDL_Rect* src, int frame) {
                if(!source || src->w <= 0 || src->h <= 0) {
                    return;
                }
                SDL_Rect dst{frame * frameWidth + (frameWidth - src->w) / 2, frameHeight - src->h, src->w, src->h};
                SDL_BlitSurface(source, src, atlas.get(), &dst);
            };

            blitFrame(buildSource, &buildTop, 0);
            blitFrame(buildSource, &buildBottom, 1);
            blitFrame(raw.get(), &srcTop, 2);
            blitFrame(raw.get(), &srcBottom, 3);
            logTornieStructureSurfaceDiagnostics("atlas-built", label, atlas.get(), frameWidth, frameHeight);

            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK] = rawBlack;
                buildSource->format->palette->colors[PALCOLOR_BLACK] = buildBlack;
                atlas->format->palette->colors[PALCOLOR_BLACK] = atlasBlack;
            }

            normalizeTransparentPaletteIndexes(atlas.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, atlas.get(), frameWidth, frameHeight);

            objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(atlas);
            // Generate zoom levels 1 and 2
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0]) {
                objPic[objPicEnum][HOUSE_HARKONNEN][1] =
                    scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 2);
                if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
                    objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                        scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 3);
                }
            }
            SDL_Log("GFXManager: %s sprite '%s' loaded (all zoom levels)", label, pngName);
        } catch(std::exception& e) {
            SDL_Log("GFXManager: %s — %s sprite load failed, using vanilla fallback", e.what(), label);
        }
    };
    loadTornieStructureSprite(ObjPic_Worfinery,  "Worfinery.png",  Coord(3,2), "BUILDING_3x2_prebuild.png", "Worfinery", true, true);
    loadTornieStructureSprite(ObjPic_TechCenter, "TechCenter.png", Coord(3,2), "BUILDING_3x2_prebuild.png", "TechCenter", true);
    loadTornieStructureSprite(ObjPic_Scoutpost,  "Scoutpost.png",  Coord(1,1), "BUILDING_1x1_prebuild.png", "Scoutpost", true);

    // v1.0.173-compatible Tornie structure rendering path.
    //
    // The older working implementation converted custom building atlases to
    // truecolor RGBA surfaces before SDL texture creation and populated every
    // house slot up front. Keep that behavior here for all Tornie structures.
    // This avoids renderer/backend differences when an 8-bit indexed PNG atlas
    // is converted lazily after house remapping (the object remains selectable
    // but its final texture can render as fully transparent on Direct3D11).
    auto installTornieStructureTruecolorSlots = [&](unsigned int objPicEnum, const char* label) {
        SDL_Surface* base = objPic[objPicEnum][HOUSE_HARKONNEN][0].get();
        if(base == nullptr || base->format == nullptr || base->format->BytesPerPixel != 1
           || base->format->palette == nullptr) {
            SDL_Log("TornieGFX: v173-rgba %s skipped (base atlas is not indexed)", label);
            return;
        }

        // Preserve the indexed base while every visual colour slot is derived.
        sdl2::surface_ptr indexedBase = copySurface(base);
        if(!indexedBase) {
            SDL_Log("TornieGFX: v173-rgba %s skipped (base copy failed)", label);
            return;
        }

        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            sdl2::surface_ptr indexed;
            if(colorSlot == HOUSE_HARKONNEN) {
                indexed = copySurface(indexedBase.get());
            } else {
                indexed = mapSurfaceColorRange(indexedBase.get(),
                                               PALCOLOR_HARKONNEN,
                                               getHouseColorPaletteIndexFromSlot(colorSlot));
                if(indexed) {
                    applyCustomVisualColorRamp(indexed.get(), colorSlot);
                    if(colorSlot == HOUSE_REBELS) {
                        applyRebelsTint(indexed.get());
                    }
                }
            }

            if(!indexed) {
                SDL_Log("TornieGFX: v173-rgba %s colorSlot=%d remap failed", label, colorSlot);
                continue;
            }

            normalizeTransparentPaletteIndexes(indexed.get());
            SDL_SetColorKey(indexed.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            auto rgba = convertTornieIndexedSurfaceToRGBA(indexed.get(), label, colorSlot, 0, false);
            if(!rgba) {
                SDL_Log("TornieGFX: v173-rgba %s colorSlot=%d conversion failed", label, colorSlot);
                continue;
            }

            SDL_SetColorKey(rgba.get(), SDL_FALSE, 0);
            SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_BLEND);

            objPic[objPicEnum][colorSlot][0] = std::move(rgba);
            objPic[objPicEnum][colorSlot][1] =
                scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 2);
            objPic[objPicEnum][colorSlot][2] =
                scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 3);

            if(objPic[objPicEnum][colorSlot][1]) {
                SDL_SetSurfaceBlendMode(objPic[objPicEnum][colorSlot][1].get(), SDL_BLENDMODE_BLEND);
            }
            if(objPic[objPicEnum][colorSlot][2]) {
                SDL_SetSurfaceBlendMode(objPic[objPicEnum][colorSlot][2].get(), SDL_BLENDMODE_BLEND);
            }
        }

        SDL_Log("TornieGFX: v173-rgba %s installed for %d visual colour slots",
                label, NUM_HOUSE_COLOR_SLOTS);
    };

    installTornieStructureTruecolorSlots(ObjPic_AdvancedWindTrap,     "AdvancedWindTrap3x3");
    installTornieStructureTruecolorSlots(ObjPic_AdvancedWindTrap2x3, "AdvancedWindTrap2x3");
    installTornieStructureTruecolorSlots(ObjPic_AdvancedWindTrap3x2, "AdvancedWindTrap3x2");
    installTornieStructureTruecolorSlots(ObjPic_Worfinery,           "Worfinery");
    installTornieStructureTruecolorSlots(ObjPic_TechCenter,          "TechCenter");
    installTornieStructureTruecolorSlots(ObjPic_Scoutpost,           "Scoutpost");

    SDL_Color fogTransparent = { 0, 0, 0, 96};
    SDL_SetPaletteColors(objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0]->format->palette, &fogTransparent, PALCOLOR_BLACK, 1);

    loadCompactObjPicOverrides();

    // scale obj pics and apply color key
    for(int id = 0; id < NUM_OBJPICS; id++) {
        // Some built-in sprites and mod overrides are 32-bit RGBA with per-pixel alpha.
        // SDL_SetColorKey with PALCOLOR_TRANSPARENT (== 0) on them would
        // treat any pixel with raw value 0 as color-keyed, overriding the
        // alpha channel and producing black squares where transparent
        // pixels have non-zero RGB.  Skip color keying for these IDs.
        const bool isTruecolorSprite = (objPic[id][HOUSE_HARKONNEN][0] != nullptr
                                     && objPic[id][HOUSE_HARKONNEN][0]->format->BytesPerPixel >= 3)
                                     || (id == ObjPic_ZoneResidential
                                     || id == ObjPic_ZoneCommercial
                                     || id == ObjPic_ZoneIndustrial
                                     || id == ObjPic_CityRoad
                                     || id == ObjPic_NuclearPlant
                                     || id == ObjPic_PoliceStation
                                     || id == ObjPic_Stadium
                                     || id == ObjPic_Airport
                                     || id == ObjPic_Star);

        for(int h = 0; h < (int) NUM_HOUSES; h++) {
            if(objPic[id][h][0] != nullptr) {
                const bool isCurrentTruecolorSprite = isTruecolorSprite || objPic[id][h][0]->format->BytesPerPixel != 1;
                if(objPic[id][h][1] == nullptr) {
                    objPic[id][h][1] = generateDoubledObjPic(id, h);
                }
                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][1].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(objPic[id][h][2] == nullptr) {
                    objPic[id][h][2] = generateTripledObjPic(id, h);
                }
                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][2].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][0].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }
            }
        }
    }

    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][2].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][2].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][2].get());

    // load small detail pics
    smallDetailPicTex[Picture_Barracks] = extractSmallDetailPic("BARRAC.WSA");
    smallDetailPicTex[Picture_ConstructionYard] = extractSmallDetailPic("CONSTRUC.WSA");
    smallDetailPicTex[Picture_Carryall] = extractSmallDetailPic("CARRYALL.WSA");
    smallDetailPicTex[Picture_Devastator] = extractSmallDetailPic("HARKTANK.WSA");
    smallDetailPicTex[Picture_Deviator] = extractSmallDetailPic("ORDRTANK.WSA");
    smallDetailPicTex[Picture_DeathHand] = extractSmallDetailPic("GOLD-BB.WSA");
    smallDetailPicTex[Picture_Fremen] = extractSmallDetailPic("FREMEN.WSA");
    if(pFileManager->exists("FRIGATE.WSA")) {
        smallDetailPicTex[Picture_Frigate] = extractSmallDetailPic("FRIGATE.WSA");
    } else {
        // US-Version 1.07 does not contain FRIGATE.WSA
        // We replace it with the starport
        smallDetailPicTex[Picture_Frigate] = extractSmallDetailPic("STARPORT.WSA");
    }
    smallDetailPicTex[Picture_GunTurret] = extractSmallDetailPic("TURRET.WSA");
    smallDetailPicTex[Picture_Harvester] = extractSmallDetailPic("HARVEST.WSA");
    smallDetailPicTex[Picture_HeavyFactory] = extractSmallDetailPic("HVYFTRY.WSA");
    smallDetailPicTex[Picture_HighTechFactory] = extractSmallDetailPic("HITCFTRY.WSA");
    smallDetailPicTex[Picture_Soldier] = extractSmallDetailPic("INFANTRY.WSA");
    smallDetailPicTex[Picture_IX] = extractSmallDetailPic("IX.WSA");
    smallDetailPicTex[Picture_Launcher] = extractSmallDetailPic("RTANK.WSA");
    smallDetailPicTex[Picture_LightFactory] = extractSmallDetailPic("LITEFTRY.WSA");
    smallDetailPicTex[Picture_MCV] = extractSmallDetailPic("MCV.WSA");
    smallDetailPicTex[Picture_Ornithopter] = extractSmallDetailPic("ORNI.WSA");
    smallDetailPicTex[Picture_Palace] = extractSmallDetailPic("PALACE.WSA");
    smallDetailPicTex[Picture_Quad] = extractSmallDetailPic("QUAD.WSA");
    smallDetailPicTex[Picture_Radar] = extractSmallDetailPic("HEADQRTS.WSA");
    smallDetailPicTex[Picture_RaiderTrike] = extractSmallDetailPic("OTRIKE.WSA");
    smallDetailPicTex[Picture_Refinery] = extractSmallDetailPic("REFINERY.WSA");
    smallDetailPicTex[Picture_RepairYard] = extractSmallDetailPic("REPAIR.WSA");
    smallDetailPicTex[Picture_RocketTurret] = extractSmallDetailPic("RTURRET.WSA");
    smallDetailPicTex[Picture_Saboteur] = extractSmallDetailPic("SABOTURE.WSA");
    smallDetailPicTex[Picture_Sandworm] = extractSmallDetailPic("WORM.WSA");
    smallDetailPicTex[Picture_Sardaukar] = extractSmallDetailPic("SARDUKAR.WSA");
    smallDetailPicTex[Picture_SiegeTank] = extractSmallDetailPic("HTANK.WSA");
    smallDetailPicTex[Picture_Silo] = extractSmallDetailPic("STORAGE.WSA");
    smallDetailPicTex[Picture_Slab1] = extractSmallDetailPic("SLAB.WSA");
    smallDetailPicTex[Picture_Slab4] = extractSmallDetailPic("4SLAB.WSA");
    smallDetailPicTex[Picture_SonicTank] = extractSmallDetailPic("STANK.WSA");
    smallDetailPicTex[Picture_Special]  = nullptr;
    smallDetailPicTex[Picture_StarPort] = extractSmallDetailPic("STARPORT.WSA");
    smallDetailPicTex[Picture_Tank] = extractSmallDetailPic("LTANK.WSA");
    smallDetailPicTex[Picture_Trike] = extractSmallDetailPic("TRIKE.WSA");
    smallDetailPicTex[Picture_Trooper] = extractSmallDetailPic("HYINFY.WSA");
    smallDetailPicTex[Picture_Wall] = extractSmallDetailPic("WALL.WSA");
    smallDetailPicTex[Picture_WindTrap] = extractSmallDetailPic("WINDTRAP.WSA");
    smallDetailPicTex[Picture_WOR] = extractSmallDetailPic("WOR.WSA");

    // DuneCity zone build-menu icons — scale the imported zone sprite down
    // to 91x55 for the small detail pic.  Falls back to SLAB.WSA if the
    // zone surface was not loaded.
    {
        auto makeZoneDetailPic = [&](int objPicId) -> sdl2::texture_ptr {
            SDL_Surface* zoneSrc = objPic[objPicId][HOUSE_HARKONNEN][0].get();
            if (!zoneSrc) return extractSmallDetailPic("SLAB.WSA");

            // Atlas layout: columns = density, rows = value tier. Sample
            // the medium-density v0 cell (col 2, row 0) as the build-menu
            // representative — the v0/d0 corner is an empty-lot placeholder.
            const int cellSize = 2 * D2_TILESIZE;  // 32 px
            SDL_Rect srcRect = { 2 * cellSize, 0, cellSize, cellSize };

            // Create a transparent 91x55 canvas.  Reserve gutters so the
            // icon doesn't overlap the top-left lattice overlay (13x13 at
            // offset 2,2) or the bottom-left price text (~12px tall).
            sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!canvas) return extractSmallDetailPic("SLAB.WSA");
            SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

            const int leftGutter  = 16;  // clear the 13px lattice + padding
            const int bottomGutter = 14; // clear the ~12px price text
            const int topMargin   = 2;
            const int rightMargin = 4;
            const int usableW = 91 - leftGutter - rightMargin;  // 71
            const int usableH = 55 - topMargin - bottomGutter;  // 39
            float scale = std::min(static_cast<float>(usableW) / cellSize,
                                   static_cast<float>(usableH) / cellSize);
            // Cap at 1.5x to avoid over-enlarging small sprites.
            if (scale > 1.5f) scale = 1.5f;
            int destW = static_cast<int>(cellSize * scale);
            int destH = static_cast<int>(cellSize * scale);
            // Center within the usable area (right of lattice, above price).
            int destX = leftGutter + (usableW - destW) / 2;
            int destY = topMargin  + (usableH - destH) / 2;
            SDL_Rect destRect = { destX, destY, destW, destH };

            SDL_BlendMode prevMode;
            SDL_GetSurfaceBlendMode(zoneSrc, &prevMode);
            SDL_SetSurfaceBlendMode(zoneSrc, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(zoneSrc, &srcRect, canvas.get(), &destRect);
            SDL_SetSurfaceBlendMode(zoneSrc, prevMode);

            auto tex = convertSurfaceToTexture(canvas.get());
            if (tex) SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
            return tex ? std::move(tex) : extractSmallDetailPic("SLAB.WSA");
        };
        smallDetailPicTex[Picture_ZoneResidential] = makeZoneDetailPic(ObjPic_ZoneResidential);
        smallDetailPicTex[Picture_ZoneCommercial]  = makeZoneDetailPic(ObjPic_ZoneCommercial);
        smallDetailPicTex[Picture_ZoneIndustrial]  = makeZoneDetailPic(ObjPic_ZoneIndustrial);

        // Road build-menu icon: use the cross-intersection frame (mask=15)
        // of the Micropolis road atlas so the icon visually reads as a road,
        // not as a slab of concrete (the previous SLAB.WSA placeholder).
        SDL_Surface* roadAtlas = objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][0].get();
        if (roadAtlas) {
            const int crossFrame = 15;  // four-way intersection — most "road"-looking glyph
            sdl2::surface_ptr crossTile = getSubPicture(roadAtlas, crossFrame * D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
            if (crossTile) {
                sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (canvas) {
                    SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));
                    const int leftGutter = 16, bottomGutter = 14, topMargin = 2, rightMargin = 4;
                    const int usableW = 91 - leftGutter - rightMargin;
                    const int usableH = 55 - topMargin - bottomGutter;
                    // Scale up the 16x16 tile to make it readable at sidebar size.
                    float scale = std::min(static_cast<float>(usableW) / crossTile->w,
                                           static_cast<float>(usableH) / crossTile->h);
                    if (scale > 2.0f) scale = 2.0f;
                    int destW = static_cast<int>(crossTile->w * scale);
                    int destH = static_cast<int>(crossTile->h * scale);
                    int destX = leftGutter + (usableW - destW) / 2;
                    int destY = topMargin  + (usableH - destH) / 2;
                    SDL_Rect destRect = { destX, destY, destW, destH };
                    SDL_SetSurfaceBlendMode(crossTile.get(), SDL_BLENDMODE_NONE);
                    SDL_BlitScaled(crossTile.get(), nullptr, canvas.get(), &destRect);
                    auto tex = convertSurfaceToTexture(canvas.get());
                    if (tex) {
                        SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                        smallDetailPicTex[Picture_Road] = std::move(tex);
                    }
                }
            }
        }
    }
    if (!smallDetailPicTex[Picture_Road]) {
        smallDetailPicTex[Picture_Road]        = extractSmallDetailPic("SLAB.WSA");
    }
    smallDetailPicTex[Picture_PowerLine]       = extractSmallDetailPic("SLAB.WSA");

    // Nuclear plant, police, stadium, airport build-menu icons — pull
    // first frame from their Micropolis atlases so they're recognizable.
    {
        auto makeStructDetailPic = [&](int objPicID, int frameW, int frameH) -> sdl2::texture_ptr {
            SDL_Surface* src = objPic[objPicID][HOUSE_HARKONNEN][0].get();
            if (!src) return sdl2::texture_ptr{};
            sdl2::surface_ptr cell = getSubPicture(src, 0, 0, frameW, frameH);
            if (!cell) return sdl2::texture_ptr{};
            sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!canvas) return sdl2::texture_ptr{};
            SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));
            const int leftGutter = 16, bottomGutter = 14, topMargin = 2, rightMargin = 4;
            const int usableW = 91 - leftGutter - rightMargin;
            const int usableH = 55 - topMargin - bottomGutter;
            float scale = std::min(static_cast<float>(usableW) / frameW,
                                   static_cast<float>(usableH) / frameH);
            if (scale > 1.5f) scale = 1.5f;
            int destW = static_cast<int>(frameW * scale);
            int destH = static_cast<int>(frameH * scale);
            int destX = leftGutter + (usableW - destW) / 2;
            int destY = topMargin  + (usableH - destH) / 2;
            SDL_Rect destRect = { destX, destY, destW, destH };
            SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
            SDL_BlitScaled(cell.get(), nullptr, canvas.get(), &destRect);
            auto tex = convertSurfaceToTexture(canvas.get());
            if (tex) SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
            return tex;
        };
        auto nucTex = makeStructDetailPic(ObjPic_NuclearPlant, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (nucTex) smallDetailPicTex[Picture_NuclearPlant] = std::move(nucTex);
        else        smallDetailPicTex[Picture_NuclearPlant] = extractSmallDetailPic("HTEC.WSA");

        auto polTex = makeStructDetailPic(ObjPic_PoliceStation, 2 * D2_TILESIZE, 2 * D2_TILESIZE);
        if (polTex) smallDetailPicTex[Picture_PoliceStation] = std::move(polTex);
        else        smallDetailPicTex[Picture_PoliceStation] = extractSmallDetailPic("BARRAC.WSA");

        auto stadTex = makeStructDetailPic(ObjPic_Stadium, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (stadTex) smallDetailPicTex[Picture_Stadium] = std::move(stadTex);
        else         smallDetailPicTex[Picture_Stadium] = extractSmallDetailPic("PALACE.WSA");

        auto airTex = makeStructDetailPic(ObjPic_Airport, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (airTex) smallDetailPicTex[Picture_Airport] = std::move(airTex);
        else        smallDetailPicTex[Picture_Airport] = extractSmallDetailPic("STARPORT.WSA");
    }

    // DuneCity 1.0.506: Tornie unit portraits. The mod ships 91x55 PNG icons
    // (RocketTrikeIcon.png, FlameTankIcon.png, EliteLauncherIcon.png,
    // EliteSiegeTankIcon.png) as WSA replacements — simpler than authoring
    // 4 new WSA animations. Load via LoadPNG_RW; if missing, fall back to a
    // related vanilla portrait so the build/sidebar still has an icon.
    {
        constexpr int SmallDetailPicWidth = 91;
        constexpr int SmallDetailPicHeight = 55;

        auto loadIcon = [&](int pictureIndex, const std::string& pngName,
                            const char* fallbackWsa) {
            try {
                if(auto iconAsset = openTornieAsset(pngName.c_str(), "portrait")) {
                    auto raw = LoadPNG_RW(iconAsset.get());
                    if(raw) {
                        preserveOpaqueBlackIndex(raw.get());
                        normalizeTransparentPaletteIndexes(raw.get());
                        if(raw->w != SmallDetailPicWidth || raw->h != SmallDetailPicHeight) {
                            auto resized = resizeSurfaceNearest(raw.get(), SmallDetailPicWidth, SmallDetailPicHeight);
                            if(resized) {
                                SDL_Log("GFXManager: resized portrait %s from %dx%d to %dx%d",
                                        pngName.c_str(), raw->w, raw->h, SmallDetailPicWidth, SmallDetailPicHeight);
                                raw = std::move(resized);
                            }
                        }
                        sdl2::texture_ptr tex{ SDL_CreateTextureFromSurface(renderer, raw.get()) };
                        if(tex) {
                            smallDetailPicTex[pictureIndex] = std::move(tex);
                            return;
                        }
                    }
                }
                smallDetailPicTex[pictureIndex] = extractSmallDetailPic(fallbackWsa);
            } catch(const std::exception& e) {
                SDL_Log("GFXManager: portrait %s load failed (%s) — falling back to %s",
                        pngName.c_str(), e.what(), fallbackWsa);
                smallDetailPicTex[pictureIndex] = extractSmallDetailPic(fallbackWsa);
            }
        };
        loadIcon(Picture_RocketTrike,    "RocketTrikeIcon.png",    "TRIKE.WSA");
        loadIcon(Picture_SonicTrike,     "SonicTrikeIcon.png",     "TRIKE.WSA");
        loadIcon(Picture_FlameTank,      "FlameTankIcon.png",      "HTANK.WSA");
        loadIcon(Picture_EliteLauncher,  "EliteLauncherIcon.png",  "HTANK.WSA");
        loadIcon(Picture_EliteSiegeTank, "EliteSiegeTankIcon.png", "HTANK.WSA");
        loadIcon(Picture_AdvancedWindTrap, "Tornie_AdvancedWindtrap_icon.png", "WINDTRAP.WSA");
        loadIcon(Picture_Worfinery,      "WorfineryIcon.png",      "WOR.WSA");
        loadIcon(Picture_TechCenter,     "TechCenterIcon.png",     "PALACE.WSA");
        loadIcon(Picture_Scoutpost,      "ScoutpostIcon.png",      "RTURRET.WSA");
        loadIcon(Picture_PalaceLightVehicles, "PalaceTrikeAndQuadIcon.png", "FREMEN.WSA");
        loadIcon(Picture_Harvestank,     "HarvestankIcon.png",     "HARVEST.WSA");
    }

    // unused: FARTR.WSA, FHARK.WSA, FORDOS.WSA


    // Helper function to safely create tiny picture textures
    auto createTinyPictureTexture = [&](int pictureIndex, const char* name) {
        sdl2::texture_ptr texture = convertSurfaceToTexture(shapes->getPicture(pictureIndex));
        if(texture == nullptr) {
            SDL_Log("Warning: Failed to create tiny picture texture for %s (index %d)", name, pictureIndex);
        }
        return texture;
    };

    tinyPictureTex[TinyPicture_Spice] = createTinyPictureTexture(94, "Spice");
    tinyPictureTex[TinyPicture_Barracks] = createTinyPictureTexture(62, "Barracks");
    tinyPictureTex[TinyPicture_ConstructionYard] = createTinyPictureTexture(60, "ConstructionYard");
    tinyPictureTex[TinyPicture_GunTurret] = createTinyPictureTexture(67, "GunTurret");
    tinyPictureTex[TinyPicture_HeavyFactory] = createTinyPictureTexture(56, "HeavyFactory");
    tinyPictureTex[TinyPicture_HighTechFactory] = createTinyPictureTexture(57, "HighTechFactory");
    tinyPictureTex[TinyPicture_IX] = createTinyPictureTexture(58, "IX");
    tinyPictureTex[TinyPicture_LightFactory] = createTinyPictureTexture(55, "LightFactory");
    tinyPictureTex[TinyPicture_Palace] = createTinyPictureTexture(54, "Palace");
    tinyPictureTex[TinyPicture_Radar] = createTinyPictureTexture(70, "Radar");
    tinyPictureTex[TinyPicture_Refinery] = createTinyPictureTexture(64, "Refinery");
    tinyPictureTex[TinyPicture_RepairYard] = createTinyPictureTexture(65, "RepairYard");
    tinyPictureTex[TinyPicture_RocketTurret] = createTinyPictureTexture(68, "RocketTurret");
    tinyPictureTex[TinyPicture_Silo] = createTinyPictureTexture(69, "Silo");
    tinyPictureTex[TinyPicture_Slab1] = createTinyPictureTexture(53, "Slab1");
    tinyPictureTex[TinyPicture_Slab4] = createTinyPictureTexture(71, "Slab4");
    tinyPictureTex[TinyPicture_StarPort] = createTinyPictureTexture(63, "StarPort");
    tinyPictureTex[TinyPicture_Wall] = createTinyPictureTexture(66, "Wall");
    tinyPictureTex[TinyPicture_WindTrap] = createTinyPictureTexture(61, "WindTrap");
    tinyPictureTex[TinyPicture_WOR] = createTinyPictureTexture(59, "WOR");
    tinyPictureTex[TinyPicture_Carryall] = createTinyPictureTexture(77, "Carryall");
    tinyPictureTex[TinyPicture_Devastator] = createTinyPictureTexture(75, "Devastator");
    tinyPictureTex[TinyPicture_Deviator] = createTinyPictureTexture(86, "Deviator");
    tinyPictureTex[TinyPicture_Frigate] = createTinyPictureTexture(77, "Frigate");    // use carryall picture
    tinyPictureTex[TinyPicture_Harvester] = createTinyPictureTexture(88, "Harvester");
    tinyPictureTex[TinyPicture_Soldier] = createTinyPictureTexture(90, "Soldier");
    tinyPictureTex[TinyPicture_Launcher] = createTinyPictureTexture(73, "Launcher");
    tinyPictureTex[TinyPicture_MCV] = createTinyPictureTexture(89, "MCV");
    tinyPictureTex[TinyPicture_Ornithopter] = createTinyPictureTexture(85, "Ornithopter");
    tinyPictureTex[TinyPicture_Quad] = createTinyPictureTexture(74, "Quad");
    tinyPictureTex[TinyPicture_Saboteur] = createTinyPictureTexture(84, "Saboteur");
    tinyPictureTex[TinyPicture_Sandworm] = createTinyPictureTexture(93, "Sandworm");
    tinyPictureTex[TinyPicture_SiegeTank] = createTinyPictureTexture(72, "SiegeTank");
    tinyPictureTex[TinyPicture_SonicTank] = createTinyPictureTexture(79, "SonicTank");
    tinyPictureTex[TinyPicture_Tank] = createTinyPictureTexture(78, "Tank");
    tinyPictureTex[TinyPicture_Trike] = createTinyPictureTexture(80, "Trike");
    tinyPictureTex[TinyPicture_RaiderTrike] = createTinyPictureTexture(87, "RaiderTrike");
    tinyPictureTex[TinyPicture_Trooper] = createTinyPictureTexture(76, "Trooper");
    tinyPictureTex[TinyPicture_Special] = createTinyPictureTexture(75, "Special");    // use devastator picture
    tinyPictureTex[TinyPicture_Infantry] = createTinyPictureTexture(81, "Infantry");
    tinyPictureTex[TinyPicture_Troopers] = createTinyPictureTexture(91, "Troopers");

    // load UI graphics
    uiGraphic[UI_RadarAnimation][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(radar->getAnimationAsPictureRow(NUM_STATIC_ANIMATIONS_PER_ROW).get());

    uiGraphic[UI_CursorNormal][HOUSE_HARKONNEN] = mouse->getPicture(0);
    SDL_SetColorKey(uiGraphic[UI_CursorNormal][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorUp][HOUSE_HARKONNEN] = mouse->getPicture(1);
    SDL_SetColorKey(uiGraphic[UI_CursorUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorRight][HOUSE_HARKONNEN] = mouse->getPicture(2);
    SDL_SetColorKey(uiGraphic[UI_CursorRight][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorDown][HOUSE_HARKONNEN] = mouse->getPicture(3);
    SDL_SetColorKey(uiGraphic[UI_CursorDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorLeft][HOUSE_HARKONNEN] = mouse->getPicture(4);
    SDL_SetColorKey(uiGraphic[UI_CursorLeft][HOUSE_HARKONNEN].get() , SDL_TRUE, 0);

    uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN] = mouse->getPicture(5);
    SDL_SetColorKey(uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorAttack_Zoomlevel0][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN].get(), 232, PALCOLOR_HARKONNEN);
    SDL_SetColorKey(uiGraphic[UI_CursorAttack_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorCapture_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Capture.png").get());
    SDL_SetColorKey(uiGraphic[UI_CursorCapture_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorCarryallDrop_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CarryallDrop.png").get());
    SDL_SetColorKey(uiGraphic[UI_CursorCarryallDrop_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_ReturnIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Return.png").get());
    SDL_SetColorKey(uiGraphic[UI_ReturnIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_DeployIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Deploy.png").get());
    SDL_SetColorKey(uiGraphic[UI_DeployIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_DestructIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Destruct.png").get());
    SDL_SetColorKey(uiGraphic[UI_DestructIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_SendToRepairIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("SendToRepair.png").get());
    SDL_SetColorKey(uiGraphic[UI_SendToRepairIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CreditsDigits][HOUSE_HARKONNEN] = shapes->getPictureArray(10,1,2|TILE_NORMAL,3|TILE_NORMAL,4|TILE_NORMAL,5|TILE_NORMAL,6|TILE_NORMAL,
                                                                                7|TILE_NORMAL,8|TILE_NORMAL,9|TILE_NORMAL,10|TILE_NORMAL,11|TILE_NORMAL);
    uiGraphic[UI_SideBar][HOUSE_HARKONNEN] = PicFactory->createSideBar(false);
    uiGraphic[UI_Indicator][HOUSE_HARKONNEN] = units1->getPictureArray(3,1,8|TILE_NORMAL,9|TILE_NORMAL,10|TILE_NORMAL);
    SDL_SetColorKey(uiGraphic[UI_Indicator][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    SDL_Color indicatorTransparent = { 255, 255, 255, 48 };
    SDL_SetPaletteColors(uiGraphic[UI_Indicator][HOUSE_HARKONNEN]->format->palette, &indicatorTransparent, PALCOLOR_WHITE, 1);
    uiGraphic[UI_InvalidPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTRED);
    uiGraphic[UI_InvalidPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTRED);
    uiGraphic[UI_InvalidPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTRED);
    uiGraphic[UI_ValidPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_ValidPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_ValidPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_GreyPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTGREY);
    uiGraphic[UI_GreyPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTGREY);
    uiGraphic[UI_GreyPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTGREY);
    uiGraphic[UI_MenuBackground][HOUSE_HARKONNEN] = PicFactory->createMainBackground();
    uiGraphic[UI_GameStatsBackground][HOUSE_HARKONNEN] = PicFactory->createGameStatsBackground(HOUSE_HARKONNEN);
    uiGraphic[UI_GameStatsBackground][HOUSE_ATREIDES] = PicFactory->createGameStatsBackground(HOUSE_ATREIDES);
    uiGraphic[UI_GameStatsBackground][HOUSE_ORDOS] = PicFactory->createGameStatsBackground(HOUSE_ORDOS);
    uiGraphic[UI_GameStatsBackground][HOUSE_FREMEN] = PicFactory->createGameStatsBackground(HOUSE_FREMEN);
    uiGraphic[UI_GameStatsBackground][HOUSE_SARDAUKAR] = PicFactory->createGameStatsBackground(HOUSE_SARDAUKAR);
    uiGraphic[UI_GameStatsBackground][HOUSE_MERCENARY] = PicFactory->createGameStatsBackground(HOUSE_MERCENARY);
    uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("UI_SelectionBox.png").get());
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_SelectionBox_Zoomlevel1][HOUSE_HARKONNEN] = Scaler::defaultDoubleTiledSurface(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_SelectionBox_Zoomlevel2][HOUSE_HARKONNEN] = Scaler::defaultTripleTiledSurface(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel2][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("UI_OtherPlayerSelectionBox.png").get());
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel1][HOUSE_HARKONNEN] = Scaler::defaultDoubleTiledSurface(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel2][HOUSE_HARKONNEN] = Scaler::defaultTripleTiledSurface(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel2][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_TopBar][HOUSE_HARKONNEN] = PicFactory->createTopBar();
    uiGraphic[UI_ButtonUp][HOUSE_HARKONNEN] = choam->getPicture(0);
    uiGraphic[UI_ButtonUp_Pressed][HOUSE_HARKONNEN] = choam->getPicture(1);
    uiGraphic[UI_ButtonDown][HOUSE_HARKONNEN] = choam->getPicture(2);
    uiGraphic[UI_ButtonDown_Pressed][HOUSE_HARKONNEN] = choam->getPicture(3);
    uiGraphic[UI_BuilderListUpperCap][HOUSE_HARKONNEN] = PicFactory->createBuilderListUpperCap();
    uiGraphic[UI_BuilderListLowerCap][HOUSE_HARKONNEN] = PicFactory->createBuilderListLowerCap();
    uiGraphic[UI_CustomGamePlayersArrow][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CustomGamePlayers_Arrow.png").get());
    SDL_SetColorKey(uiGraphic[UI_CustomGamePlayersArrow][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CustomGamePlayersArrowNeutral][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CustomGamePlayers_ArrowNeutral.png").get());
    SDL_SetColorKey(uiGraphic[UI_CustomGamePlayersArrowNeutral][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MessageBox][HOUSE_HARKONNEN] = PicFactory->createMessageBoxBorder();

    if(bttn != nullptr) {
        uiGraphic[UI_Mentat][HOUSE_HARKONNEN] = bttn->getPicture(0);
        uiGraphic[UI_Mentat_Pressed][HOUSE_HARKONNEN] = bttn->getPicture(1);
        uiGraphic[UI_Options][HOUSE_HARKONNEN] = bttn->getPicture(2);
        uiGraphic[UI_Options_Pressed][HOUSE_HARKONNEN] = bttn->getPicture(3);
    } else {
        uiGraphic[UI_Mentat][HOUSE_HARKONNEN] = shapes->getPicture(94);
        uiGraphic[UI_Mentat_Pressed][HOUSE_HARKONNEN] = shapes->getPicture(95);
        uiGraphic[UI_Options][HOUSE_HARKONNEN] = shapes->getPicture(96);
        uiGraphic[UI_Options_Pressed][HOUSE_HARKONNEN] = shapes->getPicture(97);
    }

    uiGraphic[UI_Upgrade][HOUSE_HARKONNEN] = choam->getPicture(4);
    SDL_SetColorKey(uiGraphic[UI_Upgrade][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_Upgrade_Pressed][HOUSE_HARKONNEN] = choam->getPicture(5);
    SDL_SetColorKey(uiGraphic[UI_Upgrade_Pressed][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_Repair][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Repair.png").get());
    uiGraphic[UI_Repair_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_RepairPushed.png").get());
    uiGraphic[UI_Minus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Minus.png").get());
    uiGraphic[UI_Minus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_Minus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-2);
    uiGraphic[UI_Minus_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_MinusPushed.png").get());
    uiGraphic[UI_Plus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Plus.png").get());
    uiGraphic[UI_Plus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_Plus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-2);
    uiGraphic[UI_Plus_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_PlusPushed.png").get());
    uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Menu_MissionSelect.png").get());
    PicFactory->drawFrame(uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame,nullptr);
    SDL_SetColorKey(uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OptionsMenu][HOUSE_HARKONNEN] = PicFactory->createOptionsMenu();
    uiGraphic[UI_LoadSaveWindow][HOUSE_HARKONNEN] = PicFactory->createMenu(280,228);
    uiGraphic[UI_NewMapWindow][HOUSE_HARKONNEN] = PicFactory->createMenu(600,440);
    uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("DuneLegacy.png").get());
    {
        // Replace the baked-in "Dune Legacy" title with "Dune City": fill the
        // central text region with the banner's dark interior tone and draw
        // our own title centered. Decorative wood frame at the edges remains
        // visible. The same surface is then reused as UI_GameMenu's header.
        SDL_Surface* pBanner = uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN].get();
        const int bw = pBanner->w;
        const int bh = pBanner->h;
        SDL_Rect inner = { bw / 16, bh / 8, bw - (bw / 16) * 2, bh - (bh / 8) * 2 };
        SDL_FillRect(pBanner, &inner, SDL_MapRGB(pBanner->format, 18, 22, 60));

        const int titleFontSize = std::max(16, std::min(34, bh - 16));
        sdl2::surface_ptr titleText{
            pFontManager->createSurfaceWithText("Dune City", COLOR_LIGHTYELLOW, titleFontSize) };
        SDL_Rect titleDest = calcDrawingRect(titleText.get(), bw / 2, bh / 2,
                                             HAlign::Center, VAlign::Center);
        SDL_BlitSurface(titleText.get(), nullptr, pBanner, &titleDest);
    }
    uiGraphic[UI_GameMenu][HOUSE_HARKONNEN] = PicFactory->createMenu(uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN].get(),158);
    PicFactory->drawFrame(uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame);

    uiGraphic[UI_PlanetBackground][HOUSE_HARKONNEN] = LoadCPS_RW(pFileManager->openFile("BIGPLAN.CPS").get());
    PicFactory->drawFrame(uiGraphic[UI_PlanetBackground][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame);
    uiGraphic[UI_MenuButtonBorder][HOUSE_HARKONNEN] = PicFactory->createFrame(PictureFactory::DecorationFrame1,190,140,false);

    PicFactory->drawFrame(uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame);

    const bool tornieActive = ModManager::instance().isInitialized()
        && (ModManager::instance().getActiveModName() == "Tornie");
    auto loadMentatBackgroundPng = [&](const char* filename) -> sdl2::surface_ptr {
        if(!pFileManager->exists(filename)) {
            return nullptr;
        }

        auto png = LoadPNG_RW(pFileManager->openFile(filename).get());
        if(!png) {
            return nullptr;
        }

        if(png->w <= SCREEN_MIN_WIDTH/2 && png->h <= SCREEN_MIN_HEIGHT/2) {
            return Scaler::defaultDoubleSurface(png.get());
        }

        return png;
    };

    uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATH.CPS").get()).get());
    auto vanillaAtreidesMentat = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATA.CPS").get()).get());
    uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] = tornieActive ? loadMentatBackgroundPng("PaulAtreidesMentat.png") : nullptr;
    if(uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] == nullptr) {
        uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] = copySurface(vanillaAtreidesMentat.get());
    }
    if(uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] == nullptr) {
        uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] = copySurface(vanillaAtreidesMentat.get());
    }
    uiGraphic[UI_MentatBackground][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATO.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_FREMEN] = PictureFactory::mapMentatSurfaceToFremen(vanillaAtreidesMentat.get());
    uiGraphic[UI_MentatBackground][HOUSE_SARDAUKAR] = PictureFactory::mapMentatSurfaceToSardaukar(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get());
    uiGraphic[UI_MentatBackground][HOUSE_MERCENARY] = PictureFactory::mapMentatSurfaceToMercenary(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());
    if(auto chaniMentat = loadMentatBackgroundPng("ChaniMentat.png")) {
        uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = std::move(chaniMentat);
        uiGraphic[UI_MentatBackground][HOUSE_REBELS] = loadMentatBackgroundPng("ChaniMentat.png");
    } else {
        uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[HOUSE_NEUTRAL]);
        uiGraphic[UI_MentatBackground][HOUSE_REBELS] = mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_ATREIDES].get(), PALCOLOR_ATREIDES, houseToPaletteIndex[HOUSE_REBELS]);
    }

    uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATM.CPS").get()).get());
    if(uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN] != nullptr) {
        benePalette.applyToSurface(uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN].get());
    }

    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_HARKONNEN] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_HARKONNEN, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_ATREIDES] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_ATREIDES, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_ORDOS] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_ORDOS, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_SARDAUKAR] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_SARDAUKAR, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_FREMEN] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_FREMEN, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_MERCENARY] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_MERCENARY, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_NEUTRAL] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_NEUTRAL, benePalette);
    uiGraphic[UI_MentatHouseChoiceInfoQuestion][HOUSE_REBELS] = PicFactory->createMentatHouseChoiceQuestion(HOUSE_REBELS, benePalette);

    uiGraphic[UI_MentatYes][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(0).get());
    uiGraphic[UI_MentatYes_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(1).get());
    uiGraphic[UI_MentatNo][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(2).get());
    uiGraphic[UI_MentatNo_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(3).get());
    uiGraphic[UI_MentatExit][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(4).get());
    uiGraphic[UI_MentatExit_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(5).get());
    uiGraphic[UI_MentatProcced][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(6).get());
    uiGraphic[UI_MentatProcced_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(7).get());
    uiGraphic[UI_MentatRepeat][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(8).get());
    uiGraphic[UI_MentatRepeat_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(9).get());

    { // Scope
        sdl2::surface_ptr pHouseChoiceBackground;
        if (pFileManager->exists("HERALD." + _("LanguageFileExtension"))) {
            pHouseChoiceBackground = LoadCPS_RW(pFileManager->openFile("HERALD." + _("LanguageFileExtension")).get());
        }
        else {
            pHouseChoiceBackground = LoadCPS_RW(pFileManager->openFile("HERALD.CPS").get());
        }

        uiGraphic[UI_HouseSelect][HOUSE_HARKONNEN] = PicFactory->createHouseSelect(pHouseChoiceBackground.get());
        uiGraphic[UI_SelectYourHouseLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(getSubPicture(pHouseChoiceBackground.get(), 0, 0, 320, 50).get());
        uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES] = getSubPicture(pHouseChoiceBackground.get(), 20, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_ATREIDES] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
        uiGraphic[UI_Herald_Colored][HOUSE_ORDOS] = getSubPicture(pHouseChoiceBackground.get(), 117, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
        uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN] = getSubPicture(pHouseChoiceBackground.get(), 215, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        uiGraphic[UI_Herald_Colored][HOUSE_FREMEN] = PicFactory->createHeraldFre(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_FREMEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_FREMEN].get());
        uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR] = PicFactory->createHeraldSard(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get(), uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_SARDAUKAR] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR].get());
        uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY] = PicFactory->createHeraldMerc(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get(), uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_MERCENARY] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY].get());

        auto loadBonusHerald = [&](int house, const char* filename, SDL_Surface* fallback) {
            if(pFileManager->exists(filename)) {
                auto herald = LoadPNG_RW(pFileManager->openFile(filename).get());
                if(herald) {
                    if(house != HOUSE_REBELS) {
                        SDL_SetColorKey(herald.get(), SDL_TRUE, 0);
                    }
                    uiGraphic[UI_Herald_Colored][house] = std::move(herald);
                }
            }

            if(uiGraphic[UI_Herald_Colored][house] == nullptr) {
                if(house == HOUSE_REBELS) {
                    uiGraphic[UI_Herald_Colored][house] = copySurface(fallback);
                } else {
                    uiGraphic[UI_Herald_Colored][house] =
                        mapSurfaceColorRange(fallback, PALCOLOR_HARKONNEN, houseToPaletteIndex[house]);
                }
            }

            uiGraphic[UI_Herald_ColoredLarge][house] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][house].get());
        };

        loadBonusHerald(HOUSE_NEUTRAL, "HeraldNeu.png", uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        loadBonusHerald(HOUSE_REBELS, "HeraldRebels.png", uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
    }

    uiGraphic[UI_Herald_Grey][HOUSE_HARKONNEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ATREIDES] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ORDOS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
    uiGraphic[UI_Herald_Grey][HOUSE_FREMEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_FREMEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_SARDAUKAR] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR].get());
    uiGraphic[UI_Herald_Grey][HOUSE_MERCENARY] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY].get());
    uiGraphic[UI_Herald_Grey][HOUSE_NEUTRAL] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL].get());
    uiGraphic[UI_Herald_Grey][HOUSE_REBELS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_REBELS].get());

    uiGraphic[UI_Herald_ArrowLeft][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowLeft.png").get());
    uiGraphic[UI_Herald_ArrowLeftLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowLeft][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowLeftHighlight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowLeftHighlight.png").get());
    uiGraphic[UI_Herald_ArrowLeftHighlightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowLeftHighlight][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowRight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowRight.png").get());
    uiGraphic[UI_Herald_ArrowRightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowRight][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowRightHighlight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowRightHighlight.png").get());
    uiGraphic[UI_Herald_ArrowRightHighlightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowRightHighlight][HOUSE_HARKONNEN].get());

    uiGraphic[UI_MapChoiceScreen][HOUSE_HARKONNEN] = PicFactory->createMapChoiceScreen(HOUSE_HARKONNEN);
    uiGraphic[UI_MapChoiceScreen][HOUSE_ATREIDES] = PicFactory->createMapChoiceScreen(HOUSE_ATREIDES);
    uiGraphic[UI_MapChoiceScreen][HOUSE_ORDOS] = PicFactory->createMapChoiceScreen(HOUSE_ORDOS);
    uiGraphic[UI_MapChoiceScreen][HOUSE_FREMEN] = PicFactory->createMapChoiceScreen(HOUSE_FREMEN);
    uiGraphic[UI_MapChoiceScreen][HOUSE_SARDAUKAR] = PicFactory->createMapChoiceScreen(HOUSE_SARDAUKAR);
    uiGraphic[UI_MapChoiceScreen][HOUSE_MERCENARY] = PicFactory->createMapChoiceScreen(HOUSE_MERCENARY);
    uiGraphic[UI_MapChoiceScreen][HOUSE_NEUTRAL] = PicFactory->createMapChoiceScreen(HOUSE_NEUTRAL);
    uiGraphic[UI_MapChoiceScreen][HOUSE_REBELS] = PicFactory->createMapChoiceScreen(HOUSE_REBELS);
    uiGraphic[UI_MapChoicePlanet][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("PLANET.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoicePlanet][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceMapOnly][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("DUNEMAP.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceMapOnly][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("DUNERGN.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    // make black lines inside the map non-transparent
    {
        const auto surface = uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN].get();

        sdl2::surface_lock lock{ surface };

        for(auto y = 48; y < 48+240; y++) {
            for(auto x = 16; x < 16 + 608; x++) {
                if(getPixel(surface, x, y) == 0) {
                    putPixel(surface, x, y, PALCOLOR_BLACK);
                }
            }
        }
    }

    uiGraphic[UI_MapChoiceClickMap][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("RGNCLK.CPS").get()).get());
    uiGraphic[UI_MapChoiceArrow_None][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(0).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_None][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_LeftUp][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(1).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_LeftUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Up][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(2).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Up][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_RightUp][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(3).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_RightUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Right][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(4).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Right][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_RightDown][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(5).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_RightDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Down][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(6).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Down][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_LeftDown][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(7).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_LeftDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Left][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(8).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Left][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_StructureSizeLattice][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("StructureSizeLattice.png").get());
    SDL_SetColorKey(uiGraphic[UI_StructureSizeLattice][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_StructureSizeConcrete][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("StructureSizeConcrete.png").get());
    SDL_SetColorKey(uiGraphic[UI_StructureSizeConcrete][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_MapEditor_SideBar][HOUSE_HARKONNEN] = PicFactory->createSideBar(true);
    uiGraphic[UI_MapEditor_BottomBar][HOUSE_HARKONNEN] = PicFactory->createBottomBar();

    uiGraphic[UI_MapEditor_ExitIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorExitIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ExitIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_NewIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorNewIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_NewIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_LoadIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorLoadIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_LoadIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_SaveIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorSaveIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_SaveIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_UndoIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorUndoIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_UndoIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RedoIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRedoIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RedoIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_PlayerIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPlayerIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_PlayerIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MapSettingsIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMapSettingsIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MapSettingsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ChoamIcon][HOUSE_HARKONNEN] = scaleSurface(getSubFrame(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0].get(),1,0,8,1).get(), 0.5);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ChoamIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ReinforcementsIcon][HOUSE_HARKONNEN] = scaleSurface(getSubFrame(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get(),1,0,8,2).get(), 0.66667);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ReinforcementsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_TeamsIcon][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_TeamsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorNoneIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorNone.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorNoneIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorHorizontalIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorHorizontal.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorHorizontalIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorVerticalIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorVertical.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorVerticalIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorBothIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorBoth.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorBothIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorPointIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorPoint.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorPointIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorArrowUp.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowUp_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorArrowDown.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowDown_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPlus.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Plus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMinus.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Minus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRotateLeft.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateLeftHighlightIcon][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateLeftHighlightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRotateRight.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateRightHighlightIcon][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateRightHighlightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(127).get());
    uiGraphic[UI_MapEditor_Dunes][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(159).get());
    uiGraphic[UI_MapEditor_SpecialBloom][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(209).get());
    uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(191).get());
    uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(207).get());
    uiGraphic[UI_MapEditor_GreenSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 24, 112, 48, 255 });
    uiGraphic[UI_MapEditor_ThickGreenSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 20, 84, 42, 255 });
    uiGraphic[UI_MapEditor_RedSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 136, 48, 40, 255 });
    uiGraphic[UI_MapEditor_ThickRedSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 96, 32, 30, 255 });
    uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(208).get());
    uiGraphic[UI_MapEditor_GreenSpiceBloom][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 24, 112, 48, 255 });
    uiGraphic[UI_MapEditor_RedSpiceBloom][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 136, 48, 40, 255 });
    uiGraphic[UI_MapEditor_Slab][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(126).get());
    uiGraphic[UI_MapEditor_Rock][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(143).get());
    uiGraphic[UI_MapEditor_Mountain][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(175).get());

    uiGraphic[UI_MapEditor_Slab1][HOUSE_HARKONNEN] = icon->getPicture(126);
    uiGraphic[UI_MapEditor_Wall][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Wall][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_GunTurret][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_GunTurret][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_RocketTurret][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_ConstructionYard][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Windtrap][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    SDL_Color windtrapColor = { 70, 70, 70, 255};
    SDL_SetPaletteColors(uiGraphic[UI_MapEditor_Windtrap][HOUSE_HARKONNEN]->format->palette, &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
    auto loadMapEditorStructurePreview = [&](const char* pngName, const char* label) -> sdl2::surface_ptr {
        auto rwop = openTornieAsset(pngName, label);
        if(!rwop) {
            return nullptr;
        }

        auto raw = LoadPNG_RW(rwop.get());
        if(!raw || raw->format->BitsPerPixel != 8 || !raw->format->palette) {
            SDL_Log("GFXManager: %s editor sprite '%s' is not 8-bit indexed, using object sprite", label, pngName);
            return nullptr;
        }

        preserveOpaqueBlackIndex(raw.get());
        normalizeTransparentPaletteIndexes(raw.get());
        if(ibmPaletteLoaded) {
            if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                raw = std::move(remapped);
            } else {
                ibmPalette.applyToSurface(raw.get());
            }
            normalizeTransparentPaletteIndexes(raw.get());
        }
        normalizeHouseColorRangesToHarkonnen(raw.get());
        normalizeHarkonnenTeamRed(raw.get());
        SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
        return raw;
    };

    if(auto advancedWindtrapEditor = loadMapEditorStructurePreview("Tornie_AdvancedWindtrap_gfx_editor.png",
                                                                   "Advanced Windtrap editor")) {
        uiGraphic[UI_MapEditor_AdvancedWindTrap][HOUSE_HARKONNEN] = std::move(advancedWindtrapEditor);
    } else {
        uiGraphic[UI_MapEditor_AdvancedWindTrap][HOUSE_HARKONNEN] =
            getSubPicture(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(),
                          2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 3*D2_TILESIZE);
    }
    // Tornie: Adv Windtrap MK2 variant — same sprite as vanilla Adv Windtrap
    uiGraphic[UI_MapEditor_AdvancedWindTrapMK2][HOUSE_HARKONNEN] =
        getSubPicture(objPic[ObjPic_AdvancedWindTrap2x3][HOUSE_HARKONNEN][0].get(),
                      2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE);
    uiGraphic[UI_MapEditor_AdvancedWindTrapMK3][HOUSE_HARKONNEN] =
        getSubPicture(objPic[ObjPic_AdvancedWindTrap3x2][HOUSE_HARKONNEN][0].get(),
                      2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    auto applyMapEditorWindtrapColor = [&](unsigned int uiId) {
        if(uiGraphic[uiId][HOUSE_HARKONNEN] && uiGraphic[uiId][HOUSE_HARKONNEN]->format->palette) {
            SDL_SetPaletteColors(uiGraphic[uiId][HOUSE_HARKONNEN]->format->palette,
                                 &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
        }
    };
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrap);
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrapMK2);
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrapMK3);
    uiGraphic[UI_MapEditor_Radar][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Radar][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Silo][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Silo][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_IX][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_IX][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Barracks][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Barracks][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_WOR][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_WOR][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    // Tornie: Worfinery = 48x64 PNG, take top 2-tile-tall frame (first of 2 vertical frames).
    if(objPic[ObjPic_Worfinery][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_Worfinery][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_Worfinery][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    } else {
        // Fallback to vanilla WOR sprite if Tornie Worfinery.png missing
        uiGraphic[UI_MapEditor_Worfinery][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_WOR][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    }
    uiGraphic[UI_MapEditor_LightFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_LightFactory][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Refinery][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Refinery][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HighTechFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HeavyFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HeavyFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_RepairYard][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_RepairYard][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Starport][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Starport][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Palace][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    // Tornie: Tech Center = 48x64 PNG, take top 2-tile-tall frame (first of 2 vertical frames).
    if(objPic[ObjPic_TechCenter][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_TechCenter][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_TechCenter][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    } else {
        // Fallback to vanilla Palace sprite if Tornie TechCenter.png missing
        uiGraphic[UI_MapEditor_TechCenter][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    }
    if(objPic[ObjPic_Scoutpost][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_Scoutpost][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_Scoutpost][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
    } else {
        uiGraphic[UI_MapEditor_Scoutpost][HOUSE_HARKONNEN] =
            getSubPicture(objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
    }

    // Custom structures are prebuilt for every visual colour slot. Install
    // their matching editor previews now so the lazy truecolour UI fallback
    // cannot copy the Harkonnen preview unchanged for another player colour.
    struct TornieEditorStructurePreview {
        unsigned int uiID;
        unsigned int objPicID;
        int x;
        int y;
        int width;
        int height;
    };
    const TornieEditorStructurePreview tornieEditorStructures[] = {
        { UI_MapEditor_AdvancedWindTrap,    ObjPic_AdvancedWindTrap,    2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK2, ObjPic_AdvancedWindTrap2x3, 2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK3, ObjPic_AdvancedWindTrap3x2, 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Worfinery,           ObjPic_Worfinery,           2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_TechCenter,          ObjPic_TechCenter,          2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Scoutpost,           ObjPic_Scoutpost,           2*D2_TILESIZE,   0, D2_TILESIZE,   D2_TILESIZE   }
    };
    for(const auto& preview : tornieEditorStructures) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            // Keep the dedicated Harkonnen editor image when one was loaded.
            if(colorSlot == HOUSE_HARKONNEN && uiGraphic[preview.uiID][colorSlot]) {
                continue;
            }
            SDL_Surface* atlas = objPic[preview.objPicID][colorSlot][0].get();
            if(atlas) {
                uiGraphic[preview.uiID][colorSlot] = getSubPicture(
                    atlas, preview.x, preview.y, preview.width, preview.height);
            }
        }
    }

    sdl2::surface_ptr customMapEditorStar = createCustomMapEditorStar(objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get());
    auto addMapEditorStar = [&](unsigned int uiGraphicID, bool customStar = false) {
        SDL_Surface* starSurface = (customStar && customMapEditorStar) ? customMapEditorStar.get()
                                                                       : objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get();
        if(uiGraphic[uiGraphicID][HOUSE_HARKONNEN] && starSurface) {
            uiGraphic[uiGraphicID][HOUSE_HARKONNEN] = combinePictures(
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN].get(),
                starSurface,
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN]->w - starSurface->w,
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN]->h - starSurface->h);
        }
    };

    uiGraphic[UI_MapEditor_Soldier][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Soldier][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Trooper][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trooper][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Harvester][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_RebelHarvester][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    addMapEditorStar(UI_MapEditor_RebelHarvester, true);
    uiGraphic[UI_MapEditor_Infantry][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Infantry][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    uiGraphic[UI_MapEditor_Troopers][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    uiGraphic[UI_MapEditor_MCV][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_MCV][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Trike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                      uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                      uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Quad][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Quad][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Tank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Tank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 0, 0);
    uiGraphic[UI_MapEditor_SiegeTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_Launcher][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Launcher_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    uiGraphic[UI_MapEditor_Devastator][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_SonicTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Sonictank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 1);
    const unsigned int deviatorEditorGun = tornieActive ? ObjPic_DeviatorGunTornie : ObjPic_Launcher_Gun;
    const unsigned int flameTankEditorGun = tornieActive ? ObjPic_FlameTankGunTornie : ObjPic_Launcher_Gun;
    const unsigned int eliteLauncherEditorGun = tornieActive ? ObjPic_EliteLauncherGunTornie : ObjPic_Launcher_Gun;
    uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[deviatorEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_Deviator);
    // Tornie: dedicated sprites for the 3 mod units with their own .png sheets.
// Each is null-guarded so a missing PNG on a partial install doesn't crash
    // the sidebar init — the unit's button just won't have a custom icon (it
    // falls back to whatever the framework draws for an uninitialized SymbolButton).
    if (objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_RocketTrike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
        addMapEditorStar(UI_MapEditor_RocketTrike, true);
    }
    if (objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_SonicTrike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
        addMapEditorStar(UI_MapEditor_SonicTrike, true);
    }
    uiGraphic[UI_MapEditor_FlameTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[flameTankEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_FlameTank, true);
    uiGraphic[UI_MapEditor_EliteLauncher][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[eliteLauncherEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_EliteLauncher, true);
    uiGraphic[UI_MapEditor_EliteSiegeTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    addMapEditorStar(UI_MapEditor_EliteSiegeTank, true);

    // Compose custom vehicle previews one part at a time for every visual
    // colour. This keeps fixed Tornie cannons in their authored colour while
    // allowing the Harvestank and Elite Siege Tank turrets to follow the
    // owning player's colour. It also gives the Harvestank its actual turret
    // in the editor instead of displaying a plain vanilla Harvester.
    auto getColoredEditorFrame = [&](unsigned int objPicID, int colorSlot,
                                     int frameX, int frameY, int framesX, int framesY) -> sdl2::surface_ptr {
        SDL_Surface* atlas = objPic[objPicID][colorSlot][0].get();
        sdl2::surface_ptr remappedAtlas;

        if(!atlas) {
            SDL_Surface* harkonnenAtlas = objPic[objPicID][HOUSE_HARKONNEN][0].get();
            if(!harkonnenAtlas) {
                return nullptr;
            }

            if(colorSlot != HOUSE_HARKONNEN && harkonnenAtlas->format->BytesPerPixel == 1) {
                remappedAtlas = mapSurfaceColorRange(
                    harkonnenAtlas, PALCOLOR_HARKONNEN, getHouseColorPaletteIndexFromSlot(colorSlot));
                applyCustomVisualColorRamp(remappedAtlas.get(), colorSlot);
                if(colorSlot == HOUSE_REBELS) {
                    applyRebelsTint(remappedAtlas.get());
                }
                normalizeTransparentPaletteIndexes(remappedAtlas.get());
                SDL_SetColorKey(remappedAtlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                atlas = remappedAtlas.get();
            } else {
                atlas = harkonnenAtlas;
            }
        }

        return getSubFrame(atlas, frameX, frameY, framesX, framesY);
    };

    auto composeEditorVehicle = [&](unsigned int baseObjPicID, int baseColorSlot,
                                    int gunObjPicID, int gunColorSlot,
                                    int gunOffsetX, int gunOffsetY) -> sdl2::surface_ptr {
        auto base = getColoredEditorFrame(baseObjPicID, baseColorSlot, 0, 0, NUM_ANGLES, 1);
        if(!base || gunObjPicID < 0) {
            return base;
        }

        auto gun = getColoredEditorFrame(static_cast<unsigned int>(gunObjPicID), gunColorSlot,
                                         0, 0, NUM_ANGLES, 1);
        if(!gun) {
            return base;
        }
        return combinePictures(base.get(), gun.get(), gunOffsetX, gunOffsetY);
    };

    auto decorateEditorVehicle = [&](sdl2::surface_ptr vehicle, bool customStar) -> sdl2::surface_ptr {
        SDL_Surface* star = (customStar && customMapEditorStar)
            ? customMapEditorStar.get()
            : objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get();
        if(!vehicle || !star) {
            return vehicle;
        }
        return combinePictures(vehicle.get(), star,
                               vehicle->w - star->w,
                               vehicle->h - star->h);
    };

    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        const int fixedTornieGunSlot = tornieActive ? HOUSE_HARKONNEN : colorSlot;

        auto harvestank = composeEditorVehicle(
            ObjPic_Harvester, colorSlot,
            tornieActive ? static_cast<int>(ObjPic_HarvestankGunTornie) : -1,
            colorSlot, 0, 0);
        if(harvestank) {
            uiGraphic[UI_MapEditor_RebelHarvester][colorSlot] =
                decorateEditorVehicle(std::move(harvestank), true);
        }

        auto deviator = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(deviatorEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(deviator) {
            uiGraphic[UI_MapEditor_Deviator][colorSlot] =
                decorateEditorVehicle(std::move(deviator), false);
        }

        auto rocketTrike = getColoredEditorFrame(ObjPic_RocketTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(rocketTrike) {
            uiGraphic[UI_MapEditor_RocketTrike][colorSlot] =
                decorateEditorVehicle(std::move(rocketTrike), true);
        }

        auto sonicTrike = getColoredEditorFrame(ObjPic_SonicTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(sonicTrike) {
            uiGraphic[UI_MapEditor_SonicTrike][colorSlot] =
                decorateEditorVehicle(std::move(sonicTrike), true);
        }

        auto flameTank = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(flameTankEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(flameTank) {
            uiGraphic[UI_MapEditor_FlameTank][colorSlot] =
                decorateEditorVehicle(std::move(flameTank), true);
        }

        auto eliteLauncher = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(eliteLauncherEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(eliteLauncher) {
            uiGraphic[UI_MapEditor_EliteLauncher][colorSlot] =
                decorateEditorVehicle(std::move(eliteLauncher), true);
        }

        auto eliteSiegeTank = composeEditorVehicle(
            ObjPic_Siegetank_Base, colorSlot,
            static_cast<int>(ObjPic_EliteSiegeTankGunTornie), colorSlot, 2, -4);
        if(eliteSiegeTank) {
            uiGraphic[UI_MapEditor_EliteSiegeTank][colorSlot] =
                decorateEditorVehicle(std::move(eliteSiegeTank), true);
        }
    }

    uiGraphic[UI_MapEditor_Saboteur][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Saboteur][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Sandworm][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Sandworm][HOUSE_HARKONNEN][0].get(),0,5,1,9);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Carryall][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get(),0,0,8,2);
    uiGraphic[UI_MapEditor_Ornithopter][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0].get(),0,0,8,3);

    uiGraphic[UI_MapEditor_Pen1x1][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen1x1.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen1x1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Pen3x3][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen3x3.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen3x3][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Pen5x5][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen5x5.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen5x5][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    // DuneCity: map-editor icons for SimCity-style buildings exposed when the
    // city mod is active. Zone atlases are a single 2x2-tile frame each, so
    // we just take the whole surface. Zones are RGBA truecolor (house-agnostic
    // city buildings), so we pre-fill every house slot — the lazy remap in
    // getUIGraphicSurface() assumes palette-indexed surfaces and would corrupt
    // RGBA. NuclearPlant reuses the HighTechFactory icon (same convention used
    // by the in-game detail pic — see sand.cpp) and inherits the standard
    // house-colour remap.
    {
        struct ZoneIcon { int uiID; int objPicID; };
        const ZoneIcon zoneIcons[] = {
            { UI_MapEditor_ZoneResidential, ObjPic_ZoneResidential },
            { UI_MapEditor_ZoneCommercial,  ObjPic_ZoneCommercial  },
            { UI_MapEditor_ZoneIndustrial,  ObjPic_ZoneIndustrial  },
        };
        // Editor icon = the medium-density v0 cell (column 2, row 0) of the
        // atlas. The v0/d0 top-left corner is an "empty lot" placeholder
        // which would make every zone button look like dirt.
        const int iconCellX = 2 * (2 * D2_TILESIZE);  // column 2 (d=2)
        const int iconCellY = 0;                      // row 0 (v=0)
        for (const auto& z : zoneIcons) {
            for (int h = 0; h < (int)NUM_HOUSES; ++h) {
                uiGraphic[z.uiID][h] = getSubPicture(objPic[z.objPicID][HOUSE_HARKONNEN][0].get(),
                                                    iconCellX, iconCellY,
                                                    2*D2_TILESIZE, 2*D2_TILESIZE);
            }
        }
    }
    // Pull the nuclear icon directly from the Micropolis nuclear-plant atlas
    // — first 3x3 frame is identical across all 8 animation slots. Pre-fill
    // every house slot like the zone icons (sprite is house-agnostic).
    for (int h = 0; h < (int)NUM_HOUSES; ++h) {
        if (objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0]) {
            uiGraphic[UI_MapEditor_NuclearPlant][h] = getSubPicture(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 0, 0, 3*D2_TILESIZE, 3*D2_TILESIZE);
        } else {
            // Fall back to HighTechFactory if the Micropolis PNG is missing.
            uiGraphic[UI_MapEditor_NuclearPlant][h] = getSubPicture(objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
        }
    }

    // Road icon: pull frame 15 (four-way intersection) from the CityRoad
    // atlas at zoom level 1 (D2_TILESIZE-per-cell) so it matches the size of
    // other 1x1 structure icons (Slab1, Wall). Road is house-agnostic — fill
    // every house slot directly so the palette-remap helper doesn't run on
    // RGBA surfaces.
    for (int h = 0; h < (int)NUM_HOUSES; ++h) {
        if (objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][1]) {
            uiGraphic[UI_MapEditor_Road][h] = getSubPicture(objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][1].get(), 15 * D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
        } else {
            // Fall back to SLAB.WSA-style if the road atlas wasn't built.
            uiGraphic[UI_MapEditor_Road][h] = getSubPicture(objPic[ObjPic_Wall][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
        }
    }


    // load animations
    animation[Anim_HarkonnenEyes] = menshph->getAnimation(0,4,true,true);
    animation[Anim_HarkonnenEyes]->setFrameRate(0.3);
    animation[Anim_HarkonnenMouth] = menshph->getAnimation(5,9,true,true,true);
    animation[Anim_HarkonnenMouth]->setFrameRate(5.0);
    animation[Anim_HarkonnenShoulder] = menshph->getAnimation(10,10,true,true);
    animation[Anim_HarkonnenShoulder]->setFrameRate(1.0);
    animation[Anim_AtreidesEyes] = menshpa->getAnimation(0,4,true,true);
    animation[Anim_AtreidesEyes]->setFrameRate(0.5);
    animation[Anim_AtreidesMouth] = menshpa->getAnimation(5,9,true,true,true);
    animation[Anim_AtreidesMouth]->setFrameRate(5.0);
    animation[Anim_AtreidesShoulder] = menshpa->getAnimation(10,10,true,true);
    animation[Anim_AtreidesShoulder]->setFrameRate(1.0);
    animation[Anim_AtreidesBook] = menshpa->getAnimation(11,12,true,true,true);
    animation[Anim_AtreidesBook]->setNumLoops(1);
    animation[Anim_AtreidesBook]->setFrameRate(0.2);
    if(tornieActive) {
        animation[Anim_PaulAtreidesEyes] = loadPngStripAnimation("PaulAtreidesEyes.png", 5, 0.5, false, 196);
        animation[Anim_PaulAtreidesMouth] = loadPngStripAnimation("PaulAtreidesMouth.png", 5, 5.0, false, 18);
    }
    animation[Anim_OrdosEyes] = menshpo->getAnimation(0,4,true,true);
    animation[Anim_OrdosEyes]->setFrameRate(0.5);
    animation[Anim_OrdosMouth] = menshpo->getAnimation(5,9,true,true,true);
    animation[Anim_OrdosMouth]->setFrameRate(5.0);
    animation[Anim_OrdosShoulder] = menshpo->getAnimation(10,10,true,true);
    animation[Anim_OrdosShoulder]->setFrameRate(1.0);
    animation[Anim_OrdosRing] = menshpo->getAnimation(11,14,true,true,true);
    animation[Anim_OrdosRing]->setNumLoops(1);
    animation[Anim_OrdosRing]->setFrameRate(6.0);
    animation[Anim_FremenEyes] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesEyes].get());
    animation[Anim_FremenMouth] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesMouth].get());
    animation[Anim_FremenShoulder] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesShoulder].get());
    animation[Anim_FremenBook] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesBook].get());
    animation[Anim_SardaukarEyes] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenEyes].get());
    animation[Anim_SardaukarMouth] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenMouth].get());
    animation[Anim_SardaukarShoulder] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenShoulder].get());
    animation[Anim_MercenaryEyes] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosEyes].get());
    animation[Anim_MercenaryMouth] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosMouth].get());
    animation[Anim_MercenaryShoulder] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosShoulder].get());
    animation[Anim_MercenaryRing] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosRing].get());

    animation[Anim_ChaniEyes] = loadPngStripAnimation("ChaniMentatEyes.png", 5, 0.5);
    if(animation[Anim_ChaniEyes] == nullptr) {
        animation[Anim_ChaniEyes] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesEyes].get());
    }
    animation[Anim_ChaniMouth] = loadPngStripAnimation("ChaniMentatMouth.png", 5, 5.0);
    if(animation[Anim_ChaniMouth] == nullptr) {
        animation[Anim_ChaniMouth] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesMouth].get());
    }

    animation[Anim_BeneEyes] = menshpm->getAnimation(0,4,true,true);
    if(animation[Anim_BeneEyes] != nullptr) {
        animation[Anim_BeneEyes]->setPalette(benePalette);
        animation[Anim_BeneEyes]->setFrameRate(0.5);
    }
    animation[Anim_BeneMouth] = menshpm->getAnimation(5,9,true,true,true);
    if(animation[Anim_BeneMouth] != nullptr) {
        animation[Anim_BeneMouth]->setPalette(benePalette);
        animation[Anim_BeneMouth]->setFrameRate(5.0);
    }
    // the remaining animation are loaded on demand to save some loading time

    // load map choice pieces
    for(int i = 0; i < NUM_MAPCHOICEPIECES; i++) {
        mapChoicePieces[i][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(pieces->getPicture(i).get());
        SDL_SetColorKey(mapChoicePieces[i][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    }

    // pBackgroundSurface is separate as we never draw it but use it to construct other sprites
    pBackgroundSurface = convertSurfaceToDisplayFormat(PicFactory->createBackground().get());
}

GFXManager::~GFXManager() = default;

static std::unique_ptr<Animation> loadPngStripAnimation(const std::string& filename, int frameCount, double frameRate, bool bDoublePic, int transparentColorKey) {
    if(frameCount <= 0 || !pFileManager->exists(filename)) {
        return nullptr;
    }

    auto strip = LoadPNG_RW(pFileManager->openFile(filename).get());
    if(!strip || strip->w < frameCount || strip->h <= 0) {
        return nullptr;
    }

    const int frameWidth = strip->w / frameCount;
    if(frameWidth <= 0) {
        return nullptr;
    }

    const int extraPixels = strip->w - frameWidth * frameCount;
    int frameStride = frameWidth;
    if(frameCount > 1 && extraPixels > 0 && (extraPixels % (frameCount - 1)) == 0) {
        frameStride += extraPixels / (frameCount - 1);
    }

    auto animation = std::make_unique<Animation>();
    for(int frame = 0; frame < frameCount; frame++) {
        const int sourceX = frame * frameStride;
        if(sourceX + frameWidth > strip->w) {
            return nullptr;
        }
        auto frameSurface = getSubPicture(strip.get(), sourceX, 0, frameWidth, strip->h);
        if(transparentColorKey >= 0) {
            SDL_SetColorKey(frameSurface.get(), SDL_TRUE, static_cast<Uint32>(transparentColorKey));
        }
        animation->addFrame(std::move(frameSurface), bDoublePic, false);
    }
    animation->setFrameRate(frameRate);
    return animation;
}

static void applyRebelsTint(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette) {
        return;
    }
    static const Uint8 rebelsGreyRamp[8] = { 82, 72, 62, 52, 42, 34, 27, 20 };
    const int rebelsBase = houseToPaletteIndex[HOUSE_REBELS];
    for(int k = 0; k < 8; k++) {
        SDL_Color& color = surface->format->palette->colors[rebelsBase + k];
        color.r = rebelsGreyRamp[k];
        color.g = rebelsGreyRamp[k];
        color.b = rebelsGreyRamp[k];
        color.a = 255;
    }
}

static void applyCustomVisualColorRamp(SDL_Surface* surface, int colorSlot) {
    if(!isCustomHouseColorSlot(colorSlot) || !customPaletteLoaded || !surface || !surface->format || !surface->format->palette) {
        return;
    }

    const int paletteBase = getHouseColorPaletteIndexFromSlot(colorSlot);
    customPalette.applyToSurface(surface, paletteBase, paletteBase + 7);
    normalizeTransparentPaletteIndexes(surface);
}

static void preserveOpaqueBlackIndex(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    if(PALCOLOR_TRANSPARENT >= palette->ncolors || PALCOLOR_BLACK >= palette->ncolors) {
        return;
    }

    const SDL_Color transparentIndexColor = palette->colors[PALCOLOR_TRANSPARENT];
    const bool opaqueBlack =
        transparentIndexColor.a >= 128
        && transparentIndexColor.r <= 8
        && transparentIndexColor.g <= 8
        && transparentIndexColor.b <= 8;
    if(!opaqueBlack) {
        return;
    }

    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            if(pixels[x] == PALCOLOR_TRANSPARENT) {
                pixels[x] = PALCOLOR_BLACK;
            }
        }
    }
}

static void normalizeTransparentPaletteIndexes(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    if(PALCOLOR_TRANSPARENT >= palette->ncolors) {
        return;
    }

    bool hasAlphaTransparentIndex = false;
    for(int i = 0; i < palette->ncolors; i++) {
        if(i != PALCOLOR_TRANSPARENT && palette->colors[i].a < 128) {
            hasAlphaTransparentIndex = true;
            break;
        }
    }

    if(hasAlphaTransparentIndex) {
        sdl2::surface_lock lock{ surface };
        for(int y = 0; y < surface->h; y++) {
            Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
            for(int x = 0; x < surface->w; x++) {
                const Uint8 index = pixels[x];
                if(index != PALCOLOR_TRANSPARENT && index < palette->ncolors && palette->colors[index].a < 128) {
                    pixels[x] = PALCOLOR_TRANSPARENT;
                }
            }
        }
    }

    for(int i = 0; i < palette->ncolors; i++) {
        palette->colors[i].a = (i == PALCOLOR_TRANSPARENT) ? 0 : SDL_ALPHA_OPAQUE;
    }
    SDL_SetColorKey(surface, SDL_TRUE, PALCOLOR_TRANSPARENT);
}

static sdl2::surface_ptr convertTornieIndexedSurfaceToRGBA(SDL_Surface* source, const char* label, int house, unsigned int zoom, bool useTextureMask) {
    if(!source || !source->format || !source->format->palette || source->format->BytesPerPixel != 1) {
        return nullptr;
    }

    sdl2::surface_ptr rgba{ SDL_CreateRGBSurfaceWithFormat(0, source->w, source->h, 32, SCREEN_FORMAT) };
    if(!rgba || !rgba->format) {
        SDL_Log("TornieGFX: rgba-convert %s house=%d zoom=%u failed: %s",
                label ? label : "Unknown",
                house,
                zoom,
                SDL_GetError());
        return nullptr;
    }

    Uint32 sourceColorKey = PALCOLOR_TRANSPARENT;
    const bool hasColorKey = SDL_GetColorKey(source, &sourceColorKey) == 0;
    const SDL_Palette* palette = source->format->palette;
    int opaquePixels = 0;
    int transparentPixels = 0;

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock rgbaLock{ rgba.get() };
        for(int y = 0; y < source->h; y++) {
            const Uint8* srcPixels = static_cast<const Uint8*>(sourceLock.pixels()) + y * source->pitch;
            auto* dstPixels = reinterpret_cast<Uint32*>(static_cast<Uint8*>(rgbaLock.pixels()) + y * rgba->pitch);
            for(int x = 0; x < source->w; x++) {
                const Uint8 index = srcPixels[x];
            // Tornie structure previews use the sprite texture as their mask. Keep
            // only keyed/palette-transparent pixels hidden; forcing the full frame
            // opaque makes empty atlas space draw as black rectangles.
            const bool transparent =
                index == PALCOLOR_TRANSPARENT
                || (hasColorKey && index == static_cast<Uint8>(sourceColorKey))
                || index >= palette->ncolors
                || palette->colors[index].a < 128;

                if(transparent) {
                    dstPixels[x] = SDL_MapRGBA(rgba->format, 0, 0, 0, 0);
                    transparentPixels++;
                } else {
                    const SDL_Color color = palette->colors[index];
                    dstPixels[x] = SDL_MapRGBA(rgba->format, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
                    opaquePixels++;
                }
            }
        }
    }

    if(zoom == 0) {
        SDL_Log("TornieGFX: rgba-convert %s house=%d zoom=%u surface=%dx%d opaque=%d transparent=%d mask=%s",
                label ? label : "Unknown",
                house,
                zoom,
                rgba->w,
                rgba->h,
                opaquePixels,
                transparentPixels,
                useTextureMask ? "texture" : "palette");
    }

    return rgba;
}

namespace {
struct TornieSurfaceFrameStats {
    int totalPixels = 0;
    int opaquePixels = 0;
    int colorKeyTransparentPixels = 0;
    int alphaTransparentPixels = 0;
};

Uint32 readSurfacePixelUnchecked(SDL_Surface* surface, int x, int y) {
    Uint8* pixel = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x * surface->format->BytesPerPixel;

    switch(surface->format->BytesPerPixel) {
        case 1:
            return *pixel;
        case 2:
            return *reinterpret_cast<Uint16*>(pixel);
        case 3:
            if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                return (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
            }
            return pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
        case 4:
            return *reinterpret_cast<Uint32*>(pixel);
        default:
            return 0;
    }
}

int getTornieDiagnosticFrameCount(SDL_Surface* surface, int frameWidth, int frameHeight) {
    if(!surface || frameWidth <= 0 || frameHeight <= 0) {
        return 0;
    }

    if(surface->w >= 2 * frameWidth && surface->h >= frameHeight && surface->h < 2 * frameHeight) {
        return std::max(1, surface->w / frameWidth);
    }

    if(surface->h >= 2 * frameHeight) {
        return std::max(1, surface->h / frameHeight);
    }

    return 1;
}

SDL_Rect getTornieDiagnosticFrameRect(SDL_Surface* surface, int frameWidth, int frameHeight, int frame) {
    if(!surface || frameWidth <= 0 || frameHeight <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }

    const bool horizontal =
        surface->w >= 2 * frameWidth
        && surface->h >= frameHeight
        && surface->h < 2 * frameHeight;
    const int frameCount = getTornieDiagnosticFrameCount(surface, frameWidth, frameHeight);
    const int clampedFrame = std::max(0, std::min(frame, std::max(0, frameCount - 1)));

    if(horizontal) {
        const int sourceX = clampedFrame * frameWidth;
        return SDL_Rect{ sourceX, 0, std::min(frameWidth, std::max(0, surface->w - sourceX)), std::min(frameHeight, surface->h) };
    }

    const int sourceY = (frameCount > 1) ? clampedFrame * frameHeight : 0;
    return SDL_Rect{ 0, sourceY, std::min(frameWidth, surface->w), std::min(frameHeight, std::max(0, surface->h - sourceY)) };
}

TornieSurfaceFrameStats collectTornieSurfaceFrameStats(SDL_Surface* surface, SDL_Rect rect) {
    TornieSurfaceFrameStats stats;
    if(!surface || !surface->format || rect.w <= 0 || rect.h <= 0) {
        return stats;
    }

    const int xStart = std::max(0, rect.x);
    const int yStart = std::max(0, rect.y);
    const int xEnd = std::min(surface->w, rect.x + rect.w);
    const int yEnd = std::min(surface->h, rect.y + rect.h);
    if(xStart >= xEnd || yStart >= yEnd) {
        return stats;
    }

    Uint32 colorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(surface, &colorKey) == 0;
    SDL_Palette* palette = surface->format->palette;

    sdl2::surface_lock lock{ surface };
    for(int y = yStart; y < yEnd; y++) {
        for(int x = xStart; x < xEnd; x++) {
            const Uint32 pixel = readSurfacePixelUnchecked(surface, x, y);
            stats.totalPixels++;

            bool transparent = false;
            if(hasColorKey && pixel == colorKey) {
                stats.colorKeyTransparentPixels++;
                transparent = true;
            }

            if(palette && pixel < static_cast<Uint32>(palette->ncolors)) {
                if(palette->colors[pixel].a < 128) {
                    stats.alphaTransparentPixels++;
                    transparent = true;
                }
            } else if(!palette) {
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                Uint8 a = 255;
                SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
                if(a < 128) {
                    stats.alphaTransparentPixels++;
                    transparent = true;
                }
            }

            if(!transparent) {
                stats.opaquePixels++;
            }
        }
    }

    return stats;
}
}

static bool isTornieStructureObjPic(unsigned int id) {
    return id == ObjPic_AdvancedWindTrap
        || id == ObjPic_AdvancedWindTrap2x3
        || id == ObjPic_AdvancedWindTrap3x2
        || id == ObjPic_Worfinery
        || id == ObjPic_TechCenter
        || id == ObjPic_Scoutpost;
}

static const char* getTornieStructureObjPicName(unsigned int id) {
    switch(id) {
        case ObjPic_AdvancedWindTrap:    return "AdvancedWindTrap3x3";
        case ObjPic_AdvancedWindTrap2x3: return "AdvancedWindTrap2x3";
        case ObjPic_AdvancedWindTrap3x2: return "AdvancedWindTrap3x2";
        case ObjPic_Worfinery:           return "Worfinery";
        case ObjPic_TechCenter:          return "TechCenter";
        case ObjPic_Scoutpost:           return "Scoutpost";
        default:                         return "Unknown";
    }
}

static void logTornieStructureSurfaceDiagnostics(const char* stage, const char* label, SDL_Surface* surface, int frameWidth, int frameHeight) {
    if(!surface) {
        SDL_Log("TornieGFX: %s %s surface=null", stage, label);
        return;
    }

    Uint32 colorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(surface, &colorKey) == 0;
    SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
    SDL_GetSurfaceBlendMode(surface, &blendMode);
    const int paletteColors = (surface->format && surface->format->palette) ? surface->format->palette->ncolors : 0;
    const int frameCount = getTornieDiagnosticFrameCount(surface, frameWidth, frameHeight);

    SDL_Log("TornieGFX: %s %s surface=%dx%d bpp=%d bytes=%d paletteColors=%d colorKey=%s/%u blend=%d frameSize=%dx%d frames=%d",
            stage,
            label,
            surface->w,
            surface->h,
            surface->format ? surface->format->BitsPerPixel : 0,
            surface->format ? surface->format->BytesPerPixel : 0,
            paletteColors,
            hasColorKey ? "yes" : "no",
            hasColorKey ? colorKey : 0,
            static_cast<int>(blendMode),
            frameWidth,
            frameHeight,
            frameCount);

    if(surface->format && surface->format->palette && PALCOLOR_TRANSPARENT < surface->format->palette->ncolors) {
        const SDL_Color transparent = surface->format->palette->colors[PALCOLOR_TRANSPARENT];
        const SDL_Color opaqueBlack = (PALCOLOR_BLACK < surface->format->palette->ncolors)
            ? surface->format->palette->colors[PALCOLOR_BLACK]
            : SDL_Color{0, 0, 0, 255};
        SDL_Log("TornieGFX: %s %s palette transparent[%d]=(%u,%u,%u,%u) black[%d]=(%u,%u,%u,%u)",
                stage,
                label,
                PALCOLOR_TRANSPARENT,
                transparent.r,
                transparent.g,
                transparent.b,
                transparent.a,
                PALCOLOR_BLACK,
                opaqueBlack.r,
                opaqueBlack.g,
                opaqueBlack.b,
                opaqueBlack.a);
    }

    for(int frame = 0; frame < std::min(frameCount, 8); frame++) {
        SDL_Rect rect = getTornieDiagnosticFrameRect(surface, frameWidth, frameHeight, frame);
        TornieSurfaceFrameStats stats = collectTornieSurfaceFrameStats(surface, rect);
        SDL_Log("TornieGFX: %s %s frame=%d rect=(%d,%d,%d,%d) total=%d opaque=%d keyTransparent=%d alphaTransparent=%d",
                stage,
                label,
                frame,
                rect.x,
                rect.y,
                rect.w,
                rect.h,
                stats.totalPixels,
                stats.opaquePixels,
                stats.colorKeyTransparentPixels,
                stats.alphaTransparentPixels);

        if(stats.totalPixels == 0 || stats.opaquePixels == 0) {
            SDL_Log("TornieGFX: WARNING %s %s frame=%d is empty or fully transparent", stage, label, frame);
        }
    }
}

static int findNearestPaletteIndex(const SDL_Palette* palette, const SDL_Color color) {
    if(!palette || palette->ncolors <= 1) {
        return PALCOLOR_TRANSPARENT;
    }

    int bestIndex = std::min(PALCOLOR_BLACK, palette->ncolors - 1);
    int bestDistance = 1 << 30;
    for(int i = 1; i < palette->ncolors; i++) {
        const SDL_Color candidate = palette->colors[i];
        const int dr = static_cast<int>(candidate.r) - static_cast<int>(color.r);
        const int dg = static_cast<int>(candidate.g) - static_cast<int>(color.g);
        const int db = static_cast<int>(candidate.b) - static_cast<int>(color.b);
        const int distance = dr * dr + dg * dg + db * db;
        if(distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if(distance == 0) {
                break;
            }
        }
    }

    return bestIndex;
}

static sdl2::surface_ptr convertTruecolorSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette) {
    if(!source || !source->format || source->format->BytesPerPixel == 1 || !targetPalette) {
        return nullptr;
    }

    sdl2::surface_ptr indexed{ SDL_CreateRGBSurface(0, source->w, source->h, 8, 0, 0, 0, 0) };
    if(!indexed || !indexed->format || !indexed->format->palette) {
        return nullptr;
    }

    const int colorCount = std::min(targetPalette->ncolors, indexed->format->palette->ncolors);
    SDL_SetPaletteColors(indexed->format->palette, targetPalette->colors, 0, colorCount);
    for(int i = 0; i < indexed->format->palette->ncolors; i++) {
        indexed->format->palette->colors[i].a =
            (i == PALCOLOR_TRANSPARENT) ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE;
    }

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(indexed.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock indexedLock{ indexed.get() };
        for(int y = 0; y < source->h; y++) {
            Uint8* destination = static_cast<Uint8*>(indexedLock.pixels()) + y * indexed->pitch;
            for(int x = 0; x < source->w; x++) {
                const Uint32 pixel = readSurfacePixelUnchecked(source, x, y);
                SDL_Color color{};
                SDL_GetRGBA(pixel, source->format, &color.r, &color.g, &color.b, &color.a);
                destination[x] = (color.a < 128)
                    ? static_cast<Uint8>(PALCOLOR_TRANSPARENT)
                    : static_cast<Uint8>(findNearestPaletteIndex(targetPalette, color));
            }
        }
    }

    SDL_SetColorKey(indexed.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
    return indexed;
}

static sdl2::surface_ptr remapIndexedSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette) {
    if(!source || !source->format || !source->format->palette || source->format->BytesPerPixel != 1 || !targetPalette) {
        return nullptr;
    }

    sdl2::surface_ptr remapped{ SDL_CreateRGBSurface(0, source->w, source->h, 8, 0, 0, 0, 0) };
    if(!remapped || !remapped->format || !remapped->format->palette) {
        return nullptr;
    }

    SDL_SetPaletteColors(remapped->format->palette,
                         targetPalette->colors,
                         0,
                         std::min(targetPalette->ncolors, remapped->format->palette->ncolors));
    for(int i = 0; i < remapped->format->palette->ncolors; i++) {
        remapped->format->palette->colors[i].a = (i == PALCOLOR_TRANSPARENT) ? 0 : SDL_ALPHA_OPAQUE;
    }

    Uint32 sourceColorKey = PALCOLOR_TRANSPARENT;
    const bool hasSourceColorKey = (SDL_GetColorKey(source, &sourceColorKey) == 0);
    Uint8 indexMap[256];
    for(int i = 0; i < 256; i++) {
        indexMap[i] = static_cast<Uint8>(PALCOLOR_TRANSPARENT);
    }

    const SDL_Palette* sourcePalette = source->format->palette;
    const int sourceColors = std::min(sourcePalette->ncolors, 256);
    for(int i = 0; i < sourceColors; i++) {
        if(i == PALCOLOR_TRANSPARENT
           || (hasSourceColorKey && i == static_cast<int>(sourceColorKey))
           || sourcePalette->colors[i].a < 128) {
            indexMap[i] = static_cast<Uint8>(PALCOLOR_TRANSPARENT);
            continue;
        }

        indexMap[i] = static_cast<Uint8>(findNearestPaletteIndex(targetPalette, sourcePalette->colors[i]));
    }

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(remapped.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock remappedLock{ remapped.get() };
        for(int y = 0; y < source->h; y++) {
            const Uint8* srcPixels = static_cast<const Uint8*>(sourceLock.pixels()) + y * source->pitch;
            Uint8* dstPixels = static_cast<Uint8*>(remappedLock.pixels()) + y * remapped->pitch;
            for(int x = 0; x < source->w; x++) {
                dstPixels[x] = indexMap[srcPixels[x]];
            }
        }
    }

    SDL_SetColorKey(remapped.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
    return remapped;
}

static void normalizeHouseColorRangesToHarkonnen(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    static const int houseColorBases[] = {
        PALCOLOR_HARKONNEN,
        PALCOLOR_ATREIDES,
        PALCOLOR_ORDOS,
        PALCOLOR_FREMEN,
        PALCOLOR_SARDAUKAR,
        PALCOLOR_MERCENARY
    };

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }

            for(const int colorBase : houseColorBases) {
                if(index >= colorBase && index < colorBase + 7) {
                    const int shade = std::clamp((index - colorBase) + 2, 0, 6);
                    index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
                    break;
                }
            }
        }
    }
}

static void normalizeHarkonnenTeamRed(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }

            const SDL_Color color = palette->colors[index];
            const int r = static_cast<int>(color.r);
            const int g = static_cast<int>(color.g);
            const int b = static_cast<int>(color.b);
            const int strongestOther = std::max(g, b);
            const bool redTeamPaint = r >= 70 && r > strongestOther + 16 && g < 120 && b < 120;
            const bool darkRustTeamPaint = r >= 85 && r > g + 8 && r > b + 8 && g < 95 && b < 85;
            if(!redTeamPaint && !darkRustTeamPaint) {
                continue;
            }

            const int brightness = std::max(std::max(r, g), b);
            int shade = 2;
            if(brightness >= 210) {
                shade = 6;
            } else if(brightness >= 180) {
                shade = 5;
            } else if(brightness >= 150) {
                shade = 4;
            } else if(brightness >= 120) {
                shade = 3;
            }
            index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
        }
    }
}

static void normalizeLooseTeamPaintToHarkonnen(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }

            const SDL_Color color = palette->colors[index];
            const int r = static_cast<int>(color.r);
            const int g = static_cast<int>(color.g);
            const int b = static_cast<int>(color.b);
            const bool redPaint = r >= 70 && r > std::max(g, b) + 16 && g < 130 && b < 130;
            const bool greenPaint = g >= 70 && g > std::max(r, b) + 14 && r < 150 && b < 150;
            if(!redPaint && !greenPaint) {
                continue;
            }

            const int brightness = std::max(std::max(r, g), b);
            int shade = 2;
            if(brightness >= 210) {
                shade = 6;
            } else if(brightness >= 180) {
                shade = 5;
            } else if(brightness >= 150) {
                shade = 4;
            } else if(brightness >= 120) {
                shade = 3;
            }
            index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
        }
    }
}

static SDL_Color createSpiceTintColor(SDL_Color base, SDL_Color tint) {
    const int brightness = std::max(std::max(static_cast<int>(base.r), static_cast<int>(base.g)),
                                    static_cast<int>(base.b));
    const int scale = std::clamp(brightness, 48, 176);

    SDL_Color color{};
    color.r = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.r) * scale) / 160));
    color.g = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.g) * scale) / 160));
    color.b = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.b) * scale) / 160));
    color.a = base.a;
    return color;
}

static bool paletteColorsEqual(SDL_Surface* aSurface, Uint8 aIndex, SDL_Surface* bSurface, Uint8 bIndex) {
    if(!aSurface || !bSurface || !aSurface->format || !bSurface->format
       || !aSurface->format->palette || !bSurface->format->palette) {
        return aIndex == bIndex;
    }

    if(aIndex >= aSurface->format->palette->ncolors || bIndex >= bSurface->format->palette->ncolors) {
        return aIndex == bIndex;
    }

    const SDL_Color a = aSurface->format->palette->colors[aIndex];
    const SDL_Color b = bSurface->format->palette->colors[bIndex];
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void collectUsedPaletteIndices(SDL_Surface* surface, bool used[256]) {
    for(int i = 0; i < 256; i++) {
        used[i] = false;
    }

    if(!surface || !surface->format || surface->format->BytesPerPixel != 1) {
        return;
    }

    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        const Uint8* pixels = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            used[pixels[x]] = true;
        }
    }
}

static int allocatePaletteIndex(SDL_Surface* surface, bool used[256]) {
    if(!surface || !surface->format || !surface->format->palette) {
        return -1;
    }

    const int colorCount = std::min(surface->format->palette->ncolors, 256);
    for(int i = colorCount - 1; i > PALCOLOR_TRANSPARENT; i--) {
        if(!used[i]) {
            used[i] = true;
            return i;
        }
    }

    return -1;
}

static void remapPixelToTint(SDL_Surface* surface, Uint8& pixel, SDL_Color tint, bool used[256], int remap[256]) {
    if(!surface || !surface->format || !surface->format->palette
       || pixel == PALCOLOR_TRANSPARENT || pixel >= surface->format->palette->ncolors) {
        return;
    }

    if(remap[pixel] < 0) {
        const SDL_Color color = createSpiceTintColor(surface->format->palette->colors[pixel], tint);
        const int paletteIndex = allocatePaletteIndex(surface, used);
        if(paletteIndex >= 0) {
            SDL_SetPaletteColors(surface->format->palette, &color, paletteIndex, 1);
            remap[pixel] = paletteIndex;
        } else {
            SDL_SetPaletteColors(surface->format->palette, &color, pixel, 1);
            remap[pixel] = pixel;
        }
    }

    pixel = static_cast<Uint8>(remap[pixel]);
}

static void tintTerrainSpiceTiles(SDL_Surface* surface,
                                  int firstTile,
                                  int tileCount,
                                  SDL_Color tint,
                                  bool used[256],
                                  int remap[256]) {
    if(!surface || !surface->format || surface->format->BytesPerPixel != 1
       || surface->w <= 0 || surface->h <= 0) {
        return;
    }

    constexpr int TerrainTile_Sand = 0x03;
    const int tileWidth = surface->w / NUM_TERRAIN_TILES_X;
    const int tileHeight = surface->h / NUM_TERRAIN_TILES_Y;
    if(tileWidth <= 0 || tileHeight <= 0) {
        return;
    }

    const int sandX = (TerrainTile_Sand % NUM_TERRAIN_TILES_X) * tileWidth;
    const int sandY = (TerrainTile_Sand / NUM_TERRAIN_TILES_X) * tileHeight;

    sdl2::surface_lock lock{ surface };
    for(int tileOffset = 0; tileOffset < tileCount; tileOffset++) {
        const int tile = firstTile + tileOffset;
        const int tileX = (tile % NUM_TERRAIN_TILES_X) * tileWidth;
        const int tileY = (tile / NUM_TERRAIN_TILES_X) * tileHeight;

        for(int y = 0; y < tileHeight; y++) {
            Uint8* row = static_cast<Uint8*>(surface->pixels) + (tileY + y) * surface->pitch;
            const Uint8* sandRow = static_cast<const Uint8*>(surface->pixels) + (sandY + y) * surface->pitch;
            for(int x = 0; x < tileWidth; x++) {
                Uint8& pixel = row[tileX + x];
                const Uint8 sandPixel = sandRow[sandX + x];
                if(pixel != PALCOLOR_TRANSPARENT && !paletteColorsEqual(surface, pixel, surface, sandPixel)) {
                    remapPixelToTint(surface, pixel, tint, used, remap);
                }
            }
        }
    }
}

static sdl2::surface_ptr createTintedTerrainSpiceSurface(SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint) {
    if(!source) {
        return nullptr;
    }

    auto tinted = copySurface(source);
    if(!tinted || !tinted->format || !tinted->format->palette || tinted->format->BytesPerPixel != 1) {
        return tinted;
    }

    bool used[256];
    collectUsedPaletteIndices(tinted.get(), used);
    int remap[256];
    for(int& value : remap) {
        value = -1;
    }

    constexpr int TerrainTile_Spice = 0x34;
    constexpr int TerrainTile_ThickSpice = 0x44;
    constexpr int TerrainTile_SpiceBloom = 0x54;
    tintTerrainSpiceTiles(tinted.get(), TerrainTile_Spice, 16, thinTint, used, remap);
    for(int& value : remap) {
        value = -1;
    }
    tintTerrainSpiceTiles(tinted.get(), TerrainTile_ThickSpice, 16, thickTint, used, remap);
    for(int& value : remap) {
        value = -1;
    }
    tintTerrainSpiceTiles(tinted.get(), TerrainTile_SpiceBloom, 1, thinTint, used, remap);

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(tinted.get(), SDL_TRUE, colorKey);
    }

    return tinted;
}

static sdl2::surface_ptr createTintedMapEditorIcon(SDL_Surface* source, SDL_Surface* sand, SDL_Color tint) {
    if(!source) {
        return nullptr;
    }

    auto tinted = copySurface(source);
    if(!tinted || !tinted->format || !tinted->format->palette || tinted->format->BytesPerPixel != 1
       || !sand || !sand->format || sand->format->BytesPerPixel != 1) {
        return tinted;
    }

    bool used[256];
    collectUsedPaletteIndices(tinted.get(), used);
    int remap[256];
    for(int& value : remap) {
        value = -1;
    }

    sdl2::surface_lock tintedLock{ tinted.get() };
    sdl2::surface_lock sandLock{ sand };
    const int width = std::min(tinted->w, sand->w);
    const int height = std::min(tinted->h, sand->h);
    for(int y = 0; y < height; y++) {
        Uint8* row = static_cast<Uint8*>(tinted->pixels) + y * tinted->pitch;
        const Uint8* sandRow = static_cast<const Uint8*>(sand->pixels) + y * sand->pitch;
        for(int x = 0; x < width; x++) {
            Uint8& pixel = row[x];
            const Uint8 sandPixel = sandRow[x];
            if(pixel != PALCOLOR_TRANSPARENT && !paletteColorsEqual(tinted.get(), pixel, sand, sandPixel)) {
                remapPixelToTint(tinted.get(), pixel, tint, used, remap);
            }
        }
    }

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(tinted.get(), SDL_TRUE, colorKey);
    }

    return tinted;
}

static sdl2::surface_ptr resizeSurfaceNearest(SDL_Surface* source, int width, int height) {
    if(!source || width <= 0 || height <= 0) {
        return nullptr;
    }

    sdl2::surface_ptr resized;
    if(source->format->BytesPerPixel == 1) {
        resized = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0) };
        if(resized && resized->format->palette && source->format->palette) {
            SDL_SetPaletteColors(resized->format->palette,
                                 source->format->palette->colors,
                                 0,
                                 source->format->palette->ncolors);
        }
    } else {
        resized = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height,
                                                          source->format->BitsPerPixel,
                                                          source->format->Rmask,
                                                          source->format->Gmask,
                                                          source->format->Bmask,
                                                          source->format->Amask) };
    }

    if(!resized) {
        return nullptr;
    }

    SDL_BlendMode blendMode;
    SDL_GetSurfaceBlendMode(source, &blendMode);
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(resized.get(), SDL_BLENDMODE_NONE);
    SDL_BlitScaled(source, nullptr, resized.get(), nullptr);
    SDL_SetSurfaceBlendMode(source, blendMode);
    SDL_SetSurfaceBlendMode(resized.get(), blendMode);

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(resized.get(), SDL_TRUE, colorKey);
    }

    return resized;
}

static sdl2::surface_ptr scaleSurfaceNearest(SDL_Surface* source, int factor) {
    if(!source || factor <= 0) {
        return nullptr;
    }

    return resizeSurfaceNearest(source, source->w * factor, source->h * factor);
}

static sdl2::surface_ptr createCustomMapEditorStar(SDL_Surface* source) {
    if(!source) {
        return nullptr;
    }
    auto star = copySurface(source);
    if(!star) {
        return nullptr;
    }

    if(star->format->BytesPerPixel == 1) {
        sdl2::surface_lock lock{ star.get() };
        for(int y = 0; y < star->h; y++) {
            Uint8* p = static_cast<Uint8*>(star->pixels) + y * star->pitch;
            for(int x = 0; x < star->w; x++, p++) {
                if(*p != PALCOLOR_TRANSPARENT) {
                    *p = PALCOLOR_LIGHTBLUE;
                }
            }
        }
    } else {
        const Uint32 lightBlue = MapRGBA(star->format, COLOR_LIGHTBLUE);
        sdl2::surface_lock lock{ star.get() };
        for(int y = 0; y < star->h; y++) {
            for(int x = 0; x < star->w; x++) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                SDL_GetRGBA(getPixel(star.get(), x, y), star->format, &r, &g, &b, &a);
                if(a != 0) {
                    putPixel(star.get(), x, y, lightBlue);
                }
            }
        }
    }

    return star;
}

// DuneCity 1.0.487: invalidate sprite texture cache.
// Clears objPicTex + objPic (NOT uiGraphic - preserves
// mentat background and editor sidebar icons).
void GFXManager::invalidateAllSpriteTextures() {
    SDL_Log("GFXManager::invalidateAllSpriteTextures(): clearing textures and derived house sprite caches");
    const auto keepAllHouseSurfaces = [](int id) {
        return id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
               || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
               || id == ObjPic_NuclearPlant || id == ObjPic_PoliceStation
               || id == ObjPic_Stadium || id == ObjPic_Airport
               || id == ObjPic_Hospital || id == ObjPic_Church
               // v1.0.173-compatible Tornie structure surfaces are prebuilt
               // as truecolor RGBA for every visual colour slot. Keep them
               // across renderer cache invalidation; discarding them would
               // re-enter the newer lazy indexed remap path.
               || id == ObjPic_AdvancedWindTrap
               || id == ObjPic_AdvancedWindTrap2x3
               || id == ObjPic_AdvancedWindTrap3x2
               || id == ObjPic_Worfinery
               || id == ObjPic_TechCenter
               || id == ObjPic_Scoutpost;
    };
    for(int id = 0; id < NUM_OBJPICS; id++) {
        for(int h = 0; h < NUM_HOUSE_COLOR_SLOTS; h++) {
            for(unsigned int z = 0; z < NUM_ZOOMLEVEL; z++) {
                objPicTex[id][h][z].reset();
                if(h != HOUSE_HARKONNEN && !keepAllHouseSurfaces(id)) {
                    objPic[id][h][z].reset();
                }
            }
        }
    }
}

void GFXManager::reloadModDependentUiGraphics() {
    SDL_Log("GFXManager::reloadModDependentUiGraphics(): reloading mentat backgrounds for active mod");

    for(int house = 0; house < NUM_HOUSE_COLOR_SLOTS; house++) {
        uiGraphic[UI_MentatBackground][house].reset();
        uiGraphic[UI_MentatBackgroundPaul][house].reset();
        uiGraphicTex[UI_MentatBackground][house].reset();
        uiGraphicTex[UI_MentatBackgroundPaul][house].reset();
    }

    const bool tornieActive = ModManager::instance().isInitialized()
        && (ModManager::instance().getActiveModName() == "Tornie");
    auto loadMentatBackgroundPng = [&](const char* filename) -> sdl2::surface_ptr {
        if(!pFileManager->exists(filename)) {
            return nullptr;
        }

        auto png = LoadPNG_RW(pFileManager->openFile(filename).get());
        if(!png) {
            return nullptr;
        }

        if(png->w <= SCREEN_MIN_WIDTH/2 && png->h <= SCREEN_MIN_HEIGHT/2) {
            return Scaler::defaultDoubleSurface(png.get());
        }

        return png;
    };

    uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATH.CPS").get()).get());
    auto vanillaAtreidesMentat = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATA.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] = copySurface(vanillaAtreidesMentat.get());
    uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] = tornieActive ? loadMentatBackgroundPng("PaulAtreidesMentat.png") : nullptr;
    if(uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] == nullptr) {
        uiGraphic[UI_MentatBackgroundPaul][HOUSE_ATREIDES] = copySurface(vanillaAtreidesMentat.get());
    }

    uiGraphic[UI_MentatBackground][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATO.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_FREMEN] = PictureFactory::mapMentatSurfaceToFremen(vanillaAtreidesMentat.get());
    uiGraphic[UI_MentatBackground][HOUSE_SARDAUKAR] = PictureFactory::mapMentatSurfaceToSardaukar(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get());
    uiGraphic[UI_MentatBackground][HOUSE_MERCENARY] = PictureFactory::mapMentatSurfaceToMercenary(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());

    if(auto chaniMentat = loadMentatBackgroundPng("ChaniMentat.png")) {
        uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = std::move(chaniMentat);
        auto rebelsChani = loadMentatBackgroundPng("ChaniMentat.png");
        uiGraphic[UI_MentatBackground][HOUSE_REBELS] = rebelsChani ? std::move(rebelsChani)
                                                                  : copySurface(uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL].get());
    } else {
        uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[HOUSE_NEUTRAL]);
        uiGraphic[UI_MentatBackground][HOUSE_REBELS] = mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_ATREIDES].get(), PALCOLOR_ATREIDES, houseToPaletteIndex[HOUSE_REBELS]);
    }
}


bool GFXManager::hasObjPic(unsigned int id, int house, unsigned int z) const {
    if(id >= NUM_OBJPICS || z >= NUM_ZOOMLEVEL) {
        return false;
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        return false;
    }
    return objPic[id][house][z] != nullptr;
}

SDL_Texture* GFXManager::getZoomedObjPic(unsigned int id, int house, unsigned int z) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not available!", id);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(objPic[id][house][z] == nullptr) {
        // remap to this color
        if(objPic[id][HOUSE_HARKONNEN][z] == nullptr) {
            // DuneCity civic sprites: fall back to ConstructionYard instead of crashing
            static const unsigned int duneCityCivicIds[] = {
                ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
                ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
            };
            // Tornie mod sprites with optional dedicated graphics: fall back to
            // their closest vanilla equivalent when the dedicated PNG is missing.
            static const unsigned int tornieModSpriteIds[] = {
                ObjPic_RocketTrike, ObjPic_SonicTrike, ObjPic_FlameTankGunTornie,
                ObjPic_EliteSiegeTankGunTornie, ObjPic_DeviatorGunTornie,
                ObjPic_EliteLauncherGunTornie, ObjPic_RebelSonicTankGun,
                ObjPic_HarvestankGunTornie,
                ObjPic_AdvancedWindTrap, ObjPic_AdvancedWindTrap2x3, ObjPic_AdvancedWindTrap3x2,
                ObjPic_RebelHarvester,  // falls back to vanilla Harvester
                ObjPic_Worfinery,       // falls back to vanilla WOR
                ObjPic_TechCenter,      // falls back to vanilla Palace
                ObjPic_Scoutpost        // falls back to vanilla Rocket Turret
            };
            bool isDuneCityCivic = false;
            for(auto cid : duneCityCivicIds) {
                if(id == cid) { isDuneCityCivic = true; break; }
            }
            bool isTornieModSprite = false;
            for(auto tid : tornieModSpriteIds) {
                if(id == tid) { isTornieModSprite = true; break; }
            }
            if(isDuneCityCivic && objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]) {
                SDL_Log("GFXManager::getZoomedObjPic(): DuneCity civic sprite ID %u not loaded, falling back to ConstructionYard", id);
                objPic[id][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z].get(),
                                       objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]->format, 0)
                };
            } else if(isTornieModSprite) {
                unsigned int fallbackId = ObjPic_Tank_Base;
                if(id == ObjPic_RebelHarvester) {
                    fallbackId = ObjPic_Harvester;
                } else if(id == ObjPic_SonicTrike) {
                    fallbackId = ObjPic_Trike;
                } else if(id == ObjPic_DeviatorGunTornie
                          || id == ObjPic_FlameTankGunTornie
                          || id == ObjPic_EliteLauncherGunTornie) {
                    fallbackId = ObjPic_Launcher_Gun;
                } else if(id == ObjPic_EliteSiegeTankGunTornie) {
                    fallbackId = ObjPic_Siegetank_Gun;
                } else if(id == ObjPic_RebelSonicTankGun) {
                    fallbackId = ObjPic_Sonictank_Gun;
                } else if(id == ObjPic_HarvestankGunTornie) {
                    fallbackId = ObjPic_Siegetank_Gun;
                } else if(id == ObjPic_Worfinery) {
                    fallbackId = ObjPic_WOR;
                } else if(id == ObjPic_TechCenter) {
                    fallbackId = ObjPic_Palace;
                } else if(id == ObjPic_AdvancedWindTrap || id == ObjPic_AdvancedWindTrap2x3 || id == ObjPic_AdvancedWindTrap3x2) {
                    fallbackId = ObjPic_Windtrap;
                } else if(id == ObjPic_Scoutpost) {
                    fallbackId = ObjPic_RocketTurret;
                }
                if(objPic[fallbackId][HOUSE_HARKONNEN][z] == nullptr) {
                    return nullptr;
                }
                SDL_Log("GFXManager::getZoomedObjPic(): Tornie sprite ID %u not loaded, falling back to sprite ID %u", id, fallbackId);
                objPic[id][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[fallbackId][HOUSE_HARKONNEN][z].get(),
                                       objPic[fallbackId][HOUSE_HARKONNEN][z]->format, 0)
                };
            } else {
                THROW(std::runtime_error, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not loaded!", id);
            }
        }

        if(objPic[id][HOUSE_HARKONNEN][z]->format->BytesPerPixel == 1) {
            objPic[id][house][z] = mapSurfaceColorRange(objPic[id][HOUSE_HARKONNEN][z].get(), PALCOLOR_HARKONNEN, getHouseColorPaletteIndexFromSlot(house));
            applyCustomVisualColorRamp(objPic[id][house][z].get(), house);
            if(house == HOUSE_REBELS) {
                applyRebelsTint(objPic[id][house][z].get());
            }
            normalizeTransparentPaletteIndexes(objPic[id][house][z].get());
            if(z == 0 && isTornieStructureObjPic(id)) {
                const Coord tiles = objPicTiles[id];
                const int frameWidth = (tiles.x > 0) ? objPic[id][house][z]->w / tiles.x : objPic[id][house][z]->w;
                const int frameHeight = (tiles.y > 0) ? objPic[id][house][z]->h / tiles.y : objPic[id][house][z]->h;
                logTornieStructureSurfaceDiagnostics("house-remapped", getTornieStructureObjPicName(id), objPic[id][house][z].get(), frameWidth, frameHeight);
            }
        } else {
            objPic[id][house][z] = copySurface(objPic[id][HOUSE_HARKONNEN][z].get());
        }
    }

    if(objPicTex[id][house][z] == nullptr) {
        // now convert to display format
        if(id == ObjPic_Windtrap) {
            // Windtrap uses palette animation on PALCOLOR_WINDTRAP_COLORCYCLE; fake this
            objPicTex[id][house][z] = convertSurfaceToTexture(generateWindtrapAnimationFrames(objPic[id][house][z].get()));
        } else if(id == ObjPic_Bullet_SonicTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else if(id == ObjPic_SandwormShimmerTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else if(isTornieStructureObjPic(id)) {
            // The custom Tornie structure sprites are already palette-indexed and
            // normalized above. Converting them through the special RGBA mask path
            // can turn every pixel transparent, which leaves an invisible but still
            // selectable structure in-game. Use the same indexed-surface texture
            // conversion as vanilla structures so the palette colorkey is preserved.
            objPicTex[id][house][z] = convertSurfaceToTexture(objPic[id][house][z].get());
        } else {
            objPicTex[id][house][z] = convertSurfaceToTexture(objPic[id][house][z].get());
        }

        // Truecolor RGBA sprites need explicit blend mode so their alpha
        // channel is respected during rendering; without this, transparent
        // pixels appear as solid black.
        if(objPic[id][house][z]->format->BytesPerPixel != 1
           || id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
           || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
           || id == ObjPic_NuclearPlant || id == ObjPic_PoliceStation
           || id == ObjPic_Stadium || id == ObjPic_Airport
           || id == ObjPic_Hospital || id == ObjPic_Church
           || id == ObjPic_Windtrap || id == ObjPic_AdvancedWindTrap
           || id == ObjPic_AdvancedWindTrap2x3 || id == ObjPic_AdvancedWindTrap3x2
           || id == ObjPic_Worfinery || id == ObjPic_TechCenter || id == ObjPic_Scoutpost
           || id == ObjPic_Star) {
            if(objPicTex[id][house][z]) {
                SDL_SetTextureBlendMode(objPicTex[id][house][z].get(), SDL_BLENDMODE_BLEND);
            }
        }

        if(z == 0 && (isTornieStructureObjPic(id) || id == ObjPic_SonicTrike)) {
            int textureWidth = 0;
            int textureHeight = 0;
            Uint32 textureFormat = 0;
            int textureAccess = 0;
            SDL_BlendMode textureBlend = SDL_BLENDMODE_NONE;
            Uint8 textureAlpha = 0;
            Uint8 textureRed = 0;
            Uint8 textureGreen = 0;
            Uint8 textureBlue = 0;
            if(objPicTex[id][house][z]) {
                SDL_QueryTexture(objPicTex[id][house][z].get(), &textureFormat, &textureAccess, &textureWidth, &textureHeight);
                SDL_GetTextureBlendMode(objPicTex[id][house][z].get(), &textureBlend);
                SDL_GetTextureAlphaMod(objPicTex[id][house][z].get(), &textureAlpha);
                SDL_GetTextureColorMod(objPicTex[id][house][z].get(), &textureRed, &textureGreen, &textureBlue);
            }
            const char* diagnosticName = (id == ObjPic_SonicTrike)
                                             ? "SonicTrike"
                                             : getTornieStructureObjPicName(id);
            SDL_Log("TornieGFX: texture-ready %s house=%d z=%u texture=%dx%d format=%u access=%d blend=%d alpha=%u color=%u,%u,%u",
                    diagnosticName,
                    house,
                    z,
                    textureWidth,
                    textureHeight,
                    textureFormat,
                    textureAccess,
                    static_cast<int>(textureBlend),
                    textureAlpha,
                    textureRed,
                    textureGreen,
                    textureBlue);
        }
    }

    return objPicTex[id][house][z].get();
}

zoomable_texture GFXManager::getObjPic(unsigned int id, int house) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getObjPic(): Unit Picture with ID %u is not available!", id);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    for(int z = 0; z < NUM_ZOOMLEVEL; z++) {
        if(objPicTex[id][house][z] == nullptr) {
            getZoomedObjPic(id, house, z);  // no assignment as the return value is already stored in objPicTex
        }
    }

    return zoomable_texture{ objPicTex[id][house][0].get(), objPicTex[id][house][1].get(), objPicTex[id][house][2].get() };
}


SDL_Texture* GFXManager::getSmallDetailPic(unsigned int id) {
    if(id >= NUM_SMALLDETAILPICS) {
        return nullptr;
    }
    return smallDetailPicTex[id].get();
}


SDL_Texture* GFXManager::getTinyPicture(unsigned int id) {
    if(id >= NUM_TINYPICTURE) {
        return nullptr;
    }
    return tinyPictureTex[id].get();
}


SDL_Surface* GFXManager::getUIGraphicSurface(unsigned int id, int house) {
    if(id >= NUM_UIGRAPHICS) {
        THROW(std::invalid_argument, "GFXManager::getUIGraphicSurface(): UI Graphic with ID %u is not available!", id);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(uiGraphic[id][house] == nullptr) {
        // remap to this color
        if(uiGraphic[id][HOUSE_HARKONNEN] == nullptr) {
            THROW(std::runtime_error, "GFXManager::getUIGraphicSurface(): UI Graphic with ID %u is not loaded!", id);
        }

        SDL_Surface* base = uiGraphic[id][HOUSE_HARKONNEN].get();
        if(base->format->BytesPerPixel == 1) {
            uiGraphic[id][house] = mapSurfaceColorRange(base, PALCOLOR_HARKONNEN,
                                                       getHouseColorPaletteIndexFromSlot(house));
            applyCustomVisualColorRamp(uiGraphic[id][house].get(), house);
            if(house == HOUSE_REBELS) {
                applyRebelsTint(uiGraphic[id][house].get());
            }
        } else {
            // Truecolor custom icons use alpha and do not contain palette
            // indices. Byte-wise palette remapping corrupts their RGBA data.
            uiGraphic[id][house] = copySurface(base);
        }
    }

    return uiGraphic[id][house].get();
}

SDL_Texture* GFXManager::getUIGraphic(unsigned int id, int house) {
    if(id >= NUM_UIGRAPHICS) {
        THROW(std::invalid_argument, "GFXManager::getUIGraphic(): UI Graphic with ID %u is not available!", id);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(uiGraphicTex[id][house] == nullptr) {
        SDL_Surface* pSurface = getUIGraphicSurface(id, house);

        if(id >= UI_MapChoiceArrow_None && id <= UI_MapChoiceArrow_Left) {
            uiGraphicTex[id][house] = convertSurfaceToTexture(generateMapChoiceArrowFrames(pSurface, house));
        } else {
            uiGraphicTex[id][house] = convertSurfaceToTexture(pSurface);
        }
    }

    return uiGraphicTex[id][house].get();
}

SDL_Surface* GFXManager::getMapChoicePieceSurface(unsigned int num, int house) {
    if(num >= NUM_MAPCHOICEPIECES) {
        THROW(std::invalid_argument, "GFXManager::getMapChoicePieceSurface(): Map Piece with number %u is not available!", num);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(mapChoicePieces[num][house] == nullptr) {
        // remap to this color
        if(mapChoicePieces[num][HOUSE_HARKONNEN] == nullptr) {
            THROW(std::runtime_error, "GFXManager::getMapChoicePieceSurface(): Map Piece with number %u is not loaded!", num);
        }

        mapChoicePieces[num][house] = mapSurfaceColorRange(mapChoicePieces[num][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, getHouseColorPaletteIndexFromSlot(house));
        applyCustomVisualColorRamp(mapChoicePieces[num][house].get(), house);
        if(house == HOUSE_REBELS) {
            applyRebelsTint(mapChoicePieces[num][house].get());
        }
    }

    return mapChoicePieces[num][house].get();
}

SDL_Texture* GFXManager::getMapChoicePiece(unsigned int num, int house) {
    if(num >= NUM_MAPCHOICEPIECES) {
        THROW(std::invalid_argument, "GFXManager::getMapChoicePiece(): Map Piece with number %u is not available!", num);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(mapChoicePiecesTex[num][house] == nullptr) {
        mapChoicePiecesTex[num][house] = convertSurfaceToTexture(getMapChoicePieceSurface(num, house));
    }

    return mapChoicePiecesTex[num][house].get();
}

Animation* GFXManager::getAnimation(unsigned int id) {
    if(id >= NUM_ANIMATION) {
        THROW(std::invalid_argument, "GFXManager::getAnimation(): Animation with ID %u is not available!", id);
    }

    if(animation[id] == nullptr) {
        switch(id) {
            case Anim_HarkonnenPlanet: {
                animation[Anim_HarkonnenPlanet] = loadAnimationFromWsa("FHARK.WSA");
                animation[Anim_HarkonnenPlanet]->setFrameRate(10);
            } break;

            case Anim_AtreidesPlanet: {
                animation[Anim_AtreidesPlanet] = loadAnimationFromWsa("FARTR.WSA");
                animation[Anim_AtreidesPlanet]->setFrameRate(10);
            } break;

            case Anim_OrdosPlanet: {
                animation[Anim_OrdosPlanet] = loadAnimationFromWsa("FORDOS.WSA");
                animation[Anim_OrdosPlanet]->setFrameRate(10);
            } break;

            case Anim_FremenPlanet: {
                animation[Anim_FremenPlanet] = PictureFactory::createFremenPlanet(uiGraphic[UI_Herald_ColoredLarge][HOUSE_FREMEN].get());
                animation[Anim_FremenPlanet]->setFrameRate(10);
            } break;

            case Anim_SardaukarPlanet: {
                animation[Anim_SardaukarPlanet] = PictureFactory::createSardaukarPlanet(getAnimation(Anim_OrdosPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_SARDAUKAR].get());
                animation[Anim_SardaukarPlanet]->setFrameRate(10);
            } break;

            case Anim_MercenaryPlanet: {
                animation[Anim_MercenaryPlanet] = PictureFactory::createMercenaryPlanet(getAnimation(Anim_AtreidesPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_MERCENARY].get());
                animation[Anim_MercenaryPlanet]->setFrameRate(10);
            } break;

            case Anim_NeutralPlanet: {
                animation[Anim_NeutralPlanet] = PictureFactory::createNeutralPlanet(getAnimation(Anim_HarkonnenPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_NEUTRAL].get());
                animation[Anim_NeutralPlanet]->setFrameRate(10);
            } break;

            case Anim_RebelsPlanet: {
                animation[Anim_RebelsPlanet] = PictureFactory::createRebelsPlanet(getAnimation(Anim_AtreidesPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_REBELS].get());
                animation[Anim_RebelsPlanet]->setFrameRate(10);
            } break;

            case Anim_Win1:             animation[Anim_Win1] = loadAnimationFromWsa("WIN1.WSA");                 break;
            case Anim_Win2:             animation[Anim_Win2] = loadAnimationFromWsa("WIN2.WSA");                 break;
            case Anim_Lose1:            animation[Anim_Lose1] = loadAnimationFromWsa("LOSTBILD.WSA");            break;
            case Anim_Lose2:            animation[Anim_Lose2] = loadAnimationFromWsa("LOSTVEHC.WSA");            break;
            case Anim_Barracks:         animation[Anim_Barracks] = loadAnimationFromWsa("BARRAC.WSA");           break;
            case Anim_Carryall:         animation[Anim_Carryall] = loadAnimationFromWsa("CARRYALL.WSA");         break;
            case Anim_ConstructionYard: animation[Anim_ConstructionYard] = loadAnimationFromWsa("CONSTRUC.WSA"); break;
            case Anim_Fremen:           animation[Anim_Fremen] = loadAnimationFromWsa("FREMEN.WSA");             break;
            case Anim_DeathHand:        animation[Anim_DeathHand] = loadAnimationFromWsa("GOLD-BB.WSA");         break;
            case Anim_Devastator:       animation[Anim_Devastator] = loadAnimationFromWsa("HARKTANK.WSA");       break;
            case Anim_Harvester:        animation[Anim_Harvester] = loadAnimationFromWsa("HARVEST.WSA");         break;
            case Anim_Radar:            animation[Anim_Radar] = loadAnimationFromWsa("HEADQRTS.WSA");            break;
            case Anim_HighTechFactory:  animation[Anim_HighTechFactory] = loadAnimationFromWsa("HITCFTRY.WSA");  break;
            case Anim_SiegeTank:        animation[Anim_SiegeTank] = loadAnimationFromWsa("HTANK.WSA");           break;
            case Anim_HeavyFactory:     animation[Anim_HeavyFactory] = loadAnimationFromWsa("HVYFTRY.WSA");      break;
            case Anim_Trooper:          animation[Anim_Trooper] = loadAnimationFromWsa("HYINFY.WSA");            break;
            case Anim_Infantry:         animation[Anim_Infantry] = loadAnimationFromWsa("INFANTRY.WSA");         break;
            case Anim_IX:               animation[Anim_IX] = loadAnimationFromWsa("IX.WSA");                     break;
            case Anim_LightFactory:     animation[Anim_LightFactory] = loadAnimationFromWsa("LITEFTRY.WSA");     break;
            case Anim_Tank:             animation[Anim_Tank] = loadAnimationFromWsa("LTANK.WSA");                break;
            case Anim_MCV:              animation[Anim_MCV] = loadAnimationFromWsa("MCV.WSA");                   break;
            case Anim_Deviator:         animation[Anim_Deviator] = loadAnimationFromWsa("ORDRTANK.WSA");         break;
            case Anim_Ornithopter:      animation[Anim_Ornithopter] = loadAnimationFromWsa("ORNI.WSA");          break;
            case Anim_Raider:           animation[Anim_Raider] = loadAnimationFromWsa("OTRIKE.WSA");             break;
            case Anim_Palace:           animation[Anim_Palace] = loadAnimationFromWsa("PALACE.WSA");             break;
            case Anim_Quad:             animation[Anim_Quad] = loadAnimationFromWsa("QUAD.WSA");                 break;
            case Anim_Refinery:         animation[Anim_Refinery] = loadAnimationFromWsa("REFINERY.WSA");         break;
            case Anim_RepairYard:       animation[Anim_RepairYard] = loadAnimationFromWsa("REPAIR.WSA");         break;
            case Anim_Launcher:         animation[Anim_Launcher] = loadAnimationFromWsa("RTANK.WSA");            break;
            case Anim_RocketTurret:     animation[Anim_RocketTurret] = loadAnimationFromWsa("RTURRET.WSA");      break;
            case Anim_Saboteur:         animation[Anim_Saboteur] = loadAnimationFromWsa("SABOTURE.WSA");         break;
            case Anim_Slab1:            animation[Anim_Slab1] = loadAnimationFromWsa("SLAB.WSA");                break;
            case Anim_SonicTank:        animation[Anim_SonicTank] = loadAnimationFromWsa("STANK.WSA");           break;
            case Anim_StarPort:         animation[Anim_StarPort] = loadAnimationFromWsa("STARPORT.WSA");         break;
            case Anim_Silo:             animation[Anim_Silo] = loadAnimationFromWsa("STORAGE.WSA");              break;
            case Anim_Trike:            animation[Anim_Trike] = loadAnimationFromWsa("TRIKE.WSA");               break;
            case Anim_GunTurret:        animation[Anim_GunTurret] = loadAnimationFromWsa("TURRET.WSA");          break;
            case Anim_Wall:             animation[Anim_Wall] = loadAnimationFromWsa("WALL.WSA");                 break;
            case Anim_WindTrap:         animation[Anim_WindTrap] = loadAnimationFromWsa("WINDTRAP.WSA");         break;
            case Anim_WOR:              animation[Anim_WOR] = loadAnimationFromWsa("WOR.WSA");                   break;
            case Anim_Sandworm:         animation[Anim_Sandworm] = loadAnimationFromWsa("WORM.WSA");             break;
            case Anim_Sardaukar:        animation[Anim_Sardaukar] = loadAnimationFromWsa("SARDUKAR.WSA");        break;
            case Anim_Frigate: {
                if(pFileManager->exists("FRIGATE.WSA")) {
                    animation[Anim_Frigate] = loadAnimationFromWsa("FRIGATE.WSA");
                } else {
                    // US-Version 1.07 does not contain FRIGATE.WSA
                    // We replace it with the starport
                    animation[Anim_Frigate] = loadAnimationFromWsa("STARPORT.WSA");
                }
            } break;
            case Anim_Slab4:            animation[Anim_Slab4] = loadAnimationFromWsa("4SLAB.WSA");               break;

            default: {
                THROW(std::runtime_error, "GFXManager::getAnimation(): Invalid animation ID %u", id);
            } break;
        }

        if(id >= Anim_Barracks && id <= Anim_Slab4) {
            animation[id]->setFrameRate(6);
        }
    }

    return animation[id].get();
}

std::unique_ptr<Shpfile> GFXManager::loadShpfile(const std::string& filename) const {
    try {
        return std::make_unique<Shpfile>(pFileManager->openFile(filename).get());
    } catch (std::exception &e) {
        THROW(std::runtime_error, "Error in file \"" + filename + "\":" + e.what());
    }
}

std::unique_ptr<Wsafile> GFXManager::loadWsafile(const std::string& filename) const {
    try {
        return std::make_unique<Wsafile>(pFileManager->openFile(filename).get());
    } catch (std::exception &e) {
        THROW(std::runtime_error, std::string("Error in file \"" + filename + "\":") + e.what());
    }
}

sdl2::texture_ptr GFXManager::extractSmallDetailPic(const std::string& filename) const
{
    sdl2::surface_ptr pSurface{ SDL_CreateRGBSurface(0, 91, 55, 8, 0, 0, 0, 0) };

    // create new picture surface
    if (pSurface == nullptr) {
        THROW(sdl_error, "Cannot create new surface: %s!", SDL_GetError());
    }

    { // Scope
        auto myWsafile = std::make_unique<Wsafile>(pFileManager->openFile(filename).get());

        sdl2::surface_ptr tmp{ myWsafile->getPicture(0) };
        if(tmp == nullptr) {
            THROW(std::runtime_error, "Cannot decode first frame in file '%s'!", filename);
        }

        if((tmp->w != 184) || (tmp->h != 112)) {
            THROW(std::runtime_error, "Picture '%s' is not of size 184x112!", filename);
        }

        palette.applyToSurface(pSurface.get());

        sdl2::surface_lock lock_out{ pSurface.get() };
        sdl2::surface_lock lock_in{ tmp.get() };

        char * RESTRICT const out = static_cast<char*>(lock_out.pixels());
        const char * RESTRICT const in = static_cast<const char*>(lock_in.pixels());

        //Now we can copy pixel by pixel
        for (auto y = 0; y < 55; y++) {
            for (auto x = 0; x < 91; x++) {
                out[y*pSurface->pitch + x] = in[((y * 2) + 1)*tmp->pitch + (x * 2) + 1];
            }
        }
    }

    sdl2::texture_ptr texture = convertSurfaceToTexture(pSurface.get());
    if(texture == nullptr) {
        SDL_Log("Warning: Failed to create texture for small detail pic '%s'", filename.c_str());
        // Return a nullptr texture instead of crashing
        return nullptr;
    }
    return texture;
}

std::unique_ptr<Animation> GFXManager::loadAnimationFromWsa(const std::string& filename) const {
    auto file = pFileManager->openFile(filename);
    auto wsafile = std::make_unique<Wsafile>(file.get());
    auto animation = wsafile->getAnimation(0,wsafile->getNumFrames() - 1,true,false);
    return animation;
}

sdl2::surface_ptr GFXManager::generateWindtrapAnimationFrames(SDL_Surface* windtrapPic) const {
    int windtrapColorQuantizizer = 255/((NUM_WINDTRAP_ANIMATIONS/2)-2);
    int frameWidth = windtrapPic->w / objPicTiles[ObjPic_Windtrap].x;
    int frameHeight = windtrapPic->h;
    int sizeX = NUM_WINDTRAP_ANIMATIONS_PER_ROW*frameWidth;
    int sizeY = ((2+NUM_WINDTRAP_ANIMATIONS+NUM_WINDTRAP_ANIMATIONS_PER_ROW-1)/NUM_WINDTRAP_ANIMATIONS_PER_ROW)*frameHeight;
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, sizeX, sizeY, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    SDL_SetSurfaceBlendMode(returnPic.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(returnPic.get(), nullptr, SDL_MapRGBA(returnPic->format, 0, 0, 0, 0));

    // copy building phase
    SDL_Rect src = { 0, 0, 2*frameWidth, frameHeight};
    SDL_Rect dest = src;
    SDL_BlitSurface(windtrapPic, &src, returnPic.get(), &dest);

    src.w = frameWidth;
    dest.x += dest.w;
    dest.w = frameWidth;

    for(int i = 0; i < NUM_WINDTRAP_ANIMATIONS; i++) {
        src.x = ((i/3) % 2 == 0) ? 2*frameWidth : 3*frameWidth;

        if(windtrapPic->format->palette) {
            SDL_Color windtrapColor;
            if(i < NUM_WINDTRAP_ANIMATIONS/2) {
                int val = i*windtrapColorQuantizizer;
                windtrapColor.r = static_cast<Uint8>(std::min(80, val));
                windtrapColor.g = static_cast<Uint8>(std::min(80, val));
                windtrapColor.b = static_cast<Uint8>(std::min(255, val));
                windtrapColor.a = 255;
            } else {
                int val = (i-NUM_WINDTRAP_ANIMATIONS/2)*windtrapColorQuantizizer;
                windtrapColor.r = static_cast<Uint8>(std::max(0, 80-val));
                windtrapColor.g = static_cast<Uint8>(std::max(0, 80-val));
                windtrapColor.b = static_cast<Uint8>(std::max(0, 255-val));
                windtrapColor.a = 255;
            }
            SDL_SetPaletteColors(windtrapPic->format->palette, &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
        }

        SDL_BlitSurface(windtrapPic, &src, returnPic.get(), &dest);

        dest.x += dest.w;
        dest.y = dest.y + dest.h * (dest.x / sizeX);
        dest.x = dest.x % sizeX;
    }

    if((returnPic->w > 2048) || (returnPic->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for windtrap is %dx%d; may exceed hardware limits on older GPUs!", returnPic->w, returnPic->h);
    }

    return returnPic;
}


sdl2::surface_ptr GFXManager::generateMapChoiceArrowFrames(SDL_Surface* arrowPic, int house) const {
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, arrowPic->w * 4, arrowPic->h, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };

    SDL_Rect dest = {0, 0, arrowPic->w, arrowPic->h};

    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 4; k++) {
            const SDL_Color color = getHouseColorSDL(house, (i + k) % 4);
            SDL_SetPaletteColors(arrowPic->format->palette, &color, 251+k, 1);
        }

        SDL_BlitSurface(arrowPic, nullptr, returnPic.get(), &dest);
        dest.x += dest.w;
    }

    return returnPic;
}

void GFXManager::loadCompactObjPicOverrides() {
    if(!ModManager::instance().isInitialized()) {
        return;
    }

    const std::string activeMod = ModManager::instance().getActiveModName();
    if(activeMod == "vanilla") {
        return;
    }

    const std::filesystem::path overrideDir =
        std::filesystem::path(ModManager::instance().getModPath(activeMod))
        / "graphics_compact" / "objpics";

    if(!std::filesystem::is_directory(overrideDir)) {
        return;
    }

    for(unsigned int id = 0; id < NUM_OBJPICS; ++id) {
        const std::filesystem::path preferredPath = overrideDir / ("ObjPic_" + ObjPicNames[id] + ".png");
        const std::filesystem::path legacyPath = overrideDir / (ObjPicNames[id] + ".png");
        std::filesystem::path overridePath;

        if(std::filesystem::is_regular_file(preferredPath)) {
            overridePath = preferredPath;
        } else if(std::filesystem::is_regular_file(legacyPath)) {
            overridePath = legacyPath;
        } else {
            continue;
        }

        auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(overridePath.string().c_str(), "rb") };
        if(!rwops) {
            SDL_Log("GFXManager: Failed to open compact override %s: %s",
                    overridePath.string().c_str(), SDL_GetError());
            continue;
        }

        auto raw = LoadPNG_RW(rwops.get());
        if(!raw) {
            SDL_Log("GFXManager: Failed to load compact override %s",
                    overridePath.string().c_str());
            continue;
        }

        const int expectedW = objPicTiles[id].x;
        const int expectedH = objPicTiles[id].y;
        if(expectedW <= 0 || expectedH <= 0 || raw->w % expectedW != 0 || raw->h % expectedH != 0) {
            SDL_Log("GFXManager: Skipping compact override %s; size %dx%d does not match ObjPic_%s layout %dx%d",
                    overridePath.string().c_str(), raw->w, raw->h,
                    ObjPicNames[id].c_str(), expectedW, expectedH);
            continue;
        }

        sdl2::surface_ptr surface;
        if(raw->format->BytesPerPixel >= 3) {
            surface = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0) };
        } else {
            surface = sdl2::surface_ptr{ SDL_ConvertSurface(raw.get(), raw->format, 0) };
        }

        if(!surface) {
            SDL_Log("GFXManager: Failed to convert compact override %s: %s",
                    overridePath.string().c_str(), SDL_GetError());
            continue;
        }

        for(int h = 0; h < (int)NUM_HOUSES; ++h) {
            objPic[id][h][0].reset();
            objPic[id][h][1].reset();
            objPic[id][h][2].reset();
            objPicTex[id][h][0].reset();
            objPicTex[id][h][1].reset();
            objPicTex[id][h][2].reset();
        }

        objPic[id][HOUSE_HARKONNEN][0] = std::move(surface);

        for(int h = 1; h < (int)NUM_HOUSES; ++h) {
            objPic[id][h][0] = sdl2::surface_ptr{
                SDL_ConvertSurface(objPic[id][HOUSE_HARKONNEN][0].get(),
                                   objPic[id][HOUSE_HARKONNEN][0]->format, 0)
            };
        }

        SDL_Log("GFXManager: Loaded compact override ObjPic_%s from %s",
                ObjPicNames[id].c_str(), overridePath.string().c_str());
    }
}

bool GFXManager::loadHDObjPicOverride(unsigned int id) {
    if(id >= NUM_OBJPICS) {
        return false;
    }

    auto& hd = hdObjPicOverrides[id];
    if(hd.attempted) {
        return hd.loaded;
    }
    hd.attempted = true;

    if(!ModManager::instance().isInitialized()) {
        return false;
    }

    const std::string activeMod = ModManager::instance().getActiveModName();
    if(activeMod == "vanilla") {
        return false;
    }

    const std::filesystem::path overrideDir =
        std::filesystem::path(ModManager::instance().getModPath(activeMod))
        / "graphics_hd" / "objpics";

    const std::filesystem::path preferredPng = overrideDir / ("ObjPic_" + ObjPicNames[id] + ".png");
    const std::filesystem::path legacyPng = overrideDir / (ObjPicNames[id] + ".png");
    std::filesystem::path pngPath;

    if(std::filesystem::is_regular_file(preferredPng)) {
        pngPath = preferredPng;
    } else if(std::filesystem::is_regular_file(legacyPng)) {
        pngPath = legacyPng;
    } else {
        return false;
    }

    std::filesystem::path metadataPath = pngPath;
    metadataPath.replace_extension(".ini");
    if(std::filesystem::is_regular_file(metadataPath)) {
        try {
            INIFile metadata(metadataPath.string());
            hd.columns = std::max(1, metadata.getIntValue("Sprite", "Columns", 1));
            hd.rows = std::max(1, metadata.getIntValue("Sprite", "Rows", 1));
            hd.anchorX = metadata.getIntValue("Sprite", "AnchorX", -1);
            hd.anchorY = metadata.getIntValue("Sprite", "AnchorY", -1);
            hd.baseWidth = metadata.getIntValue("Render", "BaseWidth", 0);
            hd.baseHeight = metadata.getIntValue("Render", "BaseHeight", 0);
            hd.scale = metadata.getDoubleValue("Render", "Scale", 1.0);
            if(hd.scale <= 0.0) {
                hd.scale = 1.0;
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read HD override metadata %s: %s",
                    metadataPath.string().c_str(), e.what());
        }
    }

    auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(pngPath.string().c_str(), "rb") };
    if(!rwops) {
        SDL_Log("GFXManager: Failed to open HD override %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    auto raw = LoadPNG_RW(rwops.get());
    if(!raw) {
        SDL_Log("GFXManager: Failed to load HD override %s",
                pngPath.string().c_str());
        return false;
    }

    auto surface = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0) };
    if(!surface) {
        SDL_Log("GFXManager: Failed to convert HD override %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    if(surface->w % hd.columns != 0 || surface->h % hd.rows != 0) {
        SDL_Log("GFXManager: Skipping HD override %s; size %dx%d does not divide into %dx%d frames",
                pngPath.string().c_str(), surface->w, surface->h, hd.columns, hd.rows);
        return false;
    }

    hd.texture[HOUSE_HARKONNEN] = convertSurfaceToTexture(surface.get());
    if(!hd.texture[HOUSE_HARKONNEN]) {
        SDL_Log("GFXManager: Failed to create HD override texture %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    for(int h = 1; h < (int)NUM_HOUSES; ++h) {
        hd.texture[h] = convertSurfaceToTexture(surface.get());
    }

    hd.loaded = true;
    SDL_Log("GFXManager: Loaded HD override ObjPic_%s from %s",
            ObjPicNames[id].c_str(), pngPath.string().c_str());
    return true;
}

bool GFXManager::drawHDObjPic(unsigned int id, int house, unsigned int z,
                              int col, int numCols, int row, int numRows,
                              int x, int y) {
    if(id >= NUM_OBJPICS || z >= NUM_ZOOMLEVEL || house < 0 || house >= (int)NUM_HOUSES) {
        return false;
    }

    if(!loadHDObjPicOverride(id)) {
        return false;
    }

    auto& hd = hdObjPicOverrides[id];
    SDL_Texture* texture = hd.texture[house] ? hd.texture[house].get() : hd.texture[HOUSE_HARKONNEN].get();
    if(texture == nullptr) {
        return false;
    }

    const int columns = std::max(1, hd.columns);
    const int rows = std::max(1, hd.rows);
    const int textureW = getWidth(texture);
    const int textureH = getHeight(texture);
    const int frameW = textureW / columns;
    const int frameH = textureH / rows;
    if(frameW <= 0 || frameH <= 0) {
        return false;
    }

    const int srcCol = std::clamp(col, 0, columns - 1);
    const int srcRow = std::clamp(row, 0, rows - 1);
    SDL_Rect source = { srcCol * frameW, srcRow * frameH, frameW, frameH };

    int destW = hd.baseWidth > 0 ? hd.baseWidth * (int)(z + 1) : 0;
    int destH = hd.baseHeight > 0 ? hd.baseHeight * (int)(z + 1) : 0;
    if(destW <= 0 || destH <= 0) {
        SDL_Texture* classicTexture = objPicTex[id][house][z] ? objPicTex[id][house][z].get() : objPicTex[id][HOUSE_HARKONNEN][z].get();
        if(classicTexture != nullptr) {
            destW = getWidth(classicTexture) / std::max(1, numCols);
            destH = getHeight(classicTexture) / std::max(1, numRows);
        } else {
            destW = frameW;
            destH = frameH;
        }
    }

    destW = std::max(1, static_cast<int>(lround(destW * hd.scale)));
    destH = std::max(1, static_cast<int>(lround(destH * hd.scale)));

    const int anchorX = hd.anchorX >= 0 ? hd.anchorX : frameW / 2;
    const int anchorY = hd.anchorY >= 0 ? hd.anchorY : frameH / 2;
    const double scaleX = static_cast<double>(destW) / static_cast<double>(frameW);
    const double scaleY = static_cast<double>(destH) / static_cast<double>(frameH);

    SDL_Rect dest = {
        x - static_cast<int>(lround(anchorX * scaleX)),
        y - static_cast<int>(lround(anchorY * scaleY)),
        destW,
        destH
    };

    SDL_RenderCopy(renderer, texture, &source, &dest);
    return true;
}

sdl2::surface_ptr GFXManager::generateDoubledObjPic(unsigned int id, int h) const {
    sdl2::surface_ptr pSurface;
    if(objPic[id][h][0] && objPic[id][h][0]->format->BytesPerPixel != 1) {
        pSurface = scaleSurfaceNearest(objPic[id][h][0].get(), 2);
        if((pSurface->w > 2048) || (pSurface->h > 2048)) {
            SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 1 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
        }
        return pSurface;
    }

    std::string filename = "Mask_2x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::doubleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
            SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
        } else {
            SDL_Log("Warning: No HD sprite sheet for '%s' in zoom level 1!", ObjPicNames.at(id).c_str());
            pSurface = sdl2::surface_ptr{ Scaler::defaultDoubleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
        }
    } else {
        pSurface = sdl2::surface_ptr{ Scaler::defaultDoubleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
    }

    if((pSurface->w > 2048) || (pSurface->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 1 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
    }

    return pSurface;
}

sdl2::surface_ptr GFXManager::generateTripledObjPic(unsigned int id, int h) const {
    sdl2::surface_ptr pSurface;
    if(objPic[id][h][0] && objPic[id][h][0]->format->BytesPerPixel != 1) {
        pSurface = scaleSurfaceNearest(objPic[id][h][0].get(), 3);
        if((pSurface->w > 2048) || (pSurface->h > 2048)) {
            SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 2 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
        }
        return pSurface;
    }

    const std::string filename = "Mask_3x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::tripleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
            SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
        } else {
            SDL_Log("Warning: No HD sprite sheet for '%s' in zoom level 2!", ObjPicNames.at(id).c_str());
            pSurface = sdl2::surface_ptr{ Scaler::defaultTripleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
        }
    } else {
        pSurface = sdl2::surface_ptr{ Scaler::defaultTripleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
    }


    if((pSurface->w > 2048) || (pSurface->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 2 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
    }

    return pSurface;
}
