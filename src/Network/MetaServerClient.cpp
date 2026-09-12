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

#include <Network/MetaServerClient.h>

#include <Network/ENetHttp.h>
#include <Network/ENetHelper.h>

#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <mmath.h>

#include <config.h>

#include <sstream>
#include <iostream>
#include <map>

// Helper function to get local IP address
static std::string getLocalIPAddress() {
    // Get local IP by checking which interface would be used to reach the internet
    // We create a UDP socket and "connect" to a public IP (doesn't send data)
    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) {
        SDL_Log("getLocalIPAddress: Failed to create socket");
        return "";
    }
    
    // "Connect" to Google's DNS (8.8.8.8) - this doesn't send data but tells
    // the OS to select the appropriate network interface for routing
    ENetAddress remoteAddress;
    enet_address_set_host(&remoteAddress, "8.8.8.8");
    remoteAddress.port = 53;
    
    if (enet_socket_connect(socket, &remoteAddress) < 0) {
        SDL_Log("getLocalIPAddress: Failed to connect socket for routing check");
        enet_socket_destroy(socket);
        return "";
    }
    
    // Get the local address that was selected for this connection
    ENetAddress localAddress;
    if (enet_socket_get_address(socket, &localAddress) == 0) {
        std::string localIP = Address2String(localAddress);
        enet_socket_destroy(socket);
        SDL_Log("getLocalIPAddress: Detected local IP: %s", localIP.c_str());
        return localIP;
    }
    
    SDL_Log("getLocalIPAddress: Failed to get local address from socket");
    enet_socket_destroy(socket);
    return "";
}


MetaServerClient::MetaServerClient(const std::string& metaServerURL)
 : metaServerURL(metaServerURL) {

    availableMetaServerCommandsSemaphore = SDL_CreateSemaphore(0);
    if(availableMetaServerCommandsSemaphore == nullptr) {
        THROW(std::runtime_error, "Unable to create semaphore");
    }

    sharedDataMutex = SDL_CreateMutex();
    if(sharedDataMutex == nullptr) {
        THROW(std::runtime_error, "Unable to create mutex");
    }

    connectionThread = SDL_CreateThread(connectionThreadMain, nullptr, (void*) this);
    if(connectionThread == nullptr) {
        THROW(std::runtime_error, "Unable to create thread");
    }
}


MetaServerClient::~MetaServerClient() {

    stopAnnounce();

    enqueueMetaServerCommand(std::make_unique<MetaServerExit>());

    SDL_WaitThread(connectionThread, nullptr);

    SDL_DestroyMutex(sharedDataMutex);

    SDL_DestroySemaphore(availableMetaServerCommandsSemaphore);
}


void MetaServerClient::startAnnounce(const std::string& serverName, int serverPort, const std::string& mapName, Uint8 numPlayers, Uint8 maxPlayers,
                                     const std::string& modName, const std::string& modVersion, uint16_t stunPort) {

    stopAnnounce();

    this->serverName = serverName;
    this->serverPort = serverPort;
    this->secret = std::to_string(getRandomInt()) + std::to_string(getRandomInt());
    this->mapName = mapName;
    this->numPlayers = numPlayers;
    this->maxPlayers = maxPlayers;
    this->modName = modName;
    this->modVersion = modVersion;
    this->stunPort = stunPort;
    this->sessionId = "";  // Will be set by response

    enqueueMetaServerCommand(std::make_unique<MetaServerAdd>(serverName, serverPort, secret, mapName, numPlayers, maxPlayers, modName, modVersion, stunPort));
    lastAnnounceUpdate = SDL_GetTicks();
}


void MetaServerClient::updateAnnounce(Uint8 numPlayers) {
    if(serverPort > 0) {
        this->numPlayers = numPlayers;
        enqueueMetaServerCommand(std::make_unique<MetaServerUpdate>(serverName, serverPort, secret, mapName, numPlayers, maxPlayers, modName, modVersion));
        lastAnnounceUpdate = SDL_GetTicks();
    }
}


void MetaServerClient::stopAnnounce() {
    if(serverPort != 0) {

        enqueueMetaServerCommand(std::make_unique<MetaServerRemove>(serverPort, secret));

        serverName = "";
        serverPort = 0;
        secret = "";
        mapName = "";
        numPlayers = 0;
        maxPlayers = 0;
        modName = "vanilla";
        modVersion = "";
    }
}

void MetaServerClient::announceGameStart(const std::string& mapName, const std::string& modName, const std::string& players) {
    // Only announce if we have an active game server announcement
    if(secret.empty()) {
        SDL_Log("MetaServerClient::announceGameStart - No active game, skipping");
        return;
    }
    
    SDL_Log("MetaServerClient::announceGameStart - map=%s, mod=%s, players=%s", 
            mapName.c_str(), modName.c_str(), players.c_str());
    
    enqueueMetaServerCommand(std::make_unique<MetaServerGameStart>(secret, mapName, modName, players, VERSIONSTRING));
}

void MetaServerClient::reportGameStats(const std::string& phase, const std::string& matchID, const std::string& stats) {
    if ((phase != "start" && phase != "end") || matchID.empty() || stats.empty()) {
        SDL_Log("MetaServerClient::reportGameStats - Invalid analytics event");
        return;
    }
    // The server enforces the same bound. Native clients use POST, so this is
    // a storage bound rather than a request-line constraint. End events use
    // sparse per-item rows and normally remain far below the ceiling.
    if (stats.size() > 64 * 1024) {
        SDL_Log("MetaServerClient::reportGameStats - Summary exceeds 64 KiB; skipping");
        return;
    }
    enqueueMetaServerCommand(std::make_unique<MetaServerGameStats>(phase, matchID, stats));
}

// NAT Traversal / Hole Punch methods (synchronous)

std::string MetaServerClient::requestHolePunch(const std::string& sessionId, uint16_t stunPort) {
    std::map<std::string, std::string> parameters;
    parameters["command"] = "punch_request";
    parameters["session_id"] = sessionId;
    parameters["stun_port"] = std::to_string(stunPort);
    
    try {
        std::string result = loadFromHttp(metaServerURL, parameters);
        
        // Parse: OK\n<client_id>\n
        std::istringstream stream(result);
        std::string status;
        std::getline(stream, status);
        
        // Trim CR
        if (!status.empty() && status.back() == '\r') status.pop_back();
        
        if (status == "OK") {
            std::string clientId;
            std::getline(stream, clientId);
            if (!clientId.empty() && clientId.back() == '\r') clientId.pop_back();
            SDL_Log("MetaServerClient::requestHolePunch - got client_id: %s", clientId.c_str());
            return clientId;
        } else {
            SDL_Log("MetaServerClient::requestHolePunch - failed: %s", result.c_str());
            return "";
        }
    } catch (std::exception& e) {
        SDL_Log("MetaServerClient::requestHolePunch - exception: %s", e.what());
        return "";
    }
}

bool MetaServerClient::pollPunchStatus(const std::string& sessionId, const std::string& clientId,
                                       std::string& hostIP, uint16_t& hostPort, int& waitSeconds) {
    std::map<std::string, std::string> parameters;
    parameters["command"] = "punch_status";
    parameters["session_id"] = sessionId;
    parameters["client_id"] = clientId;
    
    try {
        std::string result = loadFromHttp(metaServerURL, parameters);
        
        // Parse: WAITING\n or READY\n<host_ip>\n<host_port>\n<punch_in_seconds>\n
        std::istringstream stream(result);
        std::string status;
        std::getline(stream, status);
        
        // Trim CR
        if (!status.empty() && status.back() == '\r') status.pop_back();
        
        if (status == "READY") {
            std::string line;
            
            std::getline(stream, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            hostIP = line;
            
            std::getline(stream, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            hostPort = static_cast<uint16_t>(std::stoi(line));
            
            std::getline(stream, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            waitSeconds = std::stoi(line);
            
            SDL_Log("MetaServerClient::pollPunchStatus - READY: %s:%d in %d sec", 
                    hostIP.c_str(), hostPort, waitSeconds);
            return true;
        } else if (status == "WAITING") {
            return false;
        } else {
            SDL_Log("MetaServerClient::pollPunchStatus - unexpected: %s", result.c_str());
            return false;
        }
    } catch (std::exception& e) {
        SDL_Log("MetaServerClient::pollPunchStatus - exception: %s", e.what());
        return false;
    }
}

bool MetaServerClient::pollPunchRequests(std::vector<std::tuple<std::string, std::string, uint16_t>>& requests) {
    if (secret.empty()) {
        return false;  // Not hosting
    }
    
    std::map<std::string, std::string> parameters;
    parameters["command"] = "punch_poll";
    parameters["secret"] = secret;
    
    try {
        std::string result = loadFromHttp(metaServerURL, parameters);
        
        // Parse: OK\n[<client_id>\t<client_ip>\t<client_port>\n...]
        std::istringstream stream(result);
        std::string status;
        std::getline(stream, status);
        
        // Trim CR
        if (!status.empty() && status.back() == '\r') status.pop_back();
        
        if (status != "OK") {
            SDL_Log("MetaServerClient::pollPunchRequests - failed: %s", result.c_str());
            return false;
        }
        
        requests.clear();
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            
            std::vector<std::string> parts = splitStringToStringVector(line, "\\t");
            if (parts.size() >= 3) {
                std::string clientId = parts[0];
                std::string clientIP = parts[1];
                uint16_t clientPort = static_cast<uint16_t>(std::stoi(parts[2]));
                requests.push_back(std::make_tuple(clientId, clientIP, clientPort));
                SDL_Log("MetaServerClient::pollPunchRequests - request from %s:%d (id: %s)", 
                        clientIP.c_str(), clientPort, clientId.c_str());
            }
        }
        
        return true;
    } catch (std::exception& e) {
        SDL_Log("MetaServerClient::pollPunchRequests - exception: %s", e.what());
        return false;
    }
}

bool MetaServerClient::signalPunchReady(const std::string& clientId) {
    if (secret.empty()) {
        return false;  // Not hosting
    }
    
    std::map<std::string, std::string> parameters;
    parameters["command"] = "punch_ready";
    parameters["secret"] = secret;
    parameters["client_id"] = clientId;
    
    try {
        std::string result = loadFromHttp(metaServerURL, parameters);
        
        // Parse: OK\n or ERROR\n<message>
        std::istringstream stream(result);
        std::string status;
        std::getline(stream, status);
        
        // Trim CR
        if (!status.empty() && status.back() == '\r') status.pop_back();
        
        if (status == "OK") {
            SDL_Log("MetaServerClient::signalPunchReady - signaled ready for %s", clientId.c_str());
            return true;
        } else {
            SDL_Log("MetaServerClient::signalPunchReady - failed: %s", result.c_str());
            return false;
        }
    } catch (std::exception& e) {
        SDL_Log("MetaServerClient::signalPunchReady - exception: %s", e.what());
        return false;
    }
}


void MetaServerClient::update() {

    if(pOnGameServerInfoList) {
        // someone is waiting for the list

        if(SDL_GetTicks() - lastServerInfoListUpdate > SERVERLIST_UPDATE_INTERVAL) {
            enqueueMetaServerCommand(std::make_unique<MetaServerList>());
            lastServerInfoListUpdate = SDL_GetTicks();
        }

    }

    if(serverPort != 0) {
        if(SDL_GetTicks() - lastAnnounceUpdate > GAMESERVER_UPDATE_INTERVAL) {
            enqueueMetaServerCommand(std::make_unique<MetaServerUpdate>(serverName, serverPort, secret, mapName, numPlayers, maxPlayers, modName, modVersion));
            lastAnnounceUpdate = SDL_GetTicks();
        }
    }


    int errorCause = 0;
    std::string errorMsg;
    bool bTmpUpdatedGameServerInfoList = false;
    std::list<GameServerInfo> tmpGameServerInfoList;

    SDL_LockMutex(sharedDataMutex);

    if(metaserverErrorCause != 0) {
        errorCause = metaserverErrorCause;
        errorMsg = metaserverError;
        metaserverErrorCause = 0;
        metaserverError = "";
    }

    if(bUpdatedGameServerInfoList == true) {
        tmpGameServerInfoList = gameServerInfoList;
        bTmpUpdatedGameServerInfoList = true;
        bUpdatedGameServerInfoList = false;
    }

    SDL_UnlockMutex(sharedDataMutex);

    if(pOnMetaServerError && (errorCause != 0)) {
        pOnMetaServerError(errorCause, errorMsg);
    }

    if(pOnGameServerInfoList && bTmpUpdatedGameServerInfoList) {
        pOnGameServerInfoList(tmpGameServerInfoList);
    }
}


void MetaServerClient::enqueueMetaServerCommand(std::unique_ptr<MetaServerCommand> metaServerCommand) {

    SDL_LockMutex(sharedDataMutex);

    bool bInsert = true;

    for(const auto& pMetaServerCommand : metaServerCommandList) {
        if(*pMetaServerCommand == *metaServerCommand) {
            bInsert = false;
            break;
        }
    }

    if(bInsert == true) {
        metaServerCommandList.push_back(std::move(metaServerCommand));
    }

    SDL_UnlockMutex(sharedDataMutex);

    if(bInsert == true) {
        SDL_SemPost(availableMetaServerCommandsSemaphore);
    }
}


std::unique_ptr<MetaServerCommand> MetaServerClient::dequeueMetaServerCommand() {

    while(SDL_SemWait(availableMetaServerCommandsSemaphore) != 0) {
        ;   // try again in case of error
    }

    SDL_LockMutex(sharedDataMutex);

    std::unique_ptr<MetaServerCommand> nextMetaServerCommand = std::move(metaServerCommandList.front());
    metaServerCommandList.pop_front();

    SDL_UnlockMutex(sharedDataMutex);

    return nextMetaServerCommand;
}


void MetaServerClient::setErrorMessage(int errorCause, const std::string& errorMessage) {
    SDL_LockMutex(sharedDataMutex);

    if(metaserverErrorCause == 0) {
        metaserverErrorCause = errorCause;
        this->metaserverError = errorMessage;
    }

    SDL_UnlockMutex(sharedDataMutex);
}


void MetaServerClient::setNewGameServerInfoList(const std::list<GameServerInfo>& newGameServerInfoList) {
    SDL_LockMutex(sharedDataMutex);

    gameServerInfoList = newGameServerInfoList;
    bUpdatedGameServerInfoList = true;

    SDL_UnlockMutex(sharedDataMutex);
}


int MetaServerClient::connectionThreadMain(void* data) {
    MetaServerClient* pMetaServerClient = static_cast<MetaServerClient*>(data);

    while(true) {
        try {
            std::unique_ptr<MetaServerCommand> nextMetaServerCommand = pMetaServerClient->dequeueMetaServerCommand();

            switch(nextMetaServerCommand->type) {

                case METASERVERCOMMAND_ADD: {

                    MetaServerAdd* pMetaServerAdd = dynamic_cast<MetaServerAdd*>(nextMetaServerCommand.get());
                    if(!pMetaServerAdd) {
                        break;
                    }

                    std::map<std::string, std::string> parameters;

                    parameters["command"] = "add";
                    parameters["port"] = std::to_string(pMetaServerAdd->serverPort);
                    parameters["secret"] = pMetaServerAdd->secret;
                    parameters["gamename"] = pMetaServerAdd->serverName;
                    parameters["gameversion"] = VERSION;
                    parameters["mapname"] = pMetaServerAdd->mapName;
                    parameters["numplayers"] = std::to_string(pMetaServerAdd->numPlayers);
                    parameters["maxplayers"] = std::to_string(pMetaServerAdd->maxPlayers);
                    parameters["pwdprotected"] = "false";
                    parameters["modname"] = pMetaServerAdd->modName;
                    parameters["modversion"] = pMetaServerAdd->modVersion;
                    
                    // NAT traversal: Add STUN-discovered external port
                    if (pMetaServerAdd->stunPort > 0) {
                        parameters["stun_port"] = std::to_string(pMetaServerAdd->stunPort);
                        SDL_Log("Announcing game with STUN port: %d", pMetaServerAdd->stunPort);
                    }
                    
                    // Add local IP for NAT traversal (allows clients on same LAN to connect directly)
                    std::string localIP = getLocalIPAddress();
                    if (!localIP.empty()) {
                        parameters["localip"] = localIP;
                        SDL_Log("Announcing game with local IP: %s, mod: %s v%s", localIP.c_str(), 
                                pMetaServerAdd->modName.c_str(), pMetaServerAdd->modVersion.c_str());
                    }

                    std::string result;

                    try {
                        result = loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                    } catch(std::exception& e) {
                        pMetaServerClient->setErrorMessage(METASERVERCOMMAND_ADD, e.what());
                        break;
                    }

                    if(result.substr(0,2) != "OK") {
                        const std::string errorMsg = result.substr(result.find_first_not_of("\x0D\x0A",5), std::string::npos);

                        pMetaServerClient->setErrorMessage(METASERVERCOMMAND_ADD, errorMsg);
                    } else {
                        // Parse session_id from response (line 3)
                        // Response format: OK\n<secret>\n<session_id>\n
                        std::istringstream resultStream(result);
                        std::string line;
                        int lineNum = 0;
                        while (std::getline(resultStream, line)) {
                            lineNum++;
                            // Trim CR if present
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
                            if (lineNum == 3 && !line.empty()) {
                                // Store session_id (thread-safe via mutex)
                                SDL_LockMutex(pMetaServerClient->sharedDataMutex);
                                pMetaServerClient->sessionId = line;
                                SDL_UnlockMutex(pMetaServerClient->sharedDataMutex);
                                SDL_Log("MetaServerClient: Received session_id: %s", line.c_str());
                                break;
                            }
                        }
                    }


                } break;

                case METASERVERCOMMAND_UPDATE: {
                    MetaServerUpdate* pMetaServerUpdate = dynamic_cast<MetaServerUpdate*>(nextMetaServerCommand.get());
                    if(!pMetaServerUpdate) {
                        break;
                    }

                    std::map<std::string, std::string> parameters;

                    parameters["command"] = "update";
                    parameters["port"] = std::to_string(pMetaServerUpdate->serverPort);
                    parameters["secret"] = pMetaServerUpdate->secret;
                    parameters["numplayers"] = std::to_string(pMetaServerUpdate->numPlayers);

                    std::string result1;

                    try {
                        result1 = loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                    } catch(std::exception& e) {
                        pMetaServerClient->setErrorMessage(METASERVERCOMMAND_UPDATE, e.what());
                        break;
                    }


                    if(result1.substr(0,2) != "OK") {
                        // try adding the game again

                        parameters["command"] = "add";
                        parameters["gamename"] = pMetaServerUpdate->serverName;
                        parameters["gameversion"] = VERSION;
                        parameters["mapname"] = pMetaServerUpdate->mapName;
                        parameters["maxplayers"] = std::to_string(pMetaServerUpdate->maxPlayers);
                        parameters["pwdprotected"] = "false";
                        parameters["modname"] = pMetaServerUpdate->modName;
                        parameters["modversion"] = pMetaServerUpdate->modVersion;

                        std::string result2;

                        try {
                            result2 = loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                        } catch(std::exception&) {
                            // adding the game again did not work => report updating error

                            const std::string errorMsg = result1.substr(result1.find_first_not_of("\x0D\x0A",5), std::string::npos);

                            pMetaServerClient->setErrorMessage(METASERVERCOMMAND_UPDATE, errorMsg);

                            break;
                        }

                        if(result2.substr(0,2) != "OK") {
                            // adding the game again did not work => report updating error

                            const std::string errorMsg = result1.substr(result1.find_first_not_of("\x0D\x0A",5), std::string::npos);

                            pMetaServerClient->setErrorMessage(METASERVERCOMMAND_UPDATE, errorMsg);
                        }
                    }

                } break;

                case METASERVERCOMMAND_REMOVE: {
                    MetaServerRemove* pMetaServerRemove = dynamic_cast<MetaServerRemove*>(nextMetaServerCommand.get());
                    if(!pMetaServerRemove) {
                        break;
                    }

                    std::map<std::string, std::string> parameters;

                    parameters["command"] = "remove";
                    parameters["port"] = std::to_string(pMetaServerRemove->serverPort);
                    parameters["secret"] = pMetaServerRemove->secret;

                    try {
                        loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                    } catch(std::exception& e) {
                        pMetaServerClient->setErrorMessage(METASERVERCOMMAND_REMOVE, e.what());
                        break;
                    }
                } break;

                case METASERVERCOMMAND_LIST: {
                    std::map<std::string, std::string> parameters;

                    // Use list2 for NAT traversal fields (session_id, stun_port)
                    // Falls back to list if list2 not available
                    parameters["command"] = "list2";
                    parameters["gameversion"] = VERSION;

                    std::string result;

                    try {
                        result = loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                    } catch(std::exception& e) {
                        // Try fallback to list if list2 fails
                        SDL_Log("MetaServerClient: list2 failed, trying list: %s", e.what());
                        parameters["command"] = "list";
                        try {
                            result = loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                        } catch(std::exception& e2) {
                            pMetaServerClient->setErrorMessage(METASERVERCOMMAND_LIST, e2.what());
                            break;
                        }
                    }

                    std::istringstream resultstream(result);

                    std::string status;

                    resultstream >> status;

                    if(status == "OK") {

                        // skip all newlines
                        resultstream >> std::ws;

                        std::list<GameServerInfo> newGameServerInfoList;

                        while(true) {

                            std::string completeLine;
                            getline(resultstream, completeLine);

                            std::vector<std::string> parts = splitStringToStringVector(completeLine, "\\t");

                            // Support formats:
                            // - Old: 9 fields (no localIP, no mod)
                            // - Intermediate: 10 fields (with localIP, no mod)
                            // - New: 11 fields (with localIP, modname, empty modversion - trailing field dropped by regex)
                            // - New: 12 fields (with localIP, modname, modversion)
                            // - list2: 14 fields (with session_id, stun_port)
                            if(parts.size() < 9 || parts.size() > 14) {
                                break;
                            }

                            GameServerInfo gameServerInfo;

                            enet_address_set_host(&gameServerInfo.serverAddress, parts[0].c_str());

                            int port;
                            if(!parseString(parts[1], port) || port <= 0 || port > 65535) {
                                break;
                            }

                            gameServerInfo.serverAddress.port = static_cast<Uint16>(port);

                            gameServerInfo.serverName = parts[2];
                            gameServerInfo.serverVersion = parts[3];
                            gameServerInfo.mapName = parts[4];
                            if(!parseString(parts[6], gameServerInfo.maxPlayers) || (gameServerInfo.maxPlayers < 1) || (gameServerInfo.maxPlayers > 12)) {
                                continue;
                            }

                            if(!parseString(parts[5], gameServerInfo.numPlayers) || (gameServerInfo.numPlayers < 0) || (gameServerInfo.numPlayers > gameServerInfo.maxPlayers)) {
                                continue;
                            }

                            gameServerInfo.bPasswordProtected = (parts[7] == "true");

                            if(!parseString(parts[8], gameServerInfo.lastUpdate)) {
                                continue;
                            }
                            
                            // Parse local IP if available (10th field)
                            if(parts.size() >= 10 && !parts[9].empty()) {
                                gameServerInfo.localIP = parts[9];
                                enet_address_set_host(&gameServerInfo.localAddress, parts[9].c_str());
                                gameServerInfo.localAddress.port = static_cast<Uint16>(port);
                            } else {
                                gameServerInfo.localIP = "";
                                gameServerInfo.localAddress.host = 0;
                                gameServerInfo.localAddress.port = 0;
                            }
                            
                            // Parse mod info if available (11th and 12th fields)
                            if(parts.size() >= 11) {
                                gameServerInfo.modName = parts[10];
                                gameServerInfo.modVersion = (parts.size() >= 12) ? parts[11] : "";
                            } else {
                                gameServerInfo.modName = "vanilla";
                                gameServerInfo.modVersion = "";
                            }
                            
                            // Parse NAT traversal fields if available (13th and 14th fields from list2)
                            if(parts.size() >= 14) {
                                gameServerInfo.sessionId = parts[12];
                                int stunPortVal = 0;
                                if(parseString(parts[13], stunPortVal) && stunPortVal > 0 && stunPortVal <= 65535) {
                                    gameServerInfo.stunPort = static_cast<uint16_t>(stunPortVal);
                                    gameServerInfo.holePunchAvailable = !gameServerInfo.sessionId.empty();
                                }
                            } else {
                                gameServerInfo.sessionId = "";
                                gameServerInfo.stunPort = 0;
                                gameServerInfo.holePunchAvailable = false;
                            }

                            if(resultstream.good() == false) {
                                break;
                            }

                            newGameServerInfoList.push_back(gameServerInfo);

                        }

                        pMetaServerClient->setNewGameServerInfoList(newGameServerInfoList);

                    } else {
                        const std::string errorMsg = result.substr(result.find_first_not_of("\x0D\x0A",5), std::string::npos);
                        pMetaServerClient->setErrorMessage(METASERVERCOMMAND_LIST, errorMsg);
                    }

                } break;

                case METASERVERCOMMAND_GAMESTART: {
                    MetaServerGameStart* pMetaServerGameStart = dynamic_cast<MetaServerGameStart*>(nextMetaServerCommand.get());
                    if(!pMetaServerGameStart) {
                        break;
                    }

                    std::map<std::string, std::string> parameters;

                    parameters["command"] = "gamestart";
                    parameters["secret"] = pMetaServerGameStart->secret;
                    parameters["map"] = pMetaServerGameStart->mapName;
                    parameters["modname"] = pMetaServerGameStart->modName;
                    parameters["players"] = pMetaServerGameStart->players;
                    parameters["version"] = pMetaServerGameStart->version;

                    try {
                        loadFromHttp(pMetaServerClient->metaServerURL, parameters);
                        SDL_Log("MetaServerClient: Game start announced to metaserver");
                    } catch(std::exception& e) {
                        SDL_Log("MetaServerClient: Failed to announce game start: %s", e.what());
                    }
                } break;

                case METASERVERCOMMAND_GAMESTATS: {
                    auto* command = dynamic_cast<MetaServerGameStats*>(nextMetaServerCommand.get());
                    if (!command) break;
                    std::map<std::string, std::string> parameters;
                    parameters["command"] = "gamestats";
                    parameters["phase"] = command->phase;
                    parameters["match_id"] = command->matchID;
                    parameters["stats"] = command->stats;
                    // Match writes are idempotent. Retry once when a transient
                    // DNS, TLS, network or server delay consumes the short
                    // telemetry timeout; a repeated request merely upserts the
                    // same opaque match ID. Keep both attempts bounded so an
                    // unavailable service cannot materially delay teardown.
                    constexpr int maxAttempts = 2;
                    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
                        try {
                            postToHttp(pMetaServerClient->metaServerURL, parameters, 3);
                            SDL_Log("MetaServerClient: Match analytics %s recorded%s", command->phase.c_str(),
                                    attempt > 1 ? " after retry" : "");
                            break;
                        } catch (std::exception& e) {
                            if (attempt < maxAttempts) {
                                SDL_Log("MetaServerClient: Match analytics %s attempt %d timed out or failed; retrying: %s",
                                        command->phase.c_str(), attempt, e.what());
                            } else {
                                // Analytics must never affect simulation or an
                                // existing multiplayer connection.
                                SDL_Log("MetaServerClient: Failed to record match analytics after %d attempts: %s",
                                        maxAttempts, e.what());
                            }
                        }
                    }
                } break;

                case METASERVERCOMMAND_EXIT: {
                    return 0;
                } break;

                default: {
                    // ignore
                } break;

            }
        } catch(std::exception& e) {
            SDL_Log("MetaServerClient::connectionThreadMain(): %s", e.what());
        }
    }
}
