#include <units/AmbientAirplane.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Map.h>
#include <Game.h>

AmbientAirplane::AmbientAirplane(House* newOwner) : AirUnit(newOwner)
{
    AmbientAirplane::init();

    setHealth(getMaxHealth());

    attackMode = GUARD;
    respondable = false;
    passedDestination = false;
}

AmbientAirplane::AmbientAirplane(InputStream& stream) : AirUnit(stream)
{
    AmbientAirplane::init();

    passedDestination = stream.readBool();
}

void AmbientAirplane::init()
{
    itemID = Unit_AmbientAirplane;
    owner->incrementUnits(itemID);

    canAttackStuff = false;

    // Placeholder: reuse Carryall graphics (single-frame, no cargo variant)
    graphicID = ObjPic_Carryall;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    shadowGraphic = pGFXManager->getObjPic(ObjPic_CarryallShadow, getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 2;
    drawnFrame = 0;  // always show the "empty" frame
}

AmbientAirplane::~AmbientAirplane() = default;

void AmbientAirplane::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeBool(passedDestination);
}

bool AmbientAirplane::canPass(int xPos, int yPos) const
{
    // Ambient airplane passes freely over everything (like Frigate)
    return currentGameMap->tileExists(xPos, yPos);
}

void AmbientAirplane::checkPos()
{
    AirUnit::checkPos();

    if ((location == destination) && !passedDestination) {
        // Reached the flyover point — pick a guard point off-map and head there
        passedDestination = true;
        setDestination(guardPoint);
    }
}

bool AmbientAirplane::update()
{
    const FixPoint& maxSpeed = currentGame->objectData.data[itemID][originalHouseID].maxspeed;

    // Constant cruise speed — ambient aircraft don't slow for approach
    currentMaxSpeed = maxSpeed;

    if (AirUnit::update() == false) {
        return false;
    }

    // Self-destruct when off-map after passing destination
    if (active && passedDestination) {
        if ((getRealX() < -TILESIZE) || (getRealX() > (currentGameMap->getSizeX() + 1) * TILESIZE)
            || (getRealY() < -TILESIZE) || (getRealY() > (currentGameMap->getSizeY() + 1) * TILESIZE)) {

            setVisible(VIS_ALL, false);
            destroy();
            return false;
        }
    }
    return true;
}

void AmbientAirplane::deploy(const Coord& newLocation)
{
    AirUnit::deploy(newLocation);

    respondable = false;
}
