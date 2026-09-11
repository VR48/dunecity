#include <dunecity/CityStructurePopulation.h>
#ifndef CITYSTATSBOX_H
#define CITYSTATSBOX_H

#include <FileClasses/TextManager.h>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

#include <GUI/Label.h>
#include <GUI/Spacer.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

#include <string>

/**
 * CityStatsBox — composable widget cluster that shows city-sim
 * contributions/effects for a selected structure.
 *
 * Displays:
 *   - Role: which SimCity-Classic effect this building maps to
 *   - Pop: people/jobs the structure contributes at its current level
 *           (zones use tile density; non-zone city-role structures use
 *           StructureBase::cityOccupancy_)
 *   - Land value, Pollution, Crime: at the structure's tile
 *
 * Add to any object interface by:
 *   1) calling attachTo(textVBox, themeColor, isZone)
 *   2) calling update(pStructure) inside the interface's update()
 *
 * Pure presentation; no game state mutation. Safe to instantiate
 * even when city sim isn't initialized — labels just stay blank.
 */
class CityStatsBox {
public:
    void attachTo(VBox& parent, Uint32 color, bool isZone = false, bool showEmissions = false) {
        forceShowPop_ = isZone;
        // Vanilla has no city roles, population or municipal services.
        // Do not allocate empty rows or display invented city statistics.
        if (!currentGame || !currentGame->isCitySimEnabled()) return;

        roleLabel_     .setTextFontSize(11);
        populationLabel_.setTextFontSize(11);
        landValueLabel_.setTextFontSize(11);
        pollutionLabel_.setTextFontSize(11);
        emissionsLabel_.setTextFontSize(11);
        crimeLabel_    .setTextFontSize(11);

        roleLabel_     .setTextColor(color);
        populationLabel_.setTextColor(color);
        landValueLabel_.setTextColor(color);
        pollutionLabel_.setTextColor(color);
        emissionsLabel_.setTextColor(color);
        emissionsLabel_.setVisible(false);
        crimeLabel_    .setTextColor(color);

        const Sint32 lineH = 20;
        parent.addWidget(&roleLabel_, lineH);
        parent.addWidget(&populationLabel_, lineH);
        parent.addWidget(&landValueLabel_, lineH);
        if (showEmissions) parent.addWidget(&emissionsLabel_, lineH);
        parent.addWidget(&pollutionLabel_, lineH);
        parent.addWidget(&crimeLabel_,     lineH);
    }

    void update(StructureBase* pStructure) {
        if (!pStructure || !currentGame || !currentGame->isCitySimEnabled()) return;

        const int itemID = pStructure->getItemID();
        const auto role = DuneCity::getStructureCityRole(itemID);
        const int maxLevel = DuneCity::getStructureMaxLevel(itemID);

        // Read the level the same way the sim does: tile density for
        // zones, occupancy field for non-zone city-role structures.
        int level = 0;
        ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(pStructure);
        if (pZone && currentGameMap) {
            const Coord loc = pZone->getLocation();
            if (currentGameMap->tileExists(loc.x, loc.y)) {
                level = currentGameMap->getTile(loc.x, loc.y)->getCityZoneDensity();
            }
        } else if (role != DuneCity::CityRole::None) {
            const int occupancy = pStructure->getCityOccupancy();
            level = DuneCity::effectiveCityLevel(itemID, std::max(1, occupancy));
        }

        roleLabel_.setText(" " + roleStringFor(itemID));

        // Separate clean wind generation from pollution drifting in from nearby industry.
        const bool isWindtrap = itemID == Structure_WindTrap;
        emissionsLabel_.setVisible(isWindtrap);
        if (isWindtrap) {
            emissionsLabel_.setText(" Emissions: "
                + std::to_string(DuneCity::getPollutionEmission(itemID, level)));
        }

        // Pop line: shown for any city-role structure (zones AND the
        // non-zone Refinery/Silo/Radar/etc.) — it's the way the player
        // knows that an "empty" factory has nobody working in it yet.
        const bool showPop = forceShowPop_ || (role != DuneCity::CityRole::None);
        const bool isTurret = itemID == Structure_RocketTurret || itemID == Structure_GunTurret;
        populationLabel_.setVisible(showPop || isTurret);
        if (isTurret) populationLabel_.setText("Police: 15%");
        if (showPop) {
            std::string text;
            if (itemID == Structure_Palace) {
                const int resPop = DuneCity::getStructurePopulation(pStructure, level);
                const int comPop = DuneCity::getPalaceCommercialPopulation(level);
                text = " R: " + std::to_string(resPop) + " C: " + std::to_string(comPop);
            } else {
                const int pop = DuneCity::getStructurePopulation(pStructure, level);
                text = " Pop: " + std::to_string(pop);
            }
            if (maxLevel > 0 && !(pZone && pZone->getZoneType() == DuneCity::ZoneType::Residential && pZone->getResidentialPopulation() <= 8)) {
                text += " (lvl " + std::to_string(level) + "/" + std::to_string(maxLevel) + ")";
            }
            populationLabel_.setText(text);
        }

        // Tile-local effects from the live city sim. We always show the
        // raw map values whenever the sim is initialized — even if effect
        // scans haven't run yet they'll be 0/0/0 rather than "—".
        auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
        if (citySim && citySim->isInitialized()) {
            const Coord loc = pStructure->getLocation();
            const auto& lvMap   = citySim->getLandValueMap();
            const auto& polMap  = citySim->getPollutionDensityMap();
            const auto& crMap   = citySim->getCrimeRateMap();

            const int bs = lvMap.getBlockSize();
            const int bx = (loc.x >= 0) ? loc.x / bs : 0;
            const int by = (loc.y >= 0) ? loc.y / bs : 0;

            const int landValue  = lvMap .get(bx, by);
            const int pollution  = polMap.get(bx, by);
            const int crimeRate  = crMap .get(bx, by);

            // Show this building's own emission alongside the tile total.
            const int ownEmission = DuneCity::getPollutionEmission(itemID, level);

            landValueLabel_.setText(std::string(" Value: ") + DuneCity::landValueCategory(landValue));
            if (ownEmission > 0) {
                pollutionLabel_.setText(std::string(" Pollution: ")
                                        + DuneCity::pollutionCategory(pollution) + " (emits)");
            } else {
                pollutionLabel_.setText(std::string(isWindtrap ? " Local pollution: " : " Pollution: ")
                                        + DuneCity::pollutionCategory(pollution));
            }
            crimeLabel_   .setText(std::string(" Crime: ") + DuneCity::crimeCategory(crimeRate));
        } else {
            landValueLabel_.setText(" Value: \xE2\x80\x94");
            pollutionLabel_.setText(std::string(isWindtrap ? " Local pollution: " : " Pollution: ")
                                    + "\xE2\x80\x94");
            crimeLabel_   .setText(" Crime: \xE2\x80\x94");
        }
    }

private:
    /// Human-readable SC Classic mapping per the spec discussion.
    static std::string roleStringFor(int itemID) {
        switch (itemID) {
            case Structure_ZoneResidential:  return "Role: Residential";
            case Structure_ZoneCommercial:   return "Role: Commercial";
            case Structure_ZoneIndustrial:   return "Role: Industrial";
            case Structure_Refinery:         return "Role: I-medium";
            case Structure_Silo:             return "Role: I-light";
            case Structure_Radar:            return "Role: C-medium";
            case Structure_HighTechFactory:  return "Role: I-medium";
            case Structure_IX:               return "Role: C-high";
            case Structure_LightFactory:     return "Role: I-light";
            case Structure_HeavyFactory:     return "Role: I-medium";
            case Structure_RepairYard:       return "Role: I-medium";
            case Structure_StarPort:         return "Role: Seaport";
            case Structure_Palace:           return "Role: R+C Palace";
            case Structure_PoliceStation:    return "Role: Police";
            case Structure_Barracks:         return "Role: Infantry";
            case Structure_WOR:              return "Role: Infantry";
            case Structure_GunTurret:        return "Park: 1 fountain";
            case Structure_RocketTurret:     return "Park: 1 fountain";
            case Structure_Wall:             return "Role: Park bonus";
            case Structure_WindTrap:         return "Role: Wind Power";
            case Structure_NuclearPlant:     return "Role: Nuclear Power";
            default:                         return "Role: \xE2\x80\x94";
        }
    }

    Label roleLabel_;
    Label populationLabel_;
    Label landValueLabel_;
    Label pollutionLabel_;
    Label emissionsLabel_;
    Label crimeLabel_;
    bool  forceShowPop_ = false;
};

#endif // CITYSTATSBOX_H
