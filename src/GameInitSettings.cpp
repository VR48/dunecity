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

#include <GameInitSettings.h>
#include <algorithm>

#include <misc/IFileStream.h>
#include <misc/IMemoryStream.h>
#include <misc/InputStream.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <mmath.h>

#include <globals.h>
#include <mod/ModManager.h>

namespace {
constexpr Uint32 GAMEINIT_MOD_MARKER = 0x4D4F4421;   // "MOD!"
constexpr Uint32 GAMEINIT_MOD2_MARKER = 0x4D4F4432;  // "MOD2"
}

// Helper to capture current mod info
static void setModInfo(std::string& modName, std::string& modChecksum) {
    if (ModManager::instance().isInitialized()) {
        modName = ModManager::instance().getActiveModName();
        modChecksum = ModManager::instance().getEffectiveChecksums().combined;
    } else {
        modName = "vanilla";
        modChecksum = "";
    }
}

GameInitSettings::GameInitSettings() {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(HOUSETYPE newHouseID, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::Campaign), houseID(newHouseID), mission(1), alreadyShownTutorialHints(0), gameOptions(gameOptions) {
    filename = getScenarioFilename(houseID, mission);
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const GameInitSettings& prevGameInitInfoClass, int nextMission, Uint32 alreadyPlayedRegions, Uint32 alreadyShownTutorialHints) {
    *this = prevGameInitInfoClass;
    mission = nextMission;
    this->alreadyPlayedRegions = alreadyPlayedRegions;
    this->alreadyShownTutorialHints = alreadyShownTutorialHints;
    filename = getScenarioFilename(houseID, mission);
    filedata.clear();
    randomSeed = getRandomInt();
}

GameInitSettings::GameInitSettings(HOUSETYPE newHouseID, int newMission, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::Skirmish), houseID(newHouseID), mission(newMission), gameOptions(gameOptions) {
    filename = getScenarioFilename(houseID, mission);
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& mapfile, const std::string& filedata, bool multiplePlayersPerHouse, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::CustomGame), filename(mapfile), filedata(filedata), multiplePlayersPerHouse(multiplePlayersPerHouse), gameOptions(gameOptions) {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& mapfile, const std::string& filedata, const std::string& serverName, bool multiplePlayersPerHouse, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::CustomMultiplayer), filename(mapfile), filedata(filedata), servername(serverName), multiplePlayersPerHouse(multiplePlayersPerHouse), gameOptions(gameOptions) {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& savegame)
 : gameType(GameType::LoadSavegame) {
    checkSaveGame(savegame);
    filename = savegame;
}

GameInitSettings::GameInitSettings(const std::string& savegame, const std::string& filedata, const std::string& serverName)
 : gameType(GameType::LoadMultiplayer), filename(savegame), filedata(filedata), servername(serverName) {
    IMemoryStream memStream(filedata.c_str(), filedata.size());
    checkSaveGame(memStream);
}

GameInitSettings::GameInitSettings(InputStream& stream) {
    gameType = static_cast<GameType>(stream.readSint8());
    houseID = static_cast<HOUSETYPE>(stream.readSint8());

    filename = stream.readString();
    filedata = stream.readString();

    mission = stream.readUint8();
    alreadyPlayedRegions = stream.readUint32();
    alreadyShownTutorialHints = stream.readUint32();
    randomSeed = stream.readUint32();

    multiplePlayersPerHouse = stream.readBool();
    gameOptions.gameSpeed = stream.readUint32();
    gameOptions.concreteRequired = stream.readBool();
    gameOptions.structuresDegradeOnConcrete = stream.readBool();
    gameOptions.fogOfWar = stream.readBool();
    gameOptions.startWithExploredMap = stream.readBool();
    gameOptions.instantBuild = stream.readBool();
    gameOptions.onlyOnePalace = stream.readBool();
    gameOptions.rocketTurretsNeedPower = stream.readBool();
    gameOptions.sandwormsRespawn = stream.readBool();
    gameOptions.killedSandwormsDropSpice = stream.readBool();
    gameOptions.manualCarryallDrops = stream.readBool();
    gameOptions.maximumNumberOfUnitsOverride = stream.readSint32();
    gameOptions.maximumNumberOfHarvestersOverride = stream.readSint32();
    gameOptions.immortalHumanPlayer = stream.readBool();

    Uint32 numHouseInfo = stream.readUint32();
    // A house info is at least houseID + team + player count = 12 bytes.
    stream.requireReadableElements(numHouseInfo, 12);
    for(Uint32 i=0;i<numHouseInfo;i++) {
        houseInfoList.push_back(HouseInfo(stream));
    }
    
    // Read mod info (added in version with mod system)
    // Use marker to detect presence for backward compatibility
    try {
        Uint32 modMarker = stream.readUint32();
        if (modMarker == GAMEINIT_MOD_MARKER || modMarker == GAMEINIT_MOD2_MARKER) {
            modName = stream.readString();
            modChecksum = stream.readString();

            if(modMarker == GAMEINIT_MOD2_MARKER) {
                Uint32 numHouseColors = stream.readUint32();
                stream.requireReadableElements(numHouseColors, 4);
                for(Uint32 i = 0; i < numHouseColors; i++) {
                    const int colorOfHouse = stream.readSint32();
                    if(i < houseInfoList.size()) {
                        houseInfoList[i].colorOfHouse = colorOfHouse;
                    }
                }
            }
        }
    } catch (InputStream::eof&) {
        // Old format without mod info - use defaults
        modName = "vanilla";
        modChecksum = "";
    }
}

void GameInitSettings::configureCoopSave(const GameInitSettings& saved, const HouseInfoList& houses) {
    houseID = saved.houseID;
    mission = saved.mission;
    alreadyPlayedRegions = saved.alreadyPlayedRegions;
    gameOptions = saved.gameOptions;
    modName = saved.modName;
    modChecksum = saved.modChecksum;
    houseInfoList = houses;
    // Future campaign missions can introduce enemies not present in this save.
    for(const auto& planned : saved.houseInfoList) {
        if(std::none_of(houseInfoList.begin(), houseInfoList.end(), [&](const HouseInfo& h) { return h.houseID == planned.houseID; }))
            houseInfoList.push_back(planned);
    }
}

void GameInitSettings::enableCoop(bool campaign, const std::string& serverName) {
    gameType = (gameType == GameType::LoadMultiplayer || gameType == GameType::LoadCoop)
        ? GameType::LoadCoop : (campaign ? GameType::CampaignCoop : GameType::SkirmishCoop);
    multiplePlayersPerHouse = true;
    servername = serverName;
    gameOptions.immortalHumanPlayer = false;
}

GameInitSettings GameInitSettings::readSaveSetup(InputStream& stream, HouseInfoList& houses) {
    if(stream.readUint32() != SAVEMAGIC) THROW(std::runtime_error, "Not a valid savegame.");
    const auto version = stream.readUint32();
    stream.readString();
    if(version < 9705 || version > SAVEGAMEVERSION) THROW(std::runtime_error, "Unsupported savegame version.");
    if(version >= 9806) { stream.readString(); stream.readString(); }
    GameInitSettings saved(stream);
    if(version <= 9820) saved.migrateLegacyHouseColorSlots();
    const auto count = stream.readUint32();
    if(count > NUM_HOUSES) THROW(std::runtime_error, "Invalid saved house count.");
    houses.clear();
    for(Uint32 i = 0; i < count; ++i) houses.emplace_back(stream);
    if(version >= 9814) {
        if(stream.readUint32() != 0x53434F4C) THROW(std::runtime_error, "Invalid saved house colors.");
        const auto colors = stream.readUint32();
        if(colors != count) THROW(std::runtime_error, "Invalid saved color count.");
        for(auto& house : houses) {
            house.colorOfHouse = stream.readSint32();
            if(version <= 9820) house.colorOfHouse = migrateLegacyHouseColorSlot(house.colorOfHouse);
        }
    }
    return saved;
}

GameInitSettings::~GameInitSettings() {
}

void GameInitSettings::save(OutputStream& stream) const {
    stream.writeSint8(static_cast<Sint8>(gameType));
    stream.writeSint8(houseID);

    stream.writeString(filename);
    stream.writeString(filedata);

    stream.writeUint8(mission);
    stream.writeUint32(alreadyPlayedRegions);
    stream.writeUint32(alreadyShownTutorialHints);
    stream.writeUint32(randomSeed);

    stream.writeBool(multiplePlayersPerHouse);
    stream.writeUint32(gameOptions.gameSpeed);
    stream.writeBool(gameOptions.concreteRequired);
    stream.writeBool(gameOptions.structuresDegradeOnConcrete);
    stream.writeBool(gameOptions.fogOfWar);
    stream.writeBool(gameOptions.startWithExploredMap);
    stream.writeBool(gameOptions.instantBuild);
    stream.writeBool(gameOptions.onlyOnePalace);
    stream.writeBool(gameOptions.rocketTurretsNeedPower);
    stream.writeBool(gameOptions.sandwormsRespawn);
    stream.writeBool(gameOptions.killedSandwormsDropSpice);
    stream.writeBool(gameOptions.manualCarryallDrops);
    stream.writeSint32(gameOptions.maximumNumberOfUnitsOverride);
    stream.writeSint32(gameOptions.maximumNumberOfHarvestersOverride);
    stream.writeBool(gameOptions.immortalHumanPlayer);

    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        houseInfo.save(stream);
    }
    
    // Write mod info with marker for forward compatibility
    stream.writeUint32(GAMEINIT_MOD2_MARKER);
    stream.writeString(modName);
    stream.writeString(modChecksum);

    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        stream.writeSint32(houseInfo.colorOfHouse);
    }
}



void GameInitSettings::migrateLegacyHouseColorSlots() {
    for(HouseInfo& houseInfo : houseInfoList) {
        houseInfo.colorOfHouse = migrateLegacyHouseColorSlot(houseInfo.colorOfHouse);
    }
}

std::string GameInitSettings::getScenarioFilename(HOUSETYPE newHouse, int mission) {
    if((newHouse < 0) || (newHouse >= NUM_HOUSES) || !isHouseAvailable(newHouse)) {
        THROW(std::invalid_argument, "GameInitSettings::getScenarioFilename(): Invalid house id " + std::to_string(newHouse) + ".");
    }

    if( (mission < 0) || (mission > 22)) {
        THROW(std::invalid_argument, "GameInitSettings::getScenarioFilename(): There is no mission number " + std::to_string(mission) + ".");
    }

    std::string name = "SCEN?0??.INI";
    name[4] = getHouseScenarioLetter(newHouse);

    name[6] = '0' + (mission / 10);
    name[7] = '0' + (mission % 10);

    return name;
}

void GameInitSettings::checkSaveGame(const std::string& savegame) {
    IFileStream fs;

    if(fs.open(savegame) == false) {
        THROW(std::runtime_error, "Cannot open savegame. Make sure you have read access to this savegame!");
    }

    checkSaveGame(fs);

    fs.close();
}


void GameInitSettings::checkSaveGame(InputStream& stream) {
    Uint32 magicNum;
    Uint32 savegameVersion;
    std::string duneVersion;
    try {
        magicNum = stream.readUint32();
        savegameVersion = stream.readUint32();
        duneVersion = stream.readString();
    } catch (std::exception&) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it seems to be truncated!");
    }

    if(magicNum != SAVEMAGIC) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it has a wrong magic number!");
    }

    // Support backward compatibility: Accept version 9705 (pre-Original AI) and newer
    constexpr Uint32 MINIMUM_SUPPORTED_VERSION = 9705;
    
    if(savegameVersion < MINIMUM_SUPPORTED_VERSION) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it was created with an older version:\n" + duneVersion);
    }

    if(savegameVersion > SAVEGAMEVERSION) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it was created with a newer version:\n" + duneVersion);
    }
}
