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
#include <mod/ModPayloadIntegrity.h>
#include <mod/ModTransferValidation.h>
#include <mod/Dune2RAssetManager.h>
#include <mod/CustomHouseConfig.h>
#include <mod/ModMentatConfig.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <FileClasses/TextManager.h>
#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/exceptions.h>
#include <Definitions.h>
#include <globals.h>
#include <config.h>

#include <SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <list>
#include <map>
#include <set>

// File names
static const char* ACTIVE_MOD_FILE = "active_mod.txt";
static const char* MOD_INI_FILE = "mod.ini";
static const char* OBJECT_DATA_FILE = "ObjectData.ini";
static const char* QUANTBOT_CONFIG_FILE = "QuantBot Config.ini";
static const char* GAME_OPTIONS_FILE = "GameOptions.ini";
static const char* VANILLA_MOD_NAME = "vanilla";
static const char* DUNECITY_MOD_NAME = "dunecity";
static const char* TORNIE_MOD_NAME = "Tornie";
static const char* DUNE2R_MOD_NAME = "Dune2R";
static const char* MANAGED_MOD_STAMP = ".dunecity-managed";
static const char* TORNIE_ENGINE_COMPATIBILITY = "dunecity-tornie-engine/1";

// Install config file names (with .default suffix)
static const char* OBJECT_DATA_DEFAULT = "ObjectData.ini.default";
static const char* QUANTBOT_CONFIG_DEFAULT = "QuantBot Config.ini.default";
static const char* CUSTOM_HOUSE_CONFIG = "CustomHouse.ini";

namespace {

std::filesystem::path findBundledModPath(const std::string& modName) {
    const std::filesystem::path dataRoot = getDuneLegacyDataDir();
    const std::filesystem::path candidates[] = {
        dataRoot / "mods" / modName,
        dataRoot / ".." / "mods" / modName,
        dataRoot / ".." / ".." / "mods" / modName,
        dataRoot / ".." / ".." / ".." / "mods" / modName
    };

    for(const auto& candidate : candidates) {
        if(std::filesystem::is_regular_file(candidate / MOD_INI_FILE)) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }
    return {};
}

void appendFingerprintBytes(uint64_t& hash, const std::string& value) {
    constexpr uint64_t prime = 1099511628211ULL;
    for(const unsigned char c : value) {
        hash ^= c;
        hash *= prime;
    }
}

std::string bundledModFingerprint(const std::filesystem::path& source) {
    if(source.empty() || !std::filesystem::is_directory(source)) {
        return {};
    }

    std::vector<std::string> entries;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        if(entry.is_regular_file()) {
            entries.push_back(std::filesystem::relative(entry.path(), source).generic_string()
                              + "\n" + std::to_string(entry.file_size()));
        }
    }
    std::sort(entries.begin(), entries.end());

    uint64_t hash = 14695981039346656037ULL;
    for(const auto& entry : entries) {
        appendFingerprintBytes(hash, entry);
    }

    // Include authoritative metadata contents so same-sized release updates
    // still refresh the installed managed copy.
    for(const char* metadata : {MOD_INI_FILE, "manifest.json", "checksums.sha256"}) {
        std::ifstream file(source / metadata, std::ios::binary);
        if(file) {
            std::ostringstream contents;
            contents << file.rdbuf();
            appendFingerprintBytes(hash, contents.str());
        }
    }

    char value[17];
    std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(hash));
    return value;
}

std::string readManagedModStamp(const std::filesystem::path& installed) {
    std::ifstream file(installed / MANAGED_MOD_STAMP);
    std::string value;
    if(file) {
        std::getline(file, value);
    }
    return value;
}

bool managedModNeedsRefresh(const std::filesystem::path& installed,
                            const std::filesystem::path& bundled) {
    if(bundled.empty()) {
        return false;
    }
    const std::string expected = bundledModFingerprint(bundled);
    return expected.empty() || readManagedModStamp(installed) != expected
           || !std::filesystem::is_regular_file(installed / MOD_INI_FILE);
}

bool refreshManagedMod(const std::string& modName,
                       const std::filesystem::path& destination,
                       const std::filesystem::path& source) {
    if(source.empty()) {
        SDL_Log("ModManager: Warning - bundled %s mod was not found", modName.c_str());
        return false;
    }

    if(modName == TORNIE_MOD_NAME) {
        std::string integrityError;
        if(!ModPayloadIntegrity::verifyChecksummedPayload(source, integrityError,
                                                          MANAGED_MOD_STAMP)) {
            SDL_Log("ModManager: Warning - bundled Tornie payload failed integrity verification: %s",
                    integrityError.c_str());
            return false;
        }
    }

    const std::filesystem::path staged = destination.string() + ".update";
    const std::filesystem::path backup = destination.string() + ".previous";
    try {
        std::filesystem::remove_all(staged);
        std::filesystem::remove_all(backup);
        std::filesystem::copy(source, staged,
            std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing);

        // Dune2R art is downloaded independently of the managed mod shell.
        // Carry user-installed packs into the replacement before the atomic
        // swap so a game update never erases a large, verified download.
        if(modName == DUNE2R_MOD_NAME && std::filesystem::is_directory(destination)) {
            const std::filesystem::path persistentDirectories[] = {
                std::filesystem::path("graphics_hd") / "units",
                std::filesystem::path("graphics_hd") / ".atlas-backups",
                std::filesystem::path("graphics_compact") / "objpics"
            };
            for(const auto& relative : persistentDirectories) {
                const auto installedAssets = destination / relative;
                if(!std::filesystem::is_directory(installedAssets)) {
                    continue;
                }
                const auto stagedAssets = staged / relative;
                std::filesystem::remove_all(stagedAssets);
                std::filesystem::create_directories(stagedAssets.parent_path());
                std::filesystem::copy(installedAssets, stagedAssets,
                    std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing);
            }
            for(const auto* filename : {"asset-catalog-online.ini", ".asset-catalog.previous"}) {
                const auto installedCatalog = destination / filename;
                if(std::filesystem::is_regular_file(installedCatalog)) {
                    std::filesystem::copy_file(installedCatalog, staged / filename,
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        }

        const std::string fingerprint = bundledModFingerprint(source);
        std::ofstream stamp(staged / MANAGED_MOD_STAMP, std::ios::trunc);
        stamp << fingerprint << "\n";
        stamp.close();
        if(fingerprint.empty() || !stamp) {
            throw std::runtime_error("could not write managed-mod stamp");
        }

        if(std::filesystem::exists(destination)) {
            std::filesystem::rename(destination, backup);
        }
        try {
            std::filesystem::rename(staged, destination);
        } catch(...) {
            if(std::filesystem::exists(backup) && !std::filesystem::exists(destination)) {
                std::filesystem::rename(backup, destination);
            }
            throw;
        }
        std::filesystem::remove_all(backup);
        SDL_Log("ModManager: Refreshed managed mod '%s' from %s",
                modName.c_str(), source.string().c_str());
        return true;
    } catch(const std::exception& e) {
        std::error_code ignored;
        std::filesystem::remove_all(staged, ignored);
        SDL_Log("ModManager: Warning - managed mod '%s' refresh failed: %s",
                modName.c_str(), e.what());
        return false;
    }
}

CustomHouseInfo makeTornieGuestCustomHouse(const std::string& activeModName) {
    CustomHouseInfo info;
    if(activeModName != TORNIE_MOD_NAME) {
        return info;
    }

    info.enabled = true;
    info.displayName = "Tharpique";
    info.scenarioLetter = 'T';
    info.regionPrefix = "THA";
    info.paletteIndex = 136;
    info.fallbackHouse = HOUSE_MERCENARY;
    info.heraldAsset = "HeraldTharpique.png";
    info.houseNameVoiceAsset = "OTHARP.VOC";
    info.voicePlaybackRate = 1.06;
    info.voiceGain = 1.15;
    return info;
}

} // namespace

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

    // Seed built-in Tornie mod if needed. DuneCity 1.0.492:
    // both "dunecity" and "Tornie" mods are seeded on first
    // opening per Tornie's OOB. The Tornie mod contains the
    // 8-house campaigns, custom units (Deviator/Flame Tank/
    // Sonic Tank/Elite Siege Tank), custom buildings
    // (Advanced Windtrap), palettes (Custom_IBM.PAL),
    // and VOC files.
    if (!modExists(TORNIE_MOD_NAME) || tornieNeedsReseed()) {
        seedTornieFromDefaults();
    }

    // Dune2R is a bundled graphics payload. Refresh stale installed copies so
    // new manifests and atlases are not hidden by an older user-data folder.
    if (!modExists(DUNE2R_MOD_NAME) || dune2rNeedsReseed()) {
        seedDune2RFromDefaults();
    }

    // Load active mod from file
    loadActiveMod();

    // A failed refresh must never leave startup pointing at a stale bundled
    // payload. Preserve user data, but disable the managed mod until a later
    // launch can refresh it successfully.
    const bool activeManagedModInvalid =
        (activeMod == TORNIE_MOD_NAME && tornieNeedsReseed())
        || (activeMod == DUNE2R_MOD_NAME && dune2rNeedsReseed());

    // Verify the selected mod before any of its metadata or assets are read.
    if (!modExists(activeMod) || activeManagedModInvalid) {
        SDL_Log("ModManager: Active mod '%s' is missing or stale, falling back to vanilla",
                activeMod.c_str());
        activeMod = VANILLA_MOD_NAME;
        saveActiveMod();
    }

    try {
        const ModInfo activeInfo = readModIni(getModPath(activeMod));
        activeCustomHouse = activeInfo.customHouse;
        activeGuestCustomHouse = makeTornieGuestCustomHouse(activeMod);
        activeMentats = activeInfo.mentats;
    } catch(const std::exception& e) {
        SDL_Log("ModManager: Active mod '%s' metadata is invalid, falling back to vanilla: %s",
                activeMod.c_str(), e.what());
        activeMod = VANILLA_MOD_NAME;
        activeCustomHouse = {};
        activeGuestCustomHouse = {};
        activeMentats.clear();
        saveActiveMod();
    }
    initialized = true;
    SDL_Log("ModManager: Initialized with active mod '%s'", activeMod.c_str());
}

std::string ModManager::getActiveModName() const {
    return activeMod;
}

const CustomHouseInfo& ModManager::getActiveCustomHouseInfo() const {
    return activeCustomHouse;
}

const CustomHouseInfo& ModManager::getCustomHouseInfo(int house) const {
    static const CustomHouseInfo noHouse;
    if(house == HOUSE_CUSTOM) {
        return activeCustomHouse;
    }
    if(isTornieContentActive() && house == HOUSE_THARPIQUE) {
        return activeGuestCustomHouse;
    }
    return noHouse;
}

bool ModManager::isCustomHouseRegistered() const {
    return initialized && activeCustomHouse.enabled;
}

bool ModManager::isCustomHouseRegistered(int house) const {
    return initialized && getCustomHouseInfo(house).enabled;
}

const ModMentatInfo& ModManager::getActiveMentatInfo(int house) const {
    static const ModMentatInfo noOverride;
    if(isTornieContentActive() && house >= HOUSE_WILDSPADE && house <= HOUSE_THARPIQUE) {
        house = HOUSE_NEUTRAL + (house - HOUSE_WILDSPADE);
    }
    if(!initialized || house < 0 || static_cast<std::size_t>(house) >= activeMentats.size()) {
        return noOverride;
    }
    return activeMentats[house];
}

int ModManager::getEffectiveMentatIdentity(int house) const {
    const ModMentatInfo& overrideInfo = getActiveMentatInfo(house);
    int identity = overrideInfo.enabled && overrideInfo.identityHouse >= 0
        ? overrideInfo.identityHouse
        : house;

    if(identity == HOUSE_CUSTOM) {
        const CustomHouseInfo& info = getCustomHouseInfo(house);
        identity = info.enabled ? info.fallbackHouse : HOUSE_HARKONNEN;
    } else if(isTornieContentActive()
              && identity >= HOUSE_WILDSPADE && identity <= HOUSE_THARPIQUE) {
        const int localIdentity = HOUSE_NEUTRAL + (identity - HOUSE_WILDSPADE);
        if(localIdentity == HOUSE_CUSTOM) {
            identity = activeGuestCustomHouse.enabled
                ? activeGuestCustomHouse.fallbackHouse
                : HOUSE_HARKONNEN;
        } else {
            identity = localIdentity;
        }
    }

    switch(identity) {
        case HOUSE_HARKONNEN:
        case HOUSE_ATREIDES:
        case HOUSE_ORDOS:
        case HOUSE_FREMEN:
        case HOUSE_SARDAUKAR:
        case HOUSE_MERCENARY:
            return identity;
        case HOUSE_NEUTRAL:
            return HOUSE_HARKONNEN;
        case HOUSE_REBELS:
            return HOUSE_ATREIDES;
        default:
            return HOUSE_HARKONNEN;
    }
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
    if(name == TORNIE_MOD_NAME) {
        std::string integrityError;
        if(!ModPayloadIntegrity::verifyChecksummedPayload(getModPath(name), integrityError,
                                                          MANAGED_MOD_STAMP)) {
            SDL_Log("ModManager: Cannot activate Tornie - integrity verification failed: %s",
                    integrityError.c_str());
            return false;
        }
    }
    
    const std::string previousMod = activeMod;
    const CustomHouseInfo previousCustomHouse = activeCustomHouse;
    const CustomHouseInfo previousGuestCustomHouse = activeGuestCustomHouse;
    const std::vector<ModMentatInfo> previousMentats = activeMentats;

    try {
        activeMod = name;
        const ModInfo activeInfo = readModIni(getModPath(activeMod));
        activeCustomHouse = activeInfo.customHouse;
        activeGuestCustomHouse = makeTornieGuestCustomHouse(activeMod);
        activeMentats = activeInfo.mentats;
        checksumsDirty = true;
        resetHouseVisualHouseMapping();
        loadCustomPalette();
        applyCustomPaletteRuntimeHouseRamps();
        if(pTextManager != nullptr) {
            pTextManager->loadData();
        }
        if(pGFXManager != nullptr) {
            pGFXManager->invalidateAllSpriteTextures();
            pGFXManager->reloadAllObjectGraphicsForActiveMod();
        }
        if(pSFXManager != nullptr) {
            pSFXManager->reloadVoices();
        }
        saveActiveMod();
    } catch(const std::exception& e) {
        SDL_Log("ModManager: Activation of '%s' failed, restoring '%s': %s",
                name.c_str(), previousMod.c_str(), e.what());
        activeMod = previousMod;
        activeCustomHouse = previousCustomHouse;
        activeGuestCustomHouse = previousGuestCustomHouse;
        activeMentats = previousMentats;
        checksumsDirty = true;
        resetHouseVisualHouseMapping();
        loadCustomPalette();
        applyCustomPaletteRuntimeHouseRamps();
        saveActiveMod();

        try {
            if(pTextManager != nullptr) {
                pTextManager->loadData();
            }
            if(pGFXManager != nullptr) {
                pGFXManager->invalidateAllSpriteTextures();
                pGFXManager->reloadAllObjectGraphicsForActiveMod();
            }
            if(pSFXManager != nullptr) {
                pSFXManager->reloadVoices();
            }
        } catch(const std::exception& restoreError) {
            SDL_Log("ModManager: Warning - restoring '%s' also failed: %s",
                    previousMod.c_str(), restoreError.what());
        }
        return false;
    }
    
    SDL_Log("ModManager: Activated mod '%s'", name.c_str());
    return true;
}

bool ModManager::modExists(const std::string& name) const {
    if(!isValidModName(name)) {
        return false;
    }
    std::string modPath = getModPath(name);
    // Just check if mod.json exists - if file exists, directory must exist
    return existsFile(modPath + "/" + MOD_INI_FILE);
}

bool ModManager::isValidModName(const std::string& name) const {
    return ModTransferValidation::isValidModName(name);
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
    
    // Ensure string fields are never null (defensive)
    if(info.displayName.empty()) info.displayName = name;
    if(info.author.empty()) info.author = "Unknown";
    if(info.description.empty()) info.description = "";
    if(info.gameVersion.empty()) info.gameVersion = "";
    
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
            else if (key == "Immortal Human Player") result.immortalHumanPlayer = parseBool(value);
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
    std::string defaults = getInstallConfigPath() + "/" + OBJECT_DATA_DEFAULT;

    if (modName == TORNIE_MOD_NAME) {
        const std::string candidatePaths[] = {
            getDuneLegacyDataDir() + "/mods/" + TORNIE_MOD_NAME,
            getDuneLegacyDataDir() + "/../mods/" + TORNIE_MOD_NAME,
            getDuneLegacyDataDir() + "/../../mods/" + TORNIE_MOD_NAME,
            getDuneLegacyDataDir() + "/../../../mods/" + TORNIE_MOD_NAME
        };
        for (const std::string& candidatePath : candidatePaths) {
            const std::string candidateObjectData = candidatePath + "/" + OBJECT_DATA_FILE;
            if (existsFile(candidateObjectData)) {
                defaults = candidateObjectData;
                break;
            }
        }
    }

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
    
    const std::string customHousePath = getModPath(activeMod) + "/" + CUSTOM_HOUSE_CONFIG;
    cachedChecksums.customHouse = existsFile(customHousePath)
        ? hashFileCanonical(customHousePath)
        : std::string();

    std::string engineCompatibility;
    if(isTornieContentActive()) {
        const std::string manifestPath = getModPath(activeMod) + "/manifest.json";
        const std::string checksumsPath = getModPath(activeMod) + "/checksums.sha256";
        engineCompatibility = TORNIE_ENGINE_COMPATIBILITY;
        engineCompatibility += ':';
        engineCompatibility += existsFile(manifestPath)
            ? hashFileCanonical(manifestPath)
            : "MANIFEST_NOT_FOUND";
        engineCompatibility += ':';
        engineCompatibility += existsFile(checksumsPath)
            ? Dune2RAssetManager::sha256File(checksumsPath)
            : "CHECKSUMS_NOT_FOUND";
    }

    // The compatibility suffix is empty outside Tornie, preserving every
    // existing mod checksum while rejecting incompatible Tornie engines.
    std::string combined = cachedChecksums.objectData + cachedChecksums.quantBotConfig
        + cachedChecksums.gameOptions + cachedChecksums.customHouse + engineCompatibility;
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
    if (!isValidModName(name) || name == VANILLA_MOD_NAME) {
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
    if (!isValidModName(name) || name == VANILLA_MOD_NAME) {
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
    if (!isValidModName(modName) || modName == VANILLA_MOD_NAME) {
        SDL_Log("ModManager: Invalid mod name for save: '%s'", modName.c_str());
        return false;
    }

    constexpr std::size_t MAX_RECEIVED_MOD_SIZE = 10U * 1024U * 1024U;
    constexpr uint32_t MAX_RECEIVED_MOD_FILES = 4096;
    constexpr uint32_t MAX_RECEIVED_PATH_LENGTH = 512;
    if(packagedData.size() > MAX_RECEIVED_MOD_SIZE) {
        SDL_Log("ModManager: Received mod exceeds the size limit");
        return false;
    }

    SDL_Log("ModManager: Saving received mod '%s' (%zu bytes)", modName.c_str(), packagedData.size());

    const std::filesystem::path modPath = getModPath(modName);
    const std::filesystem::path stagedPath = modPath.string() + ".incoming";
    const std::filesystem::path backupPath = modPath.string() + ".previous";
    std::error_code ignored;
    std::filesystem::remove_all(stagedPath, ignored);
    std::filesystem::remove_all(backupPath, ignored);

    auto fail = [&](const char* message) {
        SDL_Log("ModManager: %s", message);
        std::error_code cleanupError;
        std::filesystem::remove_all(stagedPath, cleanupError);
        return false;
    };

    try {
        std::filesystem::create_directories(stagedPath);
    } catch(const std::exception& e) {
        SDL_Log("ModManager: Could not create received-mod staging directory: %s", e.what());
        return false;
    }

    // Unpack the packaged data
    // Format: numFiles (4 bytes) + [nameLen (4 bytes) + name + dataLen (4 bytes) + data] * numFiles
    if (packagedData.size() < sizeof(uint32_t)) {
        return fail("Packaged data too small");
    }

    size_t offset = 0;
    uint32_t numFiles;
    memcpy(&numFiles, packagedData.data() + offset, sizeof(numFiles));
    offset += sizeof(numFiles);
    if(numFiles == 0 || numFiles > MAX_RECEIVED_MOD_FILES) {
        return fail("Received mod has an invalid file count");
    }

    SDL_Log("ModManager: Unpacking %u files", numFiles);

    std::set<std::string> receivedPathKeys;

    for (uint32_t i = 0; i < numFiles; i++) {
        if (offset + sizeof(uint32_t) > packagedData.size()) {
            return fail("Unexpected end of received mod while reading a path length");
        }

        uint32_t nameLen;
        memcpy(&nameLen, packagedData.data() + offset, sizeof(nameLen));
        offset += sizeof(nameLen);

        if (nameLen == 0 || nameLen > MAX_RECEIVED_PATH_LENGTH
            || offset + nameLen > packagedData.size()) {
            return fail("Received mod contains an invalid path length");
        }

        std::string fileName(packagedData.data() + offset, nameLen);
        offset += nameLen;

        std::filesystem::path relativePath;
        if(!ModTransferValidation::normalizeRelativeFilePath(fileName, relativePath)) {
            return fail("Received mod contains an invalid or non-portable path");
        }
        if(relativePath.generic_string() == MANAGED_MOD_STAMP) {
            return fail("Received mod contains reserved local metadata");
        }
        if(!receivedPathKeys.insert(ModTransferValidation::portablePathKey(relativePath)).second) {
            return fail("Received mod contains duplicate or case-colliding paths");
        }

        if (offset + sizeof(uint32_t) > packagedData.size()) {
            return fail("Unexpected end of received mod while reading a file length");
        }

        uint32_t dataLen;
        memcpy(&dataLen, packagedData.data() + offset, sizeof(dataLen));
        offset += sizeof(dataLen);

        if (offset + dataLen > packagedData.size()) {
            return fail("Unexpected end of received mod while reading file data");
        }

        const std::filesystem::path filePath = stagedPath / relativePath;
        try {
            std::filesystem::create_directories(filePath.parent_path());
        } catch(const std::exception&) {
            return fail("Could not create a received-mod subdirectory");
        }
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return fail("Could not create a received-mod file");
        }

        file.write(packagedData.data() + offset, dataLen);
        file.close();
        if(!file) {
            return fail("Could not finish writing a received-mod file");
        }
        offset += dataLen;
    }

    if(offset != packagedData.size()) {
        return fail("Received mod contains trailing data");
    }
    if(!std::filesystem::is_regular_file(stagedPath / MOD_INI_FILE)) {
        return fail("Received mod does not contain mod.ini");
    }
    if(modName == TORNIE_MOD_NAME) {
        std::string integrityError;
        if(!ModPayloadIntegrity::verifyChecksummedPayload(stagedPath, integrityError,
                                                          MANAGED_MOD_STAMP)) {
            SDL_Log("ModManager: Received Tornie payload failed integrity verification: %s",
                    integrityError.c_str());
            return fail("Received Tornie payload failed integrity verification");
        }
    }

    try {
        if(std::filesystem::exists(modPath)) {
            std::filesystem::rename(modPath, backupPath);
        }
        try {
            std::filesystem::rename(stagedPath, modPath);
        } catch(...) {
            if(std::filesystem::exists(backupPath) && !std::filesystem::exists(modPath)) {
                std::filesystem::rename(backupPath, modPath);
            }
            throw;
        }
        std::filesystem::remove_all(backupPath);
    } catch(const std::exception& e) {
        SDL_Log("ModManager: Could not install received mod atomically: %s", e.what());
        std::filesystem::remove_all(stagedPath, ignored);
        return false;
    }

    SDL_Log("ModManager: Mod '%s' saved successfully", modName.c_str());
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

    // GameOptions.ini: dunecity tunes game switches for city-builder play
    // (open map, sandworm respawn/spice, concrete required, rocket-turrets
    // need power). The city-mode toggle itself lives in mod.ini.
    std::string gameOptionsPath = dunecityPath + "/" + GAME_OPTIONS_FILE;
    std::ofstream gameOptionsFile(gameOptionsPath);
    if (gameOptionsFile.is_open()) {
        gameOptionsFile << "# Dune City Game Options (default values)\n";
        gameOptionsFile << "[Game Options]\n";
        gameOptionsFile << "Game Speed = 16\n";
        gameOptionsFile << "Concrete Required = true\n";
        gameOptionsFile << "Structures Degrade On Concrete = false\n";
        gameOptionsFile << "Fog of War = false\n";
        gameOptionsFile << "Start with Explored Map = true\n";
        gameOptionsFile << "Instant Build = false\n";
        gameOptionsFile << "Only One Palace = false\n";
        gameOptionsFile << "Rocket-Turrets Need Power = true\n";
        gameOptionsFile << "Sandworms Respawn = true\n";
        gameOptionsFile << "Killed Sandworms Drop Spice = true\n";
        gameOptionsFile << "Manual Carryall Drops = false\n";
        gameOptionsFile << "Maximum Number of Units Override = 0\n";
        gameOptionsFile << "Maximum Number of Harvesters Override = -1\n";
        gameOptionsFile << "Immortal Human Player = false\n";
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

// DuneCity 1.0.492: seed the Tornie mod. The source files
// (mod.ini, ObjectData.ini, campaign/) are shipped in the
// install at mods/Tornie/. We register this directory as
// a mod so it appears in the Mods menu and can be activated.
void ModManager::seedTornieFromDefaults() {
    SDL_Log("ModManager: Seeding Tornie mod from install defaults...");
    refreshManagedMod(TORNIE_MOD_NAME, getModPath(TORNIE_MOD_NAME),
                      findBundledModPath(TORNIE_MOD_NAME));
}

void ModManager::seedDune2RFromDefaults() {
    SDL_Log("ModManager: Seeding Dune2R mod from bundled install...");
    refreshManagedMod(DUNE2R_MOD_NAME, getModPath(DUNE2R_MOD_NAME),
                      findBundledModPath(DUNE2R_MOD_NAME));
}

bool ModManager::dune2rNeedsReseed() const {
    const std::filesystem::path installed = getModPath(DUNE2R_MOD_NAME);
    const std::filesystem::path bundled = findBundledModPath(DUNE2R_MOD_NAME);

    if(managedModNeedsRefresh(installed, bundled)) {
        SDL_Log("ModManager: Dune2R managed payload changed, needs reseed");
        return true;
    }

    if(!std::filesystem::is_regular_file(installed / MOD_INI_FILE)
       || !std::filesystem::is_regular_file(installed / GAME_OPTIONS_FILE)
       || !std::filesystem::is_directory(installed / "graphics_hd" / "units")) {
        SDL_Log("ModManager: Dune2R payload is incomplete, needs reseed");
        return true;
    }

    if(!std::filesystem::is_regular_file(bundled / MOD_INI_FILE)) {
        return false;
    }

    const ModInfo installedInfo = readModIni(installed.string());
    const ModInfo bundledInfo = readModIni(bundled.string());
    if(installedInfo.version != bundledInfo.version
       || installedInfo.gameVersion != bundledInfo.gameVersion) {
        SDL_Log("ModManager: Dune2R version mismatch (%s/%s vs %s/%s), needs reseed",
                installedInfo.version.c_str(), installedInfo.gameVersion.c_str(),
                bundledInfo.version.c_str(), bundledInfo.gameVersion.c_str());
        return true;
    }

    return false;
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

bool ModManager::tornieNeedsReseed() const {
    // DuneCity 1.0.494: re-seed on version mismatch (same
    // pattern as dunecityNeedsReseed). Without this, the
    // Tornie mod files seeded by an older version (e.g.
    // v1.0.492 which had the wrong install path) would
    // persist in the user mods dir even after a new version
    // is installed.
    std::string torniePath = getModPath(TORNIE_MOD_NAME);

    std::string modIniPath = torniePath + "/" + MOD_INI_FILE;
    std::string objectDataPath = torniePath + "/" + OBJECT_DATA_FILE;
    std::string gameOptionsPath = torniePath + "/" + GAME_OPTIONS_FILE;
    std::string customHousePath = torniePath + "/" + CUSTOM_HOUSE_CONFIG;
    std::string manifestPath = torniePath + "/manifest.json";
    std::string checksumsPath = torniePath + "/checksums.sha256";

    const std::filesystem::path bundled = findBundledModPath(TORNIE_MOD_NAME);
    if(managedModNeedsRefresh(torniePath, bundled)) {
        SDL_Log("ModManager: Tornie managed payload changed, needs reseed");
        return true;
    }

    if (!existsFile(modIniPath)) {
        SDL_Log("ModManager: Tornie missing %s, needs reseed", MOD_INI_FILE);
        return true;
    }
    if (!existsFile(objectDataPath)) {
        SDL_Log("ModManager: Tornie missing %s, needs reseed", OBJECT_DATA_FILE);
        return true;
    }
    if (!existsFile(gameOptionsPath)) {
        SDL_Log("ModManager: Tornie missing %s, needs reseed", GAME_OPTIONS_FILE);
        return true;
    }
    if (!existsFile(customHousePath) || !existsFile(manifestPath) || !existsFile(checksumsPath)
        || !std::filesystem::is_directory(std::filesystem::path(torniePath) / "campaign")
        || !std::filesystem::is_directory(std::filesystem::path(torniePath) / "data")) {
        SDL_Log("ModManager: Tornie payload is incomplete, needs reseed");
        return true;
    }

    const std::string bundledChecksums = (bundled / "checksums.sha256").string();
    if(existsFile(bundledChecksums)
       && hashFileCanonical(checksumsPath) != hashFileCanonical(bundledChecksums)) {
        SDL_Log("ModManager: Tornie bundled checksum manifest changed, needs reseed");
        return true;
    }

    std::string integrityError;
    if(!ModPayloadIntegrity::verifyChecksummedPayload(torniePath, integrityError,
                                                      MANAGED_MOD_STAMP)) {
        SDL_Log("ModManager: Tornie payload integrity check failed (%s), needs reseed",
                integrityError.c_str());
        return true;
    }

    // Check if installed Tornie ObjectData.ini differs from defaults
    if (installedObjectDataDiffersFromDefaults(TORNIE_MOD_NAME)) {
        SDL_Log("ModManager: Tornie %s drifted from defaults, needs reseed", OBJECT_DATA_FILE);
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
    if(name.empty()) {
        return modsBasePath;  // Safety: avoid trailing slash + empty name
    }
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
    auto trim = [](std::string& s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    };

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        
        size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) continue;
        
        std::string key = line.substr(0, equalsPos);
        std::string value = line.substr(equalsPos + 1);
        
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

    info.mentats.resize(NUM_HOUSE_COLOR_SLOTS);
    std::vector<bool> requestedMentatEnabled(NUM_HOUSE_COLOR_SLOTS, false);
    std::vector<bool> mentatFieldsValid(NUM_HOUSE_COLOR_SLOTS, true);
    std::ifstream mentatFile(iniPath);
    if(mentatFile.is_open()) {
        int mentatHouse = -1;
        while(std::getline(mentatFile, line)) {
            trim(line);
            if(line.empty() || line[0] == '#' || line[0] == ';') continue;

            if(line.front() == '[' && line.back() == ']') {
                std::string section = line.substr(1, line.size() - 2);
                trim(section);
                mentatHouse = -1;
                const std::string prefix = "Mentat ";
                if(section.rfind(prefix, 0) == 0) {
                    int parsedHouse = -1;
                    if(CustomHouseConfig::parseInteger(section.substr(prefix.size()), parsedHouse)
                       && parsedHouse >= 0 && parsedHouse < NUM_HOUSE_COLOR_SLOTS) {
                        mentatHouse = parsedHouse;
                    }
                }
                continue;
            }

            if(mentatHouse < 0) continue;
            const size_t mentatEqualsPos = line.find('=');
            if(mentatEqualsPos == std::string::npos) continue;

            std::string mentatKey = line.substr(0, mentatEqualsPos);
            std::string mentatValue = line.substr(mentatEqualsPos + 1);
            trim(mentatKey);
            trim(mentatValue);
            ModMentatInfo& mentat = info.mentats[mentatHouse];

            auto parseIntegerField = [&](int& destination) {
                if(!CustomHouseConfig::parseInteger(mentatValue, destination)) {
                    mentatFieldsValid[mentatHouse] = false;
                }
            };
            auto parseDoubleField = [&](double& destination) {
                if(!ModMentatConfig::parseDouble(mentatValue, destination)) {
                    mentatFieldsValid[mentatHouse] = false;
                }
            };
            auto parseBooleanField = [&](bool& destination) {
                if(!ModMentatConfig::parseBoolean(mentatValue, destination)) {
                    mentatFieldsValid[mentatHouse] = false;
                }
            };

            if(mentatKey == "Enabled") {
                bool requestedEnabled = false;
                if(ModMentatConfig::parseBoolean(mentatValue, requestedEnabled)) {
                    requestedMentatEnabled[mentatHouse] = requestedEnabled;
                } else {
                    mentatFieldsValid[mentatHouse] = false;
                }
            }
            else if(mentatKey == "Identity House") parseIntegerField(mentat.identityHouse);
            else if(mentatKey == "Background") mentat.backgroundAsset = mentatValue;
            else if(mentatKey == "Eyes") mentat.eyesAsset = mentatValue;
            else if(mentatKey == "Eyes Frames") parseIntegerField(mentat.eyesFrames);
            else if(mentatKey == "Eyes Frame Rate") parseDoubleField(mentat.eyesFrameRate);
            else if(mentatKey == "Eyes Double") parseBooleanField(mentat.doubleEyes);
            else if(mentatKey == "Eyes Transparent Color") parseIntegerField(mentat.eyesTransparentColor);
            else if(mentatKey == "Eyes X") parseIntegerField(mentat.eyesX);
            else if(mentatKey == "Eyes Y") parseIntegerField(mentat.eyesY);
            else if(mentatKey == "Mouth") mentat.mouthAsset = mentatValue;
            else if(mentatKey == "Mouth Frames") parseIntegerField(mentat.mouthFrames);
            else if(mentatKey == "Mouth Frame Rate") parseDoubleField(mentat.mouthFrameRate);
            else if(mentatKey == "Mouth Double") parseBooleanField(mentat.doubleMouth);
            else if(mentatKey == "Mouth Transparent Color") parseIntegerField(mentat.mouthTransparentColor);
            else if(mentatKey == "Mouth X") parseIntegerField(mentat.mouthX);
            else if(mentatKey == "Mouth Y") parseIntegerField(mentat.mouthY);
            else if(mentatKey == "Use Base Extras") parseBooleanField(mentat.useBaseExtras);
        }
    }

    for(int house = 0; house < NUM_HOUSE_COLOR_SLOTS; ++house) {
        ModMentatInfo& mentat = info.mentats[house];
        mentat.enabled = requestedMentatEnabled[house]
            && mentatFieldsValid[house]
            && ModMentatConfig::isValid(mentat);
        if(requestedMentatEnabled[house] && !mentat.enabled) {
            SDL_Log("ModManager: Ignoring invalid Mentat %d configuration in %s", house, modPath.c_str());
        }
    }

    std::ifstream customFile(modPath + "/" + CUSTOM_HOUSE_CONFIG);
    if(customFile.is_open()) {
        std::string section;
        bool requestedEnabled = false;
        bool numericFieldsValid = true;
        while(std::getline(customFile, line)) {
            trim(line);
            if(line.empty() || line[0] == '#' || line[0] == ';') continue;
            if(line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                trim(section);
                continue;
            }
            if(section != "House") continue;

            const size_t customEqualsPos = line.find('=');
            if(customEqualsPos == std::string::npos) continue;
            std::string customKey = line.substr(0, customEqualsPos);
            std::string customValue = line.substr(customEqualsPos + 1);
            trim(customKey);
            trim(customValue);

            if(customKey == "Enabled") {
                requestedEnabled = (customValue == "true" || customValue == "1" || customValue == "yes");
            } else if(customKey == "Display Name") {
                info.customHouse.displayName = customValue;
            } else if(customKey == "Scenario Letter" && customValue.size() == 1) {
                char value = customValue[0];
                info.customHouse.scenarioLetter = (value >= 'a' && value <= 'z') ? static_cast<char>(value - 'a' + 'A') : value;
            } else if(customKey == "Region Prefix") {
                info.customHouse.regionPrefix = customValue;
                for(char& value : info.customHouse.regionPrefix) {
                    if(value >= 'a' && value <= 'z') value = static_cast<char>(value - 'a' + 'A');
                }
            } else if(customKey == "Palette Index") {
                if(!CustomHouseConfig::parseInteger(customValue, info.customHouse.paletteIndex)) {
                    numericFieldsValid = false;
                }
            } else if(customKey == "Fallback House") {
                if(!CustomHouseConfig::parseInteger(customValue, info.customHouse.fallbackHouse)) {
                    numericFieldsValid = false;
                }
            } else if(customKey == "Herald") {
                if(CustomHouseConfig::isSafeAssetPath(customValue)) {
                    info.customHouse.heraldAsset = customValue;
                } else {
                    SDL_Log("ModManager: Ignoring unsafe custom-house herald path in %s", modPath.c_str());
                }
            } else if(customKey == "House Name Voice") {
                if(CustomHouseConfig::isSafeAssetPath(customValue)) {
                    info.customHouse.houseNameVoiceAsset = customValue;
                } else {
                    SDL_Log("ModManager: Ignoring unsafe custom-house voice path in %s", modPath.c_str());
                }
            } else if(customKey == "Voice Playback Rate") {
                double parsedValue = info.customHouse.voicePlaybackRate;
                if(CustomHouseConfig::parseDouble(customValue, parsedValue)
                   && CustomHouseConfig::isValidVoicePlaybackRate(parsedValue)) {
                    info.customHouse.voicePlaybackRate = parsedValue;
                } else {
                    SDL_Log("ModManager: Ignoring invalid custom-house voice playback rate in %s",
                            modPath.c_str());
                }
            } else if(customKey == "Voice Gain") {
                double parsedValue = info.customHouse.voiceGain;
                if(CustomHouseConfig::parseDouble(customValue, parsedValue)
                   && CustomHouseConfig::isValidVoiceGain(parsedValue)) {
                    info.customHouse.voiceGain = parsedValue;
                } else {
                    SDL_Log("ModManager: Ignoring invalid custom-house voice gain in %s", modPath.c_str());
                }
            }
        }

        const bool letterValid = info.customHouse.scenarioLetter >= 'A' && info.customHouse.scenarioLetter <= 'Z';
        const bool prefixValid = info.customHouse.regionPrefix.size() == 3
            && info.customHouse.regionPrefix.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos;
        const bool paletteValid = info.customHouse.paletteIndex >= 0 && info.customHouse.paletteIndex <= 248;
        const bool fallbackValid = info.customHouse.fallbackHouse >= 0
            && info.customHouse.fallbackHouse < NUM_LEGACY_HOUSES;
        info.customHouse.enabled = requestedEnabled && !info.customHouse.displayName.empty()
            && numericFieldsValid && letterValid && prefixValid && paletteValid && fallbackValid;
        if(requestedEnabled && !info.customHouse.enabled) {
            SDL_Log("ModManager: Ignoring invalid generic custom-house registration in %s", modPath.c_str());
        }
    }
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
