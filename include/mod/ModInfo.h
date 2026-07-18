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

#ifndef MODINFO_H
#define MODINFO_H

#include <mod/ModMentatConfig.h>

#include <string>
#include <vector>

/**
 * Optional registration for the fixed generic ninth-house slot.
 * Content remains entirely mod-owned.
 */
struct CustomHouseInfo {
    bool enabled = false;
    std::string displayName;
    char scenarioLetter = '?';
    std::string regionPrefix;
    int paletteIndex = 0;
    int fallbackHouse = 0;
    std::string heraldAsset;
    std::string houseNameVoiceAsset;
    double voicePlaybackRate = 1.0;
    double voiceGain = 1.0;
};

/**
 * Checksums for mod verification in multiplayer.
 * Each hash is a 16-character hex string (FNV-1a).
 */
struct ModChecksums {
    std::string objectData;      ///< Hash of ObjectData (unit/structure stats)
    std::string quantBotConfig;  ///< Hash of QuantBot AI config
    std::string gameOptions;     ///< Hash of game options/rules
    std::string customHouse;     ///< Hash of optional CustomHouse.ini registration
    std::string combined;        ///< Combined hash of all synchronized configuration
    
    bool operator==(const ModChecksums& other) const {
        return combined == other.combined;
    }
    
    bool operator!=(const ModChecksums& other) const {
        return !(*this == other);
    }
};

/**
 * Metadata about a mod.
 */
struct ModInfo {
    std::string name;            ///< Mod folder name (e.g., "vanilla", "balanced-warfare")
    std::string displayName;     ///< Human-readable name
    std::string author;          ///< Mod author
    std::string description;     ///< Short description
    std::string version;         ///< Mod version (user-defined, e.g., "1.0.0")
    std::string gameVersion;     ///< Game version this mod was created for
    ModChecksums checksums;      ///< Cached checksums
    CustomHouseInfo customHouse; ///< Optional generic ninth-house registration
    std::vector<ModMentatInfo> mentats; ///< Optional active-mod Mentat presentations by house ID
    
    bool hasObjectData;          ///< Does this mod have ObjectData.ini?
    bool hasQuantBotConfig;      ///< Does this mod have QuantBot Config.ini?
    bool hasGameOptions;         ///< Does this mod have GameOptions.ini?

    bool enablesCityMode = false; ///< When true, DuneCity city-sim features are active for this mod.
};

#endif // MODINFO_H
