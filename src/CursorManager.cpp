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

#include <CursorManager.h>
#include <FileClasses/GFXManager.h>
#include <globals.h>
#include <Game.h>
#include <ObjectManager.h>
#include <units/UnitBase.h>
#include <structures/StructureBase.h>
#include <structures/Palace.h>

#include <algorithm>

namespace {

// Scale an SDL_Surface up by an integer factor. Returns a new surface the
// caller must SDL_FreeSurface() after use, or nullptr on failure.
SDL_Surface* scaleSurface(SDL_Surface* src, int scale) {
    if (!src || scale <= 1) {
        return nullptr;
    }
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(
        0, src->w * scale, src->h * scale, src->format->BitsPerPixel, src->format->format);
    if (!dst) {
        return nullptr;
    }
    // Copy palette from src so indexed (8-bit) pixels map correctly on dst.
    if (src->format->palette) {
        SDL_SetPixelFormatPalette(dst->format, src->format->palette);
    }
    // Preserve color key on the scaled surface.
    Uint32 colorKey = 0;
    if (SDL_GetColorKey(src, &colorKey) == 0) {
        SDL_SetColorKey(dst, SDL_TRUE, colorKey);
    }
    // Disable blending on src so the blitter treats color-keyed pixels as
    // opaque indices, not as alpha values (SDL2 trap with paletted surfaces).
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
    SDL_Rect srcRect = { 0, 0, src->w, src->h };
    SDL_Rect dstRect = { 0, 0, src->w * scale, src->h * scale };
    SDL_BlitScaled(src, &srcRect, dst, &dstRect);
    return dst;
}

// Determine the effective cursor scale from settings. 0 = auto-detect from DPI.
int getEffectiveCursorScale() {
    const int configured = settings.video.cursorScale;
    if (configured >= 1 && configured <= 4) {
        return configured;
    }
    // Auto-detect: use the display's logical-to-physical pixel ratio.
    // SDL_GetDisplayDPI gives horizontal DPI; 96 dpi is "1x" baseline.
    float ddpi = 96.0f, hdpi = 96.0f, vdpi = 96.0f;
    int displayIndex = window ? SDL_GetWindowDisplayIndex(window) : 0;
    if (displayIndex < 0) displayIndex = 0;
    SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi);
    const float dpi = hdpi > 0.0f ? hdpi : ddpi;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                   "CursorManager: display %d DPI=%.1f, auto-detecting cursor scale", displayIndex, dpi);
    if (dpi >= 288.0f) return 4;  // 4K HiDPI (e.g. 4x retina)
    if (dpi >= 192.0f) return 3;
    if (dpi >= 144.0f) return 2;  // 2x Retina / 150% Windows
    return 1;
}

struct CursorCache {
    SDL_Cursor* normal = nullptr;
    SDL_Cursor* move = nullptr;
    SDL_Cursor* attack = nullptr;
    SDL_Cursor* capture = nullptr;
    SDL_Cursor* carryallDrop = nullptr;

    ~CursorCache() {
        if(normal) {
            SDL_FreeCursor(normal);
            normal = nullptr;
        }
        if(move) {
            SDL_FreeCursor(move);
            move = nullptr;
        }
        if(attack) {
            SDL_FreeCursor(attack);
            attack = nullptr;
        }
        if(capture) {
            SDL_FreeCursor(capture);
            capture = nullptr;
        }
        if(carryallDrop) {
            SDL_FreeCursor(carryallDrop);
            carryallDrop = nullptr;
        }
    }
};

CursorCache& getCursorCache() {
    static CursorCache cache;
    return cache;
}

inline Uint32 getPixelValue(SDL_Surface* surface, int x, int y) {
    const Uint8* p = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch + x * surface->format->BytesPerPixel;
    switch(surface->format->BytesPerPixel) {
        case 1:
            return *p;
        case 2:
            return *reinterpret_cast<const Uint16*>(p);
        case 3:
            #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                return p[0] << 16 | p[1] << 8 | p[2];
            #else
                return p[0] | (p[1] << 8) | (p[2] << 16);
            #endif
        case 4:
            return *reinterpret_cast<const Uint32*>(p);
        default:
            return 0;
    }
}

SDL_Point findTopLeftOpaquePixel(SDL_Surface* surface) {
    SDL_Point hotspot{surface->w / 2, surface->h / 2};

    Uint32 colorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(surface, &colorKey) == 0;

    const bool needsLock = SDL_MUSTLOCK(surface);
    if(needsLock) {
        if(SDL_LockSurface(surface) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CursorManager: Failed to lock cursor surface: %s", SDL_GetError());
            return hotspot;
        }
    }

    for(int y = 0; y < surface->h; ++y) {
        for(int x = 0; x < surface->w; ++x) {
            Uint32 pixel = getPixelValue(surface, x, y);
            if(!hasColorKey || pixel != colorKey) {
                hotspot.x = x;
                hotspot.y = y;
                if(needsLock) {
                    SDL_UnlockSurface(surface);
                }
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "CursorManager: detected arrow hotspot at (%d,%d)", hotspot.x, hotspot.y);
                return hotspot;
            }
        }
    }

    if(needsLock) {
        SDL_UnlockSurface(surface);
    }

    return hotspot;
}

SDL_Cursor* createColorCursorSafe(SDL_Surface* source, int hotspotX, int hotspotY, int scale, SDL_SystemCursor fallback) {
#if defined(_WIN32)
    (void) source;
    (void) hotspotX;
    (void) hotspotY;
    (void) scale;
    return SDL_CreateSystemCursor(fallback);
#else
    if(source == nullptr) {
        return SDL_CreateSystemCursor(fallback);
    }

    SDL_Surface* scaled = scaleSurface(source, scale);
    SDL_Surface* cursorSource = scaled != nullptr ? scaled : source;
    sdl2::surface_ptr converted{ SDL_ConvertSurfaceFormat(cursorSource, SDL_PIXELFORMAT_ARGB8888, 0) };
    if(scaled != nullptr) {
        SDL_FreeSurface(scaled);
    }

    if(converted == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CursorManager: failed to convert cursor surface: %s", SDL_GetError());
        return SDL_CreateSystemCursor(fallback);
    }

    const int clampedX = std::clamp(hotspotX * scale, 0, converted->w - 1);
    const int clampedY = std::clamp(hotspotY * scale, 0, converted->h - 1);
    SDL_Cursor* cursor = SDL_CreateColorCursor(converted.get(), clampedX, clampedY);
    if(cursor == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CursorManager: failed to create color cursor: %s", SDL_GetError());
        return SDL_CreateSystemCursor(fallback);
    }

    return cursor;
#endif
}
}

CursorManager::CursorManager() : 
    normalCursor(nullptr),
    moveCursor(nullptr),
    attackCursor(nullptr),
    captureCursor(nullptr),
    carryallDropCursor(nullptr),
    initialized(false) {
}

CursorManager::~CursorManager() {
    cleanup();
}

void CursorManager::initialize() {
    if (initialized) {
        return;
    }

    auto& cache = getCursorCache();

    if(cache.normal == nullptr) {
        const int scale = getEffectiveCursorScale();
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                       "CursorManager: using cursor scale %dx (setting=%d)", scale, settings.video.cursorScale);

        SDL_Surface* normalSurface = pGFXManager->getUIGraphicSurface(UI_CursorNormal);
        SDL_Surface* moveSurface = pGFXManager->getUIGraphicSurface(UI_CursorMove_Zoomlevel0);
        SDL_Surface* attackSurface = pGFXManager->getUIGraphicSurface(UI_CursorAttack_Zoomlevel0);
        SDL_Surface* captureSurface = pGFXManager->getUIGraphicSurface(UI_CursorCapture_Zoomlevel0);
        SDL_Surface* carryallDropSurface = pGFXManager->getUIGraphicSurface(UI_CursorCarryallDrop_Zoomlevel0);

        if (normalSurface) {
            SDL_Point hotspot = findTopLeftOpaquePixel(normalSurface);
            cache.normal = createColorCursorSafe(normalSurface, hotspot.x, hotspot.y, scale, SDL_SYSTEM_CURSOR_ARROW);
        }
        if (moveSurface) {
            cache.move = createColorCursorSafe(moveSurface, moveSurface->w / 2, moveSurface->h / 2, scale, SDL_SYSTEM_CURSOR_SIZEALL);
        }
        if (attackSurface) {
            cache.attack = createColorCursorSafe(attackSurface, attackSurface->w / 2, attackSurface->h / 2, scale, SDL_SYSTEM_CURSOR_CROSSHAIR);
        }
        if (captureSurface) {
            cache.capture = createColorCursorSafe(captureSurface, captureSurface->w / 2, captureSurface->h / 2, scale, SDL_SYSTEM_CURSOR_HAND);
        }
        if (carryallDropSurface) {
            cache.carryallDrop = createColorCursorSafe(carryallDropSurface, carryallDropSurface->w / 2, carryallDropSurface->h / 2, scale, SDL_SYSTEM_CURSOR_SIZEALL);
        }
    }

    normalCursor = cache.normal;
    moveCursor = cache.move;
    attackCursor = cache.attack;
    captureCursor = cache.capture;
    carryallDropCursor = cache.carryallDrop;

    // Set default cursor
    if (normalCursor) {
        SDL_SetCursor(normalCursor);
        SDL_ShowCursor(SDL_ENABLE);
    }

    initialized = true;
}

void CursorManager::cleanup() {
    initialized = false;
    normalCursor = nullptr;
    moveCursor = nullptr;
    attackCursor = nullptr;
    captureCursor = nullptr;
    carryallDropCursor = nullptr;
}

void CursorManager::setCursorMode(int mode) {
    if (!initialized) {
        return;
    }

    SDL_Cursor* cursorToSet = normalCursor; // Default fallback

    switch (mode) {
        case Game::CursorMode_Normal:
        case Game::CursorMode_Placing:
            cursorToSet = normalCursor;
            break;
        case Game::CursorMode_Move:
            cursorToSet = moveCursor ? moveCursor : normalCursor;
            break;
        case Game::CursorMode_Attack:
            cursorToSet = attackCursor ? attackCursor : normalCursor;
            break;
        case Game::CursorMode_Capture:
            cursorToSet = captureCursor ? captureCursor : normalCursor;
            break;
        case Game::CursorMode_CarryallDrop:
            cursorToSet = carryallDropCursor ? carryallDropCursor : normalCursor;
            break;
        default:
            cursorToSet = normalCursor;
            break;
    }

    if (cursorToSet) {
        SDL_SetCursor(cursorToSet);
    }
}

bool CursorManager::canSetCursorMode(int mode, const std::vector<Uint32>& selectedObjects) {
    if (selectedObjects.empty()) {
        return mode == Game::CursorMode_Normal || mode == Game::CursorMode_Placing;
    }

    for (Uint32 objectID : selectedObjects) {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if (!pObject) {
            continue;
        }

        switch (mode) {
            case Game::CursorMode_Move:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                    return true;
                }
                break;
            case Game::CursorMode_Attack:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable() && pObject->canAttack()) {
                    return true;
                } else if ((pObject->getItemID() == Structure_Palace) && 
                          ((pObject->getOwner()->getHouseID() == HOUSE_HARKONNEN) || (pObject->getOwner()->getHouseID() == HOUSE_SARDAUKAR))) {
                    Palace* pPalace = static_cast<Palace*>(pObject);
                    if (pPalace->isSpecialWeaponReady()) {
                        return true;
                    }
                }
                break;
            case Game::CursorMode_Capture:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable() && pObject->canAttack() && pObject->isInfantry()) {
                    return true;
                }
                break;
            case Game::CursorMode_CarryallDrop:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                    return true;
                }
                break;
            case Game::CursorMode_Normal:
            case Game::CursorMode_Placing:
                return true;
        }
    }

    return false;
}
