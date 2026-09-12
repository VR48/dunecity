#include <players/AIDecisionLog.h>
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
#include <dunecity/CitySpritePolicy.h>
#include <dunecity/ZonePower.h>
#include <dunecity/ResidentialPopulation.h>
#include <FileClasses/GFXManager.h>

#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

#include <ObjectBase.h>
#include <ScreenBorder.h>

ZoneStructure::ZoneStructure(House* newOwner, DuneCity::ZoneType zoneType)
 : StructureBase(newOwner), zoneType_(zoneType) {
    structureSize = Coord(2, 2);
}

void ZoneStructure::setLocation(int xPos, int yPos) {
    StructureBase::setLocation(xPos, yPos);
    residentialPopulation_ = 0;

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
    // Stable site variants select among all original models. Industrial
    // animation uses prebuilt phases and does not consume simulation randomness.
    if (!currentGameMap) return;
    const Coord pos = getLocation();
    if (pos.isInvalid()) return;

    const Tile* pTile = currentGameMap->getTile(pos.x, pos.y);
    if (!pTile) return;

    const int density = pTile->getCityZoneDensity();
    skinDensity_ = density;

    // Civic overlay: hospital/church sprites replace the normal zone art.
    // These are single-cell (1×1) atlases loaded as ObjPic_Hospital/Church.
    if (civicOverlay_ != CivicOverlay::None && density > 0) {
        // Keep the cache-refresh ID in sync with the single-cell layout.
        // StructureBase::blitToScreen reloads by graphicID on every draw.
        graphicID = (civicOverlay_ == CivicOverlay::Hospital)
            ? ObjPic_Hospital : ObjPic_Church;
        graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
        numImagesX = 1;
        numImagesY = 1;
        firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
        return;
    }

    // Restore both ID and layout when the civic overlay clears or the lot
    // becomes vacant. Pointer equality cannot identify an atlas layout.
    switch (zoneType_) {
        case DuneCity::ZoneType::Residential: graphicID = ObjPic_ZoneResidential; break;
        case DuneCity::ZoneType::Commercial: graphicID = ObjPic_ZoneCommercial; break;
        case DuneCity::ZoneType::Industrial: graphicID = ObjPic_ZoneIndustrial; break;
        default: return;
    }
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = DuneCity::CitySprites::zoneColumns(zoneType_);
    numImagesY = DuneCity::CitySprites::zoneRows(zoneType_);

    int valueT = 0;
    if (auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
        citySim && citySim->isInitialized()) {
        const auto& lvMap = citySim->getLandValueMap();
        const int bs = std::max(1, lvMap.getBlockSize());
        const int landValue = lvMap.get(pos.x / bs, pos.y / bs);
        valueT = DuneCity::getZoneValueTier(landValue, zoneType_ == DuneCity::ZoneType::Industrial ? 2 : 4);
    }
    skinValueTier_ = valueT;

    const int frame = DuneCity::CitySprites::zoneFrame(
        zoneType_, density, valueT, pos.x, pos.y,
        currentGame->getGameCycleCount(), owner->hasPower(), getResidentialPopulation());
    firstAnimFrame = lastAnimFrame = curAnimFrame = frame;
}

void ZoneStructure::blitToScreen() {
    StructureBase::blitToScreen();
    if(fogged || civicOverlay_ != CivicOverlay::None || owner == nullptr || currentGame == nullptr) {
        return;
    }
    const int anchorX = screenborder->world2screenX(
        lround(realX) + structureSize.x * TILESIZE / 2);
    const int anchorY = screenborder->world2screenY(
        lround(realY) + structureSize.y * TILESIZE);
    pGFXManager->drawDuneCityZone(
        itemID, owner->getHouseID(), currentZoomlevel,
        skinDensity_, skinValueTier_, GFXManager::DuneCityZoneActivity::Idle, 0, anchorX, anchorY);
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
    if (zoneType_ == DuneCity::ZoneType::Residential && getResidentialPopulation() <= 8)
        target = (DuneCity::getZonePower(itemID,1)*getResidentialPopulation()+15)/16;
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
    residentialPopulation_ = DuneCity::ResidentialPopulation::read(stream,
        currentGame ? currentGame->getLoadedSavegameVersion() : SAVEGAMEVERSION);
}

int ZoneStructure::getResidentialPopulation() const {
    if (zoneType_ != DuneCity::ZoneType::Residential) return 0;
    if (residentialPopulation_ != DuneCity::ResidentialPopulation::legacy)
        return residentialPopulation_;
    const auto pos = getLocation();
    const auto* tile = currentGameMap && currentGameMap->tileExists(pos.x,pos.y)
        ? currentGameMap->getTile(pos.x,pos.y) : nullptr;
    return DuneCity::ResidentialPopulation::fromDensity(tile ? tile->getCityZoneDensity() : 0);
}

void ZoneStructure::setResidentialPopulation(int population) {
    if (zoneType_ != DuneCity::ZoneType::Residential) return;
    residentialPopulation_ = DuneCity::ResidentialPopulation::normalize(population);
    const auto pos = getLocation();
    if (currentGameMap && pos.isValid())
        for (int dy=0;dy<structureSize.y;++dy) for (int dx=0;dx<structureSize.x;++dx)
            if (auto* tile = currentGameMap->getTile(pos.x+dx,pos.y+dy))
                tile->setCityZoneDensity(DuneCity::ResidentialPopulation::density(residentialPopulation_));
    refreshZonePowerDraw();
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
    DuneCity::ResidentialPopulation::write(stream,getResidentialPopulation());
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
    int anchoredTiles = 0;
    for (int y1 = 0; y1 < structureSize.y; y1++) {
        for (int x1 = 0; x1 < structureSize.x; x1++) {
            Tile* pTile = currentGameMap->getTile(x + x1, y + y1);
            if (pTile->hasANonInfantryGroundObject()) {
                return false;
            }
            auto terrain = pTile->getType();
            if (!DuneCity::isCityZoneTerrain(terrain)) {
                return false;
            }
            if (DuneCity::isCityBuildableTerrain(terrain)) {
                anchoredTiles++;
            }
        }
    }
    // A lot may reach onto sand, but at least one tile must sit on rock.
    if (anchoredTiles == 0) {
        return false;
    }

    // Trigger milestone notification for first zone built
    if (currentGame && currentGame->getCitySimulation()) {
        currentGame->getCitySimulation()->onFirstZoneBuilt();
    }
    return true;
}

void ZoneStructure::clearZoneState() {
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
}

void ZoneStructure::destroy() {
    clearZoneState();
    StructureBase::destroy();
}

void ZoneStructure::demolish() {
    demolishedByOwner_ = true;
    const Coord pos = getLocation();
    const auto* sim = currentGame->getCitySimulation();
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "zone_demolished", AITelemetry::Record().set("object", objectID).set("item", itemID)
            .set("x",pos.x).set("y",pos.y).set("refund",0)
            .set("density",currentGameMap->getTile(pos.x,pos.y)->getCityZoneDensity())
            .set("pollution",sim ? sim->getPollutionDensityMap().worldGet(pos.x,pos.y) : 0)
            .set("land_value",sim ? sim->getLandValueMap().worldGet(pos.x,pos.y) : 0));
    clearZoneState();
    for (int dy=0; dy<structureSize.y; ++dy) for (int dx=0; dx<structureSize.x; ++dx) {
        if (auto* tile=currentGameMap->getTile(pos.x+dx,pos.y+dy))
            tile->setDestroyedStructureTile(DestroyedStructure_None);
    }
    // Normal destructor unregisters ownership, pathing and selection. Roads and
    // concrete remain; demolition does not spawn soldiers or combat explosions.
    delete this;
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
    // Layout must match the prebuilt atlas and GFXManager metadata.
    numImagesX = DuneCity::CitySprites::residentialColumns;
    numImagesY = DuneCity::CitySprites::residentialRows;
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
    numImagesX = DuneCity::CitySprites::commercialColumns;
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
    numImagesX = DuneCity::CitySprites::industrialColumns;
    numImagesY = DuneCity::CitySprites::industrialRows;
    firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
}
