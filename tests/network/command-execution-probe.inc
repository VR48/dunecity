// Included by the headless native integration test main. Not part of the game binary.
#include <Game.h>
#include <House.h>
#include <GameInitSettings.h>
#include <Command.h>
#include <GUI/dune/FeedbackWindow.h>
#include <FileClasses/LoadSavePNG.h>
#include <units/UnitBase.h>
#include <players/HumanPlayer.h>
#include <Menu/MenuBase.h>
#include <Network/NetworkManager.h>
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
    Command(hostPlayer->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
    if(game->isGameFinished()) throw std::runtime_error("skip finished a non-campaign game");
    // Exercise the real Game entry points used by Options and Mentat, not a duplicate policy.
    game->pauseGame();
    if(!game->isGamePaused()) throw std::runtime_error("local game no longer pauses");
    game->resumeGame();
    if(game->isGamePaused()) throw std::runtime_error("local game no longer resumes");
    pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::RoomRelay);
    game->pauseGame();
    if(game->isGamePaused() || !game->pausedPlayers.empty())
        throw std::runtime_error("relay menu stopped lockstep or queued pause state");
    game->resumeGame();
    if(game->isGamePaused() || !game->pausedPlayers.empty())
        throw std::runtime_error("closing relay menu changed pause state");
    SDL_Log("RELAY_PAUSE_PROBE_PASS: real Game preserves local pause and keeps relay simulation running");
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    pNetworkManager.reset();
    // Run the real skip handler against campaign state, including the final-level exit.
    for(int level : {4, 9}) {
        game = std::make_unique<Game>(); currentGame = game.get();
        GameInitSettings campaign(HOUSE_ATREIDES, settings.gameOptions, level);
        {
            auto file = pFileManager->openCampaignFile(campaign.getFilename());
            std::string data(static_cast<size_t>(SDL_RWsize(file.get())), '\0');
            if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size())
                throw std::runtime_error("campaign probe scenario read failed");
            campaign.setScenarioData(data);
            campaign.enableCoop(true, "Campaign probe");
        }
        campaign.setMultiplePlayersPerHouse(true);
        campaign.addHouseInfo(humans);
        campaign.addHouseInfo(enemies);
        game->initGame(campaign);
        auto* controller = game->getPlayerByName(level == 4 ? "Host" : "Coop");
        if(!controller || !game->canSkipMission()) throw std::runtime_error("campaign skip unavailable");
        if(level == 4) {
            GameInterface interface;
            const int x = getRendererWidth() - SIDEBARWIDTH - 15;
            const int y = getRendererHeight() - 15;
            if(!interface.handleMouseLeft(x, y, true) || !interface.handleMouseLeft(x, y, false)
               || !game->isGamePaused()) throw std::runtime_error("feedback button did not open its modal");
            game->resumeGame();
            FeedbackWindow feedback;
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            feedback.draw();
            auto image = renderReadSurface(renderer);
            SavePNG(image.get(), "feedback-window.png");
        }
        Command(255, CMD_CAMPAIGN_SKIP).executeCommand();
        auto* other = game->getPlayerByName("Enemy");
        if(other) Command(other->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
        Command(controller->getPlayerID(), CMD_CAMPAIGN_SKIP, 1).executeCommand();
        if(game->isGameFinished()) throw std::runtime_error("invalid campaign skip accepted");
        Command(controller->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
        if(!game->isGameFinished() || game->canSkipMission()) throw std::runtime_error("campaign skip did not finish");
        if(game->whatNext() != GAME_DEBRIEFING_WIN) throw std::runtime_error("skip did not use victory path");
        if(game->whatNext() != (level == 9 ? GAME_RETURN_TO_MENU : GAME_NEXTMISSION))
            throw std::runtime_error("skip chose wrong campaign continuation");
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
    {
        FeedbackEditor editor;
        editor.resize(400, 140);
        editor.setActive();
        SDL_TextInputEvent text{};
        SDL_strlcpy(text.text, "Café", sizeof(text.text));
        editor.handleTextInput(text);
        SDL_KeyboardEvent key{};
        key.keysym.sym = SDLK_BACKSPACE;
        editor.handleKeyPress(key);
        if(editor.value() != "Caf") throw std::runtime_error("feedback backspace broke Unicode");
        key.keysym.sym = SDLK_RETURN; editor.handleKeyPress(key);
        SDL_strlcpy(text.text, "Feedback", sizeof(text.text)); editor.handleTextInput(text);
        if(editor.value() != "Caf\nFeedback") throw std::runtime_error("feedback multiline input failed");
        key.keysym.sym = SDLK_a; key.keysym.mod = KMOD_CTRL; editor.handleKeyPress(key);
        SDL_strlcpy(text.text, "Replacement", sizeof(text.text)); editor.handleTextInput(text);
        if(editor.value() != "Replacement") throw std::runtime_error("feedback select-all replace failed");
        editor.setInactive();
    }
    SDL_Log("FEEDBACK_EDITOR_PROBE_PASS: multiline input, Unicode deletion and selection replacement work");
    SDL_Log("CAMPAIGN_SKIP_PROBE_PASS: real commands win campaigns, advance levels, finish final mission and reject invalid issuers");
    return MENU_QUIT_DEFAULT;
}
