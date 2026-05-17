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

#include <Game.h>
#include <main.h>
#include <cstdarg>
#include <ctime>
#include <chrono>

// Initialize static performance logging members
std::ofstream Game::performanceLogFile;
std::mutex Game::performanceLogMutex;

#include <globals.h>
#include <config.h>
#include <main.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <FileClasses/LoadSavePNG.h>
#include <SoundPlayer.h>
#include <misc/IFileStream.h>
#include <misc/OFileStream.h>
#include <misc/IMemoryStream.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>
#include <misc/draw_util.h>
#include <misc/md5.h>
#include <misc/exceptions.h>
#include <misc/format.h>
#include <misc/SDL2pp.h>
#include <misc/DiscordManager.h>

#include <players/HumanPlayer.h>

#include <Network/NetworkManager.h>
#include <mod/ModManager.h>

#include <GUI/dune/InGameMenu.h>
#include <GUI/dune/WaitingForOtherPlayers.h>
#include <GUI/dune/CityBudgetWindow.h>
#include <Menu/MentatHelp.h>
#include <Menu/BriefingMenu.h>
#include <Menu/MapChoice.h>

#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <Bullet.h>
#include <AStarSearch.h>
#include <Explosion.h>
#include <GameInitSettings.h>
#include <ScreenBorder.h>
#include <sand.h>

#include <structures/StructureBase.h>
#include <structures/ConstructionYard.h>
#include <units/UnitBase.h>
#include <structures/BuilderBase.h>
#include <structures/Palace.h>
#include <units/Harvester.h>
#include <units/InfantryBase.h>

#include <algorithm>
#include <exception>
#include <sstream>
#include <iomanip>

namespace {

DuneCity::CityTilePlacementState makeCityTilePlacementState(const Tile& tile) {
    return DuneCity::makeCityTilePlacementState(
        tile.isRock(),
        tile.isMountain(),
        tile.hasAGroundObject(),
        tile.hasCityZone(),
        tile.isRoad());
}

bool hasVisibleRoadNeighbor(int x, int y) {
    constexpr int dx[] = {0, 1, 0, -1};
    constexpr int dy[] = {-1, 0, 1, 0};

    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];

        if (!currentGameMap->tileExists(nx, ny)) {
            continue;
        }

        const Tile* neighbor = currentGameMap->getTile(nx, ny);
        if (neighbor->isRoad() && !neighbor->hasCityZone()) {
            return true;
        }
    }

    return false;
}

} // namespace

Game::Game() {
    currentZoomlevel = settings.video.preferredZoomLevel;

    localPlayerName = settings.general.playerName;

    // City-sim features are gated by the active mod. A savegame load may
    // re-enable this later if the save contains city-sim state.
    citySimEnabled_ = ModManager::instance().isCityModeActive();

    unitList.clear();       //holds all the units
    structureList.clear();  //all the structures
    bulletList.clear();

    sideBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_SideBar), HAlign::Right, VAlign::Top);
    topBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_TopBar), HAlign::Left, VAlign::Top);

    // set to true for now
    debug = false;

    powerIndicatorPos.h = spiceIndicatorPos.h = settings.video.height - 146 - 2;

    musicPlayer->changeMusic(MUSIC_PEACE);
    //////////////////////////////////////////////////////////////////////////
    SDL_Rect gameBoardRect = { 0, topBarPos.h, sideBarPos.x, getRendererHeight() - topBarPos.h };
    screenborder = new ScreenBorder(gameBoardRect);
}

void Game::initializeSpatialGrid(int mapWidth, int mapHeight) {
    constexpr int kSpatialGridCellSize = 4;

    if(mapWidth <= 0 || mapHeight <= 0) {
        spatialGrid.reset();
        return;
    }

    try {
        spatialGrid = std::make_unique<SpatialGrid>(mapWidth, mapHeight, kSpatialGridCellSize);
    } catch(const std::exception& ex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize spatial grid (%s)", ex.what());
        spatialGrid.reset();
    }
}

void Game::queueTargetRequest(Uint32 objectId) {
    if(objectId == NONE_ID) {
        return;
    }

    if(pendingTargetRequestIds.insert(objectId).second) {
        targetRequestQueue.push_back({objectId});
    }
}

void Game::queuePathRequest(Uint32 objectId) {
    if(objectId == NONE_ID) {
        return;
    }

    if(pendingPathRequestIds.insert(objectId).second) {
        pathRequestQueue.push_back({objectId});
    }
}

/**
    The destructor frees up all the used memory.
*/
Game::~Game() {
    // Close performance log
    closePerformanceLog();
    
    // Clean up cursor manager
    cursorManager.cleanup();

    if(pNetworkManager != nullptr) {
        pNetworkManager->setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)>());
        pNetworkManager->setOnReceiveCommandList(std::function<void (const std::string&, const CommandList&)>());
        pNetworkManager->setOnReceiveSelectionList(std::function<void (const std::string&, const std::set<Uint32>&, int)>());
        pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
    }

    for(StructureBase* pStructure : structureList) {
        delete pStructure;
    }
    structureList.clear();

    for(UnitBase* pUnit : unitList) {
        delete pUnit;
    }
    unitList.clear();

    for(Bullet* pBullet : bulletList) {
        delete pBullet;
    }
    bulletList.clear();

    for(Explosion* pExplosion : explosionList) {
        delete pExplosion;
    }
    explosionList.clear();

    if(spatialGrid) {
        spatialGrid->clear();
        spatialGrid.reset();
    }

    delete currentGameMap;
    currentGameMap = nullptr;
    delete screenborder;
    screenborder = nullptr;
}

void Game::initPerformanceLog() {
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(performanceLogFile.is_open()) {
        return;  // Already initialized
    }
    
    std::string logPath = getPerformanceLogFilepath();
    performanceLogFile.open(logPath, std::ios::out | std::ios::trunc);
    
    if(performanceLogFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&nowTime));
        
        performanceLogFile << "=== Dune City Performance Log Started " << timeStr << " ===" << std::endl;
        performanceLogFile << "Version: " << VERSION << std::endl;
        performanceLogFile << "Platform: " << SDL_GetPlatform() << std::endl;
        performanceLogFile << std::endl;
        performanceLogFile.flush();
        SDL_Log("Performance log initialized: %s", logPath.c_str());
    } else {
        SDL_Log("Warning: Could not open performance log file: %s", logPath.c_str());
    }
}

void Game::closePerformanceLog() {
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(performanceLogFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&nowTime));
        
        performanceLogFile << std::endl;
        performanceLogFile << "=== Performance Log Closed " << timeStr << " ===" << std::endl;
        performanceLogFile.close();
    }
}

void Game::logPerformance(const char* format, ...) {
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(!performanceLogFile.is_open()) {
        return;
    }
    
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    performanceLogFile << buffer << std::endl;
    performanceLogFile.flush();
}


void Game::initGame(const GameInitSettings& newGameInitSettings) {
    gameInitSettings = newGameInitSettings;

    // The host's mod choice is the source of truth for whether the
    // city-sim feature flag is on — not whatever mod the local player
    // happens to have active in their main menu. This matters most for
    // multiplayer clients (their local ModManager may be on vanilla),
    // but also keeps single-player consistent if mod state drifted
    // between menu navigation and game start.
    {
        const std::string& sessionMod = gameInitSettings.getModName();
        if (!sessionMod.empty()) {
            ModInfo info = ModManager::instance().getModInfo(sessionMod);
            // If the named mod isn't installed locally we keep the
            // earlier value (set in the Game constructor) so single
            // player still works — but for any installed mod the
            // session's choice wins.
            if (!info.name.empty()) {
                citySimEnabled_ = info.enablesCityMode;
            }
        }
    }

    targetRequestQueue.clear();
    pendingTargetRequestIds.clear();
    pathRequestQueue.clear();
    pendingPathRequestIds.clear();

    // Initialize performance logging
    initPerformanceLog();

    // Initialize cursor manager
    cursorManager.initialize();

    switch(gameInitSettings.getGameType()) {
        case GameType::LoadSavegame: {
            if(loadSaveGame(gameInitSettings.getFilename()) == false) {
                THROW(std::runtime_error, "Loading save game failed!");
            }
        } break;

        case GameType::LoadMultiplayer: {
            IMemoryStream memStream(gameInitSettings.getFiledata().c_str(), gameInitSettings.getFiledata().size());

            if(loadSaveGame(memStream) == false) {
                THROW(std::runtime_error, "Loading save game failed!");
            }
        } break;

        case GameType::Campaign:
        case GameType::Skirmish:
        case GameType::CustomGame:
        case GameType::CustomMultiplayer: {
            gameType = gameInitSettings.getGameType();
            randomGen.setSeed(gameInitSettings.getRandomSeed());

            objectData.loadFromINIFile("config/ObjectData.ini");

            // City zones must not be buildable when the active mod doesn't
            // opt into DuneCity city-sim features. Force-disable them in the
            // loaded ObjectData so every consumer (build lists, prerequisites,
            // AI, save/load) sees them as unavailable, regardless of what the
            // mod's ObjectData.ini says.
            if (!citySimEnabled_) {
                for (int h = 0; h < NUM_HOUSES; h++) {
                    objectData.data[Structure_ZoneResidential][h].enabled = false;
                    objectData.data[Structure_ZoneCommercial][h].enabled  = false;
                    objectData.data[Structure_ZoneIndustrial][h].enabled  = false;
                }
            }

            objectData.logSettings();

            if(gameInitSettings.getMission() != 0) {
                techLevel = ((gameInitSettings.getMission() + 1)/3) + 1 ;
            }

            INIMapLoader(this, gameInitSettings.getFilename(), gameInitSettings.getFiledata());

            if(bReplay == false && gameInitSettings.getGameType() != GameType::CustomGame && gameInitSettings.getGameType() != GameType::CustomMultiplayer) {
                /* do briefing */
                SDL_Log("Briefing...");
                BriefingMenu(gameInitSettings.getHouseID(), gameInitSettings.getMission(),BRIEFING).showMenu();
            }
        } break;

        default: {
        } break;
    }
}

void Game::initReplay(const std::string& filename) {
    bReplay = true;

    IFileStream fs;

    if(fs.open(filename) == false) {
        THROW(io_error, "Error while opening '%s'!", filename);
    }

    // override local player name as it was when the replay was created
    localPlayerName = fs.readString();

    // read GameInitInfo
    GameInitSettings loadedGameInitSettings(fs);

    // load all commands
    cmdManager.load(fs);

    initGame(loadedGameInitSettings);
}


void Game::processObjects()
{
    processTargetRequests();
    
    // Time pathfinding
    Uint64 pathStart = SDL_GetPerformanceCounter();
    processPathRequests();
    Uint64 pathEnd = SDL_GetPerformanceCounter();
    const double pathMs = getElapsedMs(pathStart, pathEnd);
    frameTiming.pathfindingMs += pathMs;
    frameTiming.pathfindingMsThisFrame += pathMs;

    // update all tiles
    for(int y = 0; y < currentGameMap->getSizeY(); y++) {
        for(int x = 0; x < currentGameMap->getSizeX(); x++) {
            currentGameMap->getTile(x,y)->update();
        }
    }

    // Time structure updates
    Uint64 structStart = SDL_GetPerformanceCounter();
    for(StructureBase* pStructure : structureList) {
        pStructure->update();
    }
    Uint64 structEnd = SDL_GetPerformanceCounter();
    const double structMs = getElapsedMs(structStart, structEnd);
    frameTiming.structuresMs += structMs;
    frameTiming.structuresMsThisFrame += structMs;

    if ((currentCursorMode == CursorMode_Placing) && selectedList.empty()) {
        setCursorMode(CursorMode_Normal);
    }

    // Time unit updates
    Uint64 unitStart = SDL_GetPerformanceCounter();
    frameTiming.unitCount = unitList.size();
    for(UnitBase* pUnit : unitList) {
        pUnit->update();
    }
    Uint64 unitEnd = SDL_GetPerformanceCounter();
    const double unitMs = getElapsedMs(unitStart, unitEnd);
    frameTiming.unitsMs += unitMs;
    frameTiming.unitsMsThisFrame += unitMs;

    for(Bullet* pBullet : bulletList) {
        pBullet->update();
    }

    for(Explosion* pExplosion : explosionList) {
        pExplosion->update();
    }
}

void Game::processTargetRequests() {
    if(targetRequestQueue.empty()) {
        return;
    }

    // MULTIPLAYER FIX (Issue #2): Removed time-based budget check
    // Now uses ONLY deterministic per-cycle request limit
    // Timing is still measured for profiling, but does NOT affect execution

    const Uint64 start = SDL_GetPerformanceCounter();  // PROFILING ONLY

    // Deterministic per-cycle limit (same on all clients regardless of performance)
    // Target acquisition is fast (~0.05ms each), so we can process many per cycle
    static constexpr int TargetRequestsPerCycle = 50;  // ~2.5ms budget at 0.05ms each
    
    int processedCount = 0;
    while(!targetRequestQueue.empty() && processedCount < TargetRequestsPerCycle) {
        TargetRequest request = targetRequestQueue.front();
        targetRequestQueue.pop_front();
        pendingTargetRequestIds.erase(request.objectId);

        auto* unit = dynamic_cast<UnitBase*>(objectManager.getObject(request.objectId));
        if(unit != nullptr) {
            unit->resolvePendingTargetRequest();
        }

        processedCount++;
    }
    
    const Uint64 end = SDL_GetPerformanceCounter();  // PROFILING ONLY
    const double elapsedMs = getElapsedMs(start, end);  // PROFILING ONLY
    
    // MULTIPLAYER TELEMETRY: Log synchronization info and queue starvation warnings
    if(pNetworkManager != nullptr && processedCount > 0) {
        // Log every 10 seconds to verify synchronization
        if((gameCycleCount % MILLI2CYCLES(10000)) == 0) {
            SDL_Log("[MP-Sync Cycle %d] Targets: %d, Queue: %zu, Time: %.2fms",
                    gameCycleCount,
                    processedCount,
                    targetRequestQueue.size(),
                    elapsedMs);
        }
    }
    
    // Queue starvation warning (deterministic across clients)
    if(!targetRequestQueue.empty() && processedCount >= TargetRequestsPerCycle) {
        // Log every 5 seconds if queue is growing
        if((gameCycleCount % MILLI2CYCLES(5000)) == 0 && targetRequestQueue.size() > 50) {
            SDL_Log("[MP-Sync WARNING Cycle %d] Target queue may be starved: %zu requests pending",
                    gameCycleCount, targetRequestQueue.size());
        }
    }
}

void Game::recordPathTokens(size_t totalTokens) {
    const size_t lastIndex = frameTiming.pathTokenHistogram.size() - 1;
    for(size_t i = 0; i < PathTokenBucketBounds.size(); ++i) {
        if(totalTokens < PathTokenBucketBounds[i]) {
            frameTiming.pathTokenHistogram[i]++;
            return;
        }
    }
    frameTiming.pathTokenHistogram[lastIndex]++;
}

void Game::recordCompletedPathTokens(size_t totalTokens) {
    frameTiming.tokensPerCompletedPathStats.add(static_cast<double>(totalTokens));
    if(totalTokens < frameTiming.minTokensPerCompletedPath) {
        frameTiming.minTokensPerCompletedPath = totalTokens;
    }
    if(totalTokens > frameTiming.maxTokensPerCompletedPath) {
        frameTiming.maxTokensPerCompletedPath = totalTokens;
    }
}

void Game::recordFailedPathTokens(size_t totalTokens) {
    frameTiming.tokensPerFailedPathStats.add(static_cast<double>(totalTokens));
    if(totalTokens < frameTiming.minTokensPerFailedPath) {
        frameTiming.minTokensPerFailedPath = totalTokens;
    }
    if(totalTokens > frameTiming.maxTokensPerFailedPath) {
        frameTiming.maxTokensPerFailedPath = totalTokens;
    }
}

void Game::logPathInstrumentationIfNeeded() {
    constexpr Uint32 kInstrumentationInterval = MILLI2CYCLES(30 * 1000);
    if(gameCycleCount - lastPathInstrumentationLogCycle < kInstrumentationInterval) {
        return;
    }

    lastPathInstrumentationLogCycle = gameCycleCount;

    size_t hits = frameTiming.pathReuseHitsWindow;
    size_t misses = frameTiming.pathReuseMissesWindow;
    size_t totalChecks = hits + misses;

    frameTiming.totalPathReuseHits += hits;
    frameTiming.totalPathReuseMisses += misses;

    frameTiming.pathReuseHitsWindow = 0;
    frameTiming.pathReuseMissesWindow = 0;

    const auto poolStats = AStarSearch::getPoolUsageStats();
    const double reuseRate = (totalChecks > 0)
        ? (100.0 * static_cast<double>(hits) / static_cast<double>(totalChecks))
        : 0.0;

    const int mapSize = (currentGameMap != nullptr)
        ? currentGameMap->getSizeX()
        : 0;

    logPerformance("[PathInstrumentation] Cycle %u | Map: %dx%d | Reuse: %zu/%zu (%.1f%%) | Pool hits=%zu grow=%zu fallback=%zu | Pool usage=%zu/%zu",
                   gameCycleCount, mapSize, mapSize, hits, totalChecks, reuseRate,
                   poolStats.reuseHits, poolStats.bufferExpansions, poolStats.fallbackAllocs,
                   poolStats.buffersInUse, poolStats.totalBuffers);
    
    // Log path invalidation reasons
    if(misses > 0) {
        logPerformance("[PathInvalidation] DestChanged=%zu HuntTooFar=%zu Blocked=%zu",
                       frameTiming.pathInvalidDestChanged,
                       frameTiming.pathInvalidHuntTooFar,
                       frameTiming.pathInvalidBlocked);
        
        // Reset counters
        frameTiming.pathInvalidDestChanged = 0;
        frameTiming.pathInvalidHuntTooFar = 0;
        frameTiming.pathInvalidBlocked = 0;
    }
    
    // Log movement pause reasons (for debugging stuttering)
    uint64_t totalPauses = frameTiming.pauseWaitingForPath + frameTiming.pauseWaitingForBlocker +
                           frameTiming.pauseRecalcCooldown + frameTiming.pauseTurningToFace;
    if(totalPauses > 0) {
        logPerformance("[MovementPauses] Total=%llu | WaitPath=%llu WaitBlocker=%llu RecalcCD=%llu Turning=%llu",
                       totalPauses,
                       frameTiming.pauseWaitingForPath,
                       frameTiming.pauseWaitingForBlocker,
                       frameTiming.pauseRecalcCooldown,
                       frameTiming.pauseTurningToFace);
        
        // Reset counters
        frameTiming.pauseWaitingForPath = 0;
        frameTiming.pauseWaitingForBlocker = 0;
        frameTiming.pauseRecalcCooldown = 0;
        frameTiming.pauseTurningToFace = 0;
    }
}

void Game::processPathRequests() {
    if(pathRequestQueue.empty()) {
        frameTiming.pathsPerCycleStats.add(0.0);
        frameTiming.pathTokensPerCycleStats.add(0.0);
        logPathInstrumentationIfNeeded();
        return;
    }

    // MULTIPLAYER FIX (Issue #1): Removed time-based budget checks
    // Now uses ONLY deterministic token budget for multiplayer synchronization
    // Timing is still measured for profiling, but does NOT affect execution

    const Uint64 start = SDL_GetPerformanceCounter();  // PROFILING ONLY

    frameTiming.pathsProcessedThisCycle = 0;
    frameTiming.pathfindingMsThisCycle = 0.0;
    frameTiming.pathTokensThisCycle = 0;

    // Track queue depth
    const int queueDepth = static_cast<int>(pathRequestQueue.size());
    if(queueDepth > frameTiming.maxPathQueueLength) {
        frameTiming.maxPathQueueLength = queueDepth;
    }

    // PHASE 1: NEGOTIATED TOKEN BUDGET WITH CARRY-OVER (single-player only)
    // Start at 15k tokens/cycle, adapt between 5k-25k based on FPS
    // NEVER scale UP aggressively (that caused the freeze!)
    // Carry-over is DISABLED in multiplayer to prevent desync
    
    // Calculate budget with carry-over (capped at kHardCap)
    // Note: carryOverTokens is always 0 in multiplayer
    size_t budget = std::min<size_t>(
        negotiatedBudget + carryOverTokens,
        kHardCap
    );
    carryOverTokens = 0;  // Reset for this cycle
    
    size_t tokensRemaining = budget;
    size_t tokensUsedThisCycle = 0;
    
    // Process paths until token budget exhausted (deterministic stopping condition)
    while(!pathRequestQueue.empty() && tokensRemaining > 0) {
        PathRequest request = pathRequestQueue.front();
        pathRequestQueue.pop_front();
        pendingPathRequestIds.erase(request.objectId);

        auto* unit = dynamic_cast<UnitBase*>(objectManager.getObject(request.objectId));
        if(unit != nullptr) {
            UnitBase::PathRequestStats stats = unit->resolvePendingPathRequest();
            
            frameTiming.pathsProcessedThisCycle++;
            frameTiming.totalPathsProcessedThisFrame++;
            frameTiming.totalPathsProcessed++;
            
            // Track tokens (nodes expanded)
            const size_t tokens = stats.nodesExpanded;
            frameTiming.pathTokensThisCycle += tokens;
            frameTiming.pathTokensThisFrame += tokens;
            frameTiming.totalPathTokens += tokens;
            tokensUsedThisCycle += tokens;
            
            // Deduct from token budget
            if (tokens < tokensRemaining) {
                tokensRemaining -= tokens;
            } else {
                tokensRemaining = 0;
            }
            
            // Track min/max tokens per cycle
            if(frameTiming.pathTokensThisCycle > frameTiming.maxPathTokensPerCycle) {
                frameTiming.maxPathTokensPerCycle = frameTiming.pathTokensThisCycle;
            }
            
            // Record token distribution histogram
            recordPathTokens(tokens);
            
            // Track completed vs failed paths
            if(!stats.invalidDestination) {
                if(stats.pathFound) {
                    recordCompletedPathTokens(tokens);
                } else {
                    frameTiming.pathsFailedThisFrame++;
                    frameTiming.totalPathsFailed++;
                    recordFailedPathTokens(tokens);
                }
            }
        }
    }
    
    // PHASE 1.2: BANK UNUSED TOKENS FOR NEXT CYCLE
    // MULTIPLAYER FIX: Disable carry-over in multiplayer to prevent desync
    // Carry-over can accumulate drift if pathfinding is even slightly non-deterministic
    if (tokensUsedThisCycle < budget && pNetworkManager == nullptr) {
        // Bank unused tokens (capped at kDebtCap) - SINGLE-PLAYER ONLY
        carryOverTokens = std::min<size_t>(
            kDebtCap,
            budget - tokensUsedThisCycle
        );
    } else if(pNetworkManager != nullptr) {
        // Multiplayer: Always discard unused tokens to maintain sync
        carryOverTokens = 0;
    }
    
    // DESYNC DETECTION: Log token usage on specific cycles for debugging
    if(pNetworkManager != nullptr && (gameCycleCount % 375 == 0)) {
        SDL_Log("[DESYNC DEBUG] Cycle %d: budget=%zu, tokensUsed=%zu, carryOver=%zu (always 0 in MP), queue=%zu",
                gameCycleCount, negotiatedBudget, tokensUsedThisCycle, carryOverTokens, pathRequestQueue.size());
    }
    
    // Track token budget exhaustion
    if (tokensRemaining == 0 && !pathRequestQueue.empty()) {
        frameTiming.tokenBudgetExhaustedCount++;
    }
    
    const Uint64 end = SDL_GetPerformanceCounter();  // PROFILING ONLY
    frameTiming.pathfindingMsThisCycle = getElapsedMs(start, end);  // PROFILING ONLY
    
    // Record cycle statistics
    frameTiming.pathsPerCycleStats.add(static_cast<double>(frameTiming.pathsProcessedThisCycle));
    frameTiming.pathTokensPerCycleStats.add(static_cast<double>(frameTiming.pathTokensThisCycle));
    
    // Track max tokens per frame
    if(frameTiming.pathTokensThisFrame > frameTiming.maxPathTokensPerFrame) {
        frameTiming.maxPathTokensPerFrame = frameTiming.pathTokensThisFrame;
    }
    
    // Track max per-cycle values (PROFILING ONLY - does not affect gameplay)
    if(frameTiming.pathfindingMsThisCycle > frameTiming.maxPathfindingMsPerCycle) {
        frameTiming.maxPathfindingMsPerCycle = frameTiming.pathfindingMsThisCycle;
    }
    if(frameTiming.pathsProcessedThisCycle > frameTiming.maxPathsPerCycle) {
        frameTiming.maxPathsPerCycle = frameTiming.pathsProcessedThisCycle;
    }
    
    // MULTIPLAYER TELEMETRY: Log synchronization info
    if(pNetworkManager != nullptr && frameTiming.pathsProcessedThisCycle > 0) {
        // Log every 10 seconds to verify synchronization
        if((gameCycleCount % MILLI2CYCLES(10000)) == 0) {
            SDL_Log("[MP-Sync Cycle %d] Budget: %zu (carry: %zu), Paths: %d, Tokens Used: %zu, Queue: %zu",
                    gameCycleCount,
                    negotiatedBudget,
                    carryOverTokens,
                    frameTiming.pathsProcessedThisCycle,
                    frameTiming.pathTokensThisCycle,
                    pathRequestQueue.size());
        }
    }

    logPathInstrumentationIfNeeded();
}


void Game::requestLowerBudget(int steps) {
    // PHASE 1.4: REQUEST LOWER BUDGET VIA NETWORK COMMAND
    SDL_Log("[PathBudget] requestLowerBudget triggered (current=%zu, steps=%d)",
            negotiatedBudget, steps);
    
    // Calculate target: reduce by steps × 500
    // Examples:
    //   1 step: 15k → 14.5k (gradual reduction)
    //   4 steps: 15k → 13k (aggressive FPS recovery)
    size_t reduction = steps * 500;
    size_t targetBudget;
    if (negotiatedBudget > kMinBudget + reduction) {
        targetBudget = negotiatedBudget - reduction;
    } else {
        targetBudget = kMinBudget;  // Floor at minimum (8k)
    }
    
    targetBudget = std::clamp(targetBudget, kMinBudget, kMaxBudget);
    
    // Don't request if already at minimum
    if (targetBudget >= negotiatedBudget) {
        SDL_Log("[PathBudget] Already at minimum (%zu tokens/cycle)", negotiatedBudget);
        return;
    }
    
    SDL_Log("[PathBudget] Requesting reduction: %zu -> %zu tokens/cycle (%d steps × 500)",
            negotiatedBudget, targetBudget, steps);
    logPerformance("[BUDGET CHANGE] Cycle %d: Requesting reduction %zu -> %zu tokens/cycle (%d steps × 500, queue=%zu)",
            gameCycleCount, negotiatedBudget, targetBudget, steps, pathRequestQueue.size());
    
    // In single-player, apply immediately
    if (pNetworkManager == nullptr) {
        negotiatedBudget = targetBudget;
        carryOverTokens = 0;  // Reset carry-over on budget change
        lastBudgetAction = BudgetAction::DECREASED;  // Track emergency drop for anti-oscillation
        SDL_Log("[PathBudget] Applied immediately (single-player)");
        logPerformance("[BUDGET CHANGE] Cycle %d: Applied immediately - new budget=%zu", 
                gameCycleCount, negotiatedBudget);
        return;
    }
    
    // In multiplayer, use the proper budget negotiation system
    // Only the host can broadcast budget changes
    if(pNetworkManager->isServer()) {
        // HOST: Broadcast the change to all clients
        SDL_Log("[PathBudget] Host broadcasting budget reduction: %zu -> %zu", 
                negotiatedBudget, targetBudget);
        lastBudgetAction = BudgetAction::DECREASED;  // Track emergency drop for anti-oscillation
        broadcastBudgetChange(targetBudget);
    } else {
        // CLIENT: This shouldn't happen - clients don't request budget changes
        // Budget changes are driven by the host's decision logic
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[PathBudget] Client called requestLowerBudget() - ignoring (host controls budget in multiplayer)");
        logPerformance("[PathBudget] Cycle %d: Client incorrectly called requestLowerBudget() - ignoring",
                gameCycleCount);
    }
}

// MULTIPLAYER BUDGET NEGOTIATION IMPLEMENTATION

void Game::checkBudgetAdjustment() {
    if(pNetworkManager != nullptr && !pNetworkManager->isServer()) {
        // CLIENT: Send stats ONE CYCLE BEFORE the check interval
        // This ensures the host has fresh data when it makes its decision
        if((gameCycleCount + 1) % kBudgetCheckInterval == 0 && frameTiming.frameCount > 0) {
            const double avgFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
            
            // Send the budget we'll have at decision time (next cycle), not current budget
            // This avoids false DESYNC detection when a budget change is pending
            size_t budgetAtDecisionTime = negotiatedBudget;
            for (const auto& pending : pendingBudgetChanges) {
                if (pending.applyCycle == gameCycleCount + 1) {
                    budgetAtDecisionTime = pending.newBudget;
                    break;  // Use the first pending change for next cycle
                }
            }
            
            sendStatsToHost(avgFps, frameTiming.simMsAvg, pathRequestQueue.size(), budgetAtDecisionTime);
        }
    }
    else if(gameCycleCount % kBudgetCheckInterval == 0 && frameTiming.frameCount > 0) {
        const double avgFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
        
        if(pNetworkManager != nullptr && pNetworkManager->isServer()) {
            // HOST: Make decision based on collected stats + handle missing stats
            // At this point, client stats from (cycle - 1) have arrived
            makeHostBudgetDecision();
        } else {
            // SINGLE-PLAYER: Apply adjustment immediately
            applySinglePlayerBudgetAdjustment(avgFps);
        }
    }
}

void Game::applySinglePlayerBudgetAdjustment(float avgFps) {
    // VSync-compatible thresholds: 50/55/58 FPS for decrease, 59.5 FPS for increase
    const double fpsIncreaseThreshold = 59.5;
    const double fpsDecreaseThresholdModerate = 58.0;
    const double fpsDecreaseThresholdAggressive = 55.0;
    const double fpsDecreaseThresholdSevere = 50.0;
    
    // DROP: Three-tier reduction based on severity
    if(avgFps < fpsDecreaseThresholdSevere && negotiatedBudget > kMinBudget) {
        // Severe lag: massive reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(8);  // Reduce by 4k (8 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 50: %.1f FPS - reducing budget severely (8 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 50: %.1f FPS - reducing budget severely %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    else if(avgFps < fpsDecreaseThresholdAggressive && negotiatedBudget > kMinBudget) {
        // Aggressive lag: strong reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(4);  // Reduce by 2k (4 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 55: %.1f FPS - reducing budget aggressively (4 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 55: %.1f FPS - reducing budget aggressively %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    else if(avgFps < fpsDecreaseThresholdModerate && negotiatedBudget > kMinBudget) {
        // Moderate lag: gentle reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(2);  // Reduce by 1k (2 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 58: %.1f FPS - reducing budget moderately (2 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 58: %.1f FPS - reducing budget moderately %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    // RAISE: If average FPS stable at 59.5+ AND pathfinding isn't consuming too much time
    else if(avgFps >= fpsIncreaseThreshold && negotiatedBudget < kMaxBudget) {
        // ANTI-OSCILLATION: Don't increase if we just increased last cycle
        // This ensures at least 2 intervals (12 seconds) between increases
        if(lastBudgetAction == BudgetAction::INCREASED) {
            SDL_Log("[PathBudget] Cycle %d: Skipping increase - just increased last check (anti-oscillation)", 
                    gameCycleCount);
            logPerformance("[PathBudget] Cycle %d: Skipping increase - stabilizing after last increase", 
                    gameCycleCount);
            lastBudgetAction = BudgetAction::NONE;  // Reset for next cycle
            return;
        }
        
        const size_t queueDepth = pathRequestQueue.size();
        
        // Check average pathfinding time per frame over the interval
        // Use frameTiming.pathfindingMs (cumulative) not pathfindingMsThisFrame (single frame)
        const double avgPathfindingMs = (frameTiming.frameCount > 0) 
            ? (frameTiming.pathfindingMs / frameTiming.frameCount) 
            : 0.0;
        
        // Safety check: don't increase if pathfinding is already eating too much time
        if(avgPathfindingMs > 10.0) {
            SDL_Log("[PathBudget] Cycle %d: FPS=%.1f but pathfinding time too high (%.1fms) - blocking budget increase", 
                    gameCycleCount, avgFps, avgPathfindingMs);
            logPerformance("[PathBudget] Cycle %d: FPS=%.1f but pathfinding time too high (%.1fms) - blocking budget increase", 
                    gameCycleCount, avgFps, avgPathfindingMs);
            lastBudgetAction = BudgetAction::NONE;
        } else {
            // KEY INSIGHT: High queue + good FPS = we have CPU headroom to drain the queue!
            // Use consistent 500 token increments with 12-second spacing to prevent oscillation
            size_t oldBudget = negotiatedBudget;
            size_t increaseAmount = 500;  // Always 500, but with anti-oscillation delay
            const char* increaseReason = (queueDepth > 300) ? "queue>300" : "queue<=300";
            
            size_t newBudget = std::min<size_t>(negotiatedBudget + increaseAmount, kMaxBudget);
            
            SDL_Log("[PathBudget] Cycle %d: %s FPS>=59.5 (%.1f) pathfinding=%.1fms queue=%zu - increasing budget %zu -> %zu", 
                    gameCycleCount, increaseReason, avgFps, avgPathfindingMs, queueDepth, oldBudget, newBudget);
            logPerformance("[BUDGET CHANGE] Cycle %d: %s - %zu -> %zu tokens/cycle (FPS=%.1f, pathfinding=%.1fms, queue=%zu)",
                    gameCycleCount, increaseReason, oldBudget, newBudget, avgFps, avgPathfindingMs, queueDepth);
            
            negotiatedBudget = newBudget;
            carryOverTokens = 0;  // Reset carry-over when budget changes
            lastBudgetAction = BudgetAction::INCREASED;  // Track action
            
            SDL_Log("[PathBudget] Applied immediately - new budget=%zu", negotiatedBudget);
            logPerformance("[BUDGET CHANGE] Cycle %d: Applied immediately - new budget=%zu", 
                    gameCycleCount, negotiatedBudget);
        }
    }
    // NOTE: Don't reset lastBudgetAction when FPS is stable (60-80)!
    // We need to preserve the action state for anti-oscillation to work.
    // lastBudgetAction is only reset after we've skipped an increase cycle.
}

void Game::sendStatsToHost(float avgFps, float simMsAvg, size_t queueDepth, size_t currentBudget) {
    if(pNetworkManager == nullptr) {
        return;  // Single-player, nothing to send
    }
    
    // Host doesn't need to send stats to itself (it computes them locally in makeHostBudgetDecision)
    if(pNetworkManager->isServer()) {
        return;  // Host, nothing to send
    }
    
    // LOGGING: Outbound stats to host (before sending)
    SDL_Log("[PathBudget CLIENT] → OUTBOUND to host: FPS=%.1f, SimAvg=%.2fms, queue=%zu, budget=%zu (cycle %d)",
            avgFps, simMsAvg, queueDepth, currentBudget, gameCycleCount);
    logPerformance("[CLIENT OUTBOUND] Cycle %d: Sending stats to host: FPS=%.1f, SimAvg=%.2fms, queue=%zu, budget=%zu, carryOver=%zu",
            gameCycleCount, avgFps, simMsAvg, queueDepth, currentBudget, carryOverTokens);
    
    // Send via NetworkManager (including simulation timing)
    pNetworkManager->sendClientStats(avgFps, simMsAvg, queueDepth, currentBudget, gameCycleCount);
    
    SDL_Log("[PathBudget CLIENT] ✓ Stats sent to host");
    logPerformance("[CLIENT OUTBOUND] Cycle %d: Stats packet sent successfully", gameCycleCount);
}

void Game::handleClientStats(Uint32 clientId, Uint32 gameCycle, float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget) {
    // HOST ONLY: Collect stats from clients
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;  // Only host processes client stats
    }
    
    // Store client stats
    ClientPerformanceStats stats;
    stats.clientId = clientId;
    stats.lastUpdateCycle = gameCycle;
    stats.avgFps = avgFps;
    stats.simMsAvg = simMsAvg;  // POST-VSYNC: Primary metric for CPU load
    stats.queueDepth = queueDepth;  // Instantaneous at cycle boundary
    stats.currentBudget = currentBudget;
    stats.missedUpdates = 0;  // Reset counter on successful receive
    
    clientStats[clientId] = stats;
    
    // LOGGING: Inbound client stats
    SDL_Log("[PathBudget HOST] ← INBOUND from Client %d: FPS=%.1f, SimAvg=%.2fms, queue=%d, budget=%d (cycle %d)",
            clientId, avgFps, simMsAvg, queueDepth, currentBudget, gameCycle);
    logPerformance("[HOST INBOUND] Cycle %d: Client %d stats: FPS=%.1f, SimAvg=%.2fms, queue=%d, budget=%d",
            gameCycleCount, clientId, avgFps, simMsAvg, queueDepth, currentBudget);
    
    // Mark that we received stats from this client
    // Don't immediately make a decision - wait for checkBudgetAdjustment to trigger it
}

bool Game::haveStatsFromAllClients() const {
    // NOTE: This function is currently unused since we switched to time-based decision making
    // The host makes decisions every kBudgetCheckInterval, not when all stats arrive
    // Missing stats are handled by handleMissingClientStats() which uses stale data
    
    if(pNetworkManager == nullptr) {
        return false;
    }
    
    // TODO: If we ever need this again, implement getConnectedClientCount()
    // const size_t expectedClientCount = pNetworkManager->getConnectedClientCount();
    // return clientStats.size() >= expectedClientCount;
    
    return false;  // Unused
}

void Game::handleMissingClientStats() {
    // HOST ONLY: Handle clients that haven't sent stats
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    for(auto& [clientId, stats] : clientStats) {
        const Uint32 cyclesSinceLastUpdate = gameCycleCount - stats.lastUpdateCycle;
        
        // Clients send stats one cycle BEFORE the check interval
        // So we expect stats every kBudgetCheckInterval cycles, but they arrive at (interval - 1)
        // If stats are older than (kBudgetCheckInterval + a few grace cycles), they're stale
        if(cyclesSinceLastUpdate > kBudgetCheckInterval + 5) {
            stats.missedUpdates++;
            
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PathBudget] Client %d stats are stale (last update: %d cycles ago, missed: %d)",
                clientId, cyclesSinceLastUpdate, stats.missedUpdates);
            
            // After 3 consecutive missed updates (3 × 7.5s = 22.5 seconds)
            if(stats.missedUpdates >= 3) {
                // Assume worst-case: client is struggling
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget] Client %d has missed 3+ updates - assuming poor performance",
                    clientId);
                logPerformance("[PathBudget] Cycle %d: Client %d stale (missed %d) - forcing reduction",
                        gameCycleCount, clientId, stats.missedUpdates);
                
                // Force FPS to 0 to trigger reduction
                stats.avgFps = 0.0f;
                stats.queueDepth = 1000;  // Assume high queue
            }
            // After 2 missed updates: reuse last known values but log warning
            else {
                SDL_Log("[PathBudget] Client %d: Reusing last known values (FPS=%.1f, queue=%d)",
                        clientId, stats.avgFps, stats.queueDepth);
                logPerformance("[PathBudget] Cycle %d: Client %d stats reused (missed %d)",
                        gameCycleCount, clientId, stats.missedUpdates);
            }
        }
    }
}

void Game::makeHostBudgetDecision() {
    // HOST ONLY: Decide budget based on ALL client stats + own stats
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    // Calculate own stats
    const double hostFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
    const size_t hostQueueDepth = pathRequestQueue.size();  // Instantaneous
    
    // If we have no client stats yet (first interval), only use host's own stats
    // This is safe because clients send stats one cycle early, so after the first interval
    // we'll always have client data. For the very first decision, host-only is acceptable.
    if(clientStats.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[PathBudget HOST] First decision with no client stats yet - using host-only (FPS=%.1f, queue=%zu)",
            hostFps, hostQueueDepth);
        logPerformance("[HOST DECISION] Cycle %d: FIRST INTERVAL (no client stats yet - host-only decision)",
                gameCycleCount);
        // Fall through to make decision based on host stats only
    }
    
    // Handle missing client stats (timeout logic)
    handleMissingClientStats();
    
    // Find minimum FPS and maximum CPU load across all peers (slowest peer wins)
    float minFps = hostFps;
    size_t maxQueueDepth = hostQueueDepth;
    float maxSimMsAvg = frameTiming.simMsAvg;  // Track worst CPU load
    
    // Track desync status
    bool allClientsSynced = true;
    
    for(const auto& [clientId, stats] : clientStats) {
        // Validate budget synchronization using cycle-based comparison
        // See: .analysis/features/pathbudget-sync/design.md for protocol contract
        
        bool budgetMatches = (stats.currentBudget == negotiatedBudget);
        
        // Defense-in-depth: Allow previous budget ONLY if client's report is from BEFORE the change
        // This is a tight cycle-based check, not an arbitrary time window
        bool clientReportFromBeforeChange = (stats.lastUpdateCycle < lastBudgetChangeCycle);
        bool matchesPrevious = (stats.currentBudget == previousNegotiatedBudget);
        
        // Budget is valid if:
        // 1. It matches current budget (normal case), OR
        // 2. Client's report is from before the last change AND matches previous budget
        bool budgetValid = budgetMatches || (clientReportFromBeforeChange && matchesPrevious);
        
        if(!budgetValid) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "[PathBudget] DESYNC DETECTED! Client %d budget=%d but host=%zu (prev=%zu, clientCycle=%d, changeCycle=%d)",
                clientId, stats.currentBudget, negotiatedBudget, previousNegotiatedBudget,
                stats.lastUpdateCycle, lastBudgetChangeCycle);
            logPerformance("[DESYNC CRITICAL] Client %d has budget=%d but host has %zu - re-syncing immediately",
                    clientId, stats.currentBudget, negotiatedBudget);
            
            // Immediately re-sync this client
            resyncClientBudget(clientId);
            allClientsSynced = false;
        }
        
        // Decision logic uses FPS, queue depth, and CPU load (simMsAvg)
        if(stats.avgFps < minFps) {
            minFps = stats.avgFps;  // Track slowest peer (lowest FPS)
        }
        if(stats.queueDepth > maxQueueDepth) {
            maxQueueDepth = stats.queueDepth;
        }
        if(stats.simMsAvg > maxSimMsAvg) {
            maxSimMsAvg = stats.simMsAvg;  // Track highest CPU load
        }
    }
    
    // If any clients are out of sync, don't make budget changes until they're synced
    if(!allClientsSynced) {
        SDL_Log("[PathBudget] Deferring budget decision until all clients re-synced");
        logPerformance("[PathBudget] Cycle %d: Deferring decision - waiting for client re-sync",
                gameCycleCount);
        return;  // Skip this decision cycle
    }
    
    // Decision logic: "Slowest peer wins" - based on FPS AND CPU load
    size_t newBudget = negotiatedBudget;
    const char* decisionReason = "STABLE";
    
    // VSync-compatible thresholds: 50/55/58 FPS for decrease, 59.5 FPS for increase
    const double fpsIncreaseThreshold = 59.5;
    const double fpsDecreaseThresholdModerate = 58.0;
    const double fpsDecreaseThresholdAggressive = 55.0;
    const double fpsDecreaseThresholdSevere = 50.0;
    
    if(minFps < fpsDecreaseThresholdSevere) {
        // AT LEAST ONE peer is struggling severely → REDUCE budget massively
        newBudget = negotiatedBudget >= 4000 + kMinBudget ? 
                    negotiatedBudget - 4000 : kMinBudget;
        decisionReason = "REDUCE (FPS<50, severe)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps < fpsDecreaseThresholdAggressive) {
        // AT LEAST ONE peer is struggling aggressively → REDUCE budget aggressively
        newBudget = negotiatedBudget >= 2000 + kMinBudget ? 
                    negotiatedBudget - 2000 : kMinBudget;
        decisionReason = "REDUCE (FPS<55, aggressive)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps < fpsDecreaseThresholdModerate) {
        // AT LEAST ONE peer is struggling moderately → REDUCE budget moderately
        newBudget = negotiatedBudget >= 1000 + kMinBudget ? 
                    negotiatedBudget - 1000 : kMinBudget;
        decisionReason = "REDUCE (FPS<58, moderate)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(maxSimMsAvg > 12.0f) {
        // AT LEAST ONE peer is CPU-bound (>12ms per 16ms tick) → REDUCE budget
        // This catches vsync-locked clients that still report 60 FPS but are struggling
        newBudget = negotiatedBudget >= 2000 + kMinBudget ? 
                    negotiatedBudget - 2000 : kMinBudget;
        decisionReason = "REDUCE (high CPU)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps > fpsIncreaseThreshold && negotiatedBudget < kMaxBudget) {
        // ALL peers have good FPS → consider INCREASE
        
        // ANTI-OSCILLATION: Don't increase if we just increased last cycle
        if(lastBudgetAction == BudgetAction::INCREASED) {
            decisionReason = "SKIP (stabilizing after increase)";
            newBudget = negotiatedBudget;  // Keep current
            lastBudgetAction = BudgetAction::NONE;  // Reset for next cycle
        } else {
            // Check host's pathfinding time as a safety metric
            // Use frameTiming.pathfindingMs (cumulative) not pathfindingMsThisFrame (single frame)
            const double avgPathfindingMs = (frameTiming.frameCount > 0) 
                ? (frameTiming.pathfindingMs / frameTiming.frameCount) 
                : 0.0;
            
            if(avgPathfindingMs > 10.0) {
                // Pathfinding taking too much time - don't increase
                decisionReason = "BLOCKED (pathfinding>10ms)";
                newBudget = negotiatedBudget;  // Keep current
                lastBudgetAction = BudgetAction::NONE;
            } else {
                // KEY INSIGHT: High queue + good FPS = we have CPU headroom!
                // Use consistent 500 token increments with 12-second spacing to prevent oscillation
                size_t increaseAmount = 500;  // Always 500, anti-oscillation via lastBudgetAction
                decisionReason = (maxQueueDepth > 300) ? "INCREASE (queue>300)" : "INCREASE (queue<=300)";
                
                newBudget = std::min<size_t>(negotiatedBudget + increaseAmount, kMaxBudget);
                lastBudgetAction = BudgetAction::INCREASED;  // Track action for all clients
            }
        }
    }
    else {
        // Stable - no change needed
        decisionReason = "STABLE (FPS in range)";
        // NOTE: Don't reset lastBudgetAction! Preserve it for anti-oscillation.
    }
    
    // LOGGING: Only log on actual budget changes (reduce host overhead)
    if(newBudget != negotiatedBudget) {
        SDL_Log("[PathBudget HOST] ═══ BUDGET CHANGE CYCLE %d ═══", gameCycleCount);
        SDL_Log("[PathBudget HOST] Inputs: minFps=%.1f (host=%.1f), maxQueue=%zu (host=%zu)",
                minFps, hostFps, maxQueueDepth, hostQueueDepth);
        SDL_Log("[PathBudget HOST] Decision: %s → %zu → %zu",
                decisionReason, negotiatedBudget, newBudget);
        logPerformance("[HOST DECISION] Cycle %d: %s %zu → %zu (minFps=%.1f, maxQueue=%zu)",
                gameCycleCount, decisionReason, negotiatedBudget, newBudget, minFps, maxQueueDepth);
        
        broadcastBudgetChange(newBudget);
    }
    // Silent when stable (no logging overhead)
    
    // Don't clear clientStats! We need to keep them to detect missing updates
    // The stats will be updated when new packets arrive (missedUpdates reset to 0)
}

void Game::broadcastBudgetChange(size_t newBudget) {
    // HOST ONLY: Broadcast budget change to all clients
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    // Calculate apply cycle: NEXT interval boundary (e.g., cycle 750 if we're at cycle 375)
    // This gives a full interval (375 cycles) for the broadcast to reach all clients
    // and ensures budget changes always align with measurement windows
    const Uint32 nextInterval = ((gameCycleCount / kBudgetCheckInterval) + 1) * kBudgetCheckInterval;
    const Uint32 applyCycle = nextInterval;
    
    // LOGGING: Outbound budget broadcast (before sending)
    SDL_Log("[PathBudget HOST] → OUTBOUND to ALL clients: budget %zu → %zu (apply cycle %d)",
            negotiatedBudget, newBudget, applyCycle);
    logPerformance("[HOST OUTBOUND] Cycle %d: Broadcasting budget %zu → %zu (apply cycle %d)",
            gameCycleCount, negotiatedBudget, newBudget, applyCycle);
    
    // Send via NetworkManager broadcast to all clients
    pNetworkManager->broadcastPathBudget(newBudget, applyCycle);
    
    // Also queue locally (host applies the same change)
    handleSetPathBudget(newBudget, applyCycle);
    
    SDL_Log("[PathBudget HOST] ✓ Broadcast sent to all clients and queued locally");
    logPerformance("[HOST OUTBOUND] Cycle %d: Broadcast sent successfully",
            gameCycleCount);
}

void Game::resyncClientBudget(Uint32 clientId) {
    // HOST ONLY: Emergency re-sync for out-of-sync client
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "[PathBudget] Re-syncing client %d with current budget %zu",
        clientId, negotiatedBudget);
    
    // Send immediate sync packet (apply at next interval boundary)
    // Even for desyncs, we apply at the next interval to maintain determinism
    const Uint32 nextInterval = ((gameCycleCount / kBudgetCheckInterval) + 1) * kBudgetCheckInterval;
    const Uint32 applyCycle = nextInterval;
    
    // NOTE: For now, we broadcast to all clients (since ENet doesn't have per-peer send in our current API)
    // This is safe - extra sync messages won't hurt synchronized clients
    pNetworkManager->broadcastPathBudget(negotiatedBudget, applyCycle);
    
    SDL_Log("[PathBudget HOST] ✓ Re-sync broadcast sent to all clients (including client %d)", clientId);
    logPerformance("[DESYNC RECOVERY] Cycle %d: Sent re-sync broadcast to all clients (budget=%zu, apply cycle %d)",
            gameCycleCount, negotiatedBudget, applyCycle);
}

void Game::handleSetPathBudget(size_t newBudget, Uint32 applyCycle) {
    // ALL CLIENTS: Queue the budget change for deterministic application
    
    // LOGGING: Inbound budget order from host
    SDL_Log("[PathBudget CLIENT] ← INBOUND from host: budget %zu → %zu (apply cycle %d, current cycle %d, delta=%d cycles)",
            negotiatedBudget, newBudget, applyCycle, gameCycleCount, 
            (int)(applyCycle - gameCycleCount));
    logPerformance("[CLIENT INBOUND] Cycle %d: Received budget order %zu → %zu (apply cycle %d, carryOver=%zu)",
            gameCycleCount, negotiatedBudget, newBudget, applyCycle, carryOverTokens);
    
    // Store in pending commands queue
    PendingBudgetChange change;
    change.newBudget = newBudget;
    change.applyCycle = applyCycle;
    change.resetCarryOver = true;  // CRITICAL: Must clear carry-over tokens!
    pendingBudgetChanges.push_back(change);
    
    SDL_Log("[PathBudget CLIENT] ✓ Budget order queued (pending: %zu)",
            pendingBudgetChanges.size());
}

void Game::applyPendingBudgetChanges() {
    // Called every cycle to check for pending budget changes
    
    auto it = pendingBudgetChanges.begin();
    while(it != pendingBudgetChanges.end()) {
        if(gameCycleCount >= it->applyCycle) {
            // Apply budget change NOW
            size_t oldBudget = negotiatedBudget;
            
            // Track previous budget for DESYNC defense-in-depth:
            // allow the previous budget only when the client report cycle is before this change cycle.
            previousNegotiatedBudget = oldBudget;
            lastBudgetChangeCycle = gameCycleCount;
            
            negotiatedBudget = std::clamp(it->newBudget, kMinBudget, kMaxBudget);
            
            // CRITICAL FOR SYNC: Reset carry-over tokens on budget change
            // Without this, clients can drift due to accumulated token differences
            size_t oldCarryOver = carryOverTokens;
            if(it->resetCarryOver) {
                carryOverTokens = 0;
            }
            
            // LOGGING: Budget application
            SDL_Log("[PathBudget] ★ APPLIED budget change at cycle %d: %zu → %zu (carryOver %zu → 0)",
                    gameCycleCount, oldBudget, negotiatedBudget, oldCarryOver);
            logPerformance("[BUDGET APPLIED] Cycle %d: %zu → %zu tokens/cycle (carryOver %zu → 0, queue=%zu)",
                    gameCycleCount, oldBudget, negotiatedBudget, oldCarryOver, pathRequestQueue.size());
            
            // Log full performance report on budget change
            logFrameTiming();
            
            // Remove from pending queue
            it = pendingBudgetChanges.erase(it);
        } else {
            ++it;
        }
    }
}


void Game::drawScreen()
{
    Coord TopLeftTile = screenborder->getTopLeftTile();
    Coord BottomRightTile = screenborder->getBottomRightTile();

    // Add margin to avoid pop-in during scrolling
    TopLeftTile.x = std::max(0, TopLeftTile.x - 2);
    TopLeftTile.y = std::max(0, TopLeftTile.y - 2);
    BottomRightTile.x = std::min(currentGameMap->getSizeX()-1, BottomRightTile.x + 2);
    BottomRightTile.y = std::min(currentGameMap->getSizeY()-1, BottomRightTile.y + 2);

    const auto x1 = TopLeftTile.x;
    const auto y1 = TopLeftTile.y;
    const auto x2 = BottomRightTile.x + 1;
    const auto y2 = BottomRightTile.y + 1;

    /* draw ground */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitGround(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw city overlay */
    if (currentCityOverlay_ != DuneCity::CityOverlayMode::None && citySimulation_) {
        drawCityOverlay(x1, y1, x2, y2);
    }

    /* draw placeholder road visuals */
    drawCityRoads(x1, y1, x2, y2);

    /* draw structures */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitStructures(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw underground units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitUndergroundUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw dead objects */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitDeadUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw infantry */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitInfantry(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw non-infantry ground units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitNonInfantryGroundUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw bullets */
    for(const Bullet* pBullet : bulletList) {
        pBullet->blitToScreen();
    }


    /* draw explosions */
    for(const Explosion* pExplosion : explosionList) {
        pExplosion->blitToScreen();
    }

    /* draw air units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [&](Tile& t) {
            t.blitAirUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    // draw the gathering point line if a structure is selected
    if(selectedList.size() == 1) {
        StructureBase *pStructure = dynamic_cast<StructureBase*>(getObjectManager().getObject(*selectedList.begin()));
        if(pStructure != nullptr) {
            pStructure->drawGatheringPointLine();
        }
    }

    /* draw selection rectangles */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            if (debug || t.isExploredByTeam(pLocalHouse->getTeamID())) {
                t.blitSelectionRects(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                    screenborder->world2screenY(t.getLocation().y*TILESIZE));
            }
        });


//////////////////////////////draw unexplored/shade

    if(debug == false) {
        SDL_Texture* hiddenTexZoomed = pGFXManager->getZoomedObjPic(ObjPic_Terrain_Hidden, currentZoomlevel);
        SDL_Texture* hiddenFogTexZoomed = pGFXManager->getZoomedObjPic(ObjPic_Terrain_HiddenFog, currentZoomlevel);
        int zoomedTileSize = world2zoomedWorld(TILESIZE);
        for(int x = screenborder->getTopLeftTile().x - 1; x <= screenborder->getBottomRightTile().x + 1; x++) {
            for (int y = screenborder->getTopLeftTile().y - 1; y <= screenborder->getBottomRightTile().y + 1; y++) {

                if((x >= 0) && (x < currentGameMap->getSizeX()) && (y >= 0) && (y < currentGameMap->getSizeY())) {
                    Tile* pTile = currentGameMap->getTile(x, y);

                    if(pTile->isExploredByTeam(pLocalHouse->getTeamID())) {
                        int hideTile = pTile->getHideTile(pLocalHouse->getTeamID());

                        if(hideTile != 0) {
                            SDL_Rect source = { hideTile*zoomedTileSize, 0, zoomedTileSize, zoomedTileSize };
                            SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                        zoomedTileSize, zoomedTileSize };
                            SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                        }

                        if(gameInitSettings.getGameOptions().fogOfWar == true) {
                            int fogTile = pTile->getFogTile(pLocalHouse->getTeamID());

                            if(pTile->isFoggedByTeam(pLocalHouse->getTeamID()) == true) {
                                fogTile = Terrain_HiddenFull;
                            }

                            if(fogTile != 0) {
                                SDL_Rect source = { fogTile*zoomedTileSize, 0,
                                                    zoomedTileSize, zoomedTileSize };
                                SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                            zoomedTileSize, zoomedTileSize };

                                SDL_RenderCopy(renderer, hiddenFogTexZoomed, &source, &drawLocation);
                            }
                        }
                    } else {
                        if(!debug) {
                            SDL_Rect source = { zoomedTileSize*15, 0, zoomedTileSize, zoomedTileSize };
                            SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                        zoomedTileSize, zoomedTileSize };
                            SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                        }
                    }
                } else {
                    // we are outside the map => draw complete hidden
                    SDL_Rect source = { zoomedTileSize*15, 0, zoomedTileSize, zoomedTileSize };
                    SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                zoomedTileSize, zoomedTileSize };
                    SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                }
            }
        }
    }

/////////////draw placement position

    if(currentCursorMode == CursorMode_Placing) {
        //if user has selected to place a structure

        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY)) {
            //if mouse is not over game bar

            int xPos = screenborder->screen2MapX(drawnMouseX);
            int yPos = screenborder->screen2MapY(drawnMouseY);

            if(selectedList.size() == 1) {
                auto pBuilder = dynamic_cast<BuilderBase*>(objectManager.getObject(*selectedList.begin()));
                if(pBuilder) {
                    int placeItem = pBuilder->getCurrentProducedItem();
                    Coord structuresize = getStructureSize(placeItem);

                    bool withinRange = false;
                    for (int i = xPos; i < (xPos + structuresize.x); i++) {
                        for (int j = yPos; j < (yPos + structuresize.y); j++) {
                            if (currentGameMap->isWithinBuildRange(i, j, pBuilder->getOwner())) {
                                withinRange = true;         //find out if the structure is close enough to other buildings
                            }
                        }
                    }

                    SDL_Texture* validPlace = nullptr;
                    SDL_Texture* invalidPlace = nullptr;

                    switch(currentZoomlevel) {
                        case 0: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel0);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel0);
                        } break;

                        case 1: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel1);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel1);
                        } break;

                        case 2:
                        default: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel2);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel2);
                        } break;

                    }

                    for(int i = xPos; i < (xPos + structuresize.x); i++) {
                        for(int j = yPos; j < (yPos + structuresize.y); j++) {
                            SDL_Texture* image;

                            bool tileValid = false;
                            if(withinRange && currentGameMap->tileExists(i,j)) {
                                Tile* pTile = currentGameMap->getTile(i,j);
                                if(isZoneStructure(placeItem)) {
                                    tileValid = !pTile->isMountain() && !pTile->hasAGroundObject();
                                } else {
                                    tileValid = pTile->isRock() && !pTile->isMountain() && !pTile->hasAGroundObject()
                                        && !(((placeItem == Structure_Slab1) || (placeItem == Structure_Slab4)) && pTile->isConcrete());
                                }
                            }
                            image = tileValid ? validPlace : invalidPlace;

                            SDL_Rect drawLocation = calcDrawingRect(image, screenborder->world2screenX(i*TILESIZE), screenborder->world2screenY(j*TILESIZE));
                            SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
                        }
                    }
                }
            }
        }
    }

    if(currentCursorMode == CursorMode_CityRoad) {
        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY)) {
            int xPos = screenborder->screen2MapX(drawnMouseX);
            int yPos = screenborder->screen2MapY(drawnMouseY);

            SDL_Texture* validPlace = nullptr;
            SDL_Texture* invalidPlace = nullptr;

            switch(currentZoomlevel) {
                case 0:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel0);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel0);
                    break;
                case 1:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel1);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel1);
                    break;
                case 2:
                default:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel2);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel2);
                    break;
            }

            if(currentGameMap->tileExists(xPos, yPos)) {
                Tile* pTile = currentGameMap->getTile(xPos, yPos);
                const bool tileValid = DuneCity::canPlaceRoad(makeCityTilePlacementState(*pTile));
                SDL_Texture* image = tileValid ? validPlace : invalidPlace;
                SDL_Rect drawLocation = calcDrawingRect(image,
                    screenborder->world2screenX(xPos * TILESIZE),
                    screenborder->world2screenY(yPos * TILESIZE));
                SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
            }
        }
    }

///////////draw game selection rectangle
    if(selectionMode) {

        int finalMouseX = drawnMouseX;
        int finalMouseY = drawnMouseY;
        if(finalMouseX >= sideBarPos.x) {
            //this keeps the box on the map, and not over game bar
            finalMouseX = sideBarPos.x-1;
        }

        if(finalMouseY < topBarPos.y+topBarPos.h) {
            finalMouseY = topBarPos.x+topBarPos.h;
        }

        // draw the mouse selection rectangle
        renderDrawRect( renderer,
                        screenborder->world2screenX(selectionRect.x),
                        screenborder->world2screenY(selectionRect.y),
                        finalMouseX,
                        finalMouseY,
                        COLOR_WHITE);
    }



///////////draw action indicator

    if((indicatorFrame != NONE_ID) && (screenborder->isInsideScreen(indicatorPosition, Coord(TILESIZE,TILESIZE)) == true)) {
        SDL_Texture* pUIIndicator = pGFXManager->getUIGraphic(UI_Indicator);
        SDL_Rect source = calcSpriteSourceRect(pUIIndicator, indicatorFrame, 3);
        SDL_Rect drawLocation = calcSpriteDrawingRect(  pUIIndicator,
                                                        screenborder->world2screenX(indicatorPosition.x),
                                                        screenborder->world2screenY(indicatorPosition.y),
                                                        3, 1,
                                                        HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, pUIIndicator, &source, &drawLocation);
    }


///////////draw game bar
    pInterface->draw(Point(0,0));
    pInterface->drawOverlay(Point(0,0));
    drawCityPlacementHint();

    // draw chat message currently typed
    if(chatMode) {
        sdl2::texture_ptr pChatTexture = pFontManager->createTextureWithText("Chat: " + typingChatMessage + (((SDL_GetTicks() / 150) % 2 == 0) ? "_" : ""), COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcDrawingRect(pChatTexture.get(), 20, getRendererHeight() - 40);
        SDL_RenderCopy(renderer, pChatTexture.get(), nullptr, &drawLocation);
    }

    if(bShowFPS) {
        std::string strFPS = fmt::sprintf("fps: %.1f ", 1000.0f/averageFrameTime);

        sdl2::texture_ptr pFPSTexture = pFontManager->createTextureWithText(strFPS, COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcDrawingRect(pFPSTexture.get(),sideBarPos.x - strFPS.length()*8, 60);
        SDL_RenderCopy(renderer, pFPSTexture.get(), nullptr, &drawLocation);
    }

    if(bShowTime) {
        int seconds = getGameTime() / 1000;
        std::string strTime = fmt::sprintf(" %.2d:%.2d:%.2d", seconds / 3600, (seconds % 3600)/60, (seconds % 60) );

        sdl2::texture_ptr pTimeTexture = pFontManager->createTextureWithText(strTime, COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcAlignedDrawingRect(pTimeTexture.get(), HAlign::Left, VAlign::Bottom);
        drawLocation.y++;
        SDL_RenderCopy(renderer, pTimeTexture.get(), nullptr, &drawLocation);
    }

    if(finished) {
        std::string message;

        if(won) {
            message = _("You Have Completed Your Mission.");
        } else {
            message = _("You Have Failed Your Mission.");
        }

        sdl2::texture_ptr pFinishMessageTexture = pFontManager->createTextureWithText(message.c_str(), COLOR_WHITE, 28);
        SDL_Rect drawLocation = calcDrawingRect(pFinishMessageTexture.get(), sideBarPos.x/2, topBarPos.h + (getRendererHeight()-topBarPos.h)/2, HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, pFinishMessageTexture.get(), nullptr, &drawLocation);
    }

    if(pWaitingForOtherPlayers != nullptr) {
        pWaitingForOtherPlayers->draw();
    }

    if(pInGameMenu != nullptr) {
        pInGameMenu->draw();
    } else if(pInGameMentat != nullptr) {
        pInGameMentat->draw();
    }

    // Update cursor
    updateCursor();
}


void Game::doInput()
{
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        // check for a key press

        // first of all update mouse
        if(event.type == SDL_MOUSEMOTION) {
            SDL_MouseMotionEvent* mouse = &event.motion;
            drawnMouseX = std::max(0, std::min(mouse->x, settings.video.width-1));
            drawnMouseY = std::max(0, std::min(mouse->y, settings.video.height-1));

            static Uint32 lastCursorLog = 0;
            // Cursor debug logging disabled
            /*const Uint32 now = SDL_GetTicks();
            if(now - lastCursorLog >= 500) {
                bool insideMap = (screenborder != nullptr) && screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY);
                int mapX = insideMap ? screenborder->screen2MapX(drawnMouseX) : -1;
                int mapY = insideMap ? screenborder->screen2MapY(drawnMouseY) : -1;
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                               "CursorDebug: raw=(%d,%d) drawn=(%d,%d) mapInside=%s map=(%d,%d) cursorMode=%d hardware=%s",
                               mouse->x,
                               mouse->y,
                               drawnMouseX,
                               drawnMouseY,
                               insideMap ? "yes" : "no",
                               mapX,
                               mapY,
                               static_cast<int>(currentCursorMode),
                               cursorManager.isInitialized() ? "yes" : "no");
                lastCursorLog = now;
            }*/
        }

        if(pInGameMenu != nullptr) {
            pInGameMenu->handleInput(event);

            if(bMenu == false) {
                pInGameMenu.reset();
            }

        } else if(pInGameMentat != nullptr) {
            pInGameMentat->doInput(event);

            if(bMenu == false) {
                pInGameMentat.reset();
            }

        } else if(pWaitingForOtherPlayers != nullptr) {
            pWaitingForOtherPlayers->handleInput(event);

            if(bMenu == false) {
                pWaitingForOtherPlayers.reset();
            }

        } else {
            /* Look for a keypress */
            switch (event.type) {

                case SDL_KEYDOWN: {
                    if(chatMode) {
                        handleChatInput(event.key);
                    } else {
                        handleKeyInput(event.key);
                    }
                } break;

                case SDL_TEXTINPUT: {
                    if(chatMode) {
                        std::string newText = event.text.text;
                        if(utf8Length(typingChatMessage) + utf8Length(newText) <= 60) {
                            typingChatMessage += newText;
                        }
                    }
                } break;

                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y != 0) {
                        pInterface->handleMouseWheel(drawnMouseX,drawnMouseY,(event.wheel.y > 0));
                    }
                } break;

                case SDL_MOUSEBUTTONDOWN: {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {
                            pInterface->handleMouseLeft(mouse->x, mouse->y, true);
                        } break;

                        case SDL_BUTTON_RIGHT: {
                            pInterface->handleMouseRight(mouse->x, mouse->y, true);
                        } break;
                    }

                    switch(mouse->button) {

                        case SDL_BUTTON_LEFT: {

                            switch(currentCursorMode) {

                                case CursorMode_Placing: {
                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handlePlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }
                                } break;

                                case CursorMode_Attack: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsAttackClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Move: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsMoveClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CarryallDrop: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsRequestCarryallDropClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Capture: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsCaptureClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CityZone: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleCityZonePlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CityRoad: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleCityRoadPlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Normal:
                                default: {

                                    if (mouse->x < sideBarPos.x && mouse->y >= topBarPos.h) {
                                        // it isn't on the gamebar

                                        if(!selectionMode) {
                                            // if we have started the selection rectangle
                                            // the starting point of the selection rectangele
                                            selectionRect.x = screenborder->screen2worldX(mouse->x);
                                            selectionRect.y = screenborder->screen2worldY(mouse->y);
                                        }
                                        selectionMode = true;

                                    }
                                } break;
                            }
                        } break;    //end of SDL_BUTTON_LEFT

                        case SDL_BUTTON_RIGHT: {
                            //if the right mouse button is pressed

                            if(currentCursorMode != CursorMode_Normal) {
                                //cancel special cursor mode
                                setCursorMode(CursorMode_Normal);
                            } else if((!selectedList.empty()
                                            && (((objectManager.getObject(*selectedList.begin()))->getOwner() == pLocalHouse))
                                            && (((objectManager.getObject(*selectedList.begin()))->isRespondable())) ) )
                            {
                                //if user has a controlable unit selected

                                if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                    if(handleSelectedObjectsActionClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y))) {
                                        indicatorFrame = 0;
                                        indicatorPosition.x = screenborder->screen2worldX(mouse->x);
                                        indicatorPosition.y = screenborder->screen2worldY(mouse->y);
                                    }
                                }
                            }
                        } break;    //end of SDL_BUTTON_RIGHT
                    }
                } break;

                case SDL_MOUSEMOTION: {
                    SDL_MouseMotionEvent* mouse = &event.motion;

                    pInterface->handleMouseMovement(mouse->x,mouse->y);
                } break;

                case SDL_MOUSEBUTTONUP: {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {
                            pInterface->handleMouseLeft(mouse->x, mouse->y, false);
                        } break;

                        case SDL_BUTTON_RIGHT: {
                            pInterface->handleMouseRight(mouse->x, mouse->y, false);
                        } break;
                    }

                    if(selectionMode && (mouse->button == SDL_BUTTON_LEFT)) {
                        //this keeps the box on the map, and not over game bar
                        int finalMouseX = mouse->x;
                        int finalMouseY = mouse->y;

                        if(finalMouseX >= sideBarPos.x) {
                            finalMouseX = sideBarPos.x-1;
                        }

                        if(finalMouseY < topBarPos.y+topBarPos.h) {
                            finalMouseY = topBarPos.x+topBarPos.h;
                        }

                        int rectFinishX = screenborder->screen2MapX(finalMouseX);
                        if(rectFinishX > (currentGameMap->getSizeX()-1)) {
                            rectFinishX = currentGameMap->getSizeX()-1;
                        }

                        int rectFinishY = screenborder->screen2MapY(finalMouseY);

                        // convert start also to map coordinates
                        int rectStartX = selectionRect.x/TILESIZE;
                        int rectStartY = selectionRect.y/TILESIZE;

                        currentGameMap->selectObjects(  pLocalHouse,
                                                        rectStartX, rectStartY, rectFinishX, rectFinishY,
                                                        screenborder->screen2worldX(finalMouseX),
                                                        screenborder->screen2worldY(finalMouseY),
                                                        SDL_GetModState() & KMOD_SHIFT);

                        if(selectedList.size() == 1) {
                            ObjectBase* pObject = objectManager.getObject( *selectedList.begin());
                            if(pObject != nullptr && pObject->getOwner() == pLocalHouse && pObject->getItemID() == Unit_Harvester) {
                                Harvester* pHarvester = static_cast<Harvester*>(pObject);

                                std::string harvesterMessage = _("@DUNE.ENG|226#Harvester");

                                int percent = lround(100 * pHarvester->getAmountOfSpice() / HARVESTERMAXSPICE);
                                if(percent > 0) {
                                    if(pHarvester->isAwaitingPickup()) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|124#full and awaiting pickup"), percent);
                                    } else if(pHarvester->isReturning()) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|123#full and returning"), percent);
                                    } else if(pHarvester->isHarvesting()) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|122#full and harvesting"), percent);
                                    } else {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|121#full"), percent);
                                    }

                                } else {
                                    if(pHarvester->isAwaitingPickup()) {
                                        harvesterMessage += _("@DUNE.ENG|128#empty and awaiting pickup");
                                    } else if(pHarvester->isReturning()) {
                                        harvesterMessage += _("@DUNE.ENG|127#empty and returning");
                                    } else if(pHarvester->isHarvesting()) {
                                        harvesterMessage += _("@DUNE.ENG|126#empty and harvesting");
                                    } else {
                                        harvesterMessage += _("@DUNE.ENG|125#empty");
                                    }
                                }

                                if(!pInterface->newsTickerHasMessage()) {
                                    pInterface->addToNewsTicker(harvesterMessage);
                                }
                            }
                        }
                    }

                    selectionMode = false;

                } break;

                case (SDL_QUIT): {
                    bQuitGame = true;
                } break;

                default:
                    break;
            }
        }
    }

    if((pInGameMenu == nullptr) && (pInGameMentat == nullptr) && (pWaitingForOtherPlayers == nullptr) && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS)) {

        const Uint8 *keystate = SDL_GetKeyboardState(nullptr);
        scrollDownMode =  (drawnMouseY >= getRendererHeight()-1-SCROLLBORDER) || keystate[SDL_SCANCODE_DOWN];
        scrollLeftMode = (drawnMouseX <= SCROLLBORDER) || keystate[SDL_SCANCODE_LEFT];
        scrollRightMode = (drawnMouseX >= getRendererWidth()-1-SCROLLBORDER) || keystate[SDL_SCANCODE_RIGHT];
        scrollUpMode = (drawnMouseY <= SCROLLBORDER) || keystate[SDL_SCANCODE_UP];

        if(scrollLeftMode && scrollRightMode) {
            // do nothing
        } else if(scrollLeftMode) {
            scrollLeftMode = screenborder->scrollLeft();
        } else if(scrollRightMode) {
            scrollRightMode = screenborder->scrollRight();
        }

        if(scrollDownMode && scrollUpMode) {
            // do nothing
        } else if(scrollDownMode) {
            scrollDownMode = screenborder->scrollDown();
        } else if(scrollUpMode) {
            scrollUpMode = screenborder->scrollUp();
        }
    } else {
        scrollDownMode = false;
        scrollLeftMode = false;
        scrollRightMode = false;
        scrollUpMode = false;
    }
}


void Game::updateCursor()
{
    if (cursorManager.isInitialized()) {
        cursorManager.setCursorMode(currentCursorMode);
    }
}

void Game::setCursorMode(int mode)
{
    if (cursorManager.canSetCursorMode(mode, std::vector<Uint32>(selectedList.begin(), selectedList.end()))) {
        currentCursorMode = mode;
        cursorManager.setCursorMode(mode);
    }
}

void Game::setupView()
{
    int i = 0;
    int j = 0;
    int count = 0;

    //setup start location/view
    i = j = count = 0;

    for(const UnitBase* pUnit : unitList) {
        if((pUnit->getOwner() == pLocalHouse) && (pUnit->getItemID() != Unit_Sandworm)) {
            i += pUnit->getX();
            j += pUnit->getY();
            count++;
        }
    }

    for(const StructureBase* pStructure : structureList) {
        if(pStructure->getOwner() == pLocalHouse) {
            i += pStructure->getX();
            j += pStructure->getY();
            count++;
        }
    }

    if(count == 0) {
        i = currentGameMap->getSizeX()*TILESIZE/2-1;
        j = currentGameMap->getSizeY()*TILESIZE/2-1;
    } else {
        i = i*TILESIZE/count;
        j = j*TILESIZE/count;
    }

    screenborder->setNewScreenCenter(Coord(i,j));
}


void Game::runMainLoop() {
    SDL_Log("Starting game...");
    initializeGameLoop();
    
    // Update Discord Rich Presence for in-game status
    std::string houseName = pLocalHouse ? getHouseNameByNumber(static_cast<HOUSETYPE>(pLocalHouse->getHouseID())) : "Unknown";
    std::string mapName = gameInitSettings.getFilename();
    // Extract just the map name from the path
    size_t lastSlash = mapName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        mapName = mapName.substr(lastSlash + 1);
    }
    size_t lastDot = mapName.find_last_of('.');
    if (lastDot != std::string::npos) {
        mapName = mapName.substr(0, lastDot);
    }
    
    if (gameInitSettings.getGameType() == GameType::CustomMultiplayer || 
        gameInitSettings.getGameType() == GameType::LoadMultiplayer) {
        // Count human players from game init settings (not alive houses which can change during game)
        int humanPlayerCount = 0;
        int totalPlayerSlots = 0;
        for (const auto& houseInfo : gameInitSettings.getHouseInfoList()) {
            for (const auto& playerInfo : houseInfo.playerInfoList) {
                totalPlayerSlots++;
                if (playerInfo.playerClass == HUMANPLAYERCLASS) {
                    humanPlayerCount++;
                }
            }
        }
        // Use human count for current players, total slots for max party size
        DiscordManager::instance().setMultiplayerGame(houseName, mapName, humanPlayerCount, totalPlayerSlots);
    } else {
        bool isCampaign = (gameInitSettings.getGameType() == GameType::Campaign);
        DiscordManager::instance().setInGame(houseName, mapName, isCampaign);
    }

    int frameStart = SDL_GetTicks();
    int frameTime = 0;

    do {
        // Start timing this rendered frame
        const Uint64 frameStartPerf = SDL_GetPerformanceCounter();
        frameTiming.gameCyclesThisFrame = 0;
        frameTiming.totalPathsProcessedThisFrame = 0;
        
        // Reset per-frame accumulators
        frameTiming.aiMsThisFrame = 0.0;
        frameTiming.unitsMsThisFrame = 0.0;
        frameTiming.structuresMsThisFrame = 0.0;
        frameTiming.pathfindingMsThisFrame = 0.0;
        frameTiming.renderingMsThisFrame = 0.0;
        frameTiming.networkWaitMsThisFrame = 0.0;
        frameTiming.turretScanMsThisFrame = 0.0;
        frameTiming.turretScansThisFrame = 0;
        frameTiming.unitTargetingMsThisFrame = 0.0;
        frameTiming.unitNavigateMsThisFrame = 0.0;
        frameTiming.unitMoveMsThisFrame = 0.0;
        frameTiming.unitTurnMsThisFrame = 0.0;
        frameTiming.unitVisibilityMsThisFrame = 0.0;
        
        // MULTIPLAYER FIX (Issue #1): Removed time-based pathfinding budget
        // Token budget is now the only gate (deterministic)
        
        // Update Discord Rich Presence callbacks (once per second to avoid overhead)
        static Uint32 lastDiscordUpdate = 0;
        Uint32 discordNow = SDL_GetTicks();
        if (discordNow - lastDiscordUpdate >= 1000) {
            DiscordManager::instance().update();
            lastDiscordUpdate = discordNow;
        }
        
        renderFrame();

        const int frameEnd = SDL_GetTicks();
        const int actualFrameTime = frameEnd - frameStart;  // Actual time for this frame
        frameTime += actualFrameTime;
        
        // CAP frameTime to prevent excessive catch-up bursts during network stalls
        // Allow up to 3 cycles worth of catch-up per frame for smoother gameplay
        // This trades off "real-time accuracy" for "smooth gameplay feel"
        const int maxFrameTime = getGameSpeed() * 3;
        if (frameTime > maxFrameTime) {
            frameTime = maxFrameTime;
        }
        
        frameStart = frameEnd;  // Reset for next frame's game logic timing

        if(bShowFPS) {
            // FPS display uses actual frame time
            averageFrameTime = 0.99f * averageFrameTime + 0.01f * actualFrameTime;
        }

        // MULTIPLAYER FIX (Issue #9): Removed duplicate finished level check
        // This is already handled in updateGameState() with cycle-based timing

        if(takePeriodicalScreenshots && ((gameCycleCount % (MILLI2CYCLES(10*1000))) == 0)) {
            takeScreenshot();
        }

        if(bReplay && !bPause) {
            skipToGameCycle = gameCycleCount + (10*1000)/GAMESPEED_DEFAULT;
        }

        // DIAGNOSTIC: Track loop iterations
        int loopIterations = 0;
        int cyclesExecuted = 0;
        
        // PHASE 1.3: CYCLE GUARDRAIL - Prevent renderer starvation
        static constexpr int kMaxCyclesPerFrame = 10;
        
        while(((frameTime > getGameSpeed()) || (!finished && (gameCycleCount < skipToGameCycle))) 
              && cyclesExecuted < kMaxCyclesPerFrame) {
            loopIterations++;
            
            Uint64 networkWaitStart = SDL_GetPerformanceCounter();
            bool bWaitForNetwork = false;
            if(pNetworkManager != nullptr) {
                bWaitForNetwork = handleNetworkUpdates();
            }

            processInput();

            if(pInGameMentat != nullptr) {
                pInGameMentat->update();
            }

            if(pWaitingForOtherPlayers != nullptr) {
                pWaitingForOtherPlayers->update();
            }

            cmdManager.update();

            if(!bWaitForNetwork && !bPause) {
                // Time the core simulation step for CPU load detection
                const Uint64 simStart = SDL_GetPerformanceCounter();
                updateGameState();
                const Uint64 simEnd = SDL_GetPerformanceCounter();
                const double simMs = getElapsedMs(simStart, simEnd);
                
                // Update exponential moving average (0.9/0.1 split ≈ 10-tick average)
                frameTiming.simMsAvg = 0.9 * frameTiming.simMsAvg + 0.1 * simMs;
                
                // Update lagging flag with hysteresis
                static constexpr double kSimThresholdHigh = 12.0;  // ms
                static constexpr double kSimThresholdLow = 10.0;   // ms
                
                if (frameTiming.simMsAvg > kSimThresholdHigh) {
                    frameTiming.simulationLagging = true;
                } else if (frameTiming.simMsAvg < kSimThresholdLow) {
                    frameTiming.simulationLagging = false;
                }
                
                frameTiming.gameCyclesThisFrame++;
                cyclesExecuted++;
                
                // Only decrement frameTime when we actually processed a cycle
                if(gameCycleCount <= skipToGameCycle) {
                    frameTime = 0;
                } else {
                    frameTime -= getGameSpeed();
                }
            } else if(bWaitForNetwork || bPause) {
                // When waiting for network or paused, measure the wait time
                if(bWaitForNetwork) {
                    Uint64 networkWaitEnd = SDL_GetPerformanceCounter();
                    const double networkWaitMs = getElapsedMs(networkWaitStart, networkWaitEnd);
                    frameTiming.networkWaitMs += networkWaitMs;
                    frameTiming.networkWaitMsThisFrame += networkWaitMs;
                    if(networkWaitMs > frameTiming.maxNetworkWaitMs) frameTiming.maxNetworkWaitMs = networkWaitMs;
                    
                    // Don't reset frameTime - let the game catch up naturally.
                    // The guardrail (10 cycles/frame max) prevents excessive catch-up.
                }
                else if (bPause){
                    // Pause in single player shouldn't jump after resuming
                    frameTime = 0;
                }
                // Break out of loop to avoid spinning - we'll try again next frame
                // Also add a small delay to avoid burning CPU
                SDL_Delay(1);
                break;
            }
        }
        
        // PHASE 1.3: DETECT GUARDRAIL TRIPS & REQUEST LOWER BUDGET
        if(cyclesExecuted >= kMaxCyclesPerFrame) {
            cycleGuardrailTrips++;
            
            // Log guardrail trip to performance file only at key thresholds
            if(cycleGuardrailTrips % 100 == 0) {
                logPerformance("[GUARDRAIL] Cycle %d: Hit limit %d times (10 cycles/frame) - queue=%zu, budget=%zu",
                        gameCycleCount, cycleGuardrailTrips, pathRequestQueue.size(), negotiatedBudget);
            }
            
            // Log to console periodically
            if((gameCycleCount % MILLI2CYCLES(5000)) == 0) {
                SDL_Log("[Guardrail] Hit cycle limit (%d cycles/frame), queue=%zu, trips=%d",
                        kMaxCyclesPerFrame, pathRequestQueue.size(), cycleGuardrailTrips);
            }
            
            // CHANGED: Only reduce budget after SUSTAINED poor performance
            // 300 guardrail trips = ~300 frames = ~5 seconds at 60 FPS
            // This prevents reacting to temporary spikes
            if(cycleGuardrailTrips >= 300) {
                SDL_Log("[PathBudget] Sustained poor performance detected (%d guardrail trips over ~300 frames)", cycleGuardrailTrips);
                requestLowerBudget();
                cycleGuardrailTrips = 0;  // Reset counter
            }
        } else if(cyclesExecuted < 5) {
            // Running smoothly - aggressively reset counter
            // Decay faster to forgive temporary spikes
            if(cycleGuardrailTrips > 0) {
                cycleGuardrailTrips -= 3;  // Decrease by 3 per smooth frame
                if(cycleGuardrailTrips < 0) {
                    cycleGuardrailTrips = 0;
                }
            }
        }
        
        // DIAGNOSTIC: Only log severe issues (removed frequent logging)
        // Severe frame issues are now captured in the 30-second [PathInstrumentation] report
        if(loopIterations > 20 || cyclesExecuted > 15) {
            logPerformance("[DIAGNOSTIC] Severe frame issue: %d loop iterations, %d cycles executed, gameCycle=%d", 
                loopIterations, cyclesExecuted, gameCycleCount);
        }
        
        // PERIODIC STATUS SNAPSHOT: Every 60 seconds of game time (reduced from 15s)
        if((gameCycleCount % MILLI2CYCLES(60000)) == 0 && gameCycleCount > 0) {
            const double avgFps = frameTiming.frameCount > 0 ? 
                (frameTiming.frameCount * 1000.0 / frameTiming.totalMs) : 0.0;
            const double tokensPerCycle = frameTiming.totalGameCycles > 0 ?
                static_cast<double>(frameTiming.totalPathTokens) / frameTiming.totalGameCycles : 0.0;
            
            const int mapSize = (currentGameMap != nullptr) ? currentGameMap->getSizeX() : 0;
            logPerformance("[STATUS] Cycle %d (%.1f min) - Map: %dx%d | FPS: %.1f | Budget: %zu | Queue: %zu | Tokens/cycle: %.0f",
                    gameCycleCount,
                    gameCycleCount / (60.0 * MILLI2CYCLES(1000)),
                    mapSize, mapSize,
                    avgFps,
                    negotiatedBudget,
                    pathRequestQueue.size(),
                    tokensPerCycle);
        }

        musicPlayer->musicCheck();

        // End timing this rendered frame
        const Uint64 frameEndPerf = SDL_GetPerformanceCounter();
        const double thisFrameMs = getElapsedMs(frameStartPerf, frameEndPerf);
        frameTiming.totalMs += thisFrameMs;
        frameTiming.totalGameCycles += frameTiming.gameCyclesThisFrame;
        frameTiming.totalTurretScans += frameTiming.turretScansThisFrame;
        frameTiming.turretScanMs += frameTiming.turretScanMsThisFrame;
        frameTiming.frameCount++;
        
        // Track max values
        if(thisFrameMs > frameTiming.maxTotalMs) frameTiming.maxTotalMs = thisFrameMs;
        if(frameTiming.gameCyclesThisFrame > frameTiming.maxGameCyclesPerFrame) {
            frameTiming.maxGameCyclesPerFrame = frameTiming.gameCyclesThisFrame;
        }
        if(frameTiming.totalPathsProcessedThisFrame > frameTiming.maxPathsPerFrame) {
            frameTiming.maxPathsPerFrame = frameTiming.totalPathsProcessedThisFrame;
        }
        
        // Track min values (per frame)
        if(frameTiming.gameCyclesThisFrame < frameTiming.minGameCyclesPerFrame) {
            frameTiming.minGameCyclesPerFrame = frameTiming.gameCyclesThisFrame;
        }
        if(frameTiming.aiMsThisFrame < frameTiming.minAiMs) frameTiming.minAiMs = frameTiming.aiMsThisFrame;
        if(frameTiming.unitsMsThisFrame < frameTiming.minUnitsMs) frameTiming.minUnitsMs = frameTiming.unitsMsThisFrame;
        if(frameTiming.structuresMsThisFrame < frameTiming.minStructuresMs) frameTiming.minStructuresMs = frameTiming.structuresMsThisFrame;
        if(frameTiming.pathfindingMsThisFrame < frameTiming.minPathfindingMs) frameTiming.minPathfindingMs = frameTiming.pathfindingMsThisFrame;
        if(frameTiming.renderingMsThisFrame < frameTiming.minRenderingMs) frameTiming.minRenderingMs = frameTiming.renderingMsThisFrame;
        if(frameTiming.networkWaitMsThisFrame < frameTiming.minNetworkWaitMs) frameTiming.minNetworkWaitMs = frameTiming.networkWaitMsThisFrame;
        
        // Track max values (per frame)
        if(frameTiming.aiMsThisFrame > frameTiming.maxAiMs) frameTiming.maxAiMs = frameTiming.aiMsThisFrame;
        if(frameTiming.unitsMsThisFrame > frameTiming.maxUnitsMs) frameTiming.maxUnitsMs = frameTiming.unitsMsThisFrame;
        if(frameTiming.structuresMsThisFrame > frameTiming.maxStructuresMs) frameTiming.maxStructuresMs = frameTiming.structuresMsThisFrame;
        if(frameTiming.pathfindingMsThisFrame > frameTiming.maxPathfindingMs) frameTiming.maxPathfindingMs = frameTiming.pathfindingMsThisFrame;
        if(frameTiming.renderingMsThisFrame > frameTiming.maxRenderingMs) frameTiming.maxRenderingMs = frameTiming.renderingMsThisFrame;
        if(frameTiming.networkWaitMsThisFrame > frameTiming.maxNetworkWaitMs) frameTiming.maxNetworkWaitMs = frameTiming.networkWaitMsThisFrame;
        if(frameTiming.turretScanMsThisFrame > frameTiming.maxTurretScanMs) frameTiming.maxTurretScanMs = frameTiming.turretScanMsThisFrame;
        if(frameTiming.turretScanMsThisFrame < frameTiming.minTurretScanMs && frameTiming.turretScansThisFrame > 0) frameTiming.minTurretScanMs = frameTiming.turretScanMsThisFrame;
        if(frameTiming.turretScansThisFrame > frameTiming.maxTurretScansPerFrame) frameTiming.maxTurretScansPerFrame = frameTiming.turretScansThisFrame;
        
        // Log every 2 minutes as backup (reduced from 30s; main logs happen on budget changes)
        const Uint32 now = SDL_GetTicks();
        if(now - lastTimingLogMs >= 120000) {
            logFrameTiming();
            lastTimingLogMs = now;
        }

    } while (!bQuitGame && !finishedLevel);
}

void Game::initializeGameLoop() {
    if(pInterface == nullptr) {
        pInterface = std::make_unique<GameInterface>();
        if(gameState == GameState::Loading) {
            pInterface->getRadarView().setRadarMode(pLocalHouse->hasRadarOn());
        } else if(pLocalHouse->hasRadarOn()) {
            pInterface->getRadarView().switchRadarMode(true);
        }
    }

    // Configure hardware-accelerated rendering with pixel-perfect scaling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // Use nearest-neighbor scaling for pixel-perfect look
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");       // Enable render batching for performance
    // Note: Renderer driver is set in setVideoMode(), no need to override here
    
    // Enable hardware acceleration
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    if(screenTexture != nullptr) {
        SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
    }

    gameState = GameState::Running;
    finishedLevel = false;
    bShowTime = winFlags & WINLOSEFLAGS_TIMEOUT;

    // Initialize DuneCity simulation
    if (citySimEnabled_ && currentGameMap != nullptr && !citySimulation_) {
        citySimulation_ = std::make_unique<DuneCity::CitySimulation>();
        citySimulation_->init(currentGameMap->getSizeX(), currentGameMap->getSizeY());
        // City Effects is implicitly on whenever the city sim itself is
        // on — the GameOptions.ini toggle is kept for explicit overrides
        // but should never silently leave a city-mode game with the
        // pollution/land-value/crime/growth pipeline disabled.
        citySimulation_->setCityEffectsEnabled(true);
    }

    // Check if a player has lost
    for(int j = 0; j < NUM_HOUSES; j++) {
        if(house[j] != nullptr && !house[j]->isAlive()) {
            house[j]->lose(true);
        }
    }

    initializeReplay();
    initializeNetwork();
    musicPlayer->changeMusic(MUSIC_PEACE);
}

void Game::renderFrame() {
    const Uint64 renderStart = SDL_GetPerformanceCounter();
    
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    drawScreen();
    
    // Copy to main screen and present in one step
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderCopy(renderer, screenTexture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    
    const Uint64 renderEnd = SDL_GetPerformanceCounter();
    const double renderMs = getElapsedMs(renderStart, renderEnd);
    frameTiming.renderingMs += renderMs;
    frameTiming.renderingMsThisFrame += renderMs;
}

void Game::processInput() {
    // Process all input through doInput() which handles both menu and game input
    doInput();
    
    // Handle menu state changes after input processing
    if(pInGameMenu != nullptr) {
        if(bMenu == false) {
            pInGameMenu.reset();
            resumeGame();
        }
        return;
    } else if(pInGameMentat != nullptr) {
        if(bMenu == false) {
            pInGameMentat.reset();
            resumeGame();
        }
        return;
    } else if(pWaitingForOtherPlayers != nullptr) {
        if(bMenu == false) {
            pWaitingForOtherPlayers.reset();
        }
        return;
    }

    // Only update interface and network if no menu is active
    pInterface->updateObjectInterface();

    if(pNetworkManager != nullptr && bSelectionChanged) {
        pNetworkManager->sendSelectedList(selectedList);
        bSelectionChanged = false;
    }
}

void Game::updateGameState() {
    if(bPause) {
        return;
    }

    pInterface->getRadarView().update();
    cmdManager.executeCommands(gameCycleCount);

    // Time AI/house updates (this is where QuantBot and other AI runs)
    Uint64 aiStart = SDL_GetPerformanceCounter();
    for(int i = 0; i < NUM_HOUSES; i++) {
        if(house[i] != nullptr) {
            house[i]->update();
        }
    }
    Uint64 aiEnd = SDL_GetPerformanceCounter();
    const double aiMs = getElapsedMs(aiStart, aiEnd);
    frameTiming.aiMs += aiMs;
    frameTiming.aiMsThisFrame += aiMs;

    screenborder->update();
    triggerManager.trigger(gameCycleCount);
    processObjects();

    // DuneCity: advance one phase of the city simulation
    if (citySimEnabled_ && citySimulation_) {
        citySimulation_->advancePhase(gameCycleCount);

        // Power shortage warning: notify player when zones lose power
        if (citySimulation_->isPowerShortageJustStarted()) {
            int32_t unpowered = citySimulation_->getUnpoweredZoneCount();
            currentGame->addToNewsTicker(fmt::sprintf(_("WARNING: Power shortage! %d zones unpowered"), unpowered));
        }

        if ((winFlags & WINLOSEFLAGS_ECONOMIC) && !finished) {
            int32_t threshold = citySimulation_->getEconomicVictoryThreshold();
            if (threshold > 0 && citySimulation_->getTotalPop() >= threshold) {
                setGameWon();
            }
        }
    }

    if((indicatorFrame != NONE_ID) && (--indicatorTimer <= 0)) {
        indicatorTimer = indicatorTime;
        if(++indicatorFrame > 2) {
            indicatorFrame = NONE_ID;
        }
    }

    gameCycleCount++;
    
    // Apply any pending budget changes (deterministic, cycle-based)
    applyPendingBudgetChanges();
    
    // PHASE 1: Cycle-based budget adjustment (deterministic, every 375 cycles ~7.5s at 50Hz)
    checkBudgetAdjustment();
    
    // MULTIPLAYER FIX (Issue #8): Cycle-based combat stats dump (deterministic)
    // Combat stats logging disabled (broken anti-air analysis)
    /*if(combatStats.lastDumpCycle == 0) {
        combatStats.lastDumpCycle = gameCycleCount;
    } else if((gameCycleCount - combatStats.lastDumpCycle) >= MILLI2CYCLES(30000)) {
        dumpCombatStats();
        combatStats.lastDumpCycle = gameCycleCount;
    }*/
    
    // MULTIPLAYER FIX (Issue #9): Cycle-based finished level timer (deterministic)
    if(finished && (gameCycleCount - finishedLevelCycle > MILLI2CYCLES(END_WAIT_TIME))) {
        finishedLevel = true;
    }
    
    if(takePeriodicalScreenshots && ((gameCycleCount % (MILLI2CYCLES(10*1000))) == 0)) {
        takeScreenshot();
    }
    
    musicPlayer->musicCheck();
}

void Game::initializeReplay() {
    if(bReplay) {
        cmdManager.setReadOnly(true);
    } else {
        char tmp[FILENAME_MAX];
        fnkdat("replay/auto.rpl", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
        const std::string replayname(tmp);
        
        auto pStream = std::make_unique<OFileStream>();
        if(pStream->open(replayname)) {
            pStream->writeString(getLocalPlayerName());
            gameInitSettings.save(*pStream);
            cmdManager.save(*pStream);
            pStream->flush();
            cmdManager.setStream(std::move(pStream));
        } else {
            quitGame();
        }
    }
}

void Game::initializeNetwork() {
    if(pNetworkManager != nullptr) {
        pNetworkManager->setOnReceiveChatMessage(
            std::bind(&ChatManager::addChatMessage, &(pInterface->getChatManager()), 
            std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnReceiveCommandList(
            std::bind(&CommandManager::addCommandList, &cmdManager, 
            std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnReceiveSelectionList(
            std::bind(&Game::onReceiveSelectionList, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pNetworkManager->setOnPeerDisconnected(
            std::bind(&Game::onPeerDisconnected, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        
        // Multiplayer budget negotiation callbacks
        pNetworkManager->setOnReceiveClientStats(
            std::bind(&Game::handleClientStats, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, 
            std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
        pNetworkManager->setOnReceiveSetPathBudget(
            std::bind(&Game::handleSetPathBudget, this,
            std::placeholders::_1, std::placeholders::_2));
        
        // Network buffer: RTT-based + 5 cycles padding
        // LAN games: Use RTT-based (typically 5 cycles = 100ms)
        // Internet games: Use minimum of 10 cycles (200ms) to handle jitter
        const int rttBuffer = MILLI2CYCLES(pNetworkManager->getMaxPeerRoundTripTime()) + 5;
        const int minInternetBuffer = 10;  // 200ms minimum for internet
        const bool isLAN = pNetworkManager->isLANServer();
        const int networkBuffer = isLAN ? rttBuffer : std::max(rttBuffer, minInternetBuffer);
        cmdManager.setNetworkCycleBuffer(networkBuffer);
        SDL_Log("Network buffer set to %d cycles (RTT: %dms, %s)", 
                networkBuffer, pNetworkManager->getMaxPeerRoundTripTime(),
                isLAN ? "LAN" : "Internet");
    }
}


void Game::resumeGame()
{
    bMenu = false;
    bPause = false;
    
    // Notify other players in multiplayer that we resumed
    if(pNetworkManager != nullptr) {
        Player* pLocalPlayer = getPlayerByName(localPlayerName);
        if(pLocalPlayer != nullptr) {
            cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLAYER_RESUME));
            
            // Remove ourselves from paused players set
            pausedPlayers.erase(pLocalPlayer->getPlayerID());
        }
    }
}

void Game::pauseGame() {
    bPause = true;
    
    // Notify other players in multiplayer that we paused
    if(pNetworkManager != nullptr) {
        Player* pLocalPlayer = getPlayerByName(localPlayerName);
        if(pLocalPlayer != nullptr) {
            cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLAYER_PAUSE));
            
            // Add ourselves to paused players set
            pausedPlayers.insert(pLocalPlayer->getPlayerID());
        }
    }
}

void Game::logFrameTiming() {
    if(frameTiming.frameCount <= 0) {
        return;
    }
    if(bPause) {
        return;
    }

    const double avgAi = frameTiming.aiMs / frameTiming.frameCount;
    const double avgUnits = frameTiming.unitsMs / frameTiming.frameCount;
    const double avgStructures = frameTiming.structuresMs / frameTiming.frameCount;
    const double avgPathfinding = frameTiming.pathfindingMs / frameTiming.frameCount;
    const double avgNetworkWait = frameTiming.networkWaitMs / frameTiming.frameCount;
    const double avgRendering = frameTiming.renderingMs / frameTiming.frameCount;
    const double avgTotal = frameTiming.totalMs / frameTiming.frameCount;
    const double avgFps = avgTotal > 0.0 ? 1000.0 / avgTotal : 0.0;
    const double maxFps = frameTiming.maxTotalMs > 0.0 ? 1000.0 / frameTiming.maxTotalMs : 0.0;
    const double avgGameCycles = static_cast<double>(frameTiming.totalGameCycles) / frameTiming.frameCount;
    
    // Pathfinding detailed stats
    const double avgPathsPerFrame = static_cast<double>(frameTiming.totalPathsProcessed) / frameTiming.frameCount;
    const double avgPathsPerCycle = frameTiming.totalGameCycles > 0 ? 
        static_cast<double>(frameTiming.totalPathsProcessed) / frameTiming.totalGameCycles : 0.0;
    const double avgPathfindingPerCycle = frameTiming.totalGameCycles > 0 ? 
        avgPathfinding / avgGameCycles : 0.0;
    const double avgMsPerPath = frameTiming.totalPathsProcessed > 0 ? 
        avgPathfinding / avgPathsPerFrame : 0.0;

    const int mapSize = (currentGameMap != nullptr) ? currentGameMap->getSizeX() : 0;
    
    logPerformance("[Performance] === AVERAGES over %d frames ===", frameTiming.frameCount);
    logPerformance("[Performance] Map: %dx%d | Units: %d", mapSize, mapSize, frameTiming.unitCount);
    logPerformance("[Performance] FPS: %.1f | Frame: %.2fms",
        avgFps, avgTotal);
    logPerformance("[Performance] GameCycles/Frame: min=%d avg=%.1f max=%d",
        frameTiming.minGameCyclesPerFrame, avgGameCycles, frameTiming.maxGameCyclesPerFrame);
    logPerformance("[Performance] AI:         min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minAiMs, avgAi, frameTiming.maxAiMs);
    logPerformance("[Performance] Units:      min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minUnitsMs, avgUnits, frameTiming.maxUnitsMs);
    
    // Detailed unit breakdown
    const double avgTargeting = frameTiming.unitTargetingMs / frameTiming.frameCount;
    const double avgNavigate = frameTiming.unitNavigateMs / frameTiming.frameCount;
    const double avgMove = frameTiming.unitMoveMs / frameTiming.frameCount;
    const double avgTurn = frameTiming.unitTurnMs / frameTiming.frameCount;
    const double avgVisibility = frameTiming.unitVisibilityMs / frameTiming.frameCount;
    const double unitsTotal = avgTargeting + avgNavigate + avgMove + avgTurn + avgVisibility;
    logPerformance("[Performance]   ↳ Targeting:   %.2fms (%.1f%%)", avgTargeting, unitsTotal > 0 ? (avgTargeting/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Navigate:    %.2fms (%.1f%%)", avgNavigate, unitsTotal > 0 ? (avgNavigate/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Move:        %.2fms (%.1f%%)", avgMove, unitsTotal > 0 ? (avgMove/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Turn:        %.2fms (%.1f%%)", avgTurn, unitsTotal > 0 ? (avgTurn/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Visibility:  %.2fms (%.1f%%)", avgVisibility, unitsTotal > 0 ? (avgVisibility/unitsTotal*100) : 0);
    
    logPerformance("[Performance] Structures: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minStructuresMs, avgStructures, frameTiming.maxStructuresMs);
    logPerformance("[Performance] Pathfinding: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minPathfindingMs, avgPathfinding, frameTiming.maxPathfindingMs);
    logPerformance("[Performance] NetworkWait: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minNetworkWaitMs, avgNetworkWait, frameTiming.maxNetworkWaitMs);
    logPerformance("[Performance] Rendering:  min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minRenderingMs, avgRendering, frameTiming.maxRenderingMs);
    logPerformance("[Performance] Pathfinding Detail: %.1f paths/frame | %.2f paths/cycle | %.2fms/cycle | %.2fms/path",
        avgPathsPerFrame, avgPathsPerCycle, avgPathfindingPerCycle, avgMsPerPath);
    
    // Turret scan detailed stats
    const double avgTurretScans = static_cast<double>(frameTiming.totalTurretScans) / frameTiming.frameCount;
    const double avgTurretScanMs = frameTiming.turretScanMs / frameTiming.frameCount;
    const double avgMsPerTurretScan = frameTiming.totalTurretScans > 0 ? 
        frameTiming.turretScanMs / frameTiming.totalTurretScans : 0.0;
    logPerformance("[Performance] Turret Scans: %.1f scans/frame | %.2fms total/frame | %.4fms/scan",
        avgTurretScans, avgTurretScanMs, avgMsPerTurretScan);
    logPerformance("[Performance] Turret Scan Range: min=%.2fms max=%.2fms | Peak: %d scans/frame",
        frameTiming.minTurretScanMs, frameTiming.maxTurretScanMs, frameTiming.maxTurretScansPerFrame);
    
    // Phase 1: Token/node statistics
    const double avgTokensPerFrame = frameTiming.frameCount > 0 ? 
        static_cast<double>(frameTiming.totalPathTokens) / frameTiming.frameCount : 0.0;
    logPerformance("[Performance] === TOKEN STATISTICS (Phase 1) ===");
    logPerformance("[Performance] Tokens/Frame: avg=%.1f max=%zu total=%zu",
        avgTokensPerFrame, frameTiming.maxPathTokensPerFrame, frameTiming.totalPathTokens);
    logPerformance("[Performance] Tokens/Cycle: avg=%.1f±%.1f max=%zu",
        frameTiming.pathTokensPerCycleStats.mean, 
        frameTiming.pathTokensPerCycleStats.ci95(),
        frameTiming.maxPathTokensPerCycle);
    logPerformance("[Performance] Paths/Cycle: avg=%.2f±%.2f",
        frameTiming.pathsPerCycleStats.mean,
        frameTiming.pathsPerCycleStats.ci95());
    
    // Token distribution per path
    if(frameTiming.tokensPerCompletedPathStats.sampleCount > 0) {
        logPerformance("[Performance] Tokens/CompletedPath: avg=%.1f±%.1f min=%zu max=%zu (n=%zu)",
            frameTiming.tokensPerCompletedPathStats.mean,
            frameTiming.tokensPerCompletedPathStats.ci95(),
            frameTiming.minTokensPerCompletedPath,
            frameTiming.maxTokensPerCompletedPath,
            frameTiming.tokensPerCompletedPathStats.sampleCount);
    }
    if(frameTiming.tokensPerFailedPathStats.sampleCount > 0) {
        logPerformance("[Performance] Tokens/FailedPath: avg=%.1f±%.1f min=%zu max=%zu (n=%zu)",
            frameTiming.tokensPerFailedPathStats.mean,
            frameTiming.tokensPerFailedPathStats.ci95(),
            frameTiming.minTokensPerFailedPath,
            frameTiming.maxTokensPerFailedPath,
            frameTiming.tokensPerFailedPathStats.sampleCount);
    }
    
    // Path completion rates
    const double pathCompletionRate = frameTiming.totalPathsProcessed > 0 ?
        (100.0 * (frameTiming.totalPathsProcessed - frameTiming.totalPathsFailed) / frameTiming.totalPathsProcessed) : 0.0;
    logPerformance("[Performance] Path Completion: %.1f%% (%d completed, %d failed)",
        pathCompletionRate,
        frameTiming.totalPathsProcessed - frameTiming.totalPathsFailed,
        frameTiming.totalPathsFailed);
    
    // Token histogram
    logPerformance("[Performance] Token Histogram: <512:%zu 512-1k:%zu 1k-2k:%zu 2k-4k:%zu 4k-8k:%zu 8k-16k:%zu 16k+:%zu",
        frameTiming.pathTokenHistogram[0],
        frameTiming.pathTokenHistogram[1],
        frameTiming.pathTokenHistogram[2],
        frameTiming.pathTokenHistogram[3],
        frameTiming.pathTokenHistogram[4],
        frameTiming.pathTokenHistogram[5],
        frameTiming.pathTokenHistogram[6]);
    
    // Queue and budget stats
    logPerformance("[Performance] Queue: maxDepth=%d | BudgetStarvedFrames=%d",
        frameTiming.maxPathQueueLength,
        frameTiming.pathBudgetStarvedFrames);
    
    // Phase 2: Token budget statistics
    const double tokensUsedPerCycle = frameTiming.totalGameCycles > 0 ?
        static_cast<double>(frameTiming.totalPathTokens) / frameTiming.totalGameCycles : 0.0;
    const double tokenBudgetUtilization = tokensUsedPerCycle / negotiatedBudget * 100.0;
    logPerformance("[Performance] === TOKEN BUDGET (Phase 1 - Negotiated) ===");
    logPerformance("[Performance] Budget: %zu tokens/cycle | Avg Used: %.0f (%.1f%% utilization) | Carry-Over: %zu",
        negotiatedBudget, tokensUsedPerCycle, tokenBudgetUtilization, carryOverTokens);
    logPerformance("[Performance] Token Budget Exhausted: %d times | Time Budget Exceeded: %d times (safety valve)",
        frameTiming.tokenBudgetExhaustedCount,
        frameTiming.timeBudgetExceededCount);
    
    // Token-to-time correlation
    const double tokensPerMs = avgPathfindingPerCycle > 0.0 ? 
        tokensUsedPerCycle / avgPathfindingPerCycle : 0.0;
    logPerformance("[Performance] Tokens/Ms: %.0f (measured correlation)", tokensPerMs);
    
    logPerformance("[Performance] === PEAKS (worst case) ===");
    logPerformance("[Performance] FPS: %.1f | Frame: %.2fms | AI: %.2fms | Units: %.2fms | Structures: %.2fms | Pathfinding: %.2fms | NetworkWait: %.2fms | Rendering: %.2fms",
        maxFps, frameTiming.maxTotalMs,
        frameTiming.maxAiMs, frameTiming.maxUnitsMs, frameTiming.maxStructuresMs, 
        frameTiming.maxPathfindingMs, frameTiming.maxNetworkWaitMs, frameTiming.maxRenderingMs);
    logPerformance("[Performance] Pathfinding Peaks: %d paths/frame | %d paths/cycle | %.2fms/cycle",
        frameTiming.maxPathsPerFrame, frameTiming.maxPathsPerCycle, frameTiming.maxPathfindingMsPerCycle);

    // Reset counters
    frameTiming.aiMs = 0.0;
    frameTiming.unitsMs = 0.0;
    frameTiming.structuresMs = 0.0;
    frameTiming.pathfindingMs = 0.0;
    frameTiming.networkWaitMs = 0.0;
    frameTiming.renderingMs = 0.0;
    frameTiming.totalMs = 0.0;
    frameTiming.totalGameCycles = 0;
    frameTiming.totalPathsProcessed = 0;
    frameTiming.totalTurretScans = 0;
    frameTiming.turretScanMs = 0.0;
    frameTiming.frameCount = 0;
    frameTiming.maxAiMs = 0.0;
    frameTiming.maxUnitsMs = 0.0;
    frameTiming.maxStructuresMs = 0.0;
    frameTiming.maxPathfindingMs = 0.0;
    frameTiming.maxPathfindingMsPerCycle = 0.0;
    frameTiming.maxNetworkWaitMs = 0.0;
    frameTiming.maxRenderingMs = 0.0;
    frameTiming.maxTotalMs = 0.0;
    frameTiming.maxTurretScanMs = 0.0;
    frameTiming.maxTurretScansPerFrame = 0;
    frameTiming.minTurretScanMs = 999999.0;
    frameTiming.maxGameCyclesPerFrame = 0;
    frameTiming.maxPathsPerCycle = 0;
    frameTiming.maxPathsPerFrame = 0;
    frameTiming.minGameCyclesPerFrame = 999999;
    frameTiming.minAiMs = 999999.0;
    frameTiming.minUnitsMs = 999999.0;
    frameTiming.minStructuresMs = 999999.0;
    frameTiming.minPathfindingMs = 999999.0;
    frameTiming.minNetworkWaitMs = 999999.0;
    frameTiming.minRenderingMs = 999999.0;
    
    // Reset unit breakdown
    frameTiming.unitTargetingMs = 0.0;
    frameTiming.unitNavigateMs = 0.0;
    frameTiming.unitMoveMs = 0.0;
    frameTiming.unitTurnMs = 0.0;
    frameTiming.unitVisibilityMs = 0.0;
    
    // Reset Phase 1 token stats
    frameTiming.totalPathTokens = 0;
    frameTiming.maxPathTokensPerCycle = 0;
    frameTiming.maxPathTokensPerFrame = 0;
    frameTiming.pathsPerCycleStats.reset();
    frameTiming.pathTokensPerCycleStats.reset();
    frameTiming.tokensPerCompletedPathStats.reset();
    frameTiming.tokensPerFailedPathStats.reset();
    frameTiming.minTokensPerCompletedPath = SIZE_MAX;
    frameTiming.maxTokensPerCompletedPath = 0;
    frameTiming.minTokensPerFailedPath = SIZE_MAX;
    frameTiming.maxTokensPerFailedPath = 0;
    
    // Reset Phase 2 token budget stats
    frameTiming.tokenBudgetExhaustedCount = 0;
    frameTiming.timeBudgetExceededCount = 0;
    frameTiming.pathTokenHistogram = {};
    frameTiming.maxPathQueueLength = 0;
    frameTiming.pathBudgetStarvedFrames = 0;
    frameTiming.totalPathsFailed = 0;
}

void Game::onOptions()
{
    if(bReplay == true) {
        // don't show menu
        quitGame();
    } else {
        Uint32 color = SDL2RGB(palette[houseToPaletteIndex[pLocalHouse->getHouseID()] + 3]);
        pInGameMenu = std::make_unique<InGameMenu>((gameType == GameType::CustomMultiplayer), color);
        bMenu = true;
        pauseGame();
    }
}


void Game::onMentat()
{
    pInGameMentat = std::make_unique<MentatHelp>(pLocalHouse->getHouseID(), techLevel, gameInitSettings.getMission());
    bMenu = true;
    pauseGame();
}

void Game::onCityBudget()
{
    if (!citySimulation_ || !citySimulation_->isInitialized()) {
        return;
    }
    pInterface->openWindow(CityBudgetWindow::create());
}


GameInitSettings Game::getNextGameInitSettings()
{
    if(nextGameInitSettings.getGameType() != GameType::Invalid) {
        // return the prepared game init settings (load game or restart mission)
        return nextGameInitSettings;
    }

    switch(gameInitSettings.getGameType()) {
        case GameType::Campaign: {
            int currentMission = gameInitSettings.getMission();
            if(!won) {
                currentMission -= (currentMission >= 22) ? 1 : 3;
            }
            int nextMission = gameInitSettings.getMission();
            Uint32 alreadyPlayedRegions = gameInitSettings.getAlreadyPlayedRegions();
            if(currentMission >= -1) {
                // do map choice
                SDL_Log("Map Choice...");
                MapChoice mapChoice(gameInitSettings.getHouseID(), currentMission, alreadyPlayedRegions);
                mapChoice.showMenu();
                nextMission = mapChoice.getSelectedMission();
                alreadyPlayedRegions = mapChoice.getAlreadyPlayedRegions();
            }

            Uint32 alreadyShownTutorialHints = won ? pLocalPlayer->getAlreadyShownTutorialHints() : gameInitSettings.getAlreadyShownTutorialHints();
            return GameInitSettings(gameInitSettings, nextMission, alreadyPlayedRegions, alreadyShownTutorialHints);
        } break;

        default: {
            SDL_Log("Game::getNextGameInitClass(): Wrong gameType for next Game.");
            return GameInitSettings();
        } break;
    }
}


int Game::whatNext()
{
    if(whatNextParam != GAME_NOTHING) {
        int tmp = whatNextParam;
        whatNextParam = GAME_NOTHING;
        return tmp;
    }

    if(nextGameInitSettings.getGameType() != GameType::Invalid) {
        return GAME_LOAD;
    }

    switch(gameType) {
        case GameType::Campaign: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else if(won == true) {
                if(gameInitSettings.getMission() == 22) {
                    // there is no mission after this mission
                    whatNextParam = GAME_RETURN_TO_MENU;
                } else {
                    // there is a mission after this mission
                    whatNextParam = GAME_NEXTMISSION;
                }
                return GAME_DEBRIEFING_WIN;
            } else {
                // we need to play this mission again
                whatNextParam = GAME_NEXTMISSION;

                return GAME_DEBRIEFING_LOST;
            }
        } break;

        case GameType::Skirmish: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else if(won == true) {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_DEBRIEFING_WIN;
            } else {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_DEBRIEFING_LOST;
            }
        } break;

        case GameType::CustomGame:
        case GameType::CustomMultiplayer: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_CUSTOM_GAME_STATS;
            }
        } break;

        default: {
            return GAME_RETURN_TO_MENU;
        } break;
    }
}


bool Game::loadSaveGame(const std::string& filename) {
    IFileStream fs;

    if(fs.open(filename) == false) {
        return false;
    }

    bool ret = loadSaveGame(fs);

    fs.close();

    return ret;
}

bool Game::loadSaveGame(InputStream& stream) {
    gameState = GameState::Loading;

    targetRequestQueue.clear();
    pendingTargetRequestIds.clear();
    pathRequestQueue.clear();
    pendingPathRequestIds.clear();

    Uint32 magicNum = stream.readUint32();
    if (magicNum != SAVEMAGIC) {
        SDL_Log("Game::loadSaveGame(): No valid savegame! Expected magic number %.8X, but got %.8X!", SAVEMAGIC, magicNum);
        return false;
    }

    Uint32 savegameVersion = stream.readUint32();
    
    // Support backward compatibility with version 9705 (pre-Original AI)
    constexpr Uint32 MINIMUM_SUPPORTED_VERSION = 9705;
    if (savegameVersion < MINIMUM_SUPPORTED_VERSION || savegameVersion > SAVEGAMEVERSION) {
        SDL_Log("Game::loadSaveGame(): No valid savegame! Expected savegame version %d-%d, but got %d!", 
                MINIMUM_SUPPORTED_VERSION, SAVEGAMEVERSION, savegameVersion);
        return false;
    }
    
    // Store version for backward-compatible loading
    this->loadedSavegameVersion = savegameVersion;

    std::string duneVersion = stream.readString();

    // Read mod info (version 9806+)
    std::string savedModName = "vanilla";
    std::string savedModChecksum = "";
    if (savegameVersion >= 9806) {
        savedModName = stream.readString();
        savedModChecksum = stream.readString();

        // Check if save was created with a different mod
        std::string currentModName = ModManager::instance().getActiveModName();
        std::string currentChecksum = ModManager::instance().getEffectiveChecksums().combined;

        if (savedModChecksum != currentChecksum) {
            SDL_Log("Game::loadSaveGame(): Save mod mismatch detected");
            SDL_Log("  Save mod: %s (checksum: %s)", savedModName.c_str(), savedModChecksum.c_str());
            SDL_Log("  Current mod: %s (checksum: %s)", currentModName.c_str(), currentChecksum.c_str());

            if (!ModManager::instance().modExists(savedModName)) {
                SDL_Log("Game::loadSaveGame(): Required mod '%s' not found - loading with current mod active",
                        savedModName.c_str());
            } else if (savedModName != currentModName) {
                // Auto-switch to the save's mod so its rules (city sim,
                // ObjectData, GameOptions) match what the save was authored
                // against. Persists via active_mod.txt — same as picking it
                // in the mod menu.
                if (ModManager::instance().setActiveMod(savedModName)) {
                    SDL_Log("Game::loadSaveGame(): switched active mod to '%s' for save load",
                            savedModName.c_str());
                    citySimEnabled_ = ModManager::instance().isCityModeActive();
                } else {
                    SDL_Log("Game::loadSaveGame(): WARNING - failed to switch to mod '%s'; loading anyway",
                            savedModName.c_str());
                }
            }
        }
    }

    // if this is a multiplayer load we need to save some information before we overwrite gameInitSettings with the settings saved in the savegame
    bool bMultiplayerLoad = (gameInitSettings.getGameType() == GameType::LoadMultiplayer);
    GameInitSettings::HouseInfoList oldHouseInfoList = gameInitSettings.getHouseInfoList();

    // read gameInitSettings
    gameInitSettings = GameInitSettings(stream);

    // read the actual house setup choosen at the beginning of the game
    Uint32 numHouseInfo = stream.readUint32();
    for(Uint32 i=0;i<numHouseInfo;i++) {
        houseInfoListSetup.push_back(GameInitSettings::HouseInfo(stream));
    }

    //read map size
    short mapSizeX = stream.readUint32();
    short mapSizeY = stream.readUint32();

    //create the new map
    currentGameMap = new Map(mapSizeX, mapSizeY);
    initializeSpatialGrid(mapSizeX, mapSizeY);

    //read GameCycleCount
    gameCycleCount = stream.readUint32();

    // read some settings
    gameType = static_cast<GameType>(stream.readSint8());
    techLevel = stream.readUint8();
    randomGen.setSeed(stream.readUint32());

    // read in the unit/structure data
    objectData.load(stream);

    //load the house(s) info
    for(int i=0; i<NUM_HOUSES; i++) {
        if (stream.readBool() == true) {
            //house in game
            house[i] = std::make_unique<House>(stream);
        }
    }

    // we have to set the local player
    if(bMultiplayerLoad) {
        // get it from the gameInitSettings that started the game (not the one saved in the savegame)
        for(const GameInitSettings::HouseInfo& houseInfo : oldHouseInfoList) {

            // find the right house
            for(int i=0;i<NUM_HOUSES;i++) {
                if((house[i] != nullptr) && (house[i]->getHouseID() == houseInfo.houseID)) {
                    // iterate over all players
                    auto& players = house[i]->getPlayerList();
                    auto playerIter = players.cbegin();
                    for(const auto& playerInfo : houseInfo.playerInfoList) {
                        if(playerInfo.playerClass == HUMANPLAYERCLASS) {
                            while(playerIter != players.cend()) {

                                const auto pHumanPlayer = dynamic_cast<HumanPlayer*>(playerIter->get());
                                if(pHumanPlayer) {
                                    // we have actually found a human player and now assign the first unused name to it
                                    unregisterPlayer(pHumanPlayer);
                                    pHumanPlayer->setPlayername(playerInfo.playerName);
                                    registerPlayer(pHumanPlayer);

                                    if(playerInfo.playerName == getLocalPlayerName()) {
                                        pLocalHouse = house[i].get();
                                        pLocalPlayer = pHumanPlayer;
                                    }

                                    ++playerIter;
                                    break;
                                } else {
                                    ++playerIter;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // it is stored in the savegame, so set it up
        Uint8 localPlayerID = stream.readUint8();
        pLocalPlayer = dynamic_cast<HumanPlayer*>(getPlayerByID(localPlayerID));
        pLocalHouse = house[pLocalPlayer->getHouse()->getHouseID()].get();
    }

    debug = stream.readBool();
    bCheatsEnabled = stream.readBool();

    winFlags = stream.readUint32();
    loseFlags = stream.readUint32();

    currentGameMap->load(stream);

    //load the structures and units
    objectManager.load(stream);

    int numBullets = stream.readUint32();
    for(int i = 0; i < numBullets; i++) {
        bulletList.push_back(new Bullet(stream));
    }

    int numExplosions = stream.readUint32();
    for(int i = 0; i < numExplosions; i++) {
        explosionList.push_back(new Explosion(stream));
    }

    if(bMultiplayerLoad) {
        screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());

        screenborder->setNewScreenCenter(pLocalHouse->getCenterOfMainBase()*TILESIZE);

    } else {
        //load selection list
        selectedList = stream.readUint32Set();

        //load the screenborder info
        screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
        screenborder->load(stream);
    }

    // load city simulation state (version 9807+)
    if (savegameVersion >= 9807) {
        bool hasCitySim = stream.readBool();
        if (hasCitySim) {
            citySimEnabled_ = true;
            citySimulation_ = std::make_unique<DuneCity::CitySimulation>();
            citySimulation_->init(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            citySimulation_->setCityEffectsEnabled(
                gameInitSettings.getGameOptions().cityEffects);
            citySimulation_->load(stream);
        }
    }

    // Mirror the new-game gate: if the loaded save's ObjectData has zones
    // enabled but city sim is off, force them off so vanilla saves can't
    // surface zone build entries.
    if (!citySimEnabled_) {
        for (int h = 0; h < NUM_HOUSES; h++) {
            objectData.data[Structure_ZoneResidential][h].enabled = false;
            objectData.data[Structure_ZoneCommercial][h].enabled  = false;
            objectData.data[Structure_ZoneIndustrial][h].enabled  = false;
        }
    }

    // load triggers
    triggerManager.load(stream);

    // CommandManager is at the very end of the file. DO NOT CHANGE THIS!
    cmdManager.load(stream);

    finished = false;

    return true;
}


bool Game::saveGame(const std::string& filename)
{
    OFileStream fs;

    if(fs.open(filename) == false) {
        SDL_Log("Game::saveGame(): %s", strerror(errno));
        currentGame->addToNewsTicker(std::string("Game NOT saved: Cannot open \"") + filename + "\".");
        return false;
    }

    fs.writeUint32(SAVEMAGIC);

    fs.writeUint32(SAVEGAMEVERSION);

    fs.writeString(VERSIONSTRING);

    // Write mod info for save compatibility (version 9806+)
    fs.writeString(ModManager::instance().getActiveModName());
    fs.writeString(ModManager::instance().getEffectiveChecksums().combined);

    // write gameInitSettings
    gameInitSettings.save(fs);

    fs.writeUint32(houseInfoListSetup.size());
    for(const GameInitSettings::HouseInfo& houseInfo : houseInfoListSetup) {
        houseInfo.save(fs);
    }

    //write the map size
    fs.writeUint32(currentGameMap->getSizeX());
    fs.writeUint32(currentGameMap->getSizeY());

    // write GameCycleCount
    fs.writeUint32(gameCycleCount);

    // write some settings
    fs.writeSint8(static_cast<Sint8>(gameType));
    fs.writeUint8(techLevel);
    fs.writeUint32(randomGen.getSeed());

    // write out the unit/structure data
    objectData.save(fs);

    //write the house(s) info
    for(int i=0; i<NUM_HOUSES; i++) {
        fs.writeBool(house[i] != nullptr);

        if(house[i] != nullptr) {
            house[i]->save(fs);
        }
    }

    if(gameInitSettings.getGameType() != GameType::CustomMultiplayer) {
        fs.writeUint8(pLocalPlayer->getPlayerID());
    }

    fs.writeBool(debug);
    fs.writeBool(bCheatsEnabled);

    fs.writeUint32(winFlags);
    fs.writeUint32(loseFlags);

    currentGameMap->save(fs);

    // save the structures and units
    objectManager.save(fs);

    fs.writeUint32(bulletList.size());
    for(const Bullet* pBullet : bulletList) {
        pBullet->save(fs);
    }

    fs.writeUint32(explosionList.size());
    for(const Explosion* pExplosion : explosionList) {
        pExplosion->save(fs);
    }

    if(gameInitSettings.getGameType() != GameType::CustomMultiplayer) {
        // save selection lists

        // write out selected units list
        fs.writeUint32Set(selectedList);

        // write the screenborder info
        screenborder->save(fs);
    }

    // save city simulation state
    fs.writeBool(citySimEnabled_ && citySimulation_ != nullptr);
    if (citySimEnabled_ && citySimulation_) {
        citySimulation_->save(fs);
    }

    // save triggers
    triggerManager.save(fs);

    // CommandManager is at the very end of the file. DO NOT CHANGE THIS!
    cmdManager.save(fs);

    fs.close();

    return true;
}


void Game::saveObject(OutputStream& stream, ObjectBase* obj) const
{
    if(obj == nullptr)
        return;

    stream.writeUint32(obj->getItemID());
    obj->save(stream);
}


ObjectBase* Game::loadObject(InputStream& stream, Uint32 objectID)
{
    Uint32 itemID;

    itemID = stream.readUint32();

    ObjectBase* newObject = ObjectBase::loadObject(stream, itemID, objectID);
    if(newObject == nullptr) {
        THROW(std::runtime_error, "Error while loading an object!");
    }

    return newObject;
}


void Game::selectAll(const std::set<Uint32>& aList)
{
    for(Uint32 objectID : aList) {
        objectManager.getObject(objectID)->setSelected(true);
    }
}

void Game::selectAllOrnithopters()
{
    std::set<Uint32> ornithopterIDs;
    Coord summedPosition;

    for(UnitBase* pUnit : unitList) {
        if((pUnit->getOwner() == pLocalHouse) &&
           (pUnit->getItemID() == Unit_Ornithopter) &&
           pUnit->isRespondable()) {
            ornithopterIDs.insert(pUnit->getObjectID());
            summedPosition += pUnit->getLocation();
        }
    }

    if(ornithopterIDs.empty()) {
        return;
    }

    unselectAll(selectedList);
    selectedList.clear();

    for(Uint32 objectID : ornithopterIDs) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject != nullptr) {
            pObject->setSelected(true);
            selectedList.insert(objectID);
        }
    }

    selectionChanged();
    currentCursorMode = CursorMode_Normal;

    Coord averagePosition = summedPosition / static_cast<int>(ornithopterIDs.size());
    screenborder->setNewScreenCenter(averagePosition * TILESIZE);
}


void Game::unselectAll(const std::set<Uint32>& aList)
{
    for(Uint32 objectID : aList) {
        objectManager.getObject(objectID)->setSelected(false);
    }
}

void Game::onReceiveSelectionList(const std::string& name, const std::set<Uint32>& newSelectionList, int groupListIndex)
{
    HumanPlayer* pHumanPlayer = dynamic_cast<HumanPlayer*>(getPlayerByName(name));

    if(pHumanPlayer == nullptr) {
        return;
    }

    if(groupListIndex == -1) {
        // the other player controlling the same house has selected some units

        if(pHumanPlayer->getHouse() != pLocalHouse) {
            return;
        }

        for(Uint32 objectID : selectedByOtherPlayerList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if(pObject != nullptr) {
                pObject->setSelectedByOtherPlayer(false);
            }
        }

        selectedByOtherPlayerList = newSelectionList;

        for(Uint32 objectID : selectedByOtherPlayerList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if(pObject != nullptr) {
                pObject->setSelectedByOtherPlayer(true);
            }
        }
    } else {
        // some other player has assigned a number to a list of units
        pHumanPlayer->setGroupList(groupListIndex, newSelectionList);
    }
}

void Game::onPeerDisconnected(const std::string& name, bool bHost, int cause) {
    pInterface->getChatManager().addInfoMessage(name + " disconnected!");
    
    // If host disconnected, the game cannot continue - end it
    if(bHost) {
        SDL_Log("Host '%s' disconnected - ending game", name.c_str());
        pInterface->getChatManager().addInfoMessage("Host disconnected! Game ending...");
        
        // Set game as lost/quit so we return to menu
        bQuitGame = true;
        return;
    }
    
    // CRITICAL: Clear all client stats when ANY peer disconnects
    // Active clients will re-register at the next interval (< 375 cycles)
    // This is simpler and safer than trying to map player name → clientId
    if(pNetworkManager != nullptr && pNetworkManager->isServer()) {
        const size_t removedCount = clientStats.size();
        clientStats.clear();
        
        SDL_Log("[PathBudget HOST] Player '%s' disconnected - cleared all client stats (%zu entries)",
                name.c_str(), removedCount);
        SDL_Log("[PathBudget HOST] Active clients will re-register at next interval (< 375 cycles)");
        logPerformance("[PathBudget] Cycle %d: Cleared %zu client stats due to disconnect (%s) - active clients will re-register",
                gameCycleCount, removedCount, name.c_str());
    }
}

void Game::setGameWon() {
    if(!bQuitGame && !finished) {
        won = true;
        finished = true;
        finishedLevelCycle = gameCycleCount;  // MULTIPLAYER FIX (Issue #9): Use cycle count
        soundPlayer->playVoice(YourMissionIsComplete,pLocalHouse->getHouseID());
    }
}


void Game::setGameLost() {
    if(!bQuitGame && !finished) {
        won = false;
        finished = true;
        finishedLevelCycle = gameCycleCount;  // MULTIPLAYER FIX (Issue #9): Use cycle count
        soundPlayer->playVoice(YouHaveFailedYourMission,pLocalHouse->getHouseID());
    }
}


bool Game::onRadarClick(Coord worldPosition, bool bRightMouseButton, bool bDrag) {
    if(bRightMouseButton) {

        if(handleSelectedObjectsActionClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE)) {
            indicatorFrame = 0;
            indicatorPosition = worldPosition;
        }

        return false;
    } else {

        if(bDrag) {
            screenborder->setNewScreenCenter(worldPosition);
            return true;
        } else {

            switch(currentCursorMode) {
                case CursorMode_Attack: {
                    handleSelectedObjectsAttackClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Move: {
                    handleSelectedObjectsMoveClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Capture: {
                    handleSelectedObjectsCaptureClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_CarryallDrop: {
                    handleSelectedObjectsRequestCarryallDropClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Normal:
                default: {
                    screenborder->setNewScreenCenter(worldPosition);
                    return true;
                } break;
            }
        }
    }
}


bool Game::isOnRadarView(int mouseX, int mouseY) const {
    return pInterface->getRadarView().isOnRadar(mouseX - (sideBarPos.x + SIDEBAR_COLUMN_WIDTH), mouseY - sideBarPos.y);
}


void Game::handleChatInput(SDL_KeyboardEvent& keyboardEvent) {
    if(keyboardEvent.keysym.sym == SDLK_ESCAPE) {
        chatMode = false;
    } else if(keyboardEvent.keysym.sym == SDLK_RETURN) {
        if(typingChatMessage.length() > 0) {
            unsigned char md5sum[16];

            md5((const unsigned char*) typingChatMessage.c_str(), typingChatMessage.size(), md5sum);

            std::stringstream md5stream;
            md5stream << std::setfill('0') << std::hex << std::uppercase << "0x";
            for(int i=0;i<16;i++) {
                md5stream << std::setw(2) << (int) md5sum[i];
            }

            const std::string md5string = md5stream.str();

            if((bCheatsEnabled == false) && (md5string == "0xB8766C8EC7A61036B69893FC17AAF21E")) {
                bCheatsEnabled = true;
                pInterface->getChatManager().addInfoMessage("Cheat mode enabled");
            } else if((bCheatsEnabled == true) && (md5string == "0xB8766C8EC7A61036B69893FC17AAF21E")) {
                pInterface->getChatManager().addInfoMessage("Cheat mode already enabled");
            } else if((bCheatsEnabled == true) && (md5string == "0x57583291CB37F8167EDB0611D8D19E58")) {
                if (gameType != GameType::CustomMultiplayer) {
                    pInterface->getChatManager().addInfoMessage("You win this game");
                    setGameWon();
                }
            } else if((bCheatsEnabled == true) && (md5string == "0x1A12BE3DBE54C5A504CAA6EE9782C1C8")) {
                if(debug == true) {
                    pInterface->getChatManager().addInfoMessage("You are already in debug mode");
                } else if (gameType != GameType::CustomMultiplayer) {
                    pInterface->getChatManager().addInfoMessage("Debug mode enabled");
                    debug = true;
                }
            } else if((bCheatsEnabled == true) && (md5string == "0x54F68155FC64A5BC66DCD50C1E925C0B")) {
                if(debug == false) {
                    pInterface->getChatManager().addInfoMessage("You are not in debug mode");
                } else if (gameType != GameType::CustomMultiplayer) {
                    pInterface->getChatManager().addInfoMessage("Debug mode disabled");
                    debug = false;
                }
            } else if((bCheatsEnabled == true) && (md5string == "0xCEF1D26CE4B145DE985503CA35232ED8")) {
                if (gameType != GameType::CustomMultiplayer) {
                    pInterface->getChatManager().addInfoMessage("You got some credits");
                    pLocalHouse->returnCredits(10000);
                }
            } else if(md5string == "0x05362BF626E467A93FFE6FF0D8A899E3") {
                // Toggle immortality cheat (muaddib) - works in single-player only, no cheat mode required
                if (gameType != GameType::CustomMultiplayer && gameType != GameType::LoadMultiplayer) {
                    bool currentState = gameInitSettings.getGameOptions().immortalHumanPlayer;
                    gameInitSettings.setImmortalHumanPlayer(!currentState);
                    if(!currentState) {
                        pInterface->getChatManager().addInfoMessage("God mode: Immortality enabled");
                    } else {
                        pInterface->getChatManager().addInfoMessage("God mode: Immortality disabled");
                    }
                }
            } else {
                if(pNetworkManager != nullptr) {
                    pNetworkManager->sendChatMessage(typingChatMessage);
                }
                pInterface->getChatManager().addChatMessage(getLocalPlayerName(), typingChatMessage);
            }
        }

        chatMode = false;
    } else if(keyboardEvent.keysym.sym == SDLK_BACKSPACE) {
        if(typingChatMessage.length() > 0) {
            typingChatMessage = utf8Substr(typingChatMessage, 0, utf8Length(typingChatMessage) - 1);
        }
    }
}


void Game::handleKeyInput(SDL_KeyboardEvent& keyboardEvent) {
    switch(keyboardEvent.keysym.sym) {

        case SDLK_0: {
            //if ctrl and 0 remove selected units from all groups
            if(SDL_GetModState() & KMOD_CTRL) {
                for(Uint32 objectID : selectedList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    pObject->setSelected(false);
                    pObject->removeFromSelectionLists();
                    for(int i=0; i < NUMSELECTEDLISTS; i++) {
                        pLocalPlayer->getGroupList(i).erase(objectID);
                    }
                }
                selectedList.clear();
                currentGame->selectionChanged();
                currentCursorMode = CursorMode_Normal;
            } else {
                for(Uint32 objectID : selectedList) {
                    objectManager.getObject(objectID)->setSelected(false);
                }
                selectedList.clear();
                currentGame->selectionChanged();
                currentCursorMode = CursorMode_Normal;
            }
        } break;

        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
        case SDLK_5:
        case SDLK_6:
        case SDLK_7:
        case SDLK_8:
        case SDLK_9: {
            int selectListIndex = keyboardEvent.keysym.sym - SDLK_1;

            if(citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                switch(keyboardEvent.keysym.sym) {
                    case SDLK_1: currentCityOverlay_ = DuneCity::CityOverlayMode::None; break;
                    case SDLK_2: currentCityOverlay_ = DuneCity::CityOverlayMode::PowerGrid; break;
                    case SDLK_3: currentCityOverlay_ = DuneCity::CityOverlayMode::TrafficDensity; break;
                    case SDLK_4: currentCityOverlay_ = DuneCity::CityOverlayMode::Pollution; break;
                    case SDLK_5: currentCityOverlay_ = DuneCity::CityOverlayMode::LandValue; break;
                    case SDLK_6: currentCityOverlay_ = DuneCity::CityOverlayMode::CrimeRate; break;
                    case SDLK_7: currentCityOverlay_ = DuneCity::CityOverlayMode::Population; break;
                    case SDLK_8: currentCityOverlay_ = DuneCity::CityOverlayMode::WindTrapRadius; break;
                    default: break;
                }
            } else if (citySimEnabled_ && currentCursorMode == CursorMode_CityZone && selectListIndex < 3) {
                selectedZoneType_ = static_cast<DuneCity::ZoneType>(selectListIndex + 1);
            } else if(SDL_GetModState() & KMOD_CTRL) {
                pLocalPlayer->setGroupList(selectListIndex, selectedList);
                pInterface->updateObjectInterface();
            } else {
                std::set<Uint32>& groupList = pLocalPlayer->getGroupList(selectListIndex);

                // find out if we are choosing a group with all items already selected
                bool bEverythingWasSelected = (selectedList.size() == groupList.size());
                Coord averagePosition;
                for(Uint32 objectID : groupList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    bEverythingWasSelected = bEverythingWasSelected && pObject->isSelected();
                    averagePosition += pObject->getLocation();
                }

                if(groupList.empty() == false) {
                    averagePosition /= groupList.size();
                }


                if(SDL_GetModState() & KMOD_SHIFT) {
                    // we add the items from this list to the list of selected items
                } else {
                    // we replace the list of the selected items with the items from this list
                    unselectAll(selectedList);
                    selectedList.clear();
                    currentGame->selectionChanged();
                }

                // now we add the selected items
                for(Uint32 objectID : groupList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    if(pObject->getOwner() == pLocalHouse) {
                        pObject->setSelected(true);
                        selectedList.insert(pObject->getObjectID());
                        currentGame->selectionChanged();
                    }
                }

                if(bEverythingWasSelected && (groupList.empty() == false)) {
                    // we center around the newly selected units/structures
                    screenborder->setNewScreenCenter(averagePosition*TILESIZE);
                }
            }
            currentCursorMode = CursorMode_Normal;
        } break;

        case SDLK_KP_MINUS:
        case SDLK_MINUS: {
            if(gameType != GameType::CustomMultiplayer) {
                settings.gameOptions.gameSpeed = std::min(settings.gameOptions.gameSpeed+1,GAMESPEED_MAX);
                INIFile myINIFile(getConfigFilepath());
                myINIFile.setIntValue("Game Options","Game Speed", settings.gameOptions.gameSpeed);
                myINIFile.saveChangesTo(getConfigFilepath());
                currentGame->addToNewsTicker(fmt::sprintf(_("Game speed") + ": %d", settings.gameOptions.gameSpeed));
            }
        } break;

        case SDLK_KP_PLUS:
        case SDLK_PLUS:
        case SDLK_EQUALS: {
            if(gameType != GameType::CustomMultiplayer) {
                settings.gameOptions.gameSpeed = std::max(settings.gameOptions.gameSpeed-1,GAMESPEED_MIN);
                INIFile myINIFile(getConfigFilepath());
                myINIFile.setIntValue("Game Options","Game Speed", settings.gameOptions.gameSpeed);
                myINIFile.saveChangesTo(getConfigFilepath());
                currentGame->addToNewsTicker(fmt::sprintf(_("Game speed") + ": %d", settings.gameOptions.gameSpeed));
            }
        } break;

        case SDLK_b: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                onCityBudget();
            }
        } break;

        case SDLK_c: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                pInterface->toggleCityStatsOverlay();
            } else {
                setCursorMode(CursorMode_Capture);
            }
        } break;

        case SDLK_z: {
            if (!citySimEnabled_) {
                break;
            }
            if (currentCursorMode == CursorMode_CityZone) {
                setCursorMode(CursorMode_Normal);
            } else {
                setCursorMode(CursorMode_CityZone);
                selectedZoneType_ = DuneCity::ZoneType::Residential;
            }
        } break;

        case SDLK_a: {
            //set object to attack
            setCursorMode(CursorMode_Attack);
        } break;

        case SDLK_t: {
            bShowTime = !bShowTime;
        } break;

        case SDLK_ESCAPE: {
            if (currentCursorMode == CursorMode_CityZone || currentCursorMode == CursorMode_CityRoad) {
                setCursorMode(CursorMode_Normal);
            } else {
                onOptions();
            }
        } break;

        case SDLK_F1: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 0;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F2: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 1;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F3: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 2;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F4: {
            // skip a 30 seconds
            if(gameType != GameType::CustomMultiplayer || bReplay) {
                skipToGameCycle = gameCycleCount + (10*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F5: {
            // skip a 30 seconds
            if(gameType != GameType::CustomMultiplayer || bReplay) {
                skipToGameCycle = gameCycleCount + (30*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F6: {
            // skip 2 minutes
            if(gameType != GameType::CustomMultiplayer || bReplay) {
                skipToGameCycle = gameCycleCount + (120*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F7: {
            // Test: Fire disaster notification
            if (citySimEnabled_) {
                triggerFireDisaster();
            }
        } break;

        case SDLK_F8: {
            // Test: Sandstorm disaster notification
            if (citySimEnabled_) {
                triggerSandstormDisaster();
            }
        } break;

        case SDLK_F9: {
            // Test: Sandworm disaster notification
            if (citySimEnabled_) {
                triggerSandwormDisaster();
            }
        } break;

        case SDLK_F10: {
            soundPlayer->toggleSound();
        } break;

        case SDLK_F11: {
            musicPlayer->toggleSound();
        } break;

        case SDLK_F12: {
            bShowFPS = !bShowFPS;
        } break;

        case SDLK_o: {
            if((SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0) {
                selectAllOrnithopters();
            }
        } break;

        case SDLK_m: {
            //set object to move
            setCursorMode(CursorMode_Move);
        } break;

        case SDLK_g: {
            // select next construction yard
            std::set<Uint32> itemIDs;
            itemIDs.insert(Structure_ConstructionYard);
            selectNextStructureOfType(itemIDs);
        } break;

        case SDLK_f: {
            // select next factory
            std::set<Uint32> itemIDs;
            itemIDs.insert(Structure_Barracks);
            itemIDs.insert(Structure_WOR);
            itemIDs.insert(Structure_LightFactory);
            itemIDs.insert(Structure_HeavyFactory);
            itemIDs.insert(Structure_HighTechFactory);
            itemIDs.insert(Structure_StarPort);
            selectNextStructureOfType(itemIDs);
        } break;

        case SDLK_p: {
            if(SDL_GetModState() & KMOD_CTRL) {
                // fall through to SDLK_PRINT
            } else {
                // Place structure
                if(selectedList.size() == 1) {
                    ConstructionYard* pConstructionYard = dynamic_cast<ConstructionYard*>(objectManager.getObject(*selectedList.begin()));
                    if(pConstructionYard != nullptr) {
                        if(currentCursorMode == CursorMode_Placing) {
                            setCursorMode(CursorMode_Normal);
                        } else if(pConstructionYard->isWaitingToPlace()) {
                            setCursorMode(CursorMode_Placing);
                        }
                    }
                }

                break;  // do not fall through
            }

        } // fall through

        case SDLK_PRINTSCREEN:
        case SDLK_SYSREQ: {
            if(SDL_GetModState() & KMOD_SHIFT) {
                takePeriodicalScreenshots = !takePeriodicalScreenshots;
            } else {
                takeScreenshot();
            }
        } break;

        case SDLK_h: {
            for(Uint32 objectID : selectedList) {
                ObjectBase* pObject = objectManager.getObject(objectID);
                if(pObject->getItemID() == Unit_Harvester) {
                    static_cast<Harvester*>(pObject)->handleReturnClick();
                }
            }
        } break;


        case SDLK_r: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                if (currentCursorMode == CursorMode_CityRoad) {
                    setCursorMode(CursorMode_Normal);
                } else {
                    setCursorMode(CursorMode_CityRoad);
                    addToNewsTicker(_("Road tool selected: click rock or slab tiles to place roads."));
                }
            } else {
                for(Uint32 objectID : selectedList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    if(pObject->isAStructure()) {
                        static_cast<StructureBase*>(pObject)->handleRepairClick();
                    } else if(pObject->isAGroundUnit() && pObject->getHealth() < pObject->getMaxHealth()) {
                        static_cast<GroundUnit*>(pObject)->handleSendToRepairClick();
                    }
                }
            }
        } break;


        case SDLK_d: {
            setCursorMode(CursorMode_CarryallDrop);
        } break;

        case SDLK_u: {
            for(Uint32 objectID : selectedList) {
                ObjectBase* pObject = objectManager.getObject(objectID);
                if(pObject->isABuilder()) {
                    BuilderBase* pBuilder = static_cast<BuilderBase*>(pObject);
                    if(pBuilder->getHealth() >= pBuilder->getMaxHealth() && pBuilder->isAllowedToUpgrade()) {
                        pBuilder->handleUpgradeClick();
                    }
                }
            }
        } break;

        case SDLK_RETURN: {
            if(SDL_GetModState() & KMOD_ALT) {
                toogleFullscreen();
            } else {
                typingChatMessage = "";
                chatMode = true;
            }
        } break;

        case SDLK_TAB: {
            if(SDL_GetModState() & KMOD_ALT) {
                SDL_MinimizeWindow(window);
            }
        } break;

        case SDLK_SPACE: {
            bool isMultiplayer = (gameType == GameType::CustomMultiplayer);

            if(bPause) {
                resumeGame();
                const std::string message = _("Game resumed!");
                pInterface->getChatManager().addInfoMessage(message);
                if(isMultiplayer && pNetworkManager != nullptr) {
                    pNetworkManager->sendChatMessage(message);
                }
            } else {
                pauseGame();
                const std::string message = _("Game paused!");
                pInterface->getChatManager().addInfoMessage(message);
                if(isMultiplayer && pNetworkManager != nullptr) {
                    pNetworkManager->sendChatMessage(message);
                }
            }
        } break;

        default: {
        } break;
    }
}


bool Game::handlePlacementClick(int xPos, int yPos) {
    BuilderBase* pBuilder = nullptr;

    if(selectedList.size() == 1) {
        pBuilder = dynamic_cast<BuilderBase*>(objectManager.getObject(*selectedList.begin()));
    }

    if(!pBuilder) {
        return false;
    }

    int placeItem = pBuilder->getCurrentProducedItem();
    Coord structuresize = getStructureSize(placeItem);

            if(placeItem == Structure_Slab1) {
            if((currentGameMap->isWithinBuildRange(xPos, yPos, pBuilder->getOwner()))
                && (currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1, false, pBuilder->getOwner()))
                && (currentGameMap->getTile(xPos, yPos)->isConcrete() == false)) {
                getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
                //the user has tried to place and has been successful
                soundPlayer->playSound(Sound_PlaceStructure);
                setCursorMode(CursorMode_Normal);
                return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(_("@DUNE.ENG|135#Cannot place slab here."));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise
            return false;
        }
    } else if(placeItem == Structure_Slab4) {
        if( (currentGameMap->isWithinBuildRange(xPos, yPos, pBuilder->getOwner()) || currentGameMap->isWithinBuildRange(xPos+1, yPos, pBuilder->getOwner())
                || currentGameMap->isWithinBuildRange(xPos+1, yPos+1, pBuilder->getOwner()) || currentGameMap->isWithinBuildRange(xPos, yPos+1, pBuilder->getOwner()))
            && ((currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos+1, yPos, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos+1, yPos+1, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1+1, false, pBuilder->getOwner())))
            && ((currentGameMap->getTile(xPos, yPos)->isConcrete() == false) || (currentGameMap->getTile(xPos+1, yPos)->isConcrete() == false)
                || (currentGameMap->getTile(xPos, yPos+1)->isConcrete() == false) || (currentGameMap->getTile(xPos+1, yPos+1)->isConcrete() == false)) ) {

            getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
            //the user has tried to place and has been successful
            soundPlayer->playSound(Sound_PlaceStructure);
            setCursorMode(CursorMode_Normal);
            return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(_("@DUNE.ENG|135#Cannot place slab here."));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise
            return false;
        }
    } else {
        if(currentGameMap->okayToPlaceStructure(xPos, yPos, structuresize.x, structuresize.y, false, pBuilder->getOwner(), false, placeItem)) {
            getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
            //the user has tried to place and has been successful
            soundPlayer->playSound(Sound_PlaceStructure);
            setCursorMode(CursorMode_Normal);
            return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(fmt::sprintf(_("@DUNE.ENG|134#Cannot place %%s here."), resolveItemName(placeItem)));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise

            // is this building area only blocked by units?
            if(currentGameMap->okayToPlaceStructure(xPos, yPos, structuresize.x, structuresize.y, false, pBuilder->getOwner(), true, placeItem)) {
                // then we try to move all units outside the building area

                // generate a independent temporal random number generator as we are in input handling code (and outside game logic code)
                Random tempRandomGen(getGameCycleCount());

                for(int y = yPos; y < yPos + structuresize.y; y++) {
                    for(int x = xPos; x < xPos + structuresize.x; x++) {
                        Tile* pTile = currentGameMap->getTile(x,y);
                        if(pTile->hasANonInfantryGroundObject()) {
                            ObjectBase* pObject = pTile->getNonInfantryGroundObject();
                            if(pObject->isAUnit() && pObject->getOwner() == pBuilder->getOwner()) {
                                UnitBase* pUnit = static_cast<UnitBase*>(pObject);
                                Coord newDestination = currentGameMap->findDeploySpot(pUnit, Coord(xPos, yPos), tempRandomGen, pUnit->getLocation(), structuresize);
                                pUnit->handleMoveClick(newDestination.x, newDestination.y);
                            }
                        } else if(pTile->hasInfantry()) {
                            for(Uint32 objectID : pTile->getInfantryList()) {
                                InfantryBase* pInfantry = dynamic_cast<InfantryBase*>(getObjectManager().getObject(objectID));
                                if((pInfantry != nullptr) && (pInfantry->getOwner() == pBuilder->getOwner())) {
                                    Coord newDestination = currentGameMap->findDeploySpot(pInfantry, Coord(xPos, yPos), tempRandomGen, pInfantry->getLocation(), structuresize);
                                    pInfantry->handleMoveClick(newDestination.x, newDestination.y);
                                }
                            }
                        }
                    }
                }
            }

            return false;
        }
    }
}


bool Game::handleSelectedObjectsAttackClick(int xPos, int yPos) {
    UnitBase* pResponder = nullptr;
    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        House* pOwner = pObject->getOwner();
        if(pObject->isAUnit() && (pOwner == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleAttackClick(xPos,yPos);
        } else if((pObject->getItemID() == Structure_Palace) && ((pOwner->getHouseID() == HOUSE_HARKONNEN) || (pOwner->getHouseID() == HOUSE_SARDAUKAR))) {
            Palace* pPalace = static_cast<Palace*>(pObject);
            if(pPalace->isSpecialWeaponReady()) {
                pPalace->handleDeathhandClick(xPos, yPos);
            }
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}

bool Game::handleSelectedObjectsMoveClick(int xPos, int yPos) {
    UnitBase* pResponder = nullptr;

    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleMoveClick(xPos,yPos);
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}

/**
    New method for transporting units quickly using carryalls
**/
bool Game::handleSelectedObjectsRequestCarryallDropClick(int xPos, int yPos) {

    UnitBase* pResponder = nullptr;

    /*
        If manual carryall mode isn't enabled then turn this off...
    */
    if(!getGameInitSettings().getGameOptions().manualCarryallDrops) {
        setCursorMode(CursorMode_Normal);
        return false;
    }

    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if (pObject->isAGroundUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleRequestCarryallDropClick(xPos,yPos);
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}



bool Game::handleSelectedObjectsCaptureClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return false;
    }

    StructureBase* pStructure = dynamic_cast<StructureBase*>(pTile->getGroundObject());
    if((pStructure != nullptr) && (pStructure->canBeCaptured()) && (pStructure->getOwner()->getTeamID() != pLocalHouse->getTeamID())) {
        InfantryBase* pResponder = nullptr;

        for(Uint32 objectID : selectedList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if (pObject->isInfantry() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                pResponder = static_cast<InfantryBase*>(pObject);
                pResponder->handleCaptureClick(xPos,yPos);
            }
        }

        setCursorMode(CursorMode_Normal);
        if(pResponder) {
            pResponder->playConfirmSound();
            return true;
        } else {
            return false;
        }
    }

    return false;
}

void Game::handleCityZonePlacementClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return;
    }

    if (!pTile->isRock() && pTile->getType() != Terrain_Slab) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if (pTile->hasANonInfantryGroundObject()) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_CITY_PLACE_ZONE,
                                   static_cast<uint32_t>(xPos),
                                   static_cast<uint32_t>(yPos),
                                   static_cast<uint32_t>(selectedZoneType_)));

    soundPlayer->playSound(Sound_PlaceStructure);
}

void Game::handleCityRoadPlacementClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return;
    }

    if(pTile->isMountain()) {
        addToNewsTicker(_("Cannot place road on mountain."));
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if(pTile->hasAStructure()) {
        addToNewsTicker(_("Cannot place road on structure."));
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if(pTile->isRoad()) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_CITY_TOOL,
                                   static_cast<uint32_t>(xPos),
                                   static_cast<uint32_t>(yPos),
                                   static_cast<uint32_t>(DuneCity::CityTool_Road)));

    soundPlayer->playSound(Sound_PlaceStructure);
}

bool Game::handleSelectedObjectsActionClick(int xPos, int yPos) {
    //let unit handle right click on map or target
    ObjectBase  *pResponder = nullptr;
    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject->getOwner() == pLocalHouse && pObject->isRespondable()) {
            pObject->handleActionClick(xPos, yPos);

            //if this object obey the command
            if((pResponder == nullptr) && pObject->isRespondable())
                pResponder = pObject;
        }
    }

    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}


void Game::drawCityOverlay(int x1, int y1, int x2, int y2) {
    if (!citySimulation_ || currentCityOverlay_ == DuneCity::CityOverlayMode::None) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    auto drawOverlayBlock = [&](int tileX, int tileY, int blockSize, uint8_t value, 
                                 uint8_t r1, uint8_t g1, uint8_t b1, 
                                 uint8_t r2, uint8_t g2, uint8_t b2) {
        float normalized = value / 255.0f;
        uint8_t r = static_cast<uint8_t>(r1 + (r2 - r1) * normalized);
        uint8_t g = static_cast<uint8_t>(g1 + (g2 - g1) * normalized);
        uint8_t b = static_cast<uint8_t>(b1 + (b2 - b1) * normalized);
        uint8_t a = 120;

        int zoomed_tilesize = world2zoomedWorld(TILESIZE);
        int blockPixels = blockSize * zoomed_tilesize;

        SDL_Rect overlayRect = {
            screenborder->world2screenX(tileX * TILESIZE),
            screenborder->world2screenY(tileY * TILESIZE),
            blockPixels,
            blockPixels
        };

        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &overlayRect);
    };


    switch (currentCityOverlay_) {
        case DuneCity::CityOverlayMode::PowerGrid: {
            // The per-tile city power grid (CitySimulation::getPowerGridMap)
            // is stubbed — every cell reads 0.  Instead, show a simple
            // house-level power status: green overlay on zone/conductive tiles
            // when the house has power, red when it doesn't.
            bool housePower = pLocalHouse->hasPower();
            uint8_t level = housePower ? 200 : 200;
            int rOn = 0, gOn = 255, bOn = 0;     // green = powered
            int rOff = 255, gOff = 0, bOff = 0;  // red   = no power
            const auto& powerMap = citySimulation_->getPowerGridMap();
            int blockSize = powerMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    if (!currentGameMap->tileExists(x, y)) continue;
                    Tile* t = currentGameMap->getTile(x, y);
                    if (t->hasCityZone() || t->isRoad()) {
                        if (housePower)
                            drawOverlayBlock(x, y, blockSize, level, rOn, gOn, bOn, rOn, gOn, bOn);
                        else
                            drawOverlayBlock(x, y, blockSize, level, rOff, gOff, bOff, rOff, gOff, bOff);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::TrafficDensity: {
            const auto& trafficMap = citySimulation_->getTrafficDensityMap();
            int blockSize = trafficMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t traffic = trafficMap.worldGet(x, y);
                    if (traffic > 0) {
                        drawOverlayBlock(x, y, blockSize, traffic, 0, 255, 0, 255, 0, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::Pollution: {
            const auto& pollutionMap = citySimulation_->getPollutionDensityMap();
            int blockSize = pollutionMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t pollution = pollutionMap.worldGet(x, y);
                    if (pollution > 0) {
                        drawOverlayBlock(x, y, blockSize, pollution, 0, 255, 0, 150, 0, 200);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::LandValue: {
            const auto& landValueMap = citySimulation_->getLandValueMap();
            int blockSize = landValueMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t landValue = landValueMap.worldGet(x, y);
                    if (landValue > 0) {
                        drawOverlayBlock(x, y, blockSize, landValue, 255, 0, 0, 0, 255, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::CrimeRate: {
            const auto& crimeMap = citySimulation_->getCrimeRateMap();
            int blockSize = crimeMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t crime = crimeMap.worldGet(x, y);
                    if (crime > 0) {
                        drawOverlayBlock(x, y, blockSize, crime, 0, 255, 0, 255, 0, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::Population: {
            const auto& popMap = citySimulation_->getPopulationDensityMap();
            int blockSize = popMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t pop = popMap.worldGet(x, y);
                    if (pop > 0) {
                        drawOverlayBlock(x, y, blockSize, pop, 50, 50, 50, 255, 255, 255);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::WindTrapRadius: {
            // Draw circles around Wind Traps showing their power radius
            // Wind Trap power radius: typically 12-15 tiles (from Micropolis)
            constexpr int WINDTRAP_POWER_RADIUS = 12;

            for (const auto* pStructure : structureList) {
                if (pStructure->getItemID() == Structure_WindTrap) {
                    int wx = pStructure->getX();
                    int wy = pStructure->getY();

                    // Draw radius as a filled circle with transparency
                    for (int dx = -WINDTRAP_POWER_RADIUS; dx <= WINDTRAP_POWER_RADIUS; dx++) {
                        for (int dy = -WINDTRAP_POWER_RADIUS; dy <= WINDTRAP_POWER_RADIUS; dy++) {
                            int tx = wx + dx;
                            int ty = wy + dy;

                            if (!currentGameMap->tileExists(tx, ty)) continue;

                            // Check if within circular radius (center is 2x2, so adjust)
                            int distSq = dx*dx + dy*dy;
                            if (distSq <= WINDTRAP_POWER_RADIUS * WINDTRAP_POWER_RADIUS) {
                                // Draw at full opacity at edge, fading toward center
                                int alpha = 50 + (distSq * 150) / (WINDTRAP_POWER_RADIUS * WINDTRAP_POWER_RADIUS);
                                alpha = std::min(200, alpha);

                                // Draw as single tiles in the overlay
                                drawOverlayBlock(tx, ty, 1, 255, 0, 150, 255, 0, 255, alpha);
                            }
                        }
                    }
                }
            }
        } break;
        default:
            break;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void Game::drawCityRoads(int x1, int y1, int x2, int y2) {
    if (!citySimulation_) {
        return;
    }

    const int zoomedTileSize = world2zoomedWorld(TILESIZE);
    const int centerInset = std::max(2, zoomedTileSize / 3);
    const int laneHalfWidth = std::max(1, zoomedTileSize / 10);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    auto drawRoadStub = [&](int tileX, int tileY, int dx, int dy) {
        SDL_Rect stubRect{};
        const int centerX = screenborder->world2screenX(tileX * TILESIZE) + zoomedTileSize / 2;
        const int centerY = screenborder->world2screenY(tileY * TILESIZE) + zoomedTileSize / 2;

        if (dx != 0) {
            stubRect.y = centerY - laneHalfWidth;
            stubRect.h = laneHalfWidth * 2;
            if (dx < 0) {
                stubRect.x = centerX - centerInset;
                stubRect.w = centerInset;
            } else {
                stubRect.x = centerX;
                stubRect.w = centerInset;
            }
        } else {
            stubRect.x = centerX - laneHalfWidth;
            stubRect.w = laneHalfWidth * 2;
            if (dy < 0) {
                stubRect.y = centerY - centerInset;
                stubRect.h = centerInset;
            } else {
                stubRect.y = centerY;
                stubRect.h = centerInset;
            }
        }

        SDL_RenderFillRect(renderer, &stubRect);
    };

    currentGameMap->for_each(x1, y1, x2, y2, [&](Tile& tile) {
        if (!tile.isRoad() || tile.hasCityZone()) {
            return;
        }

        if (!debug && !tile.isExploredByTeam(pLocalHouse->getTeamID())) {
            return;
        }

        const int tileX = tile.getLocation().x;
        const int tileY = tile.getLocation().y;
        const int screenX = screenborder->world2screenX(tileX * TILESIZE);
        const int screenY = screenborder->world2screenY(tileY * TILESIZE);

        SDL_SetRenderDrawColor(renderer, 120, 92, 56, 210);
        SDL_Rect centerRect = {
            screenX + centerInset,
            screenY + centerInset,
            zoomedTileSize - 2 * centerInset,
            zoomedTileSize - 2 * centerInset
        };
        SDL_RenderFillRect(renderer, &centerRect);

        SDL_SetRenderDrawColor(renderer, 196, 168, 120, 210);
        if (currentGameMap->tileExists(tileX, tileY - 1) && currentGameMap->getTile(tileX, tileY - 1)->isRoad()) {
            drawRoadStub(tileX, tileY, 0, -1);
        }
        if (currentGameMap->tileExists(tileX + 1, tileY) && currentGameMap->getTile(tileX + 1, tileY)->isRoad()) {
            drawRoadStub(tileX, tileY, 1, 0);
        }
        if (currentGameMap->tileExists(tileX, tileY + 1) && currentGameMap->getTile(tileX, tileY + 1)->isRoad()) {
            drawRoadStub(tileX, tileY, 0, 1);
        }
        if (currentGameMap->tileExists(tileX - 1, tileY) && currentGameMap->getTile(tileX - 1, tileY)->isRoad()) {
            drawRoadStub(tileX, tileY, -1, 0);
        }

        if (!hasVisibleRoadNeighbor(tileX, tileY)) {
            SDL_SetRenderDrawColor(renderer, 196, 168, 120, 210);
            SDL_Rect fallbackRect = {
                screenX + zoomedTileSize / 2 - laneHalfWidth,
                screenY + centerInset,
                laneHalfWidth * 2,
                zoomedTileSize - 2 * centerInset
            };
            SDL_RenderFillRect(renderer, &fallbackRect);
        }
    });

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void Game::drawCityPlacementHint() {
    if (currentCursorMode != CursorMode_CityRoad) {
        return;
    }

    const std::string hint = std::string(DuneCity::getRoadPlacementModeLabel()) + "\nShift+R toggle · Click to place · Esc cancels";
    sdl2::texture_ptr hintTexture = pFontManager->createTextureWithMultilineText(hint, COLOR_WHITE, 12, true);
    if (!hintTexture) {
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(hintTexture.get(), nullptr, nullptr, &texW, &texH);

    const int hintX = 20;
    const int hintY = std::max(90, topBarPos.h + 18);
    SDL_Rect bgRect = {hintX - 6, hintY - 6, texW + 12, texH + 12};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &bgRect);
    SDL_SetRenderDrawColor(renderer, 196, 168, 120, 220);
    SDL_RenderDrawRect(renderer, &bgRect);

    SDL_Rect textRect = {hintX, hintY, texW, texH};
    SDL_RenderCopy(renderer, hintTexture.get(), nullptr, &textRect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}


void Game::takeScreenshot() const {
    std::string screenshotFilename;
    int i = 1;
    do {
        screenshotFilename = "Screenshot" + std::to_string(i) + ".png";
        i++;
    } while(existsFile(screenshotFilename) == true);

    sdl2::surface_ptr pCurrentScreen = renderReadSurface(renderer);
    SavePNG(pCurrentScreen.get(), screenshotFilename.c_str());
    currentGame->addToNewsTicker(_("Screenshot saved") + ": '" + screenshotFilename + "'");
}

void Game::triggerFireDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Fire, "Fire! Extinguish immediately!", 8, 3);
    }
}

void Game::triggerSandstormDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Sandstorm, "Sandstorm approaching!", 12, 5);
    }
}

void Game::triggerSandwormDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Sandworm, "Sandworm spotted!", 6, 2);
    }
}

void Game::selectNextStructureOfType(const std::set<Uint32>& itemIDs) {
    bool bSelectNext = true;

    if(selectedList.size() == 1) {
        ObjectBase* pObject = getObjectManager().getObject(*selectedList.begin());
        if((pObject != nullptr) && (itemIDs.count(pObject->getItemID()) == 1)) {
            bSelectNext = false;
        }
    }

    StructureBase* pStructure2Select = nullptr;

    for(StructureBase* pStructure : structureList) {
        if(bSelectNext) {
            if( (itemIDs.count(pStructure->getItemID()) == 1) && (pStructure->getOwner() == pLocalHouse) ) {
                pStructure2Select = pStructure;
                break;
            }
        } else {
            if(selectedList.size() == 1 && pStructure->isSelected()) {
                bSelectNext = true;
            }
        }
    }

    if(pStructure2Select == nullptr) {
        // start over at the beginning
        for(StructureBase* pStructure : structureList) {
            if( (itemIDs.count(pStructure->getItemID()) == 1) && (pStructure->getOwner() == pLocalHouse) && !pStructure->isSelected() ) {
                pStructure2Select = pStructure;
                break;
            }
        }
    }

    if(pStructure2Select != nullptr) {
        unselectAll(selectedList);
        selectedList.clear();

        pStructure2Select->setSelected(true);
        selectedList.insert(pStructure2Select->getObjectID());
        currentGame->selectionChanged();

        // we center around the newly selected construction yard
        screenborder->setNewScreenCenter(pStructure2Select->getLocation()*TILESIZE);
    }
}

int Game::getGameSpeed() const {
    if(gameType == GameType::CustomMultiplayer) {
        return gameInitSettings.getGameOptions().gameSpeed;
    } else {
        return settings.gameOptions.gameSpeed;
    }
}

bool Game::handleNetworkUpdates() {
    if(pNetworkManager == nullptr) {
        return false;
    }
    
    pNetworkManager->update();
    bool bWaitForNetwork = false;
    
    // Check for network delays
    for(const std::string& playername : pNetworkManager->getConnectedPeers()) {
        HumanPlayer* pPlayer = dynamic_cast<HumanPlayer*>(getPlayerByName(playername));
        if(pPlayer != nullptr && pPlayer->nextExpectedCommandsCycle <= gameCycleCount) {
            bWaitForNetwork = true;
            break;
        }
    }
    
    if(bWaitForNetwork) {
        if(startWaitingForOtherPlayersTime == 0) {
            startWaitingForOtherPlayersTime = SDL_GetTicks();
        } else if(SDL_GetTicks() - startWaitingForOtherPlayersTime > 1000) {
            if(pWaitingForOtherPlayers == nullptr) {
                pWaitingForOtherPlayers = std::make_unique<WaitingForOtherPlayers>();
                bMenu = true;
            }
        }
        SDL_Delay(1);  // Reduced from 10ms to 1ms to improve host performance
    } else {
        startWaitingForOtherPlayersTime = 0;
        if(pWaitingForOtherPlayers != nullptr) {
            pWaitingForOtherPlayers.reset();
            if(pInGameMenu == nullptr && pInGameMentat == nullptr) {
                bMenu = false;
            }
        }
    }
    
    return bWaitForNetwork;
}

void Game::dumpCombatStats() {
    SDL_Log("[Combat Stats] ==================== 30-SECOND ANTI-AIR ANALYSIS ====================");
    
    // ===== ROCKET TURRETS =====
    SDL_Log("[Combat Stats] ");
    SDL_Log("[Combat Stats] === ROCKET TURRETS vs ORNITHOPTERS ===");
    SDL_Log("[Combat Stats] TARGETING:");
    SDL_Log("[Combat Stats]   Ornithopters Acquired as Target:  %d", combatStats.rocketTurretTargetsOrni);
    SDL_Log("[Combat Stats]   Target Lost (out of range/dead):  %d", combatStats.rocketTurretLosesOrniTarget);
    
    SDL_Log("[Combat Stats] FIRING OPPORTUNITIES:");
    const int totalOpportunities = combatStats.orniInRangeCorrectAngle;
    SDL_Log("[Combat Stats]   Ornithopter in Range + Correct Angle: %d", combatStats.orniInRangeCorrectAngle);
    SDL_Log("[Combat Stats]   Ornithopter in Range BUT Wrong Angle: %d", combatStats.orniInRangeButWrongAngle);
    SDL_Log("[Combat Stats]   Actually Fired:                       %d", combatStats.rocketTurretFiresOnOrni);
    SDL_Log("[Combat Stats]   Blocked by Weapon Timer:              %d", combatStats.rocketTurretFireBlocked);
    
    if(totalOpportunities > 0) {
        const double fireRate = static_cast<double>(combatStats.rocketTurretFiresOnOrni) / totalOpportunities * 100.0;
        SDL_Log("[Combat Stats]   Fire Success Rate: %.1f%% (%d fired / %d opportunities)", 
                fireRate, combatStats.rocketTurretFiresOnOrni, totalOpportunities);
    }
    
    SDL_Log("[Combat Stats] ROCKET PERFORMANCE:");
    SDL_Log("[Combat Stats]   Rockets Spawned:          %d", combatStats.turretRocketsSpawned);
    SDL_Log("[Combat Stats]   Proximity Detonations:    %d", combatStats.turretRocketsProximityDetonated);
    SDL_Log("[Combat Stats]   Timer Expirations:        %d", combatStats.turretRocketsExpired);
    SDL_Log("[Combat Stats]   Rockets Hit Ornithopter:  %d", combatStats.turretRocketsHitOrni);
    SDL_Log("[Combat Stats]   Rockets Killed Ornithopter: %d", combatStats.turretRocketsKillOrni);
    
    if(combatStats.turretRocketsSpawned > 0) {
        const double hitRate = static_cast<double>(combatStats.turretRocketsHitOrni) / combatStats.turretRocketsSpawned * 100.0;
        const double killRate = static_cast<double>(combatStats.turretRocketsKillOrni) / combatStats.turretRocketsSpawned * 100.0;
        SDL_Log("[Combat Stats]   Hit Rate:  %.1f%% (%d hits / %d rockets)", 
                hitRate, combatStats.turretRocketsHitOrni, combatStats.turretRocketsSpawned);
        SDL_Log("[Combat Stats]   Kill Rate: %.1f%% (%d kills / %d rockets)", 
                killRate, combatStats.turretRocketsKillOrni, combatStats.turretRocketsSpawned);
    }
    
    // ===== LAUNCHER UNITS =====
    SDL_Log("[Combat Stats] ");
    SDL_Log("[Combat Stats] === LAUNCHER/DEVIATOR UNITS vs ORNITHOPTERS ===");
    SDL_Log("[Combat Stats] TARGETING:");
    SDL_Log("[Combat Stats]   Ornithopters Acquired as Target:  %d", combatStats.launcherTargetsOrni);
    SDL_Log("[Combat Stats]   Shots Fired at Ornithopters:      %d", combatStats.launcherFiresOnOrni);
    
    SDL_Log("[Combat Stats] ROCKET PERFORMANCE:");
    SDL_Log("[Combat Stats]   Rockets Spawned:          %d", combatStats.launcherRocketsSpawned);
    SDL_Log("[Combat Stats]   Timer Expirations:        %d", combatStats.launcherRocketsExpired);
    SDL_Log("[Combat Stats]   Rockets Hit Ornithopter:  %d", combatStats.launcherRocketsHitOrni);
    SDL_Log("[Combat Stats]   Rockets Killed Ornithopter: %d", combatStats.launcherRocketsKillOrni);
    
    if(combatStats.launcherRocketsSpawned > 0) {
        const double hitRate = static_cast<double>(combatStats.launcherRocketsHitOrni) / combatStats.launcherRocketsSpawned * 100.0;
        const double killRate = static_cast<double>(combatStats.launcherRocketsKillOrni) / combatStats.launcherRocketsSpawned * 100.0;
        SDL_Log("[Combat Stats]   Hit Rate:  %.1f%% (%d hits / %d rockets)", 
                hitRate, combatStats.launcherRocketsHitOrni, combatStats.launcherRocketsSpawned);
        SDL_Log("[Combat Stats]   Kill Rate: %.1f%% (%d kills / %d rockets)", 
                killRate, combatStats.launcherRocketsKillOrni, combatStats.launcherRocketsSpawned);
    }
    
    // Reset stats
    combatStats.rocketTurretTargetsOrni = 0;
    combatStats.rocketTurretLosesOrniTarget = 0;
    combatStats.orniInRangeButWrongAngle = 0;
    combatStats.orniInRangeCorrectAngle = 0;
    combatStats.rocketTurretFiresOnOrni = 0;
    combatStats.rocketTurretFireBlocked = 0;
    combatStats.turretRocketsSpawned = 0;
    combatStats.turretRocketsHitOrni = 0;
    combatStats.turretRocketsKillOrni = 0;
    combatStats.turretRocketsExpired = 0;
    combatStats.turretRocketsProximityDetonated = 0;
    combatStats.launcherTargetsOrni = 0;
    combatStats.launcherFiresOnOrni = 0;
    combatStats.launcherRocketsSpawned = 0;
    combatStats.launcherRocketsHitOrni = 0;
    combatStats.launcherRocketsKillOrni = 0;
    combatStats.launcherRocketsExpired = 0;
    SDL_Log("[Combat Stats] ================================================================================");
}
