#include <structures/AdvancedWindTrap.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <ScreenBorder.h>
#include <mmath.h>

#include <GUI/ObjectInterfaces/WindTrapInterface.h>
#include <Definitions.h>
#include <misc/DrawingRectHelper.h>

AdvancedWindTrap::AdvancedWindTrap(House* newOwner) : StructureBase(newOwner) {
    AdvancedWindTrap::init();

    setHealth(getMaxHealth());
}

AdvancedWindTrap::AdvancedWindTrap(InputStream& stream) : StructureBase(stream) {
    AdvancedWindTrap::init();
}

void AdvancedWindTrap::init() {
    itemID = Structure_AdvancedWindTrap;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;

    graphicID = ObjPic_AdvancedWindTrap;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = NUM_WINDTRAP_ANIMATIONS_PER_ROW;
    numImagesY = (2 + NUM_WINDTRAP_ANIMATIONS + NUM_WINDTRAP_ANIMATIONS_PER_ROW - 1) / NUM_WINDTRAP_ANIMATIONS_PER_ROW;
    firstAnimFrame = 0;
    lastAnimFrame = 2 + NUM_WINDTRAP_ANIMATIONS - 1;
}

AdvancedWindTrap::~AdvancedWindTrap() = default;

bool AdvancedWindTrap::update() {
    bool bResult = StructureBase::update();

    if(bResult) {
        if(justPlacedTimer <= 0) {
            curAnimFrame = 2 + ((currentGame->getGameCycleCount()/8) % NUM_WINDTRAP_ANIMATIONS);
        }

        auto* citySim = currentGame->getCitySimulation();
        if (citySim) {
            citySim->registerPowerSource(location.x, location.y, getProducedPower());
        }
    }

    return bResult;
}

void AdvancedWindTrap::blitToScreen() {
    // Draw the building via the base class (structure sprite + smoke)
    StructureBase::blitToScreen();

    if (fogged) return;

    // Draw animated house-colored flags at top-left and bottom-right corners
    SDL_Texture* flagTex = pGFXManager->getZoomedObjPic(ObjPic_CornerFlag, getOwner()->getHouseID(), currentZoomlevel);
    if (!flagTex) return;

    // Flag frame size is derived from the loaded texture height (supports both
    // old 7px and new 48px flag sprites).  Zoom levels scale linearly.
    int baseFlagPx = 7;  // fallback
    {
        int texW = 0, texH = 0;
        SDL_QueryTexture(flagTex, nullptr, nullptr, &texW, &texH);
        if (texH > 0) baseFlagPx = texH;
    }
    const int flagPx = baseFlagPx;
    const int flagFrame = (currentGame->getGameCycleCount() / STRUCTURE_ANIMATIONTIMER) % 2;

    // Building screen rect (same calculation as StructureBase::blitToScreen)
    SDL_Rect buildDest = calcSpriteDrawingRect(graphic[currentZoomlevel],
                                                screenborder->world2screenX(lround(realX)),
                                                screenborder->world2screenY(lround(realY)),
                                                numImagesX, numImagesY);

    SDL_Rect flagSrc = { flagFrame * flagPx, 0, flagPx, flagPx };

    // Top-left corner
    SDL_Rect tlDst = { buildDest.x, buildDest.y, flagPx, flagPx };
    SDL_RenderCopy(renderer, flagTex, &flagSrc, &tlDst);

    // Bottom-right corner
    SDL_Rect brDst = { buildDest.x + buildDest.w - flagPx, buildDest.y + buildDest.h - flagPx, flagPx, flagPx };
    SDL_RenderCopy(renderer, flagTex, &flagSrc, &brDst);
}

void AdvancedWindTrap::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    StructureBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

ObjectInterface* AdvancedWindTrap::getInterfaceContainer() {
    if((pLocalHouse == owner) || (debug == true)) {
        return WindTrapInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

int AdvancedWindTrap::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[Structure_AdvancedWindTrap][originalHouseID].power);
    FixPoint ratio = getHealth() / getMaxHealth();
    return lround(ratio * nominal);
}
