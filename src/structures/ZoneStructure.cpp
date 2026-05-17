/*
 *  This file is part of Dune Legacy.
 */

#include <structures/ZoneStructure.h>

#include <globals.h>
#include <Game.h>
#include <Map.h>
#include <Tile.h>
#include <House.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/ZoneSimulation.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>
#include <dunecity/ZonePower.h>
#include <FileClasses/GFXManager.h>

#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

#include <ObjectBase.h>

ZoneStructure::ZoneStructure(House* newOwner, DuneCity::ZoneType zoneType)
 : StructureBase(newOwner), zoneType_(zoneType) {
    structureSize = Coord(2, 2);
}

void ZoneStructure::setLocation(int xPos, int yPos) {
    StructureBase::setLocation(xPos, yPos);

    if (getLocation().isInvalid()) return;

    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (citySim && currentGameMap) {
        Coord pos = getLocation();
        for (int dy = 0; dy < structureSize.y; dy++) {
            for (int dx = 0; dx < structureSize.x; dx++) {
                Tile* pTile = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                if (pTile) {
                    pTile->setCityZoneType(zoneType_);
                    // Start at density 0 (empty lot) so players see the
                    // Micropolis-style "I just zoned this, building is
                    // about to spawn" sprite before the first growth scan
                    // walks the density up to 1.
                    pTile->setCityZoneDensity(0);
                }
            }
        }
    }

    refreshZonePowerDraw();
}

void ZoneStructure::updateStructureSpecificStuff() {
    // Map the current tile density + sampled land-value tier to the right
    // cell in this zone's sprite atlas (columns = density, rows = value
    // tier). Done every tick so the building art tracks growth and value
    // changes without needing explicit notify hooks from runZoneGrowth.
    if (!currentGameMap) return;
    const Coord pos = getLocation();
    if (pos.isInvalid()) return;

    const Tile* pTile = currentGameMap->getTile(pos.x, pos.y);
    if (!pTile) return;

    const int density = pTile->getCityZoneDensity();

    int valueT = 0;
    if (auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
        citySim && citySim->isInitialized()) {
        const auto& lvMap = citySim->getLandValueMap();
        const int bs = std::max(1, lvMap.getBlockSize());
        const int landValue = lvMap.get(pos.x / bs, pos.y / bs);
        valueT = DuneCity::getZoneValueTier(landValue, numImagesY);
    }

    const int frame = DuneCity::computeZoneSpriteFrame(
        density, valueT, numImagesX, numImagesY);
    firstAnimFrame = lastAnimFrame = curAnimFrame = frame;
}

void ZoneStructure::refreshZonePowerDraw() {
    int density = 0;
    if (currentGameMap && getLocation().isValid()) {
        Coord pos = getLocation();
        // All tiles of a zone share the same density in the current model;
        // sample the top-left tile.
        Tile* pTile = currentGameMap->getTile(pos.x, pos.y);
        if (pTile) {
            density = pTile->getCityZoneDensity();
        }
    }

    int target = DuneCity::getZonePower(itemID, density);
    int delta  = target - registeredZonePower_;
    if (delta != 0 && owner) {
        owner->adjustPowerRequirement(delta);
        registeredZonePower_ = target;
    }
}

ZoneStructure::ZoneStructure(InputStream& stream)
 : StructureBase(stream), zoneType_(DuneCity::ZoneType::None) {
    // StructureBase::init() resets structureSize to (0,0). The fresh-construct
    // ctor sets it directly, but the load path needs to restore it here or
    // blitStructures/updateStructureSpecificStuff iterate over zero tiles and
    // the saved zone never re-renders its building sprite after reload.
    structureSize = Coord(2, 2);
    zoneType_ = static_cast<DuneCity::ZoneType>(stream.readUint8());
}

ZoneStructure::~ZoneStructure() = default;

ObjectInterface* ZoneStructure::getInterfaceContainer() {
    if ((pLocalHouse == owner) || (debug == true)) {
        return ZoneStructureInterface::create(objectID);
    }
    return DefaultObjectInterface::create(objectID);
}

void ZoneStructure::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeUint8(static_cast<uint8_t>(zoneType_));
}

bool ZoneStructure::canBePlacedAt(int x, int y, bool torch) const {
    // Check bounds for all tiles in the structure
    for (int y1 = 0; y1 < structureSize.y; y1++) {
        for (int x1 = 0; x1 < structureSize.x; x1++) {
            if (!currentGameMap->tileExists(x + x1, y + y1)) {
                return false;
            }
        }
    }
    for (int y1 = 0; y1 < structureSize.y; y1++) {
        for (int x1 = 0; x1 < structureSize.x; x1++) {
            Tile* pTile = currentGameMap->getTile(x + x1, y + y1);
            if (pTile->hasANonInfantryGroundObject()) {
                return false;
            }
            auto terrain = pTile->getType();
            if (terrain != Terrain_Sand && terrain != Terrain_Rock && terrain != Terrain_Slab) {
                return false;
            }
        }
    }

    // Trigger milestone notification for first zone built
    if (currentGame && currentGame->getCitySimulation()) {
        currentGame->getCitySimulation()->onFirstZoneBuilt();
    }
    return true;
}

void ZoneStructure::destroy() {
    if (registeredZonePower_ != 0 && owner) {
        owner->adjustPowerRequirement(-registeredZonePower_);
        registeredZonePower_ = 0;
    }

    auto* citySim = currentGame->getCitySimulation();
    if (citySim) {
        Coord pos = getLocation();
        for (int dy = 0; dy < structureSize.y; dy++) {
            for (int dx = 0; dx < structureSize.x; dx++) {
                Tile* pTile = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                if (pTile) {
                    pTile->setCityZoneType(DuneCity::ZoneType::None);
                    pTile->setCityZoneDensity(0);
                }
            }
        }
    }
    StructureBase::destroy();
}

// --- ResidentialZone ---

ResidentialZone::ResidentialZone(House* newOwner)
 : ZoneStructure(newOwner, DuneCity::ZoneType::Residential) {
    ResidentialZone::init();
    setHealth(getMaxHealth());
}

ResidentialZone::ResidentialZone(InputStream& stream)
 : ZoneStructure(stream) {
    ResidentialZone::init();
}

void ResidentialZone::init() {
    itemID = Structure_ZoneResidential;
    owner->incrementStructures(itemID);

    graphicID = ObjPic_ZoneResidential;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // Atlas layout: columns = density (0..3), rows = value tier (0..3).
    // Must match the (numDensity × numValue) layout built by GFXManager
    // so blitToScreen samples a single 2x2 cell instead of squashing the
    // entire atlas into one zone footprint.
    numImagesX = 4;
    numImagesY = 4;
    firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
}

// --- CommercialZone ---

CommercialZone::CommercialZone(House* newOwner)
 : ZoneStructure(newOwner, DuneCity::ZoneType::Commercial) {
    CommercialZone::init();
    setHealth(getMaxHealth());
}

CommercialZone::CommercialZone(InputStream& stream)
 : ZoneStructure(stream) {
    CommercialZone::init();
}

void CommercialZone::init() {
    itemID = Structure_ZoneCommercial;
    owner->incrementStructures(itemID);

    graphicID = ObjPic_ZoneCommercial;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;  // density columns 0..3
    numImagesY = 4;  // value-tier rows 0..3
    firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
}

// --- IndustrialZone ---

IndustrialZone::IndustrialZone(House* newOwner)
 : ZoneStructure(newOwner, DuneCity::ZoneType::Industrial) {
    IndustrialZone::init();
    setHealth(getMaxHealth());
}

IndustrialZone::IndustrialZone(InputStream& stream)
 : ZoneStructure(stream) {
    IndustrialZone::init();
}

void IndustrialZone::init() {
    itemID = Structure_ZoneIndustrial;
    owner->incrementStructures(itemID);

    graphicID = ObjPic_ZoneIndustrial;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;  // density columns 0..3
    numImagesY = 2;  // value-tier rows 0..1 (Industrial only ships 2 tiers)
    firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
}
