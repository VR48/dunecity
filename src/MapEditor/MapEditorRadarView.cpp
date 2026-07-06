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

#include <MapEditor/MapEditorRadarView.h>

#include <MapEditor/MapEditor.h>

#include <globals.h>

#include <sand.h>

#include <ScreenBorder.h>
#include <Tile.h>

#include <misc/draw_util.h>


MapEditorRadarView::MapEditorRadarView(MapEditor* pMapEditor)
 : RadarViewBase(), pMapEditor(pMapEditor)
{
    radarSurface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, RADARWIDTH, RADARHEIGHT, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    SDL_FillRect(radarSurface.get(), nullptr, COLOR_BLACK);

    radarTexture = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_STREAMING, RADARWIDTH, RADARHEIGHT) };
}


MapEditorRadarView::~MapEditorRadarView() = default;

int MapEditorRadarView::getMapSizeX() const {
    return pMapEditor->getMap().getSizeX();
}

int MapEditorRadarView::getMapSizeY() const {
    return pMapEditor->getMap().getSizeY();
}

void MapEditorRadarView::draw(Point position)
{
    SDL_Rect radarPosition = { position.x + RADARVIEW_BORDERTHICKNESS, position.y + RADARVIEW_BORDERTHICKNESS, RADARWIDTH, RADARHEIGHT};

    const MapData& map = pMapEditor->getMap();

    RadarScaleInfo scaleInfo = calculateScaleAndOffsets(map.getSizeX(), map.getSizeY());

    updateRadarSurface(map, scaleInfo);

    SDL_UpdateTexture(radarTexture.get(), nullptr, radarSurface->pixels, radarSurface->pitch);

    SDL_RenderCopy(renderer, radarTexture.get(), nullptr, &radarPosition);

    // draw viewport rect on radar
    SDL_Rect radarRect;
    const float viewLeftTiles = screenborder->getLeft() / static_cast<float>(TILESIZE);
    const float viewTopTiles = screenborder->getTop() / static_cast<float>(TILESIZE);
    const float viewWidthTiles = (screenborder->getRight() - screenborder->getLeft()) / static_cast<float>(TILESIZE);
    const float viewHeightTiles = (screenborder->getBottom() - screenborder->getTop()) / static_cast<float>(TILESIZE);

    radarRect.x = static_cast<int>(std::floor(viewLeftTiles * scaleInfo.scale)) + scaleInfo.offsetX;
    radarRect.y = static_cast<int>(std::floor(viewTopTiles * scaleInfo.scale)) + scaleInfo.offsetY;
    radarRect.w = std::max(1, static_cast<int>(std::ceil(viewWidthTiles * scaleInfo.scale)));
    radarRect.h = std::max(1, static_cast<int>(std::ceil(viewHeightTiles * scaleInfo.scale)));

    const int mapLeft = scaleInfo.offsetX;
    const int mapTop = scaleInfo.offsetY;
    const int mapRight = scaleInfo.offsetX + scaleInfo.scaledWidth;
    const int mapBottom = scaleInfo.offsetY + scaleInfo.scaledHeight;

    if(radarRect.x < mapLeft) {
        radarRect.w -= (mapLeft - radarRect.x);
        radarRect.x = mapLeft;
    }

    if(radarRect.y < mapTop) {
        radarRect.h -= (mapTop - radarRect.y);
        radarRect.y = mapTop;
    }

    radarRect.w = std::min(mapRight, radarRect.x + radarRect.w) - radarRect.x;
    radarRect.h = std::min(mapBottom, radarRect.y + radarRect.h) - radarRect.y;

    radarRect.w = std::max(0, radarRect.w);
    radarRect.h = std::max(0, radarRect.h);

    renderDrawRect( renderer,
                    radarPosition.x + radarRect.x,
                    radarPosition.y + radarRect.y,
                    radarPosition.x + (radarRect.x + radarRect.w),
                    radarPosition.y + (radarRect.y + radarRect.h),
                    COLOR_WHITE);

}

void MapEditorRadarView::updateRadarSurface(const MapData& map, const RadarScaleInfo& scaleInfo) {

    SDL_FillRect(radarSurface.get(), nullptr, COLOR_BLACK);

    sdl2::surface_lock lock{radarSurface.get()};

    const int maxTileX = map.getSizeX() - 1;
    const int maxTileY = map.getSizeY() - 1;

    auto getTileColor = [&](int tileX, int tileY) {
        Uint32 color = getColorByTerrainType(map(tileX, tileY));

        if(map(tileX, tileY) == Terrain_Sand) {
            std::vector<Coord>& spiceFields = pMapEditor->getSpiceFields();

            for(size_t i = 0; i < spiceFields.size(); i++) {
                if(spiceFields[i].x == tileX && spiceFields[i].y == tileY) {
                    color = COLOR_THICKSPICE;
                    break;
                } else if(distanceFrom(spiceFields[i], Coord(tileX,tileY)) <= 5) {
                    color = COLOR_SPICE;
                    break;
                }
            }
        }

        // check for classic map items (spice blooms, special blooms)
        std::vector<Coord>& spiceBlooms = pMapEditor->getSpiceBlooms();
        for(size_t i = 0; i < spiceBlooms.size(); i++) {
            if(spiceBlooms[i].x == tileX && spiceBlooms[i].y == tileY) {
                color = COLOR_BLOOM;
                break;
            }
        }

        std::vector<Coord>& specialBlooms = pMapEditor->getSpecialBlooms();
        for(size_t i = 0; i < specialBlooms.size(); i++) {
            if(specialBlooms[i].x == tileX && specialBlooms[i].y == tileY) {
                color = COLOR_BLOOM;
                break;
            }
        }

        return MapRGBA(radarSurface->format, color);
    };

    for(int pixelY = 0; pixelY < scaleInfo.scaledHeight; pixelY++) {
        const int tileY = std::clamp(static_cast<int>(pixelY / scaleInfo.scale), 0, maxTileY);
        Uint32* p = reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(radarSurface->pixels)
                            + (scaleInfo.offsetY + pixelY) * radarSurface->pitch)
                    + scaleInfo.offsetX;

        for(int pixelX = 0; pixelX < scaleInfo.scaledWidth; pixelX++, p++) {
            const int tileX = std::clamp(static_cast<int>(pixelX / scaleInfo.scale), 0, maxTileX);
            *p = getTileColor(tileX, tileY);
        }
    }

    for(const MapEditor::Unit& unit : pMapEditor->getUnitList()) {

        if(unit.position.x >= 0 && unit.position.x < map.getSizeX()
            && unit.position.y >= 0 && unit.position.y < map.getSizeY()) {

            const int pixelSize = std::max(1, static_cast<int>(std::round(scaleInfo.scale)));
            const int pixelX = scaleInfo.offsetX + static_cast<int>(std::floor(unit.position.x * scaleInfo.scale));
            const int pixelY = scaleInfo.offsetY + static_cast<int>(std::floor(unit.position.y * scaleInfo.scale));

            for(int i = 0; i < pixelSize; i++) {
                for(int j = 0; j < pixelSize; j++) {
                    const int drawX = pixelX + i;
                    const int drawY = pixelY + j;
                    if(drawX >= scaleInfo.offsetX && drawX < scaleInfo.offsetX + scaleInfo.scaledWidth
                        && drawY >= scaleInfo.offsetY && drawY < scaleInfo.offsetY + scaleInfo.scaledHeight) {
                        putPixel(   radarSurface.get(),
                                    drawX,
                                    drawY,
                                    SDL2RGB(palette[houseToPaletteIndex[unit.house]]));
                    }
                }
            }
        }
    }

    for(const MapEditor::Structure& structure : pMapEditor->getStructureList()) {
        Coord structureSize = getStructureSize(structure.itemID);

        const int pixelWidth = std::max(1, static_cast<int>(std::round(structureSize.x * scaleInfo.scale)));
        const int pixelHeight = std::max(1, static_cast<int>(std::round(structureSize.y * scaleInfo.scale)));
        const int pixelX = scaleInfo.offsetX + static_cast<int>(std::floor(structure.position.x * scaleInfo.scale));
        const int pixelY = scaleInfo.offsetY + static_cast<int>(std::floor(structure.position.y * scaleInfo.scale));

        for(int y = 0; y < pixelHeight; y++) {
            for(int x = 0; x < pixelWidth; x++) {
                const int drawX = pixelX + x;
                const int drawY = pixelY + y;

                if(drawX >= scaleInfo.offsetX && drawX < scaleInfo.offsetX + scaleInfo.scaledWidth
                    && drawY >= scaleInfo.offsetY && drawY < scaleInfo.offsetY + scaleInfo.scaledHeight) {
                    putPixel(   radarSurface.get(),
                                drawX,
                                drawY,
                                SDL2RGB(palette[houseToPaletteIndex[structure.house]]));
                }
            }
        }
    }

}
