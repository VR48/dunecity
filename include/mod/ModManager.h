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

#ifndef MODMANAGER_H
#define MODMANAGER_H

#include <mod/ModInfo.h>
#include <DataTypes.h>
#include <string>
#include <vector>

/**
 * Singleton class for managing game mods.
 * 
 * Mods are stored in the user config directory under mods/:
 *   mods/
 *     active_mod.txt          - Contains name of active mod
 *     vanilla/                - Pristine game defaults (seeded from install)
 *       mod.json
 *       ObjectData.ini
 *       QuantBot Config.ini
 *       GameOptions.ini
 *     <user-mod>/             - User-created mods
 *       mod.json
 *       ObjectData.ini
 *       ...
 */
class ModManager {
public:
    /**
     * Get the singleton instance.
     */
    static ModManager& instance();
    
    // Prevent copying
    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;
    
    /**
     * Initialize the mod system. Must be called once at startup.
     * Seeds vanilla mod from install defaults if needed.
     */
    void initialize();
    
    /**
     * Check if ModManager has been initialized.
     * \return true if initialize() has been called
     */
    bool isInitialized() const;
    
    /**
     * Get the name of the currently active mod.
     * \return Mod name (e.g., "vanilla")
     */
    std::string getActiveModName() const;

    /**
     * \return true if the active mod opts into DuneCity city-sim features
     *         (i.e. its mod.ini sets `Enables City Mode = true`).
     */
    bool isCityModeActive() const;
    
    /**
     * Set the active mod by name.
     * \param name Mod folder name
     * \return true if mod exists and was activated
     */
    bool setActiveMod(const std::string& name);
    
    /**
     * Check if a mod exists.
     * \param name Mod folder name
     * \return true if mod exists
     */
    bool modExists(const std::string& name) const;
    
    /**
     * List all available mods.
     * \return Vector of ModInfo for each mod
     */
    std::vector<ModInfo> listMods() const;
    
    /**
     * Get info for a specific mod.
     * \param name Mod folder name
     * \return ModInfo (empty if not found)
     */
    ModInfo getModInfo(const std::string& name) const;
    
    // === Path getters for active mod ===
    
    /**
     * Get path to active mod's ObjectData.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveObjectDataPath() const;
    
    /**
     * Get path to active mod's QuantBot Config.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveQuantBotConfigPath() const;
    
    /**
     * Get path to active mod's GameOptions.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveGameOptionsPath() const;
    
    /**
     * Load game options from active mod's GameOptions.ini.
     * \param baseOptions The base options to override (usually settings.gameOptions)
     * \return Options with mod overrides applied
     */
    SettingsClass::GameOptionsClass loadEffectiveGameOptions(
        const SettingsClass::GameOptionsClass& baseOptions) const;
    
    // === Checksums ===
    
    /**
     * Get checksums for the effective configuration (active mod + fallbacks).
     * \return ModChecksums with hashes for multiplayer verification
     */
    ModChecksums getEffectiveChecksums() const;
    
    /**
     * Recalculate and cache checksums for current state.
     * Call after loading configs.
     */
    void updateChecksums();
    
    /**
     * Set checksums from externally computed values.
     * Use this after loading configs to set in-memory hashes.
     */
    void setChecksums(const std::string& objectDataHash, 
                      const std::string& quantBotHash,
                      const std::string& gameOptionsHash);
    
    // === Mod CRUD ===
    
    /**
     * Create a new mod by copying from another mod.
     * \param name New mod folder name
     * \param baseMod Mod to copy from (default: "vanilla")
     * \return true if created successfully
     */
    bool createMod(const std::string& name, const std::string& baseMod = "vanilla");
    
    /**
     * Delete a mod (cannot delete "vanilla").
     * \param name Mod folder name
     * \return true if deleted successfully
     */
    bool deleteMod(const std::string& name);
    
    /**
     * Save a received mod from network transfer.
     * Unpacks the packaged data and creates the mod directory.
     * \param  modName     Name of the mod to save
     * \param  packagedData  Packaged mod data (from network transfer)
     * \return true if successful
     */
    bool saveReceivedMod(const std::string& modName, const std::string& packagedData);
    
    // === Vanilla seeding ===
    
    /**
     * Seed vanilla mod from install defaults.
     * Called automatically during initialize() if needed.
     */
    void seedVanillaFromDefaults();

    /**
     * Check if vanilla mod needs to be re-seeded (version mismatch).
     * \return true if game version doesn't match vanilla mod version
     */
    bool vanillaNeedsReseed() const;

    /**
     * Seed the built-in "dunecity" mod from install defaults.
     * Same config files as vanilla, but mod.ini sets `Enables City Mode = true`.
     */
    void seedDunecityFromDefaults();

    // DuneCity 1.0.492: seed the Tornie mod
    void seedTornieFromDefaults();
    void seedDune2RFromDefaults();

    /**
     * \return true if the dunecity mod is missing required files or has
     *         a stale game version and should be re-seeded.
     */
    bool dunecityNeedsReseed() const;

    // DuneCity 1.0.492: Tornie mod reseed check
    bool tornieNeedsReseed() const;
    
    // === Paths ===
    
    /**
     * Get the base mods directory path.
     */
    std::string getModsBasePath() const;
    
    /**
     * Get path to a specific mod's directory.
     */
    std::string getModPath(const std::string& name) const;
    
    /**
     * Write mod.ini metadata for a mod.
     */
    void writeModInfo(const std::string& modPath, const ModInfo& info) const;
    
private:
    ModManager();
    ~ModManager();
    
    /**
     * Load active mod name from active_mod.txt.
     */
    void loadActiveMod();
    
    /**
     * Save active mod name to active_mod.txt.
     */
    void saveActiveMod() const;
    
    /**
     * Read mod.ini metadata for a mod.
     */
    ModInfo readModIni(const std::string& modPath) const;
    
    /**
     * Get path to install config defaults directory.
     */
    std::string getInstallConfigPath() const;

    /**
     * Compute a canonical FNV-1a hash of an INI-style file (skips comments and
     * blank lines so cosmetic edits don't change the hash). Returns
     * "FILE_NOT_FOUND" if the file is missing.
     */
    static std::string hashFileCanonical(const std::string& path);

    /**
     * Returns true when the installed mod's ObjectData.ini canonical hash
     * differs from the install's ObjectData.ini.default canonical hash.
     * Used to auto-reseed mods when the shipped defaults are updated.
     */
    bool installedObjectDataDiffersFromDefaults(const std::string& modName) const;
    
    std::string modsBasePath;        ///< Base path for mods directory
    std::string activeMod;           ///< Currently active mod name
    mutable ModChecksums cachedChecksums;  ///< Cached checksums
    mutable bool checksumsDirty;     ///< Do checksums need recalculation?
    bool initialized;                ///< Has initialize() been called?
};

#endif // MODMANAGER_H
