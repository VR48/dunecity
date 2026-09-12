#include <catch2/catch_all.hpp>
#include <GameInitSettings.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>

// GameInitSettings only needs mod identity; these tests never install mods.
ModManager::ModManager() : checksumsDirty(false), initialized(false) {}
ModManager::~ModManager() = default;
ModManager& ModManager::instance() { static ModManager manager; return manager; }
bool ModManager::isInitialized() const { return false; }
std::string ModManager::getActiveModName() const { return "vanilla"; }
ModChecksums ModManager::getEffectiveChecksums() const { return {}; }
bool isHouseAvailable(HOUSETYPE house) { return house >= 0 && house < NUM_HOUSES; }
char getHouseScenarioLetter(HOUSETYPE house) {
    return house == HOUSE_ATREIDES ? 'A' : (house == HOUSE_ORDOS ? 'O' : 'H');
}

static GameInitSettings makeCoop(bool campaign, bool bot) {
    GameInitSettings init(HOUSE_ATREIDES, 8, SettingsClass::GameOptionsClass{});
    init.enableCoop(campaign, "Test co-op");
    init.setScenarioData("[Atreides]\nBrain=Human\n[Sardaukar]\nBrain=CPU\n");
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    human.addPlayerInfo({"Partner", bot ? "qBotSupportBrutal" : HUMANPLAYERCLASS});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo enemy(HOUSE_SARDAUKAR, 2);
    enemy.addPlayerInfo({"Sardaukar", "qBotHard"});
    init.addHouseInfo(enemy);
    return init;
}

TEST_CASE("Co-op settings preserve shared control, scenario identity and seed over the wire", "[coop][network]") {
    const bool campaign = GENERATE(false, true);
    const bool bot = GENERATE(false, true);
    const auto host = makeCoop(campaign, bot);
    OMemoryStream out; out.open(); host.save(out);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings client(in);
    REQUIRE(client.getGameType() == host.getGameType());
    REQUIRE(isNetworkGameType(client.getGameType()));
    REQUIRE(client.isMultiplePlayersPerHouse());
    REQUIRE(client.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(client.getMission() == 8);
    REQUIRE(client.getRandomSeed() == host.getRandomSeed());
    REQUIRE(client.getFiledata() == host.getFiledata());
    REQUIRE(client.getHouseInfoList().size() == 2);
    const auto& shared = client.getHouseInfoList().front();
    REQUIRE(shared.team == 1);
    REQUIRE(shared.playerInfoList.size() == 2);
    REQUIRE(shared.playerInfoList.back().playerClass == (bot ? "qBotSupportBrutal" : HUMANPLAYERCLASS));
    REQUIRE(client.getHouseInfoList().back().houseID == HOUSE_SARDAUKAR);
    REQUIRE(client.getHouseInfoList().back().team == 2);
}

TEST_CASE("Next co-op mission retains controllers and progress but discards old scenario bytes", "[coop][campaign]") {
    const bool bot = GENERATE(false, true);
    const auto previous = makeCoop(true, bot);
    GameInitSettings next(previous, 11, 0x123, 0x42);
    REQUIRE(next.getGameType() == GameType::CampaignCoop);
    REQUIRE(next.getFilename() == "SCENA011.INI");
    REQUIRE(next.getFiledata().empty());
    REQUIRE(next.getAlreadyPlayedRegions() == 0x123);
    REQUIRE(next.getAlreadyShownTutorialHints() == 0x42);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.size() == 2);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.back().playerClass
        == previous.getHouseInfoList().front().playerInfoList.back().playerClass);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.back().playerName
        == previous.getHouseInfoList().front().playerInfoList.back().playerName);
}

TEST_CASE("Campaign save lobby reads mod header and setup colors without consuming game state", "[coop][save]") {
    const unsigned version = GENERATE(9806u, 9814u, 9836u);
    GameInitSettings saved(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{});
    const auto setup = makeCoop(true, false).getHouseInfoList();
    for(const auto& house : setup) saved.addHouseInfo(house);
    OMemoryStream out; out.open();
    out.writeUint32(SAVEMAGIC); out.writeUint32(version); out.writeString("test");
    out.writeString("vanilla"); out.writeString("checksum");
    saved.save(out);
    out.writeUint32(setup.size());
    for(const auto& house : setup) house.save(out);
    if(version >= 9814) {
        out.writeUint32(0x53434F4C); out.writeUint32(setup.size());
        for(const auto& house : setup) out.writeSint32(house.houseID);
    }
    out.writeUint32(0xabcdef01);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings::HouseInfoList houses;
    const auto parsed = GameInitSettings::readSaveSetup(in, houses);
    REQUIRE(parsed.getGameType() == GameType::Campaign);
    REQUIRE(parsed.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(houses.size() == 2);
    REQUIRE(houses.back().houseID == HOUSE_SARDAUKAR);
    REQUIRE(in.readUint32() == 0xabcdef01);
}

TEST_CASE("Malformed co-op save header is rejected", "[coop][save]") {
    OMemoryStream out; out.open(); out.writeUint32(0);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings::HouseInfoList houses;
    REQUIRE_THROWS(GameInitSettings::readSaveSetup(in, houses));
}

TEST_CASE("Hosting an existing campaign preserves progress and missing future enemy slots", "[coop][save]") {
    auto saved = makeCoop(true, false);
    GameInitSettings::HouseInfoList actual{saved.getHouseInfoList().front()};
    OMemoryStream header; header.open();
    header.writeUint32(SAVEMAGIC); header.writeUint32(SAVEGAMEVERSION); header.writeString("test");
    const std::string bytes(reinterpret_cast<const char*>(header.getData()), header.getDataLength());
    GameInitSettings loaded("campaign.dls", bytes, "Host");
    loaded.configureCoopSave(saved, actual);
    loaded.enableCoop(true, "Hosted campaign");
    REQUIRE(loaded.getGameType() == GameType::LoadCoop);
    REQUIRE(loaded.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(loaded.getMission() == saved.getMission());
    REQUIRE(loaded.getFiledata() == bytes);
    REQUIRE(loaded.getHouseInfoList().size() == 2);
    REQUIRE(loaded.getHouseInfoList().back().houseID == HOUSE_SARDAUKAR);
    REQUIRE_FALSE(loaded.getGameOptions().immortalHumanPlayer);
}
