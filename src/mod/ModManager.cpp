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

#include <mod/ModManager.h>
#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/exceptions.h>
#include <Definitions.h>
#include <globals.h>
#include <config.h>

#include <SDL.h>

#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <list>

// File names
static const char* ACTIVE_MOD_FILE = "active_mod.txt";
static const char* MOD_INI_FILE = "mod.ini";
static const char* OBJECT_DATA_FILE = "ObjectData.ini";
static const char* QUANTBOT_CONFIG_FILE = "QuantBot Config.ini";
static const char* GAME_OPTIONS_FILE = "GameOptions.ini";
static const char* VANILLA_MOD_NAME = "vanilla";
static const char* DUNECITY_MOD_NAME = "dunecity";

// Install config file names (with .default suffix)
static const char* OBJECT_DATA_DEFAULT = "ObjectData.ini.default";
static const char* QUANTBOT_CONFIG_DEFAULT = "QuantBot Config.ini.default";

ModManager& ModManager::instance() {
    static ModManager instance;
    return instance;
}

ModManager::ModManager()
    : activeMod(VANILLA_MOD_NAME)
    , checksumsDirty(true)
    , initialized(false)
{
}

bool ModManager::isInitialized() const {
    return initialized;
}

ModManager::~ModManager() = default;

void ModManager::initialize() {
    // Get mods base path in user config directory
    char tmp[FILENAME_MAX];
    if (fnkdat("mods", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for mods directory!");
    }
    modsBasePath = std::string(tmp);
    
    // Create mods directory if it doesn't exist
    // Note: createDir() handles "already exists" case gracefully
    createDir(modsBasePath);
    
    // Seed vanilla mod if needed
    if (!modExists(VANILLA_MOD_NAME) || vanillaNeedsReseed()) {
        seedVanillaFromDefaults();
    }

    // Seed built-in dunecity mod if needed
    if (!modExists(DUNECITY_MOD_NAME) || dunecityNeedsReseed()) {
        seedDunecityFromDefaults();
    }
    
    // Load active mod from file
    loadActiveMod();
    
    // Verify active mod exists, fall back to vanilla if not
    if (!modExists(activeMod)) {
        SDL_Log("ModManager: Active mod '%s' not found, falling back to vanilla", activeMod.c_str());
        activeMod = VANILLA_MOD_NAME;
        saveActiveMod();
    }
    
    initialized = true;
    SDL_Log("ModManager: Initialized with active mod '%s'", activeMod.c_str());
}

std::string ModManager::getActiveModName() const {
    return activeMod;
}

bool ModManager::isCityModeActive() const {
    if (!initialized) {
        return false;
    }
    if (!modExists(activeMod)) {
        return false;
    }
    return getModInfo(activeMod).enablesCityMode;
}

bool ModManager::setActiveMod(const std::string& name) {
    if (!modExists(name)) {
        SDL_Log("ModManager: Cannot activate mod '%s' - does not exist", name.c_str());
        return false;
    }
    
    activeMod = name;
    checksumsDirty = true;
    saveActiveMod();
    
    SDL_Log("ModManager: Activated mod '%s'", name.c_str());
    return true;
}

bool ModManager::modExists(const std::string& name) const {
    std::string modPath = getModPath(name);
    // Just check if mod.json exists - if file exists, directory must exist
    return existsFile(modPath + "/" + MOD_INI_FILE);
}

std::vector<ModInfo> ModManager::listMods() const {
    std::vector<ModInfo> mods;
    
    SDL_Log("ModManager::listMods() - modsBasePath: %s", modsBasePath.c_str());
    
    // List directories in mods folder
    std::list<std::string> entries = getDirectoryList(modsBasePath);
    
    SDL_Log("ModManager::listMods() - found %zu directory entries", entries.size());
    
    for (const std::string& entry : entries) {
        std::string modPath = modsBasePath + "/" + entry;
        std::string jsonPath = modPath + "/" + MOD_INI_FILE;
        SDL_Log("ModManager::listMods() - checking entry '%s', json exists: %d", 
                entry.c_str(), existsFile(jsonPath) ? 1 : 0);
        if (existsFile(jsonPath)) {
            mods.push_back(getModInfo(entry));
        }
    }
    
    SDL_Log("ModManager::listMods() - returning %zu mods", mods.size());
    return mods;
}

ModInfo ModManager::getModInfo(const std::string& name) const {
    ModInfo info;
    info.name = name;
    
    std::string modPath = getModPath(name);
    
    // Check which files exist
    info.hasObjectData = existsFile(modPath + "/" + OBJECT_DATA_FILE);
    info.hasQuantBotConfig = existsFile(modPath + "/" + QUANTBOT_CONFIG_FILE);
    info.hasGameOptions = existsFile(modPath + "/" + GAME_OPTIONS_FILE);
    
    // Read mod.json if it exists
    std::string jsonPath = modPath + "/" + MOD_INI_FILE;
    if (existsFile(jsonPath)) {
        info = readModIni(modPath);
        info.name = name;  // Ensure name matches folder
    } else {
        info.displayName = name;
        info.author = "Unknown";
        info.description = "";
        info.gameVersion = "";
    }
    
    return info;
}

std::string ModManager::getActiveObjectDataPath() const {
    std::string modPath = getModPath(activeMod);
    std::string filePath = modPath + "/" + OBJECT_DATA_FILE;
    
    // Fall back to vanilla if mod doesn't have this file
    if (!existsFile(filePath) && activeMod != VANILLA_MOD_NAME) {
        filePath = getModPath(VANILLA_MOD_NAME) + "/" + OBJECT_DATA_FILE;
    }
    
    // Final fallback to install template if vanilla also missing
    if (!existsFile(filePath)) {
        std::string templatePath = getInstallConfigPath() + "/" + OBJECT_DATA_DEFAULT;
        if (existsFile(templatePath)) {
            SDL_Log("ModManager: ObjectData.ini missing, using template: %s", templatePath.c_str());
            return templatePath;
        }
    }
    
    return filePath;
}

std::string ModManager::getActiveQuantBotConfigPath() const {
    std::string modPath = getModPath(activeMod);
    std::string filePath = modPath + "/" + QUANTBOT_CONFIG_FILE;
    
    // Fall back to vanilla if mod doesn't have this file
    if (!existsFile(filePath) && activeMod != VANILLA_MOD_NAME) {
        filePath = getModPath(VANILLA_MOD_NAME) + "/" + QUANTBOT_CONFIG_FILE;
    }
    
    // Final fallback to install template if vanilla also missing
    if (!existsFile(filePath)) {
        std::string templatePath = getInstallConfigPath() + "/" + QUANTBOT_CONFIG_DEFAULT;
        if (existsFile(templatePath)) {
            SDL_Log("ModManager: QuantBot Config.ini missing, using template: %s", templatePath.c_str());
            return templatePath;
        }
    }
    
    return filePath;
}

std::string ModManager::getActiveGameOptionsPath() const {
    std::string modPath = getModPath(activeMod);
    std::string filePath = modPath + "/" + GAME_OPTIONS_FILE;
    
    // Fall back to vanilla if mod doesn't have this file
    if (!existsFile(filePath) && activeMod != VANILLA_MOD_NAME) {
        filePath = getModPath(VANILLA_MOD_NAME) + "/" + GAME_OPTIONS_FILE;
    }
    
    return filePath;
}

SettingsClass::GameOptionsClass ModManager::loadEffectiveGameOptions(
    const SettingsClass::GameOptionsClass& baseOptions) const {
    
    // Start with base options
    SettingsClass::GameOptionsClass result = baseOptions;
    
    // If vanilla mod or no mod system, just return base options
    if (!initialized || activeMod == VANILLA_MOD_NAME) {
        return result;
    }
    
    // Try to load mod's GameOptions.ini
    std::string gameOptionsPath = getActiveGameOptionsPath();
    if (!existsFile(gameOptionsPath)) {
        return result;
    }
    
    try {
        std::ifstream file(gameOptionsPath);
        if (!file.is_open()) {
            return result;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line[0] == '[') continue; // Skip section headers
            
            // Parse key = value
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // Trim whitespace
            auto trim = [](std::string& s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
            };
            trim(key);
            trim(value);
            
            // Apply overrides
            auto parseBool = [](const std::string& v) {
                return v == "true" || v == "1" || v == "yes";
            };
            
            if (key == "Game Speed") result.gameSpeed = std::stoi(value);
            else if (key == "Concrete Required") result.concreteRequired = parseBool(value);
            else if (key == "Structures Degrade On Concrete") result.structuresDegradeOnConcrete = parseBool(value);
            else if (key == "Fog of War") result.fogOfWar = parseBool(value);
            else if (key == "Start with Explored Map") result.startWithExploredMap = parseBool(value);
            else if (key == "Instant Build") result.instantBuild = parseBool(value);
            else if (key == "Only One Palace") result.onlyOnePalace = parseBool(value);
            else if (key == "Rocket-Turrets Need Power") result.rocketTurretsNeedPower = parseBool(value);
            else if (key == "Sandworms Respawn") result.sandwormsRespawn = parseBool(value);
            else if (key == "Killed Sandworms Drop Spice") result.killedSandwormsDropSpice = parseBool(value);
            else if (key == "Manual Carryall Drops") result.manualCarryallDrops = parseBool(value);
            else if (key == "Maximum Number of Units Override") result.maximumNumberOfUnitsOverride = std::stoi(value);
            else if (key == "Maximum Number of Harvesters Override") result.maximumNumberOfHarvestersOverride = std::stoi(value);
            else if (key == "City Effects") result.cityEffects = parseBool(value);
        }
        
        file.close();
        SDL_Log("ModManager: Loaded game options from mod '%s'", activeMod.c_str());
        
    } catch (const std::exception& e) {
        SDL_Log("ModManager: Warning - failed to load game options from mod: %s", e.what());
    }
    
    return result;
}

ModChecksums ModManager::getEffectiveChecksums() const {
    if (checksumsDirty) {
        // Force recalculation by casting away const (cache pattern)
        const_cast<ModManager*>(this)->updateChecksums();
    }
    return cachedChecksums;
}

std::string ModManager::hashFileCanonical(const std::string& path) {
    // Compute canonical FNV-1a hash of an INI-style file.
    // "Canonical" means: skip comments, skip empty lines, trim whitespace,
    // so cosmetic edits don't change the hash.
    std::ifstream file(path);
    if (!file.is_open()) {
        return "FILE_NOT_FOUND";
    }

    std::string contents;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Strip inline " ;" comments (whitespace-prefixed semicolon).
        size_t commentPos = line.find(" ;");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
            end = line.find_last_not_of(" \t");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            }
        }

        if (!line.empty()) {
            contents += line + '\n';
        }
    }
    file.close();

    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    for (char c : contents) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }

    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    return std::string(hashStr);
}

bool ModManager::installedObjectDataDiffersFromDefaults(const std::string& modName) const {
    const std::string installed = getModPath(modName) + "/" + OBJECT_DATA_FILE;
    const std::string defaults = getInstallConfigPath() + "/" + OBJECT_DATA_DEFAULT;

    if (!existsFile(installed) || !existsFile(defaults)) {
        return false;  // Existence check already triggers reseed; don't double-fire.
    }

    return hashFileCanonical(installed) != hashFileCanonical(defaults);
}

void ModManager::updateChecksums() {
    cachedChecksums.objectData = hashFileCanonical(getActiveObjectDataPath());
    cachedChecksums.quantBotConfig = hashFileCanonical(getActiveQuantBotConfigPath());

    // Game options: always hash the mod's GameOptions.ini file (for all mods including vanilla).
    // This ensures all players with the same mod version get the same checksum.
    // Runtime game settings (from Dune Legacy.ini) are synced separately via the game lobby.
    const std::string gameOptionsPath = getActiveGameOptionsPath();
    if (existsFile(gameOptionsPath)) {
        cachedChecksums.gameOptions = hashFileCanonical(gameOptionsPath);
    } else {
        // No GameOptions.ini file - use a constant hash to indicate "no game options file"
        cachedChecksums.gameOptions = "0000000000000000";
    }
    
    // Combined hash
    std::string combined = cachedChecksums.objectData + cachedChecksums.quantBotConfig + cachedChecksums.gameOptions;
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    for (char c : combined) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    cachedChecksums.combined = std::string(hashStr);
    
    checksumsDirty = false;
    SDL_Log("ModManager: Updated checksums - OD:%s QB:%s GO:%s Combined:%s",
            cachedChecksums.objectData.c_str(), cachedChecksums.quantBotConfig.c_str(),
            cachedChecksums.gameOptions.c_str(), cachedChecksums.combined.c_str());
}

void ModManager::setChecksums(const std::string& objectDataHash, 
                              const std::string& quantBotHash,
                              const std::string& gameOptionsHash) {
    // Set checksums from externally computed values (e.g., from loaded in-memory data)
    cachedChecksums.objectData = objectDataHash;
    cachedChecksums.quantBotConfig = quantBotHash;
    cachedChecksums.gameOptions = gameOptionsHash;
    
    // Compute combined hash
    std::string combined = objectDataHash + quantBotHash + gameOptionsHash;
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    for (char c : combined) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    cachedChecksums.combined = std::string(hashStr);
    
    checksumsDirty = false;
}

bool ModManager::createMod(const std::string& name, const std::string& baseMod) {
    if (name.empty() || name == VANILLA_MOD_NAME) {
        SDL_Log("ModManager: Invalid mod name '%s'", name.c_str());
        return false;
    }
    
    if (modExists(name)) {
        SDL_Log("ModManager: Mod '%s' already exists", name.c_str());
        return false;
    }
    
    if (!modExists(baseMod)) {
        SDL_Log("ModManager: Base mod '%s' does not exist", baseMod.c_str());
        return false;
    }
    
    std::string newModPath = getModPath(name);
    std::string baseModPath = getModPath(baseMod);
    
    SDL_Log("ModManager::createMod - newModPath: %s", newModPath.c_str());
    SDL_Log("ModManager::createMod - baseModPath: %s", baseModPath.c_str());
    
    // Create mod directory
    if (!createDir(newModPath)) {
        SDL_Log("ModManager: Failed to create directory for mod '%s'", name.c_str());
        return false;
    }
    
    // Copy files from base mod
    const char* filesToCopy[] = {
        OBJECT_DATA_FILE,
        QUANTBOT_CONFIG_FILE,
        GAME_OPTIONS_FILE
    };
    
    for (const char* file : filesToCopy) {
        std::string srcPath = baseModPath + "/" + file;
        std::string dstPath = newModPath + "/" + file;
        
        SDL_Log("ModManager::createMod - copying %s -> %s (exists: %d)", 
                srcPath.c_str(), dstPath.c_str(), existsFile(srcPath) ? 1 : 0);
        
        if (existsFile(srcPath)) {
            if (copyFile(srcPath, dstPath)) {
                SDL_Log("ModManager::createMod - copied %s successfully", file);
            } else {
                SDL_Log("ModManager::createMod - FAILED to copy %s", file);
            }
        } else {
            SDL_Log("ModManager::createMod - source file %s does not exist!", srcPath.c_str());
        }
    }
    
    // Create mod.ini
    ModInfo info;
    info.name = name;
    info.displayName = name;
    info.author = "User";
    info.description = "Custom mod based on " + baseMod;
    info.gameVersion = VERSION;
    writeModInfo(newModPath, info);
    
    SDL_Log("ModManager: Created mod '%s' from '%s'", name.c_str(), baseMod.c_str());
    return true;
}

bool ModManager::deleteMod(const std::string& name) {
    if (name == VANILLA_MOD_NAME) {
        SDL_Log("ModManager: Cannot delete vanilla mod");
        return false;
    }
    
    if (!modExists(name)) {
        SDL_Log("ModManager: Mod '%s' does not exist", name.c_str());
        return false;
    }
    
    // If this is the active mod, switch to vanilla first
    if (activeMod == name) {
        setActiveMod(VANILLA_MOD_NAME);
    }
    
    std::string modPath = getModPath(name);
    
    // Delete mod files
    const char* filesToDelete[] = {
        OBJECT_DATA_FILE,
        QUANTBOT_CONFIG_FILE,
        GAME_OPTIONS_FILE,
        MOD_INI_FILE
    };
    
    for (const char* file : filesToDelete) {
        std::string filePath = modPath + "/" + file;
        if (existsFile(filePath)) {
            deleteFile(filePath);
        }
    }
    
    // Delete mod directory
    deleteFile(modPath);
    
    SDL_Log("ModManager: Deleted mod '%s'", name.c_str());
    return true;
}

bool ModManager::saveReceivedMod(const std::string& modName, const std::string& packagedData) {
    if (modName.empty() || modName == VANILLA_MOD_NAME) {
        SDL_Log("ModManager: Invalid mod name for save: '%s'", modName.c_str());
        return false;
    }
    
    SDL_Log("ModManager: Saving received mod '%s' (%zu bytes)", modName.c_str(), packagedData.size());
    
    // Create mod directory (createDir handles "already exists" gracefully)
    std::string modPath = getModPath(modName);
    createDir(modPath);
    
    // Unpack the packaged data
    // Format: numFiles (4 bytes) + [nameLen (4 bytes) + name + dataLen (4 bytes) + data] * numFiles
    if (packagedData.size() < sizeof(uint32_t)) {
        SDL_Log("ModManager: Packaged data too small");
        return false;
    }
    
    size_t offset = 0;
    
    // Read number of files
    uint32_t numFiles;
    memcpy(&numFiles, packagedData.data() + offset, sizeof(numFiles));
    offset += sizeof(numFiles);
    
    SDL_Log("ModManager: Unpacking %u files", numFiles);
    
    for (uint32_t i = 0; i < numFiles; i++) {
        if (offset + sizeof(uint32_t) > packagedData.size()) {
            SDL_Log("ModManager: Unexpected end of data at file %u (name length)", i);
            return false;
        }
        
        // Read file name length
        uint32_t nameLen;
        memcpy(&nameLen, packagedData.data() + offset, sizeof(nameLen));
        offset += sizeof(nameLen);
        
        if (offset + nameLen > packagedData.size()) {
            SDL_Log("ModManager: Unexpected end of data at file %u (name)", i);
            return false;
        }
        
        // Read file name
        std::string fileName(packagedData.data() + offset, nameLen);
        offset += nameLen;
        
        if (offset + sizeof(uint32_t) > packagedData.size()) {
            SDL_Log("ModManager: Unexpected end of data at file %u (data length)", i);
            return false;
        }
        
        // Read file data length
        uint32_t dataLen;
        memcpy(&dataLen, packagedData.data() + offset, sizeof(dataLen));
        offset += sizeof(dataLen);
        
        if (offset + dataLen > packagedData.size()) {
            SDL_Log("ModManager: Unexpected end of data at file %u (data)", i);
            return false;
        }
        
        // Read file data
        std::string fileData(packagedData.data() + offset, dataLen);
        offset += dataLen;
        
        // Validate filename (prevent path traversal)
        if (fileName.find('/') != std::string::npos || 
            fileName.find('\\') != std::string::npos ||
            fileName.find("..") != std::string::npos) {
            SDL_Log("ModManager: Invalid filename: '%s'", fileName.c_str());
            continue;
        }
        
        // Write file
        std::string filePath = modPath + "/" + fileName;
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            SDL_Log("ModManager: Failed to create file: %s", filePath.c_str());
            continue;
        }
        
        file.write(fileData.data(), fileData.size());
        file.close();
        
        SDL_Log("ModManager: Saved file '%s' (%u bytes)", fileName.c_str(), dataLen);
    }
    
    SDL_Log("ModManager: Mod '%s' saved successfully", modName.c_str());
    
    // Ensure mod.ini exists - create a basic one if missing
    std::string modIniPath = modPath + "/" + MOD_INI_FILE;
    if (!existsFile(modIniPath)) {
        SDL_Log("ModManager: Creating default mod.ini for received mod '%s'", modName.c_str());
        ModInfo defaultInfo;
        defaultInfo.name = modName;
        defaultInfo.displayName = modName;
        defaultInfo.author = "Downloaded from host";
        defaultInfo.description = "Mod received from multiplayer host";
        defaultInfo.gameVersion = VERSION;
        defaultInfo.version = "";
        writeModInfo(modPath, defaultInfo);
    }
    
    checksumsDirty = true;  // Checksums need recalculation
    return true;
}

void ModManager::seedVanillaFromDefaults() {
    SDL_Log("ModManager: Seeding vanilla mod from install defaults...");
    
    std::string vanillaPath = getModPath(VANILLA_MOD_NAME);
    std::string installConfigPath = getInstallConfigPath();
    
    // Create vanilla directory (createDir handles "already exists" gracefully)
    createDir(vanillaPath);
    
    // Copy ObjectData.ini.default -> ObjectData.ini
    std::string srcObjectData = installConfigPath + "/" + OBJECT_DATA_DEFAULT;
    std::string dstObjectData = vanillaPath + "/" + OBJECT_DATA_FILE;
    if (existsFile(srcObjectData)) {
        copyFile(srcObjectData, dstObjectData);
        SDL_Log("ModManager: Copied %s", OBJECT_DATA_FILE);
    } else {
        SDL_Log("ModManager: Warning - %s not found at %s", OBJECT_DATA_DEFAULT, srcObjectData.c_str());
    }
    
    // Copy QuantBot Config.ini.default -> QuantBot Config.ini
    std::string srcQuantBot = installConfigPath + "/" + QUANTBOT_CONFIG_DEFAULT;
    std::string dstQuantBot = vanillaPath + "/" + QUANTBOT_CONFIG_FILE;
    if (existsFile(srcQuantBot)) {
        copyFile(srcQuantBot, dstQuantBot);
        SDL_Log("ModManager: Copied %s", QUANTBOT_CONFIG_FILE);
    } else {
        SDL_Log("ModManager: Warning - %s not found at %s", QUANTBOT_CONFIG_DEFAULT, srcQuantBot.c_str());
    }
    
    // Create GameOptions.ini with defaults (extracted from Dune Legacy.ini defaults)
    std::string gameOptionsPath = vanillaPath + "/" + GAME_OPTIONS_FILE;
    std::ofstream gameOptionsFile(gameOptionsPath);
    if (gameOptionsFile.is_open()) {
        gameOptionsFile << "# Vanilla Game Options (default values)\n";
        gameOptionsFile << "[Game Options]\n";
        gameOptionsFile << "Game Speed = 16\n";
        gameOptionsFile << "Concrete Required = true\n";
        gameOptionsFile << "Structures Degrade On Concrete = true\n";
        gameOptionsFile << "Fog of War = false\n";
        gameOptionsFile << "Start with Explored Map = false\n";
        gameOptionsFile << "Instant Build = false\n";
        gameOptionsFile << "Only One Palace = false\n";
        gameOptionsFile << "Rocket-Turrets Need Power = false\n";
        gameOptionsFile << "Sandworms Respawn = false\n";
        gameOptionsFile << "Killed Sandworms Drop Spice = false\n";
        gameOptionsFile << "Manual Carryall Drops = false\n";
        gameOptionsFile << "Maximum Number of Units Override = 0\n";
        gameOptionsFile << "Maximum Number of Harvesters Override = -1\n";
        gameOptionsFile << "City Effects = false\n";
        gameOptionsFile.close();
        SDL_Log("ModManager: Created %s", GAME_OPTIONS_FILE);
    }
    
    // Create mod.json
    ModInfo info;
    info.name = VANILLA_MOD_NAME;
    info.displayName = "Vanilla";
    info.author = "Dune City";
    info.description = "Default game settings";
    info.gameVersion = VERSION;
    info.enablesCityMode = false;
    writeModInfo(vanillaPath, info);

    SDL_Log("ModManager: Vanilla mod seeded successfully");
}

void ModManager::seedDunecityFromDefaults() {
    SDL_Log("ModManager: Seeding dunecity mod from install defaults...");

    std::string dunecityPath = getModPath(DUNECITY_MOD_NAME);
    std::string installConfigPath = getInstallConfigPath();

    createDir(dunecityPath);

    // Copy ObjectData.ini.default -> ObjectData.ini
    std::string srcObjectData = installConfigPath + "/" + OBJECT_DATA_DEFAULT;
    std::string dstObjectData = dunecityPath + "/" + OBJECT_DATA_FILE;
    if (existsFile(srcObjectData)) {
        copyFile(srcObjectData, dstObjectData);
        SDL_Log("ModManager: Copied %s (dunecity)", OBJECT_DATA_FILE);
    } else {
        SDL_Log("ModManager: Warning - %s not found at %s", OBJECT_DATA_DEFAULT, srcObjectData.c_str());
    }

    // Copy QuantBot Config.ini.default -> QuantBot Config.ini
    std::string srcQuantBot = installConfigPath + "/" + QUANTBOT_CONFIG_DEFAULT;
    std::string dstQuantBot = dunecityPath + "/" + QUANTBOT_CONFIG_FILE;
    if (existsFile(srcQuantBot)) {
        copyFile(srcQuantBot, dstQuantBot);
        SDL_Log("ModManager: Copied %s (dunecity)", QUANTBOT_CONFIG_FILE);
    } else {
        SDL_Log("ModManager: Warning - %s not found at %s", QUANTBOT_CONFIG_DEFAULT, srcQuantBot.c_str());
    }

    // GameOptions.ini: dunecity tunes a handful of switches relative to vanilla
    // (open map, instant build, sandworm respawn/spice, manual carryall, no
    // concrete cost). The city-mode toggle itself lives in mod.ini.
    std::string gameOptionsPath = dunecityPath + "/" + GAME_OPTIONS_FILE;
    std::ofstream gameOptionsFile(gameOptionsPath);
    if (gameOptionsFile.is_open()) {
        gameOptionsFile << "# Dune City Game Options (default values)\n";
        gameOptionsFile << "[Game Options]\n";
        gameOptionsFile << "Game Speed = 16\n";
        gameOptionsFile << "Concrete Required = false\n";
        gameOptionsFile << "Structures Degrade On Concrete = true\n";
        gameOptionsFile << "Fog of War = false\n";
        gameOptionsFile << "Start with Explored Map = true\n";
        gameOptionsFile << "Instant Build = true\n";
        gameOptionsFile << "Only One Palace = false\n";
        gameOptionsFile << "Rocket-Turrets Need Power = false\n";
        gameOptionsFile << "Sandworms Respawn = true\n";
        gameOptionsFile << "Killed Sandworms Drop Spice = true\n";
        gameOptionsFile << "Manual Carryall Drops = true\n";
        gameOptionsFile << "Maximum Number of Units Override = 0\n";
        gameOptionsFile << "Maximum Number of Harvesters Override = -1\n";
        gameOptionsFile << "City Effects = true\n";  // dunecity mod opts in
        gameOptionsFile.close();
        SDL_Log("ModManager: Created %s (dunecity)", GAME_OPTIONS_FILE);
    }

    ModInfo info;
    info.name = DUNECITY_MOD_NAME;
    info.displayName = "Dune City";
    info.author = "Dune City";
    info.description = "Hybrid RTS + city-builder mode (zones, overlays, city sim).";
    info.gameVersion = VERSION;
    info.enablesCityMode = true;
    writeModInfo(dunecityPath, info);

    SDL_Log("ModManager: Dunecity mod seeded successfully");
}

bool ModManager::dunecityNeedsReseed() const {
    std::string dunecityPath = getModPath(DUNECITY_MOD_NAME);

    std::string objectDataPath = dunecityPath + "/" + OBJECT_DATA_FILE;
    std::string quantBotPath = dunecityPath + "/" + QUANTBOT_CONFIG_FILE;
    std::string gameOptionsPath = dunecityPath + "/" + GAME_OPTIONS_FILE;
    std::string modIniPath = dunecityPath + "/" + MOD_INI_FILE;

    if (!existsFile(objectDataPath)) {
        SDL_Log("ModManager: Dunecity missing %s, needs reseed", OBJECT_DATA_FILE);
        return true;
    }
    if (!existsFile(quantBotPath)) {
        SDL_Log("ModManager: Dunecity missing %s, needs reseed", QUANTBOT_CONFIG_FILE);
        return true;
    }
    if (!existsFile(gameOptionsPath)) {
        SDL_Log("ModManager: Dunecity missing %s, needs reseed", GAME_OPTIONS_FILE);
        return true;
    }
    if (!existsFile(modIniPath)) {
        SDL_Log("ModManager: Dunecity missing %s, needs reseed", MOD_INI_FILE);
        return true;
    }

    ModInfo info = readModIni(dunecityPath);
    if (info.gameVersion != VERSION) {
        SDL_Log("ModManager: Dunecity version mismatch (%s vs %s), needs reseed",
                info.gameVersion.c_str(), VERSION);
        return true;
    }

    // Self-heal: if a previous build wrote the mod.ini without the city-mode
    // flag set, reseed so it gets the correct metadata.
    if (!info.enablesCityMode) {
        SDL_Log("ModManager: Dunecity mod.ini missing 'Enables City Mode = true', needs reseed");
        return true;
    }

    if (installedObjectDataDiffersFromDefaults(DUNECITY_MOD_NAME)) {
        SDL_Log("ModManager: Dunecity %s drifted from defaults, needs reseed", OBJECT_DATA_FILE);
        return true;
    }

    return false;
}

bool ModManager::vanillaNeedsReseed() const {
    std::string vanillaPath = getModPath(VANILLA_MOD_NAME);
    
    // Check if all required files exist
    std::string objectDataPath = vanillaPath + "/" + OBJECT_DATA_FILE;
    std::string quantBotPath = vanillaPath + "/" + QUANTBOT_CONFIG_FILE;
    std::string gameOptionsPath = vanillaPath + "/" + GAME_OPTIONS_FILE;
    std::string modIniPath = vanillaPath + "/" + MOD_INI_FILE;
    
    if (!existsFile(objectDataPath)) {
        SDL_Log("ModManager: Vanilla missing %s, needs reseed", OBJECT_DATA_FILE);
        return true;
    }
    if (!existsFile(quantBotPath)) {
        SDL_Log("ModManager: Vanilla missing %s, needs reseed", QUANTBOT_CONFIG_FILE);
        return true;
    }
    if (!existsFile(gameOptionsPath)) {
        SDL_Log("ModManager: Vanilla missing %s, needs reseed", GAME_OPTIONS_FILE);
        return true;
    }
    if (!existsFile(modIniPath)) {
        SDL_Log("ModManager: Vanilla missing %s, needs reseed", MOD_INI_FILE);
        return true;
    }
    
    // Also reseed if game version doesn't match
    ModInfo info = readModIni(vanillaPath);
    if (info.gameVersion != VERSION) {
        SDL_Log("ModManager: Vanilla version mismatch (%s vs %s), needs reseed",
                info.gameVersion.c_str(), VERSION);
        return true;
    }

    if (installedObjectDataDiffersFromDefaults(VANILLA_MOD_NAME)) {
        SDL_Log("ModManager: Vanilla %s drifted from defaults, needs reseed", OBJECT_DATA_FILE);
        return true;
    }

    return false;
}

std::string ModManager::getModsBasePath() const {
    return modsBasePath;
}

std::string ModManager::getModPath(const std::string& name) const {
    return modsBasePath + "/" + name;
}

void ModManager::loadActiveMod() {
    std::string activeModFile = modsBasePath + "/" + ACTIVE_MOD_FILE;
    
    if (existsFile(activeModFile)) {
        std::ifstream file(activeModFile);
        if (file.is_open()) {
            std::getline(file, activeMod);
            file.close();
            
            // Trim whitespace
            while (!activeMod.empty() && (activeMod.back() == '\n' || activeMod.back() == '\r' || activeMod.back() == ' ')) {
                activeMod.pop_back();
            }
        }
    }
    
    // Default to vanilla if empty or invalid
    if (activeMod.empty()) {
        activeMod = VANILLA_MOD_NAME;
    }
}

void ModManager::saveActiveMod() const {
    std::string activeModFile = modsBasePath + "/" + ACTIVE_MOD_FILE;
    
    std::ofstream file(activeModFile);
    if (file.is_open()) {
        file << activeMod << "\n";
        file.close();
    }
}

ModInfo ModManager::readModIni(const std::string& modPath) const {
    ModInfo info;
    std::string iniPath = modPath + "/" + MOD_INI_FILE;
    
    std::ifstream file(iniPath);
    if (!file.is_open()) {
        return info;
    }
    
    // Simple INI parsing (key = value format)
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        
        size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) continue;
        
        std::string key = line.substr(0, equalsPos);
        std::string value = line.substr(equalsPos + 1);
        
        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
        };
        
        trim(key);
        trim(value);
        
        if (key == "Display Name") info.displayName = value;
        else if (key == "Author") info.author = value;
        else if (key == "Description") info.description = value;
        else if (key == "Version") info.version = value;
        else if (key == "Game Version") info.gameVersion = value;
        else if (key == "Enables City Mode") {
            info.enablesCityMode = (value == "true" || value == "1" || value == "yes");
        }
    }
    
    file.close();
    return info;
}

void ModManager::writeModInfo(const std::string& modPath, const ModInfo& info) const {
    std::string iniPath = modPath + "/" + MOD_INI_FILE;
    
    std::ofstream file(iniPath);
    if (!file.is_open()) {
        SDL_Log("ModManager: Failed to write mod.ini to %s", iniPath.c_str());
        return;
    }
    
    file << "[Mod]\n";
    file << "Display Name = " << info.displayName << "\n";
    file << "Author = " << info.author << "\n";
    file << "Description = " << info.description << "\n";
    file << "Version = " << info.version << "\n";
    file << "Game Version = " << info.gameVersion << "\n";
    file << "Enables City Mode = " << (info.enablesCityMode ? "true" : "false") << "\n";

    file.close();
}

std::string ModManager::getInstallConfigPath() const {
    // Template files (.default) are in the game's data directory under config/
    return getDuneLegacyDataDir() + "/config";
}
