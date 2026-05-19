#include <units/AmbientHelicopter.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Map.h>
#include <Game.h>

static constexpr Uint32 kHelicopterLifetime = 3750;  // ~60s at default speed (one city year)

AmbientHelicopter::AmbientHelicopter(House* newOwner) : AirUnit(newOwner)
{
    AmbientHelicopter::init();

    setHealth(getMaxHealth());

    attackMode = GUARD;
    respondable = false;
    spawnCycle = currentGame ? currentGame->getGameCycleCount() : 0;
    lifetime = kHelicopterLifetime;
    orbitCenter = Coord::Invalid();
}

AmbientHelicopter::AmbientHelicopter(InputStream& stream) : AirUnit(stream)
{
    AmbientHelicopter::init();

    spawnCycle = stream.readUint32();
    lifetime = stream.readUint32();
    orbitCenter.x = stream.readSint32();
    orbitCenter.y = stream.readSint32();
}

void AmbientHelicopter::init()
{
    itemID = Unit_AmbientHelicopter;
    owner->incrementUnits(itemID);

    canAttackStuff = false;

    // Placeholder: reuse Ornithopter graphics (3-frame wing animation)
    graphicID = ObjPic_Ornithopter;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    shadowGraphic = pGFXManager->getObjPic(ObjPic_OrnithopterShadow, getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 3;
}

AmbientHelicopter::~AmbientHelicopter() = default;

void AmbientHelicopter::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeUint32(spawnCycle);
    stream.writeUint32(lifetime);
    stream.writeSint32(orbitCenter.x);
    stream.writeSint32(orbitCenter.y);
}

bool AmbientHelicopter::canPass(int xPos, int yPos) const
{
    return currentGameMap->tileExists(xPos, yPos);
}

void AmbientHelicopter::checkPos()
{
    AirUnit::checkPos();

    // When we reach the current waypoint, pick a new one near the orbit center
    if (location == destination && orbitCenter.isValid()) {
        // Deterministic waypoint selection based on objectID and game cycle
        Uint32 seed = getObjectID() * 31u + currentGame->getGameCycleCount();
        int dx = static_cast<int>((seed % 9u)) - 4;         // -4..+4
        int dy = static_cast<int>(((seed / 9u) % 9u)) - 4;  // -4..+4
        int nx = orbitCenter.x + dx;
        int ny = orbitCenter.y + dy;
        // Clamp to map
        if (nx < 1) nx = 1;
        if (ny < 1) ny = 1;
        if (nx >= currentGameMap->getSizeX() - 1) nx = currentGameMap->getSizeX() - 2;
        if (ny >= currentGameMap->getSizeY() - 1) ny = currentGameMap->getSizeY() - 2;
        setDestination(Coord(nx, ny));
    }
}

bool AmbientHelicopter::update()
{
    const FixPoint& maxSpeed = currentGame->objectData.data[itemID][originalHouseID].maxspeed;

    currentMaxSpeed = maxSpeed;

    if (AirUnit::update() == false) {
        return false;
    }

    // Animate wing flapping (same as Ornithopter)
    drawnFrame = static_cast<int>((currentGame->getGameCycleCount() + getObjectID()) / 3 % numImagesY);

    // Self-destruct after lifetime expires
    if (active) {
        Uint32 age = currentGame->getGameCycleCount() - spawnCycle;
        if (age >= lifetime) {
            setVisible(VIS_ALL, false);
            destroy();
            return false;
        }
    }
    return true;
}

void AmbientHelicopter::deploy(const Coord& newLocation)
{
    AirUnit::deploy(newLocation);

    respondable = false;
    if (orbitCenter.isInvalid()) {
        orbitCenter = newLocation;
    }
}
