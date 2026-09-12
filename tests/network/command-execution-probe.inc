// Included by the headless native integration test main. Not part of the game binary.
#include <Game.h>
#include <House.h>
#include <GameInitSettings.h>
#include <Command.h>
#include <units/UnitBase.h>
#include <players/HumanPlayer.h>
#include <Menu/MenuBase.h>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <iterator>
int runOwnershipProbe() {
    settings.general.playerName = "Host";
    settings.network.metaServer = "http://127.0.0.1:1/";
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    std::ifstream map(PROBE_MAP_PATH);
    std::string mapData((std::istreambuf_iterator<char>(map)), std::istreambuf_iterator<char>());
    if(mapData.empty()) throw std::runtime_error("ownership fixture map missing");
    GameInitSettings init("ownership.ini", mapData, "Ownership probe", true, settings.gameOptions);
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Host", "HumanPlayer"});
    humans.addPlayerInfo({"Coop", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    if(!owner || !enemy) throw std::runtime_error("ownership fixture houses missing");
    auto* hostPlayer = game->getPlayerByName("Host");
    auto* coopPlayer = game->getPlayerByName("Coop");
    auto* enemyPlayer = game->getPlayerByName("Enemy");
    if(!hostPlayer || !coopPlayer || !enemyPlayer) throw std::runtime_error("ownership fixture players missing");
    UnitBase* unit = owner->createUnit(Unit_Tank, true);
    unit->doSetAttackMode(GUARD);
    auto order=[&](Uint8 player, Uint32 mode) { Command(player, CMD_UNIT_SETMODE, unit->getObjectID(), mode).executeCommand(); };
    order(enemyPlayer->getPlayerID(), HUNT);
    if(unit->getAttackMode()!=GUARD) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OWNERSHIP_PROBE_FAIL: enemy command changed real unit state"); throw std::runtime_error("enemy command changed real unit state"); }
    order(hostPlayer->getPlayerID(), HUNT);
    if(unit->getAttackMode()!=HUNT) throw std::runtime_error("owner command failed");
    order(coopPlayer->getPlayerID(), STOP);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("shared-house command failed");
    order(hostPlayer->getPlayerID(), 0xffffffffu);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("invalid enum changed unit");
    order(255, GUARD);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("unknown issuer changed unit");
    Command(hostPlayer->getPlayerID(), CMD_UNIT_SETMODE, 0xfffffff0u, GUARD).executeCommand();
    SDL_Log("OWNERSHIP_PROBE_PASS: actual game objects reject enemy, unknown and invalid commands; owner and co-op commands work");
    CommandManager batches;
    static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle = 0;
    unit->doSetAttackMode(GUARD);
    const Command legal(hostPlayer->getPlayerID(), CMD_UNIT_SETMODE, unit->getObjectID(), HUNT);
    const Command forged(enemyPlayer->getPlayerID(), CMD_UNIT_SETMODE, unit->getObjectID(), STOP);
    CommandList mixed;
    mixed.commandList.emplace_back(0, std::vector<Command>{legal});
    mixed.commandList.emplace_back(1, std::vector<Command>{forged});
    batches.addCommandList("Host", mixed);
    batches.executeCommands(0);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 0 || unit->getAttackMode() != GUARD)
        throw std::runtime_error("malformed batch partially applied");
    CommandList gap;
    gap.commandList.emplace_back(1, std::vector<Command>{legal});
    batches.addCommandList("Host", gap);
    batches.executeCommands(1);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 0 || unit->getAttackMode() != GUARD)
        throw std::runtime_error("gapped batch skipped missing cycle");
    CommandList complete;
    complete.commandList.emplace_back(0, std::vector<Command>{});
    complete.commandList.emplace_back(1, std::vector<Command>{legal});
    complete.commandList.emplace_back(2, std::vector<Command>{});
    batches.addCommandList("Host", complete);
    batches.executeCommands(1);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 3 || unit->getAttackMode() != HUNT)
        throw std::runtime_error("contiguous retransmission did not recover gap");
    batches.addCommandList("Host", complete);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 3) throw std::runtime_error("duplicate changed watermark");
    SDL_Log("COMMAND_BATCH_PROBE_PASS: mixed batch atomic, gap held, contiguous retransmission recovered, duplicate ignored");
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    return MENU_QUIT_DEFAULT;
}
