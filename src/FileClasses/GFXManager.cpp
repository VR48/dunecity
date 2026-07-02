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

#include <algorithm>
#include <cstdlib>

/**
    DuneCity: shift a paletted sprite's cool/neutral body tones toward red while
    preserving shading, the dark outline/shadow pixels, and the Harkonnen
    house-colour stripe (so per-house team-colour remapping in
    getZoomedObjPic still works).  Used to derive the red Rocket Trike and the
    red Neutral Launcher from the stock blue-grey body art.  Only palette RGB
    values are rewritten, never pixel indices.
*/
static void tintSurfaceRed(SDL_Surface* surface) {
    SDL_Palette* pal = (surface != nullptr) ? surface->format->palette : nullptr;
    if(pal == nullptr) {
        return;
    }

    for(int i = 0; i < pal->ncolors; i++) {
        // Leave the Harkonnen house-colour stripe untouched so team colours
        // survive the per-house remap.
        if(i >= PALCOLOR_HARKONNEN && i < PALCOLOR_HARKONNEN + 7) {
            continue;
        }

        const SDL_Color c = pal->colors[i];
        const int maxc = (c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b));
        const int minc = (c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b));
        const bool nearGrey = (maxc - minc) <= 32;
        const bool coolish  = (c.b >= c.r) && (c.b >= c.g);

        // Skip near-black outline/shadow pixels so the silhouette and shading
        // stay intact; recolour everything else that reads as a cool/neutral
        // body tone.
        if(maxc > 24 && (nearGrey || coolish)) {
            const int lum = (c.r * 30 + c.g * 59 + c.b * 11) / 100;
            SDL_Color red;
            int r = lum + lum / 2;          // boost red, preserve shading
            red.r = static_cast<Uint8>(r > 255 ? 255 : r);
            red.g = static_cast<Uint8>(lum / 6);
            red.b = static_cast<Uint8>(lum / 7);
            red.a = c.a;
            SDL_SetPaletteColors(pal, &red, i, 1);
        }
    }
}

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
    { 8, 1 },   // ObjPic_Quad
    { 8, 1 },   // ObjPic_Trike
    { 8, 1 },   // ObjPic_RocketTrike
    { 8, 1 },   // ObjPic_LauncherRed_Base
    { 8, 1 },   // ObjPic_LauncherRed_Gun
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
    { 14, 1 },  // ObjPic_DestroyedStructure
    { 6, 1 },   // ObjPic_RockDamage
    { 3, 1 },   // ObjPic_SandDamage
    { 16, 1 },  // ObjPic_Terrain_Hidden
    { 16, 1 },  // ObjPic_Terrain_HiddenFog
    { 8, 1 },   // ObjPic_Terrain_Tracks
    { 1, 1 },   // ObjPic_Star
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
    { NUM_WINDTRAP_ANIMATIONS_PER_ROW, (2+NUM_WINDTRAP_ANIMATIONS+NUM_WINDTRAP_ANIMATIONS_PER_ROW-1)/NUM_WINDTRAP_ANIMATIONS_PER_ROW },   // ObjPic_AdvancedWindTrap (windtrap-style animation atlas)
    { 2, 1 },   // ObjPic_CornerFlag (2 animation frames from Tornie_CornerFlagNew.png; 7x7 each)
    { 8, 1 },   // ObjPic_FlameTank (8-dir palette-indexed strip from Tornie.PAK)
    { 8, 1 },   // ObjPic_DeviatorCustom (8-dir palette-indexed strip from Tornie.PAK)
    { 8, 1 },   // ObjPic_EliteSiegeTankCustom (8-dir palette-indexed strip from Tornie.PAK)
    { 17, 2 },  // ObjPic_TerrainRedSpice (17 cols × 2 rows: thin spice + thick/bloom)
    { 17, 2 },  // ObjPic_TerrainGreenSpice (17 cols × 2 rows: thin spice + thick/bloom)
};


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

    // Tornie mod: load Custom_IBM.pal to replace palette entries 192-199 with rebels grey
    if (ModManager::instance().getActiveModName() == "Tornie" && pFileManager->exists("Custom_IBM.pal")) {
        auto palRw = pFileManager->openFile("Custom_IBM.pal");
        std::vector<Uint8> palData(768);
        SDL_RWread(palRw.get(), palData.data(), 1, 768);
        // Apply entries 192-199 (6-bit VGA → 8-bit)
        for (int i = 192; i < 200; ++i) {
            SDL_Color c;
            c.r = static_cast<Uint8>(std::min(255, static_cast<int>(palData[i*3+0]) * 4));
            c.g = static_cast<Uint8>(std::min(255, static_cast<int>(palData[i*3+1]) * 4));
            c.b = static_cast<Uint8>(std::min(255, static_cast<int>(palData[i*3+2]) * 4));
            c.a = 255;
            palette[i] = c;
        }
        SDL_Log("GFX INIT: Tornie Custom_IBM.pal applied (rebels grey range 192-199)");
    }

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

    // DuneCity: the Rocket Trike in-world sprite.
    // Preferred path: RocketTrikeMask.png as 8-bit palette-indexed strip — load
    // into HOUSE_HARKONNEN only; getZoomedObjPic remaps palette indices 144-150
    // per house (same approach as regular Trike).
    // Fallback: RocketTrike.png as truecolor RGBA — pre-build all zoom/house
    // slots so getZoomedObjPic never palette-remaps RGBA data.
    // Final fallback: SHP from the base game data.
    objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    {
        bool usedPaletteIndexed = false;
        if(pFileManager->exists("RocketTrikeMask.png")) {
            auto rtMask = LoadPNG_RW(pFileManager->openFile("RocketTrikeMask.png").get());
            if(rtMask && rtMask->format->BitsPerPixel == 8 && rtMask->format->palette) {
                benePalette.applyToSurface(rtMask.get());
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
    }

    // DuneCity: Flame Tank (Tornie mod) — palette-indexed 8-frame strip from Tornie.PAK.
    // House tinting uses luminance-based remap: each red palette index is remapped to
    // the house anchor colour scaled by relative luminance.
    if(pFileManager->exists("FlameTank.png")) {
        auto ftRaw = LoadPNG_RW(pFileManager->openFile("FlameTank.png").get());
        if(ftRaw) {
            SDL_Surface* raw = ftRaw.get();
            if(raw->format->palette) {
                // Red palette indices in the source asset (Harkonnen base colour)
                static const int redIdx[] = {58,121,139,140,141,144,145,146,147,148,199,200,201,202,203,231,243,244,245,246,247};
                static const int numRedIdx = 21;
                // Per-house bright anchor RGB (H=red, A=blue, O=green, F=orange, S=purple, M=gold)
                static const int houseAnchor[NUM_HOUSES][3] = {
                    {212,  0,  0}, {  0, 80,212}, {  0,180,  0},
                    {212,140,  0}, {120,  0,200}, {200,160,  0}, {128,128,128},
                    { 96, 96, 96}  // HOUSE_REBELS: dark grey
                };

                SDL_Palette* srcPal = raw->format->palette;
                float redLum[21], minLum = 1e9f, maxLum = 0.f;
                for(int i = 0; i < numRedIdx; i++) {
                    if(redIdx[i] < srcPal->ncolors) {
                        SDL_Color& c = srcPal->colors[redIdx[i]];
                        redLum[i] = c.r*0.299f + c.g*0.587f + c.b*0.114f;
                        if(redLum[i] < minLum) minLum = redLum[i];
                        if(redLum[i] > maxLum) maxLum = redLum[i];
                    }
                }
                float lumRange = (maxLum - minLum) > 0.f ? (maxLum - minLum) : 1.f;

                for(int h = 0; h < (int)NUM_HOUSES; h++) {
                    sdl2::surface_ptr copy{SDL_ConvertSurface(raw, raw->format, 0)};
                    if(!copy) continue;
                    SDL_Palette* pal = copy->format->palette;
                    if(!pal) continue;

                    float hr = houseAnchor[h][0], hg = houseAnchor[h][1], hb = houseAnchor[h][2];
                    for(int i = 0; i < numRedIdx; i++) {
                        if(redIdx[i] >= pal->ncolors) continue;
                        float t = 0.25f + 0.75f * (redLum[i] - minLum) / lumRange;
                        SDL_Color& col = pal->colors[redIdx[i]];
                        col.r = (Uint8)std::min(255, (int)(hr * t));
                        col.g = (Uint8)std::min(255, (int)(hg * t));
                        col.b = (Uint8)std::min(255, (int)(hb * t));
                    }

                    objPic[ObjPic_FlameTank][h][0] = std::move(copy);

                    // Convert palette surface to RGBA and make black pixels
                    // (palette index 2 = background) fully transparent.
                    SDL_Surface* palSurf = objPic[ObjPic_FlameTank][h][0].get();
                    if(palSurf) {
                        sdl2::surface_ptr rgba{ SDL_ConvertSurfaceFormat(palSurf, SDL_PIXELFORMAT_RGBA32, 0) };
                        if(rgba) {
                            SDL_LockSurface(rgba.get());
                            Uint32* pix = static_cast<Uint32*>(rgba->pixels);
                            for(int i = 0; i < rgba->w * rgba->h; ++i) {
                                Uint8 r, g, b, a;
                                SDL_GetRGBA(pix[i], rgba->format, &r, &g, &b, &a);
                                if(r == 0 && g == 0 && b == 0) {
                                    pix[i] = SDL_MapRGBA(rgba->format, 0, 0, 0, 0);
                                }
                            }
                            SDL_UnlockSurface(rgba.get());
                            objPic[ObjPic_FlameTank][h][0] = std::move(rgba);
                        }
                    }

                    // Reorder frames: FlameTank.png uses Dune II convention
                    // (DOWN=0, RIGHTUP=1, RIGHT=2, RIGHTDOWN=3, UP=4, LEFTDOWN=5, LEFT=6, LEFTUP=7)
                    // → Dune Legacy convention
                    // (RIGHT=0, RIGHTUP=1, UP=2, LEFTUP=3, LEFT=4, LEFTDOWN=5, DOWN=6, RIGHTDOWN=7)
                    {
                        static const int d2toDL[] = {2, 1, 4, 7, 6, 5, 0, 3};
                        SDL_Surface* src = objPic[ObjPic_FlameTank][h][0].get();
                        if (src) {
                            int frameW = src->w / 8;
                            int frameH = src->h;
                            sdl2::surface_ptr reordered{ SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32) };
                            if (reordered) {
                                SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
                                for (int i = 0; i < 8; i++) {
                                    SDL_Rect srcRect{d2toDL[i] * frameW, 0, frameW, frameH};
                                    SDL_Rect dstRect{i * frameW, 0, frameW, frameH};
                                    SDL_BlitSurface(src, &srcRect, reordered.get(), &dstRect);
                                }
                                objPic[ObjPic_FlameTank][h][0] = std::move(reordered);
                            }
                        }
                    }

                    // Scale RGBA surface for zoom levels
                    for(int z = 1; z < NUM_ZOOMLEVEL; z++) {
                        SDL_Surface* src0 = objPic[ObjPic_FlameTank][h][0].get();
                        if(!src0) continue;
                        sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                            src0->w * (z+1), src0->h * (z+1),
                            src0->format->BitsPerPixel,
                            src0->format->Rmask, src0->format->Gmask,
                            src0->format->Bmask, src0->format->Amask) };
                        if(dst) {
                            SDL_BlitScaled(src0, nullptr, dst.get(), nullptr);
                            objPic[ObjPic_FlameTank][h][z] = std::move(dst);
                        }
                    }
                }
            }
        }
        SDL_Log("GFXManager: FlameTank sprite loaded OK");
    }

    // DuneCity: Tornie Deviator custom sprite — palette-indexed 8-frame strip.
    // Loaded into HOUSE_HARKONNEN only; the house-tinting loop at the end of
    // getZoomedObjPic() remaps palette indices 144-150 for all other houses.
    if(ModManager::instance().getActiveModName() == "Tornie"
       && pFileManager->exists("Tornie_Deviator.png")) {
        auto devRaw = LoadPNG_RW(pFileManager->openFile("Tornie_Deviator.png").get());
        if(devRaw && devRaw->format->BitsPerPixel == 8) {
            benePalette.applyToSurface(devRaw.get());
            objPic[ObjPic_DeviatorCustom][HOUSE_HARKONNEN][0] = std::move(devRaw);
            for(int z = 1; z < NUM_ZOOMLEVEL; z++) {
                SDL_Surface* src0 = objPic[ObjPic_DeviatorCustom][HOUSE_HARKONNEN][0].get();
                if(!src0) continue;
                sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                    src0->w * (z+1), src0->h * (z+1),
                    src0->format->BitsPerPixel,
                    src0->format->Rmask, src0->format->Gmask,
                    src0->format->Bmask, src0->format->Amask) };
                if(dst) {
                    SDL_BlitScaled(src0, nullptr, dst.get(), nullptr);
                    objPic[ObjPic_DeviatorCustom][HOUSE_HARKONNEN][z] = std::move(dst);
                }
            }
            SDL_Log("GFXManager: Loaded Tornie_Deviator.png (palette-indexed)");
        } else if(devRaw) {
            SDL_Log("GFXManager WARNING: Tornie_Deviator.png is not 8-bit palette-indexed (%d bpp), skipped",
                    devRaw->format->BitsPerPixel);
        }
    }

    // DuneCity: Tornie Elite Siege Tank custom sprite — palette-indexed 8-frame strip.
    if(pFileManager->exists("Tornie_EliteSiegeTank.png")) {
        auto estRaw = LoadPNG_RW(pFileManager->openFile("Tornie_EliteSiegeTank.png").get());
        if(estRaw && estRaw->format->BitsPerPixel == 8) {
            benePalette.applyToSurface(estRaw.get());
            objPic[ObjPic_EliteSiegeTankCustom][HOUSE_HARKONNEN][0] = std::move(estRaw);
            SDL_Log("GFXManager: Loaded Tornie_EliteSiegeTank.png (palette-indexed)");
        } else if(estRaw) {
            SDL_Log("GFXManager WARNING: Tornie_EliteSiegeTank.png is not 8-bit palette-indexed (%d bpp), skipped",
                    estRaw->format->BitsPerPixel);
        }
    }

    // DuneCity: Tornie custom spice terrain strips — palette-indexed 17×2 tile grids.
    // Terrain is not house-tinted, so copy to all houses to prevent mapSurfaceColorRange.
    if(pFileManager->exists("Tornie_SpiceRed.png")) {
        auto surf = LoadPNG_RW(pFileManager->openFile("Tornie_SpiceRed.png").get());
        if(surf && surf->format->BitsPerPixel == 8) {
            benePalette.applyToSurface(surf.get());
            objPic[ObjPic_TerrainRedSpice][HOUSE_HARKONNEN][0] = std::move(surf);
            for (int h = 1; h < NUM_HOUSES; h++) {
                objPic[ObjPic_TerrainRedSpice][h][0] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[ObjPic_TerrainRedSpice][HOUSE_HARKONNEN][0].get(),
                                       objPic[ObjPic_TerrainRedSpice][HOUSE_HARKONNEN][0]->format, 0)
                };
            }
            SDL_Log("GFXManager: Loaded Tornie_SpiceRed.png (palette-indexed)");
        } else if(surf) {
            SDL_Log("GFXManager WARNING: Tornie_SpiceRed.png is not 8-bit palette-indexed (%d bpp), skipped",
                    surf->format->BitsPerPixel);
        }
    }
    if(pFileManager->exists("Tornie_SpiceGreen.png")) {
        auto surf = LoadPNG_RW(pFileManager->openFile("Tornie_SpiceGreen.png").get());
        if(surf && surf->format->BitsPerPixel == 8) {
            benePalette.applyToSurface(surf.get());
            objPic[ObjPic_TerrainGreenSpice][HOUSE_HARKONNEN][0] = std::move(surf);
            for (int h = 1; h < NUM_HOUSES; h++) {
                objPic[ObjPic_TerrainGreenSpice][h][0] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[ObjPic_TerrainGreenSpice][HOUSE_HARKONNEN][0].get(),
                                       objPic[ObjPic_TerrainGreenSpice][HOUSE_HARKONNEN][0]->format, 0)
                };
            }
            SDL_Log("GFXManager: Loaded Tornie_SpiceGreen.png (palette-indexed)");
        } else if(surf) {
            SDL_Log("GFXManager WARNING: Tornie_SpiceGreen.png is not 8-bit palette-indexed (%d bpp), skipped",
                    surf->format->BitsPerPixel);
        }
    }

    // DuneCity: the Neutral Launcher reuses the standard Launcher body (tank
    // base) and rocket turret art, but red-tinted instead of blue-grey.  Load
    // independent copies of both pieces (getPictureArray allocates fresh
    // surfaces with their own palettes, so the regular all-house Launcher,
    // Tank and other tank-base units stay untouched) and tint them with the
    // same palette remap used for the Rocket Trike.
    objPic[ObjPic_LauncherRed_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(0));
    tintSurfaceRed(objPic[ObjPic_LauncherRed_Base][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_LauncherRed_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(35));
    tintSurfaceRed(objPic[ObjPic_LauncherRed_Gun][HOUSE_HARKONNEN][0].get());

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
        // ----- DuneCity advanced wind trap (super power plant) sprite -----
        //
        // Tornie's custom super_power_plant.png art. 3x3 gameplay footprint,
        // no real animation — we build a 3-frame horizontal atlas (all
        // identical) so StructureBase animation indexing has somewhere to
        // land, and the building shows its own sprite statically. Same
        // approach as the nuclear plant above.
        {
            std::vector<std::string> superDirs = {
                getDuneLegacyDataDir(),
                binDir,
                binDir + "../../",
                binDir + "../../../../../",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string srcDir = srcDirEnv;
                if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
                superDirs.push_back(srcDir);
            }

            const int frameW = 3 * D2_TILESIZE;
            const int frameH = 3 * D2_TILESIZE;
            const int numFrames = 3;
            const int atlasW = numFrames * frameW;
            const int atlasH = frameH;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, atlasW, atlasH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (atlas) {
                SDL_FillRect(atlas.get(), nullptr, SDL_MapRGBA(atlas->format, 0, 0, 0, 0));
            }

            sdl2::surface_ptr superSrc;
            // Prefer Tornie's custom gfx (single 48x48 frame)
            if (pFileManager->exists("Tornie_AdvancedWindtrap_gfx.png")) {
                auto gfxSurf = LoadPNG_RW(pFileManager->openFile("Tornie_AdvancedWindtrap_gfx.png").get());
                if (gfxSurf) {
                    sdl2::surface_ptr converted{ SDL_ConvertSurfaceFormat(gfxSurf.get(), SDL_PIXELFORMAT_RGBA32, 0) };
                    superSrc = converted ? std::move(converted) : std::move(gfxSurf);
                    SDL_Log("Loaded AdvancedWindtrap gfx from Tornie_AdvancedWindtrap_gfx.png");
                }
            }
            if (!superSrc) for (const auto& dir : superDirs) {
                std::string path = dir + "super_power_plant.png";
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                if (rwops) {
                    superSrc = LoadPNG_RW(rwops.get());
                    if (superSrc) {
                        // Convert RGB→RGBA so the blit sets alpha=255 instead of leaving it 0
                        sdl2::surface_ptr converted{SDL_ConvertSurfaceFormat(superSrc.get(), SDL_PIXELFORMAT_RGBA32, 0)};
                        if (converted) superSrc = std::move(converted);
                        SDL_Log("Loaded super power plant sprite from: %s", path.c_str());
                        break;
                    }
                }
            }

            if (atlas && superSrc) {
                // Scale the source to fill the full 48×48 frame so the
                // advanced wind trap occupies its entire 3×3 gameplay footprint.
                SDL_SetSurfaceBlendMode(superSrc.get(), SDL_BLENDMODE_NONE);
                for (int f = 0; f < numFrames; ++f) {
                    SDL_Rect frameDst = { f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(superSrc.get(), nullptr, atlas.get(), &frameDst);
                }

                objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 3);

                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_AdvancedWindTrap][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }

                // Per-house corner flag: blit 2×2 Tornie_AdvHouseFlag.png at (0,0)
                // with palette index 147 (PALCOLOR_HARKONNEN+3) recoloured to each
                // house's own accent colour (houseToPaletteIndex[h]+3).
                if (pFileManager->exists("Tornie_AdvHouseFlag.png")) {
                    auto flagSrc = LoadPNG_RW(pFileManager->openFile("Tornie_AdvHouseFlag.png").get());
                    if (flagSrc && flagSrc->format->BitsPerPixel == 8) {
                        for (int h = 0; h < NUM_HOUSES; h++) {
                            if (!objPic[ObjPic_AdvancedWindTrap][h][0]) continue;
                            // Clone flag surface and substitute palette entries for this house
                            sdl2::surface_ptr flagCopy{ SDL_ConvertSurface(flagSrc.get(), flagSrc->format, 0) };
                            if (flagCopy && flagCopy->format->palette) {
                                // Remap dark Harkonnen slot (index 147 = PALCOLOR_HARKONNEN+3)
                                // For Fremen, source colours from vanilla IBM.PAL (ibmPalette) so Custom_IBM.pal rebels grey doesn't bleed in
                                const Palette& srcPal = (h == HOUSE_FREMEN) ? ibmPalette : palette;
                                flagCopy->format->palette->colors[147] = srcPal[houseToPaletteIndex[h] + 3];
                                // Remap highlight slot (index 51) to lighter house tone (+1)
                                flagCopy->format->palette->colors[51] = srcPal[houseToPaletteIndex[h] + 1];
                            }
                            sdl2::surface_ptr flagRGBA{ SDL_ConvertSurfaceFormat(flagCopy.get(), SDL_PIXELFORMAT_RGBA32, 0) };
                            if (flagRGBA) {
                                // Fill flag body with #65654c base color under house colours
                                {
                                    SDL_LockSurface(flagRGBA.get());
                                    const Uint32 bodyColor = SDL_MapRGBA(flagRGBA->format, 101, 101, 76, 255);
                                    const int pixelCount = flagRGBA->w * flagRGBA->h;
                                    auto* pixels = static_cast<Uint32*>(flagRGBA->pixels);
                                    for (int i = 0; i < pixelCount; i++) {
                                        Uint8 r, g, b, a;
                                        SDL_GetRGBA(pixels[i], flagRGBA->format, &r, &g, &b, &a);
                                        // Fill opaque non-house-colour pixels with the base colour
                                        if (a > 0) {
                                            // Preserve house-remapped pixels (dark=index 147, highlight=index 51)
                                            // by checking if this pixel matches a house palette entry
                                            const SDL_Color& dark = srcPal[houseToPaletteIndex[h] + 3];
                                            const SDL_Color& light = srcPal[houseToPaletteIndex[h] + 1];
                                            if ((r == dark.r && g == dark.g && b == dark.b) ||
                                                (r == light.r && g == light.g && b == light.b)) {
                                                continue; // keep house colours
                                            }
                                            pixels[i] = bodyColor;
                                        }
                                    }
                                    SDL_UnlockSurface(flagRGBA.get());
                                }
                                SDL_SetSurfaceBlendMode(flagRGBA.get(), SDL_BLENDMODE_BLEND);
                                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                    if (objPic[ObjPic_AdvancedWindTrap][h][z]) {
                                        int flagSize = (z + 1) * 2; // z=0→2px, z=1→4px, z=2→6px
                                        sdl2::surface_ptr flagScaled{ SDL_CreateRGBSurfaceWithFormat(0, flagSize, flagSize,
                                            32, SDL_PIXELFORMAT_RGBA32) };
                                        if (flagScaled) {
                                            SDL_SetSurfaceBlendMode(flagScaled.get(), SDL_BLENDMODE_NONE);
                                            SDL_BlitScaled(flagRGBA.get(), nullptr, flagScaled.get(), nullptr);
                                            SDL_SetSurfaceBlendMode(flagScaled.get(), SDL_BLENDMODE_BLEND);
                                            SDL_Rect dst{0, 0, flagSize, flagSize};
                                            SDL_BlitSurface(flagScaled.get(), nullptr, objPic[ObjPic_AdvancedWindTrap][h][z].get(), &dst);
                                        }
                                    }
                                }
                            }
                        }
                    } else if (flagSrc) {
                        // RGBA flag PNG — blit directly with blend mode, no palette remapping
                        sdl2::surface_ptr flagRGBA{ SDL_ConvertSurfaceFormat(flagSrc.get(), SDL_PIXELFORMAT_RGBA32, 0) };
                        if (flagRGBA) {
                            // Fill flag body with #65654c base color
                            {
                                SDL_LockSurface(flagRGBA.get());
                                const Uint32 bodyColor = SDL_MapRGBA(flagRGBA->format, 101, 101, 76, 255);
                                const int pixelCount = flagRGBA->w * flagRGBA->h;
                                auto* pixels = static_cast<Uint32*>(flagRGBA->pixels);
                                for (int i = 0; i < pixelCount; i++) {
                                    Uint8 r, g, b, a;
                                    SDL_GetRGBA(pixels[i], flagRGBA->format, &r, &g, &b, &a);
                                    if (a > 0) {
                                        pixels[i] = bodyColor;
                                    }
                                }
                                SDL_UnlockSurface(flagRGBA.get());
                            }
                            SDL_SetSurfaceBlendMode(flagRGBA.get(), SDL_BLENDMODE_BLEND);
                            for (int h = 0; h < NUM_HOUSES; h++) {
                                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                    if (objPic[ObjPic_AdvancedWindTrap][h][z]) {
                                        int flagSize = (z + 1) * 2;
                                        sdl2::surface_ptr flagScaled{ SDL_CreateRGBSurfaceWithFormat(0, flagSize, flagSize,
                                            32, SDL_PIXELFORMAT_RGBA32) };
                                        if (flagScaled) {
                                            SDL_SetSurfaceBlendMode(flagScaled.get(), SDL_BLENDMODE_NONE);
                                            SDL_BlitScaled(flagRGBA.get(), nullptr, flagScaled.get(), nullptr);
                                            SDL_SetSurfaceBlendMode(flagScaled.get(), SDL_BLENDMODE_BLEND);
                                            SDL_Rect dst{0, 0, flagSize, flagSize};
                                            SDL_BlitSurface(flagScaled.get(), nullptr, objPic[ObjPic_AdvancedWindTrap][h][z].get(), &dst);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Zone-colour corner markers removed — replaced by animated ObjPic_CornerFlag
                // drawn at runtime in AdvancedWindTrap::blitToScreen().

            } else {
                SDL_Log("Super power plant sprite not found; AdvancedWindTrap will fall back to Windtrap art");
                SDL_Surface* fallback = objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get();
                if (atlas && fallback) {
                    const int fallbackFrames = objPicTiles[ObjPic_Windtrap].x;
                    const int fallbackFrameW = fallback->w / fallbackFrames;
                    const int fallbackFrameH = fallback->h / objPicTiles[ObjPic_Windtrap].y;

                    SDL_SetSurfaceBlendMode(fallback, SDL_BLENDMODE_NONE);
                    for (int f = 0; f < numFrames; ++f) {
                        const int sourceFrame = f % fallbackFrames;
                        SDL_Rect src{ sourceFrame * fallbackFrameW, 0, fallbackFrameW, fallbackFrameH };
                        SDL_Rect dst{ f * frameW, 0, frameW, frameH };
                        SDL_BlitScaled(fallback, &src, atlas.get(), &dst);
                    }

                    objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0] = std::move(atlas);
                    objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 3);

                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_AdvancedWindTrap][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
                // Last resort: if fallback sprite was also null, fill atlas
                // with a debug placeholder so objPic is never null.
                if (!objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0]) {
                    sdl2::surface_ptr ph{ SDL_CreateRGBSurface(0, atlasW, atlasH,
                        SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                    if (ph) {
                        SDL_FillRect(ph.get(), nullptr, SDL_MapRGBA(ph->format, 100, 100, 180, 255));
                        objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0] = std::move(ph);
                        objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 3);
                        for (int h = 1; h < NUM_HOUSES; h++) {
                            for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                if (objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_AdvancedWindTrap][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][z]->format, 0)
                                    };
                                }
                            }
                        }
                    }
                }
            }
        }
    } // end city-sprite loading scope (binDir, srcDirEnv, scaleRGBASurface)

    // ----- DuneCity corner flag sprite -----
    // Tornie_AdvancedWindtrap_gfx.png: 48×96 (or 48×48 single-frame), 8-bit
    // palette-indexed.  2 frames stacked vertically, each 48×48.  Loaded into
    // HOUSE_HARKONNEN only; getZoomedObjPic remaps palette indices 144-150 for
    // other houses (same as Trike).  Rearranged into a horizontal strip
    // (numFrames × frameSize wide, frameSize tall) for rendering consistency.
    // Falls back to Tornie_CornerFlagNew.png (old RGBA path) if unavailable.
    {
        bool loadedNewFlag = false;
        if (pFileManager->exists("Tornie_AdvancedWindtrap_gfx.png")) {
            auto flagSrc = LoadPNG_RW(pFileManager->openFile("Tornie_AdvancedWindtrap_gfx.png").get());
            if (flagSrc && flagSrc->format->BitsPerPixel == 8 && flagSrc->format->palette) {
                const int frameSize = flagSrc->w;  // 48
                const int numFrames = (flagSrc->h >= frameSize * 2) ? 2 : 1;
                benePalette.applyToSurface(flagSrc.get());

                // Rearrange vertically-stacked frames into a horizontal strip
                sdl2::surface_ptr strip{ SDL_CreateRGBSurface(0, numFrames * frameSize, frameSize,
                    8, 0, 0, 0, 0) };
                if (strip) {
                    SDL_SetPaletteColors(strip->format->palette, flagSrc->format->palette->colors, 0, 256);
                    for (int f = 0; f < numFrames; f++) {
                        SDL_Rect srcRect = { 0, f * frameSize, frameSize, frameSize };
                        SDL_Rect dstRect = { f * frameSize, 0, frameSize, frameSize };
                        SDL_SetSurfaceBlendMode(flagSrc.get(), SDL_BLENDMODE_NONE);
                        SDL_BlitSurface(flagSrc.get(), &srcRect, strip.get(), &dstRect);
                    }
                    objPic[ObjPic_CornerFlag][HOUSE_HARKONNEN][0] = std::move(strip);
                    loadedNewFlag = true;
                    SDL_Log("GFXManager: Loaded Tornie_AdvancedWindtrap_gfx.png as CornerFlag (%dx%d, %d frames)",
                            frameSize, frameSize, numFrames);
                }
            } else if (flagSrc) {
                SDL_Log("GFXManager: Tornie_AdvancedWindtrap_gfx.png is not 8-bit palette-indexed (%d bpp), skipping",
                        flagSrc->format->BitsPerPixel);
            }
        }

        // Fallback: old RGBA corner flag (Tornie_CornerFlagNew.png, 14x7, 2×7x7 frames)
        if (!loadedNewFlag && pFileManager->exists("Tornie_CornerFlagNew.png")) {
            auto flagSrc = LoadPNG_RW(pFileManager->openFile("Tornie_CornerFlagNew.png").get());
            if (flagSrc) {
                sdl2::surface_ptr flagRGBA{ SDL_ConvertSurfaceFormat(flagSrc.get(), SDL_PIXELFORMAT_RGBA32, 0) };
                if (!flagRGBA) flagRGBA = std::move(flagSrc);

                const int frameSize = 7;
                const int numFrames = 2;
                const Uint32 srcLight = SDL_MapRGBA(flagRGBA->format, 214, 65, 48, 255);
                const Uint32 srcDark  = SDL_MapRGBA(flagRGBA->format, 153, 44, 32, 255);
                const Uint32 srcBlack = SDL_MapRGBA(flagRGBA->format, 0, 0, 0, 255);
                const Uint32 dstPole  = SDL_MapRGBA(flagRGBA->format, 30, 30, 30, 255);

                for (int h = 0; h < NUM_HOUSES; h++) {
                    // For Fremen, source colours from vanilla IBM.PAL so rebels grey doesn't bleed in
                    const Palette& hPal = (h == HOUSE_FREMEN) ? ibmPalette : palette;
                    SDL_Color cLight = hPal[houseToPaletteIndex[h] + 0];
                    SDL_Color cDark  = hPal[houseToPaletteIndex[h] + 2];
                    Uint32 dstLight = SDL_MapRGBA(flagRGBA->format, cLight.r, cLight.g, cLight.b, 255);
                    Uint32 dstDark  = SDL_MapRGBA(flagRGBA->format, cDark.r, cDark.g, cDark.b, 255);

                    sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, numFrames * frameSize, frameSize,
                        flagRGBA->format->BitsPerPixel,
                        flagRGBA->format->Rmask, flagRGBA->format->Gmask,
                        flagRGBA->format->Bmask, flagRGBA->format->Amask) };
                    if (!atlas) continue;
                    SDL_FillRect(atlas.get(), nullptr, SDL_MapRGBA(atlas->format, 0, 0, 0, 0));

                    SDL_LockSurface(flagRGBA.get());
                    SDL_LockSurface(atlas.get());
                    for (int y = 0; y < frameSize && y < flagRGBA->h; y++) {
                        const Uint32* srcRow = reinterpret_cast<const Uint32*>(
                            static_cast<const Uint8*>(flagRGBA->pixels) + y * flagRGBA->pitch);
                        Uint32* dstRow = reinterpret_cast<Uint32*>(
                            static_cast<Uint8*>(atlas->pixels) + y * atlas->pitch);
                        for (int x = 0; x < numFrames * frameSize && x < flagRGBA->w; x++) {
                            Uint32 px = srcRow[x];
                            if (px == srcLight)      dstRow[x] = dstLight;
                            else if (px == srcDark)  dstRow[x] = dstDark;
                            else if (px == srcBlack) dstRow[x] = dstPole;
                            else                     dstRow[x] = px;
                        }
                    }
                    SDL_UnlockSurface(atlas.get());
                    SDL_UnlockSurface(flagRGBA.get());

                    objPic[ObjPic_CornerFlag][h][0] = std::move(atlas);
                    for (int z = 1; z < NUM_ZOOMLEVEL; z++) {
                        const int factor = z + 1;
                        auto* src = objPic[ObjPic_CornerFlag][h][0].get();
                        sdl2::surface_ptr scaled{ SDL_CreateRGBSurface(0, src->w * factor, src->h * factor,
                            src->format->BitsPerPixel,
                            src->format->Rmask, src->format->Gmask, src->format->Bmask, src->format->Amask) };
                        if (scaled) {
                            SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
                            SDL_BlitScaled(src, nullptr, scaled.get(), nullptr);
                            objPic[ObjPic_CornerFlag][h][z] = std::move(scaled);
                        }
                    }
                }
            }
        }
    }

    // Final safety net: ensure every DuneCity civic sprite has a populated
    // objPic for HOUSE_HARKONNEN at all zoom levels, and cloned for every
    // house.  If ANY of the per-sprite load blocks above failed to populate
    // (e.g. SDL_CreateRGBSurface returned null, or an unexpected code path),
    // clone from ConstructionYard so getObjPic/getZoomedObjPic never throws.
    {
        static const unsigned int civicIds[] = {
            ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
            ObjPic_Airport, ObjPic_Hospital, ObjPic_Church, ObjPic_AdvancedWindTrap,
            ObjPic_CornerFlag
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
    objPic[ObjPic_DestroyedStructure][HOUSE_HARKONNEN][0] = icon->getPictureRow2(14, 33, 125, 213, 214, 215, 223, 224, 225, 232, 233, 234, 240, 246, 247);
    objPic[ObjPic_RockDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(1,6);
    objPic[ObjPic_SandDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(7,12);
    objPic[ObjPic_Terrain_Hidden][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_Tracks][HOUSE_HARKONNEN][0] = icon->getPictureRow(25,32);
    objPic[ObjPic_Star][HOUSE_HARKONNEN][0] = LoadPNG_RW(pFileManager->openFile("Star5x5.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][1] = LoadPNG_RW(pFileManager->openFile("Star7x7.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][2] = LoadPNG_RW(pFileManager->openFile("Star11x11.png").get());

    SDL_Color fogTransparent = { 0, 0, 0, 96};
    SDL_SetPaletteColors(objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0]->format->palette, &fogTransparent, PALCOLOR_BLACK, 1);

    // scale obj pics and apply color key
    for(int id = 0; id < NUM_OBJPICS; id++) {
        // Zone sprites (and Star) are 32-bit RGBA with per-pixel alpha.
        // SDL_SetColorKey with PALCOLOR_TRANSPARENT (== 0) on them would
        // treat any pixel with raw value 0 as color-keyed, overriding the
        // alpha channel and producing black squares where transparent
        // pixels have non-zero RGB.  Skip color keying for these IDs.
        const bool isTruecolorSprite = (id == ObjPic_ZoneResidential
                                     || id == ObjPic_ZoneCommercial
                                     || id == ObjPic_ZoneIndustrial
                                     || id == ObjPic_CityRoad
                                     || id == ObjPic_NuclearPlant
                                     || id == ObjPic_PoliceStation
                                     || id == ObjPic_Stadium
                                     || id == ObjPic_Airport
                                     || id == ObjPic_AdvancedWindTrap
                                     || id == ObjPic_CornerFlag
                                     || id == ObjPic_Star
                                     || id == ObjPic_FlameTank
                                     // RocketTrike is truecolor only when the
                                     // data/RocketTrike.png override is loaded
                                     // (32-bit); the SHP fallback stays 8-bit.
                                     || (id == ObjPic_RocketTrike
                                         && objPic[id][HOUSE_HARKONNEN][0] != nullptr
                                         && objPic[id][HOUSE_HARKONNEN][0]->format->BytesPerPixel >= 3));

        for(int h = 0; h < (int) NUM_HOUSES; h++) {
            if(objPic[id][h][0] != nullptr) {
                if(objPic[id][h][1] == nullptr) {
                    objPic[id][h][1] = generateDoubledObjPic(id, h);
                }
                if(!isTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][1].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(objPic[id][h][2] == nullptr) {
                    objPic[id][h][2] = generateTripledObjPic(id, h);
                }
                if(!isTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][2].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(!isTruecolorSprite) {
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
    // DuneCity: Neutral house gets a custom build-menu icon (NeutralLauncherIcon.png if present),
    // falling back to the plain RTANK.WSA portrait (no red tint).
    if (pFileManager->exists("NeutralLauncherIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("NeutralLauncherIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_LauncherNeutral] = std::move(tex);
            }
        }
    }
    if (!smallDetailPicTex[Picture_LauncherNeutral]) {
        smallDetailPicTex[Picture_LauncherNeutral] = extractSmallDetailPic("RTANK.WSA");
    }
    smallDetailPicTex[Picture_LightFactory] = extractSmallDetailPic("LITEFTRY.WSA");
    smallDetailPicTex[Picture_MCV] = extractSmallDetailPic("MCV.WSA");
    smallDetailPicTex[Picture_Ornithopter] = extractSmallDetailPic("ORNI.WSA");
    // Prefer PalaceIcon.png from Tornie.PAK/data for all houses if present
    if (pFileManager->exists("PalaceIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("PalaceIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_Palace] = std::move(tex);
                SDL_Log("Loaded Palace icon from PalaceIcon.png");
            }
        }
    }
    if (!smallDetailPicTex[Picture_Palace]) {
        smallDetailPicTex[Picture_Palace] = extractSmallDetailPic("PALACE.WSA");
    }
    // DuneCity: Neutral Palace ability icon (Trike & Quad spawn action button).
    // This is separate from the building icon (Picture_Palace) which uses
    // vanilla PALACE.WSA for all houses.
    if (pFileManager->exists("PalaceTrikeAndQuadIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("PalaceTrikeAndQuadIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_PalaceNeutral] = std::move(tex);
                SDL_Log("Loaded Neutral Palace ability icon from PalaceTrikeAndQuadIcon.png");
            }
        }
    }
    if (!smallDetailPicTex[Picture_PalaceNeutral]) {
        smallDetailPicTex[Picture_PalaceNeutral] = extractSmallDetailPic("PALACE.WSA");
    }
    SDL_Log("GFX INIT: post-PalaceNeutral icon");
    // Quad: the neutral Palace activation ability (PalaceInterface) uses
    // Picture_Quad. Prefer the standalone QuadIcon.png in data/ — it ships at
    // the exact 91×55 sidebar-slot size, so load it straight to a texture.
    // Fall back to the QUAD.WSA build portrait if the PNG is absent.
    if (pFileManager->exists("QuadIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("QuadIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_Quad] = std::move(tex);
            }
        }
    }
    if (!smallDetailPicTex[Picture_Quad]) {
        smallDetailPicTex[Picture_Quad] = extractSmallDetailPic("QUAD.WSA");
    }
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
    SDL_Log("GFX INIT: small detail pics done");

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
    SDL_Log("GFX INIT: zone detail pics done");

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

        // Advanced Windtrap: prefer Tornie's custom sidebar icon PNG. It ships
        // pre-sized at 91×55 — exactly the sidebar slot — so load it straight
        // to a texture with no subsampling or scaling. Fall through to the
        // sprite-atlas-derived icon, then WINDTRAP.WSA, if the file is absent.
        if (pFileManager->exists("Tornie_AdvancedWindtrap_icon.png")) {
            auto iconSurf = LoadPNG_RW(pFileManager->openFile("Tornie_AdvancedWindtrap_icon.png").get());
            if (iconSurf) {
                auto tex = convertSurfaceToTexture(iconSurf.get());
                if (tex) {
                    SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                    smallDetailPicTex[Picture_AdvancedWindTrap] = std::move(tex);
                }
            }
        }
        if (!smallDetailPicTex[Picture_AdvancedWindTrap]) {
            auto advTex = makeStructDetailPic(ObjPic_AdvancedWindTrap, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
            if (advTex) smallDetailPicTex[Picture_AdvancedWindTrap] = std::move(advTex);
            else        smallDetailPicTex[Picture_AdvancedWindTrap] = extractSmallDetailPic("WINDTRAP.WSA");
        }
    }

    // Rocket Trike: prefer RocketTrikeIcon.png (91x55 sidebar slot), fall back
    // to the standard Trike WSA portrait.
    if (pFileManager->exists("RocketTrikeIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("RocketTrikeIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_RocketTrike] = std::move(tex);
            }
        }
    }
    if (!smallDetailPicTex[Picture_RocketTrike]) {
        smallDetailPicTex[Picture_RocketTrike] = extractSmallDetailPic("TRIKE.WSA");
    }
    if (pFileManager->exists("RocketTrikeIconMask.png")) {
        SDL_Log("RocketTrikeIconMask.png found — mask companion for RocketTrikeIcon is available");
    }

    // Elite Siege Tank: prefer EliteSiegeTankIcon.png (sidebar slot), fall back to SiegeTank portrait.
    if (pFileManager->exists("EliteSiegeTankIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("EliteSiegeTankIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_EliteSiegeTank] = std::move(tex);
            }
        }
    }
    if (!smallDetailPicTex[Picture_EliteSiegeTank]) {
        // Try Tornie.PAK custom WSA first, fall back to Siege Tank WSA
        const char* estWsa = pFileManager->exists("EliteSiegeTank.WSA") ? "EliteSiegeTank.WSA" : "HTANK.WSA";
        smallDetailPicTex[Picture_EliteSiegeTank] = extractSmallDetailPic(estWsa);
    }

    // Flame Tank: prefer FlameTankIcon.png (sidebar slot), fall back to Sonic Tank portrait.
    if (pFileManager->exists("FlameTankIcon.png")) {
        auto iconSurf = LoadPNG_RW(pFileManager->openFile("FlameTankIcon.png").get());
        if (iconSurf) {
            auto tex = convertSurfaceToTexture(iconSurf.get());
            if (tex) {
                SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                smallDetailPicTex[Picture_FlameTank] = std::move(tex);
            }
        }
    }
    if (!smallDetailPicTex[Picture_FlameTank]) {
        smallDetailPicTex[Picture_FlameTank] = extractSmallDetailPic("STANK.WSA");
    }

    // unused: FARTR.WSA, FHARK.WSA, FORDOS.WSA
    SDL_Log("GFX INIT: road/struct detail pics done");


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
    SDL_Log("GFX INIT: tiny pics done");

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
    uiGraphic[UI_GameStatsBackground][HOUSE_NEUTRAL] = PicFactory->createGameStatsBackground(HOUSE_NEUTRAL);
    uiGraphic[UI_GameStatsBackground][HOUSE_REBELS] = PicFactory->createGameStatsBackground(HOUSE_REBELS);
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

    uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATH.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATA.CPS").get()).get());
    // Paul Atreides mentat background override (Tornie mod)
    if (pFileManager->exists("PaulAtreidesMentat.png")) {
        auto paulBg = LoadPNG_RW(pFileManager->openFile("PaulAtreidesMentat.png").get());
        if (paulBg) {
            SDL_Log("GFX INIT: Paul Atreides mentat background loaded (%dx%d)", paulBg->w, paulBg->h);
            uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] = std::move(paulBg);
        }
    }
    uiGraphic[UI_MentatBackground][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATO.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_FREMEN] = PictureFactory::mapMentatSurfaceToFremen(uiGraphic[UI_MentatBackground][HOUSE_ATREIDES].get());
    uiGraphic[UI_MentatBackground][HOUSE_SARDAUKAR] = PictureFactory::mapMentatSurfaceToSardaukar(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get());
    uiGraphic[UI_MentatBackground][HOUSE_MERCENARY] = PictureFactory::mapMentatSurfaceToMercenary(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());
    // DuneCity: Neutral mentat background.
    // Tornie mod: use ChaniMentat.png (320×200 palette-indexed) if available.
    // Default: Mentat Cyrit — remap Atreides portrait blue to neutral grey.
    {
        bool loadedChani = false;
        if (ModManager::instance().getActiveModName() == "Tornie" && pFileManager->exists("ChaniMentat.png")) {
            auto chaniSurf = LoadPNG_RW(pFileManager->openFile("ChaniMentat.png").get());
            if (chaniSurf) {
                if (chaniSurf->format->BitsPerPixel == 8)
                    benePalette.applyToSurface(chaniSurf.get());
                auto doubled = Scaler::defaultDoubleSurface(chaniSurf.get());
                if (doubled) {
                    uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = std::move(doubled);
                    loadedChani = true;
                    SDL_Log("GFXManager: Loaded ChaniMentat.png as Neutral mentat background");
                }
            }
        }
        if (!loadedChani) {
            auto atreidesSurf = LoadCPS_RW(pFileManager->openFile("MENTATA.CPS").get());
            if (atreidesSurf) {
                auto doubled = Scaler::defaultDoubleSurface(atreidesSurf.get());
                if (doubled) {
                    uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] =
                        mapSurfaceColorRange(doubled.get(), PALCOLOR_ATREIDES, PALCOLOR_NEUTRAL);
                }
            }
        }
        // Final fallback
        if (!uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL]) {
            uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] =
                PictureFactory::mapMentatSurfaceToNeutral(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());
        }
    }

    // HOUSE_REBELS mentat background — reuse neutral mentat style
    uiGraphic[UI_MentatBackground][HOUSE_REBELS] =
        PictureFactory::mapMentatSurfaceToNeutral(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());

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
        uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL] = PicFactory->createHeraldNeu(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_NEUTRAL] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL].get());

        // HOUSE_REBELS herald
        if (ModManager::instance().getActiveModName() == "Tornie" && pFileManager->exists("HeraldRebels.png")) {
            auto pRebels = LoadPNG_RW(pFileManager->openFile("HeraldRebels.png").get());
            if (pRebels) {
                if (pRebels->format->BitsPerPixel == 8)
                    benePalette.applyToSurface(pRebels.get());
                uiGraphic[UI_Herald_Colored][HOUSE_REBELS] = std::move(pRebels);
            }
        }
        if (!uiGraphic[UI_Herald_Colored][HOUSE_REBELS]) {
            // Fallback: remap Neutral herald to rebels palette
            uiGraphic[UI_Herald_Colored][HOUSE_REBELS] =
                mapSurfaceColorRange(uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL].get(), PALCOLOR_NEUTRAL, PALCOLOR_REBELS);
        }
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_REBELS] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_REBELS].get());
    }

    uiGraphic[UI_Herald_Grey][HOUSE_HARKONNEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ATREIDES] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ORDOS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
    uiGraphic[UI_Herald_Grey][HOUSE_FREMEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_FREMEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_SARDAUKAR] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR].get());
    uiGraphic[UI_Herald_Grey][HOUSE_MERCENARY] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY].get());
    uiGraphic[UI_Herald_Grey][HOUSE_NEUTRAL] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL].get());
    if (uiGraphic[UI_Herald_Colored][HOUSE_REBELS]) {
        uiGraphic[UI_Herald_Grey][HOUSE_REBELS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_REBELS].get());
    }

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
    uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(208).get());
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
    uiGraphic[UI_MapEditor_Radar][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Radar][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Silo][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Silo][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_IX][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_IX][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Barracks][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Barracks][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_WOR][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_WOR][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_LightFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_LightFactory][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Refinery][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Refinery][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HighTechFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HeavyFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HeavyFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_RepairYard][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_RepairYard][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Starport][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Starport][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Palace][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);

    uiGraphic[UI_MapEditor_Soldier][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Soldier][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Trooper][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trooper][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Harvester][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0].get(),0,0,8,1);
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
    uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Launcher_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                  uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                  uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Saboteur][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Saboteur][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Sandworm][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Sandworm][HOUSE_HARKONNEN][0].get(),0,5,1,9);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Carryall][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get(),0,0,8,2);
    uiGraphic[UI_MapEditor_Ornithopter][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0].get(),0,0,8,3);

    // DuneCity/Tornie: FlameTank editor icon — built per-house from the RGBA
    // ObjPic (which already has per-house colour variants), so we bypass the
    // palette-remap path in getUIGraphicSurface().
    for(int h = 0; h < (int) NUM_HOUSES; h++) {
        if(objPic[ObjPic_FlameTank][h][0] != nullptr) {
            uiGraphic[UI_MapEditor_FlameTank][h] = getSubFrame(objPic[ObjPic_FlameTank][h][0].get(),0,0,8,1);
        }
    }

    // Tornie: red/green spice field + bloom editor icons from custom terrain strips.
    // Spice field = col 0, row 0 (thin spice); bloom icons use vanilla SpiceBloom.
    for (int h = 0; h < (int)NUM_HOUSES; h++) {
        if (objPic[ObjPic_TerrainRedSpice][h][0] != nullptr) {
            uiGraphic[UI_MapEditor_RedSpice][h] = getSubFrame(
                objPic[ObjPic_TerrainRedSpice][h][0].get(), 0, 0, 17, 2);
        }
        if (objPic[ObjPic_TerrainGreenSpice][h][0] != nullptr) {
            uiGraphic[UI_MapEditor_GreenSpice][h] = getSubFrame(
                objPic[ObjPic_TerrainGreenSpice][h][0].get(), 0, 0, 17, 2);
        }
    }

    // Brush-size pen buttons — scale each down by 20px from native 31×31 → 11×11.
    {
        const int penIds[] = { UI_MapEditor_Pen1x1, UI_MapEditor_Pen3x3, UI_MapEditor_Pen5x5 };
        const char* penFiles[] = { "MapEditorPen1x1.png", "MapEditorPen3x3.png", "MapEditorPen5x5.png" };
        for (int i = 0; i < 3; i++) {
            auto raw = LoadPNG_RW(pFileManager->openFile(penFiles[i]).get());
            if (raw && raw->w > 20 && raw->h > 20) {
                int newW = raw->w - 20;
                int newH = raw->h - 20;
                sdl2::surface_ptr scaled{ SDL_CreateRGBSurface(0, newW, newH,
                    raw->format->BitsPerPixel,
                    raw->format->Rmask, raw->format->Gmask, raw->format->Bmask, raw->format->Amask) };
                if (scaled) {
                    if (raw->format->palette && scaled->format->palette)
                        SDL_SetPaletteColors(scaled->format->palette, raw->format->palette->colors, 0, raw->format->palette->ncolors);
                    SDL_BlitScaled(raw.get(), nullptr, scaled.get(), nullptr);
                    SDL_SetColorKey(scaled.get(), SDL_TRUE, 0);
                    uiGraphic[penIds[i]][HOUSE_HARKONNEN] = std::move(scaled);
                }
            } else if (raw) {
                SDL_SetColorKey(raw.get(), SDL_TRUE, 0);
                uiGraphic[penIds[i]][HOUSE_HARKONNEN] = std::move(raw);
            }
        }
    }

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

    // Advanced Windtrap: use the custom icon PNG if available, otherwise crop
    // frame 0 of the sprite atlas (all 8 frames are identical).
    for (int h = 0; h < (int)NUM_HOUSES; ++h) {
        if (objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0]) {
            uiGraphic[UI_MapEditor_AdvancedWindTrap][h] = getSubPicture(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(), 0, 0, 3*D2_TILESIZE, 3*D2_TILESIZE);
        } else {
            uiGraphic[UI_MapEditor_AdvancedWindTrap][h] = getSubPicture(objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get(), 2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 2*D2_TILESIZE);
        }
    }


    // load animations
    SDL_Log("GFX INIT: starting animation load");
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

    // Paul Atreides custom mentat animations (Tornie mod)
    if (pFileManager->exists("PaulAtreidesEyes.png")) {
        auto eyeStrip = LoadPNG_RW(pFileManager->openFile("PaulAtreidesEyes.png").get());
        if (eyeStrip) {
            auto paulEyes = std::make_unique<Animation>();
            const int fw = eyeStrip->w / 4;
            for (int i = 0; i < 4; ++i) {
                SDL_Rect src = { i * fw, 0, fw, eyeStrip->h };
                sdl2::surface_ptr frame{ SDL_CreateRGBSurfaceWithFormat(0, fw, eyeStrip->h, 32, SDL_PIXELFORMAT_RGBA32) };
                SDL_BlitSurface(eyeStrip.get(), &src, frame.get(), nullptr);
                paulEyes->addFrame(std::move(frame));
            }
            paulEyes->setFrameRate(0.5);
            animation[Anim_PaulEyes] = std::move(paulEyes);
            SDL_Log("GFX INIT: Loaded Paul Atreides eyes animation (4 frames %dx%d)", fw, eyeStrip->h);
        }
    }

    if (pFileManager->exists("PaulAtreidesMouth.png")) {
        auto mouthStrip = LoadPNG_RW(pFileManager->openFile("PaulAtreidesMouth.png").get());
        if (mouthStrip) {
            auto paulMouth = std::make_unique<Animation>();
            const int fw = mouthStrip->w / 4;
            for (int i = 0; i < 4; ++i) {
                SDL_Rect src = { i * fw, 0, fw, mouthStrip->h };
                sdl2::surface_ptr frame{ SDL_CreateRGBSurfaceWithFormat(0, fw, mouthStrip->h, 32, SDL_PIXELFORMAT_RGBA32) };
                SDL_BlitSurface(mouthStrip.get(), &src, frame.get(), nullptr);
                paulMouth->addFrame(std::move(frame));
            }
            paulMouth->setFrameRate(5.0);
            animation[Anim_PaulMouth] = std::move(paulMouth);
            SDL_Log("GFX INIT: Loaded Paul Atreides mouth animation (4 frames %dx%d)", fw, mouthStrip->h);
        }
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
    SDL_Log("GFX INIT: base house animations done");
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
    SDL_Log("GFX INIT: mentat remap animations done");
    // Neutral mentat — always use vanilla palette-mapped Ordos frames
    animation[Anim_NeutralEyes]  = PictureFactory::mapMentatAnimationToNeutral(animation[Anim_OrdosEyes].get());
    animation[Anim_NeutralMouth] = PictureFactory::mapMentatAnimationToNeutral(animation[Anim_OrdosMouth].get());
    animation[Anim_NeutralShoulder] = PictureFactory::mapMentatAnimationToNeutral(animation[Anim_OrdosShoulder].get());
    animation[Anim_NeutralRing] = PictureFactory::mapMentatAnimationToNeutral(animation[Anim_OrdosRing].get());
    SDL_Log("GFX INIT: Neutral mentat animations done");

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
    SDL_Log("GFX INIT: Bene Gesserit animations done");
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

SDL_Texture* GFXManager::getZoomedObjPic(unsigned int id, int house, unsigned int z) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not available!", id);
    }

    if(objPic[id][house][z] == nullptr) {
        // remap to this color
        if(objPic[id][HOUSE_HARKONNEN][z] == nullptr) {
            // DuneCity civic sprites: fall back to ConstructionYard instead of crashing
            static const unsigned int duneCityCivicIds[] = {
                ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
                ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
            };
            bool isDuneCityCivic = false;
            for(auto cid : duneCityCivicIds) {
                if(id == cid) { isDuneCityCivic = true; break; }
            }
            if(isDuneCityCivic && objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]) {
                SDL_Log("GFXManager::getZoomedObjPic(): DuneCity civic sprite ID %u not loaded, falling back to ConstructionYard", id);
                objPic[id][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z].get(),
                                       objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]->format, 0)
                };
            } else {
                THROW(std::runtime_error, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not loaded!", id);
            }
        }

        objPic[id][house][z] = mapSurfaceColorRange(objPic[id][HOUSE_HARKONNEN][z].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[house]);

        // For Fremen: palette[192] has been overridden with rebels grey by Custom_IBM.pal.
        // Restore vanilla IBM.PAL colours at 192-207 in this surface's embedded palette so
        // SDL renders Fremen units with their correct vanilla colour, not rebels grey.
        if (house == HOUSE_FREMEN && objPic[id][house][z] && objPic[id][house][z]->format->palette) {
            ibmPalette.applyToSurface(objPic[id][house][z].get(), PALCOLOR_FREMEN, PALCOLOR_FREMEN + 15);
        }
    }

    if(objPicTex[id][house][z] == nullptr) {
        // now convert to display format
        if(id == ObjPic_Windtrap) {
            // Windtrap uses palette animation on PALCOLOR_WINDTRAP_COLORCYCLE; fake this
            objPicTex[id][house][z] = convertSurfaceToTexture(generateWindtrapAnimationFrames(objPic[id][house][z].get()));
        } else if(id == ObjPic_AdvancedWindTrap) {
            // Generate windtrap-style light animation for the RGBA AdvancedWindTrap sprite
            SDL_Surface* base = objPic[id][house][z].get();
            if (base) {
                int fw = base->h; // square sprite: frame width = height
                sdl2::surface_ptr singleFrame;
                if (base->w > fw) {
                    singleFrame = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, fw, base->h, base->format->BitsPerPixel,
                        base->format->Rmask, base->format->Gmask, base->format->Bmask, base->format->Amask) };
                    if (singleFrame) {
                        SDL_SetSurfaceBlendMode(base, SDL_BLENDMODE_NONE);
                        SDL_Rect srcR{0, 0, fw, base->h};
                        SDL_BlitSurface(base, &srcR, singleFrame.get(), nullptr);
                    }
                }
                SDL_Surface* src = singleFrame ? singleFrame.get() : base;
                sdl2::surface_ptr rgbaSrc;
                if (src->format->BytesPerPixel != 4) {
                    rgbaSrc = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0) };
                    src = rgbaSrc.get();
                }
                objPicTex[id][house][z] = convertSurfaceToTexture(generateAdvancedWindtrapAnimationFrames(src).get());
            } else {
                objPicTex[id][house][z] = convertSurfaceToTexture(base);
            }
            if (objPicTex[id][house][z]) {
                SDL_SetTextureBlendMode(objPicTex[id][house][z].get(), SDL_BLENDMODE_BLEND);
            }
        } else if(id == ObjPic_Bullet_SonicTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else if(id == ObjPic_SandwormShimmerTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else {
            objPicTex[id][house][z] = convertSurfaceToTexture(objPic[id][house][z].get());
        }

        // Truecolor RGBA sprites need explicit blend mode so their alpha
        // channel is respected during rendering; without this, transparent
        // pixels appear as solid black.
        if(id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
           || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
           || id == ObjPic_NuclearPlant || id == ObjPic_PoliceStation
           || id == ObjPic_Stadium || id == ObjPic_Airport
           || id == ObjPic_Hospital || id == ObjPic_Church
           || id == ObjPic_Star || id == ObjPic_AdvancedWindTrap
           || id == ObjPic_CornerFlag
           || id == ObjPic_TerrainRedSpice || id == ObjPic_TerrainGreenSpice
           || (id == ObjPic_RocketTrike && objPic[id][house][z] != nullptr
               && objPic[id][house][z]->format->BytesPerPixel >= 3)) {
            if(objPicTex[id][house][z]) {
                SDL_SetTextureBlendMode(objPicTex[id][house][z].get(), SDL_BLENDMODE_BLEND);
            }
        }
    }

    return objPicTex[id][house][z].get();
}

zoomable_texture GFXManager::getObjPic(unsigned int id, int house) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getObjPic(): Unit Picture with ID %u is not available!", id);
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

    if(uiGraphic[id][house] == nullptr) {
        // remap to this color
        if(uiGraphic[id][HOUSE_HARKONNEN] == nullptr) {
            THROW(std::runtime_error, "GFXManager::getUIGraphicSurface(): UI Graphic with ID %u is not loaded!", id);
        }

        uiGraphic[id][house] = mapSurfaceColorRange(uiGraphic[id][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[house]);
        // Restore vanilla IBM.PAL at 192-207 for Fremen so rebels grey doesn't corrupt Fremen UI graphics
        if (house == HOUSE_FREMEN && uiGraphic[id][house] && uiGraphic[id][house]->format->palette) {
            ibmPalette.applyToSurface(uiGraphic[id][house].get(), PALCOLOR_FREMEN, PALCOLOR_FREMEN + 15);
        }
    }

    return uiGraphic[id][house].get();
}

SDL_Texture* GFXManager::getUIGraphic(unsigned int id, int house) {
    if(id >= NUM_UIGRAPHICS) {
        THROW(std::invalid_argument, "GFXManager::getUIGraphic(): UI Graphic with ID %u is not available!", id);
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

    if(mapChoicePieces[num][house] == nullptr) {
        // remap to this color
        if(mapChoicePieces[num][HOUSE_HARKONNEN] == nullptr) {
            THROW(std::runtime_error, "GFXManager::getMapChoicePieceSurface(): Map Piece with number %u is not loaded!", num);
        }

        mapChoicePieces[num][house] = mapSurfaceColorRange(mapChoicePieces[num][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[house]);
        // Restore vanilla IBM.PAL at 192-207 for Fremen so rebels grey doesn't corrupt Fremen map pieces
        if (house == HOUSE_FREMEN && mapChoicePieces[num][house] && mapChoicePieces[num][house]->format->palette) {
            ibmPalette.applyToSurface(mapChoicePieces[num][house].get(), PALCOLOR_FREMEN, PALCOLOR_FREMEN + 15);
        }
    }

    return mapChoicePieces[num][house].get();
}

SDL_Texture* GFXManager::getMapChoicePiece(unsigned int num, int house) {
    if(num >= NUM_MAPCHOICEPIECES) {
        THROW(std::invalid_argument, "GFXManager::getMapChoicePiece(): Map Piece with number %u is not available!", num);
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

sdl2::texture_ptr GFXManager::extractSmallDetailPic(const std::string& filename, bool tintRed) const
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

    if(tintRed) {
        tintSurfaceRed(pSurface.get());
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
    int windtrapSize = windtrapPic->h;
    int sizeX = NUM_WINDTRAP_ANIMATIONS_PER_ROW*windtrapSize;
    int sizeY = ((2+NUM_WINDTRAP_ANIMATIONS+NUM_WINDTRAP_ANIMATIONS_PER_ROW-1)/NUM_WINDTRAP_ANIMATIONS_PER_ROW)*windtrapSize;
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, sizeX, sizeY, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    SDL_SetSurfaceBlendMode(returnPic.get(), SDL_BLENDMODE_NONE);

    // copy building phase
    SDL_Rect src = { 0, 0, 2*windtrapSize, windtrapSize};
    SDL_Rect dest = src;
    SDL_BlitSurface(windtrapPic, &src, returnPic.get(), &dest);

    src.w = windtrapSize;
    dest.x += dest.w;
    dest.w = windtrapSize;

    for(int i = 0; i < NUM_WINDTRAP_ANIMATIONS; i++) {
        src.x = ((i/3) % 2 == 0) ? 2*windtrapSize : 3*windtrapSize;

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


sdl2::surface_ptr GFXManager::generateAdvancedWindtrapAnimationFrames(SDL_Surface* basePic) const {
    int fw = basePic->w;
    int fh = basePic->h;

    int sizeX = NUM_WINDTRAP_ANIMATIONS_PER_ROW * fw;
    int sizeY = ((2 + NUM_WINDTRAP_ANIMATIONS + NUM_WINDTRAP_ANIMATIONS_PER_ROW - 1) / NUM_WINDTRAP_ANIMATIONS_PER_ROW) * fh;
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, sizeX, sizeY, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if (!returnPic) return {};
    SDL_FillRect(returnPic.get(), nullptr, 0);

    // First 2 frames: copy base unmodified (for static display)
    for (int i = 0; i < 2; i++) {
        int destX = (i % NUM_WINDTRAP_ANIMATIONS_PER_ROW) * fw;
        int destY = (i / NUM_WINDTRAP_ANIMATIONS_PER_ROW) * fh;
        SDL_Rect destRect = { destX, destY, fw, fh };
        SDL_BlitSurface(basePic, nullptr, returnPic.get(), &destRect);
    }

    // Artist-approved pixel coordinates for the light animation zones
    // (derived from annotated reference image, 48x48 sprite)
    static const std::pair<int,int> lightPixels[] = {
        {18,4}, {19,4}, {37,4}, {38,4}, {17,5}, {18,5}, {19,5}, {36,5},
        {37,5}, {38,5}, {16,6}, {17,6}, {18,6}, {19,6}, {35,6}, {36,6},
        {37,6}, {38,6}, {18,7}, {19,7}, {37,7}, {38,7}, {20,8}, {39,8},
        {19,9}, {38,9}, {18,10}, {37,10}, {17,11}, {36,11}, {16,12}, {35,12},
        {15,13}, {34,13}, {9,20}, {10,20}, {26,20}, {27,20}, {43,20}, {44,20},
        {8,21}, {9,21}, {10,21}, {25,21}, {26,21}, {27,21}, {42,21}, {43,21},
        {44,21}, {7,22}, {8,22}, {9,22}, {10,22}, {24,22}, {25,22}, {26,22},
        {27,22}, {41,22}, {42,22}, {43,22}, {44,22}, {9,23}, {10,23}, {26,23},
        {27,23}, {43,23}, {44,23}, {11,24}, {28,24}, {45,24}, {10,25}, {27,25},
        {44,25}, {9,26}, {26,26}, {43,26}, {8,27}, {25,27}, {42,27}, {7,28},
        {24,28}, {41,28}, {6,29}, {23,29}, {40,29}, {18,36}, {19,36}, {37,36},
        {38,36}, {17,37}, {18,37}, {19,37}, {36,37}, {37,37}, {38,37}, {16,38},
        {17,38}, {18,38}, {19,38}, {35,38}, {36,38}, {37,38}, {38,38}, {18,39},
        {19,39}, {37,39}, {38,39}, {20,40}, {39,40}, {19,41}, {38,41}, {18,42},
        {37,42}, {17,43}, {36,43}, {16,44}, {35,44}, {15,45}, {34,45}
    };
    static const int NUM_LIGHT_PIXELS = sizeof(lightPixels) / sizeof(lightPixels[0]);

    // Animation frames 2..2+NUM_WINDTRAP_ANIMATIONS
    for (int i = 0; i < NUM_WINDTRAP_ANIMATIONS; i++) {
        int frameIdx = 2 + i;
        int destX = (frameIdx % NUM_WINDTRAP_ANIMATIONS_PER_ROW) * fw;
        int destY = (frameIdx / NUM_WINDTRAP_ANIMATIONS_PER_ROW) * fh;
        SDL_Rect destRect = { destX, destY, fw, fh };
        SDL_BlitSurface(basePic, nullptr, returnPic.get(), &destRect);

        // Darker blue color cycle (artist approved: peak R=15, G=40, B=220)
        float t;
        if (i < NUM_WINDTRAP_ANIMATIONS / 2) {
            t = (float)i / (NUM_WINDTRAP_ANIMATIONS / 2);
        } else {
            t = 1.0f - (float)(i - NUM_WINDTRAP_ANIMATIONS / 2) / (NUM_WINDTRAP_ANIMATIONS / 2);
        }
        Uint8 wr = (Uint8)(15.0f * t);
        Uint8 wg = (Uint8)(40.0f * t);
        Uint8 wb = (Uint8)(40.0f + 180.0f * t);

        // Pixel coords were annotated on the 48×48 reference sprite.
        // Scale to match the current zoom level's frame size.
        const int REFERENCE_SIZE = 48;
        const float pixelScale = (float)fw / REFERENCE_SIZE;
        const int block = std::max(1, (int)pixelScale);

        SDL_LockSurface(returnPic.get());
        for (int p = 0; p < NUM_LIGHT_PIXELS; p++) {
            int baseX = (int)(lightPixels[p].first  * pixelScale);
            int baseY = (int)(lightPixels[p].second * pixelScale);
            for (int dy = 0; dy < block; dy++) {
                for (int dx = 0; dx < block; dx++) {
                    int px = destX + baseX + dx;
                    int py = destY + baseY + dy;
                    if (px >= 0 && px < sizeX && py >= 0 && py < sizeY) {
                        Uint8* pixel = (Uint8*)returnPic->pixels + py * returnPic->pitch + px * returnPic->format->BytesPerPixel;
                        *pixel++ = wr;
                        *pixel++ = wg;
                        *pixel++ = wb;
                        *pixel   = 255;
                    }
                }
            }
        }
        SDL_UnlockSurface(returnPic.get());
    }

    return returnPic;
}

sdl2::surface_ptr GFXManager::generateMapChoiceArrowFrames(SDL_Surface* arrowPic, int house) const {
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, arrowPic->w * 4, arrowPic->h, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };

    SDL_Rect dest = {0, 0, arrowPic->w, arrowPic->h};

    for(int i = 0; i < 4; i++) {
        if (arrowPic->format->palette) {
            for(int k = 0; k < 4; k++) {
                SDL_SetPaletteColors(arrowPic->format->palette, &palette[houseToPaletteIndex[house]+((i+k)%4)], 251+k, 1);
            }
        }

        SDL_BlitSurface(arrowPic, nullptr, returnPic.get(), &dest);
        dest.x += dest.w;
    }

    return returnPic;
}

sdl2::surface_ptr GFXManager::generateDoubledObjPic(unsigned int id, int h) const {
    sdl2::surface_ptr pSurface;
    std::string filename = "Mask_2x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::doubleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            if (pOverlay->format->palette && pSurface->format->palette) {
                pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
                pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
                SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
                pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
                pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
            } else {
                SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            }
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
    const std::string filename = "Mask_3x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::tripleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            if (pOverlay->format->palette && pSurface->format->palette) {
                pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
                pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
                SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
                pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
                pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
            } else {
                SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            }
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
