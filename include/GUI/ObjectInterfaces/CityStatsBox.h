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
    void attachTo(VBox& parent, Uint32 color, bool isZone = false) {
        forceShowPop_ = isZone;

        roleLabel_     .setTextFontSize(11);
        populationLabel_.setTextFontSize(11);
        landValueLabel_.setTextFontSize(11);
        pollutionLabel_.setTextFontSize(11);
        crimeLabel_    .setTextFontSize(11);

        roleLabel_     .setTextColor(color);
        populationLabel_.setTextColor(color);
        landValueLabel_.setTextColor(color);
        pollutionLabel_.setTextColor(color);
        crimeLabel_    .setTextColor(color);

        parent.addWidget(&roleLabel_, 0.005);
        parent.addWidget(&populationLabel_, 0.005);
        parent.addWidget(&landValueLabel_, 0.005);
        parent.addWidget(&pollutionLabel_, 0.005);
        parent.addWidget(&crimeLabel_,     0.005);
    }

    void update(StructureBase* pStructure) {
        if (!pStructure) return;

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
        } else {
            level = pStructure->getCityOccupancy();
        }

        roleLabel_.setText(" " + roleStringFor(itemID));

        // Pop line: shown for any city-role structure (zones AND the
        // non-zone Refinery/Silo/Radar/etc.) — it's the way the player
        // knows that an "empty" factory has nobody working in it yet.
        const bool showPop = forceShowPop_ || (role != DuneCity::CityRole::None);
        populationLabel_.setVisible(showPop);
        if (showPop) {
            const int pop = DuneCity::getZonePopulation(itemID, level);
            std::string text = " Pop: " + std::to_string(pop);
            if (maxLevel > 0) {
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

            landValueLabel_.setText(" Value: " + std::to_string(landValue));
            if (ownEmission > 0) {
                pollutionLabel_.setText(" Pollution: " + std::to_string(pollution)
                                        + " (+" + std::to_string(ownEmission) + ")");
            } else {
                pollutionLabel_.setText(" Pollution: " + std::to_string(pollution));
            }
            crimeLabel_   .setText(" Crime: "  + std::to_string(crimeRate));
        } else {
            landValueLabel_.setText(" Value: \xE2\x80\x94");
            pollutionLabel_.setText(" Pollution: \xE2\x80\x94");
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
            case Structure_Refinery:         return "Role: Seaport";
            case Structure_Silo:             return "Role: C-low";
            case Structure_Radar:            return "Role: C-medium";
            case Structure_HighTechFactory:  return "Role: C-high";
            case Structure_IX:               return "Role: C-high";
            case Structure_LightFactory:     return "Role: I-medium";
            case Structure_HeavyFactory:     return "Role: I-high";
            case Structure_RepairYard:       return "Role: I-high";
            case Structure_StarPort:         return "Role: Airport";
            case Structure_Palace:           return "Role: Palace (Residential)";
            case Structure_Barracks:         return "Role: Police";
            case Structure_WOR:              return "Role: Police HQ";
            case Structure_GunTurret:        return "Role: Park + 1/4 Police";
            case Structure_RocketTurret:     return "Role: Park + 1/4 Police";
            case Structure_Wall:             return "Role: Park bonus";
            case Structure_WindTrap:         return "Role: Coal Power";
            case Structure_NuclearPlant:     return "Role: Nuclear Power";
            default:                         return "Role: \xE2\x80\x94";
        }
    }

    Label roleLabel_;
    Label populationLabel_;
    Label landValueLabel_;
    Label pollutionLabel_;
    Label crimeLabel_;
    bool  forceShowPop_ = false;
};

#endif // CITYSTATSBOX_H
