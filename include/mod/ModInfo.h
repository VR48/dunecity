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

#include <string>

/**
 * Checksums for mod verification in multiplayer.
 * Each hash is a 16-character hex string (FNV-1a).
 */
struct ModChecksums {
    std::string objectData;      ///< Hash of ObjectData (unit/structure stats)
    std::string quantBotConfig;  ///< Hash of QuantBot AI config
    std::string gameOptions;     ///< Hash of game options/rules
    std::string combined;        ///< Combined hash of all three
    
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
    
    bool hasObjectData;          ///< Does this mod have ObjectData.ini?
    bool hasQuantBotConfig;      ///< Does this mod have QuantBot Config.ini?
    bool hasGameOptions;         ///< Does this mod have GameOptions.ini?

    bool enablesCityMode = false; ///< When true, DuneCity city-sim features are active for this mod.
};

#endif // MODINFO_H
