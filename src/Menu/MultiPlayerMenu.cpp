
#include <Menu/MultiPlayerMenu.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <Network/NetworkManager.h>
#include <Network/ENetHelper.h>

#include <GUI/MsgBox.h>

#include <globals.h>
#include <main.h>

#include <misc/string_util.h>

MultiPlayerMenu::MultiPlayerMenu() : MenuBase() {
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    windowWidget.addWidget(&mainVBox, Point(24,23), Point(getRendererWidth() - 48, getRendererHeight() - 46));

    captionLabel.setText("Multiplayer Game");
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(24));

    // Player name row
    playerNameHBox.addWidget(Label::create(_("Player Name:")), 100);
    playerNameTextBox.setText(settings.general.playerName);
    playerNameTextBox.setMaximumTextLength(20);
    playerNameHBox.addWidget(&playerNameTextBox, 200);
    playerNameHBox.addWidget(Spacer::create());

    mainVBox.addWidget(&playerNameHBox, 28);
    mainVBox.addWidget(VSpacer::create(8));

    // Connect row
    connectHBox.addWidget(Label::create("Host:"), 50);
    connectHostTextBox.setText("localhost");
    connectHBox.addWidget(&connectHostTextBox);
    connectHBox.addWidget(HSpacer::create(20));
    connectHBox.addWidget(Label::create("Port:"), 50);
    connectPortTextBox.setText(std::to_string(DEFAULT_PORT));
    connectHBox.addWidget(&connectPortTextBox, 90);
    connectHBox.addWidget(HSpacer::create(20));
    connectButton.setText(_("Connect"));
    connectButton.setOnClick(std::bind(&MultiPlayerMenu::onConnect, this));
    connectHBox.addWidget(&connectButton, 100);

    mainVBox.addWidget(Spacer::create(), 0.05);
    mainVBox.addWidget(&connectHBox, 28);

    mainVBox.addWidget(VSpacer::create(16));

    mainVBox.addWidget(Spacer::create(), 0.05);
    mainVBox.addWidget(&mainHBox, 0.85);

    mainHBox.addWidget(&leftVBox, 180);

    createLANGameButton.setText(_("Create LAN Game"));
    createLANGameButton.setOnClick(std::bind(&MultiPlayerMenu::onCreateLANGame, this));
    leftVBox.addWidget(&createLANGameButton, 0.1);

    leftVBox.addWidget(VSpacer::create(8));

    createInternetGameButton.setText(_("Create Internet Game"));
    createInternetGameButton.setOnClick(std::bind(&MultiPlayerMenu::onCreateInternetGame, this));
    leftVBox.addWidget(&createInternetGameButton, 0.1);

    leftVBox.addWidget(Spacer::create(), 0.8);

    rightVBox.addWidget(&gameTypeButtonsHBox, 24);

    mainHBox.addWidget(HSpacer::create(8));
    mainHBox.addWidget(Spacer::create(), 0.05);

    LANGamesButton.setText(_("LAN Games"));
    LANGamesButton.setToggleButton(true);
    LANGamesButton.setOnClick(std::bind(&MultiPlayerMenu::onGameTypeChange, this, 0));
    gameTypeButtonsHBox.addWidget(&LANGamesButton, 0.35);

    internetGamesButton.setText(_("Internet Games"));
    internetGamesButton.setToggleButton(true);
    internetGamesButton.setOnClick(std::bind(&MultiPlayerMenu::onGameTypeChange, this, 1));
    gameTypeButtonsHBox.addWidget(&internetGamesButton, 0.35);

    gameTypeButtonsHBox.addWidget(Spacer::create(), 0.3);

    gameList.setAutohideScrollbar(false);
    gameList.setOnSelectionChange(std::bind(&MultiPlayerMenu::onGameListSelectionChange, this, std::placeholders::_1));
    gameList.setOnDoubleClick(std::bind(&MultiPlayerMenu::onJoin, this));
    rightVBox.addWidget(&gameList, 0.95);

    mainHBox.addWidget(&rightVBox, 0.9);
    mainHBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(Spacer::create(), 0.05);
    mainVBox.addWidget(VSpacer::create(10));

    mainVBox.addWidget(&buttonHBox, 24);

    buttonHBox.addWidget(HSpacer::create(70));
    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&MultiPlayerMenu::onQuit, this));
    buttonHBox.addWidget(&backButton, 0.1);

    buttonHBox.addWidget(Spacer::create(), 0.8);

    joinButton.setText(_("Join"));
    joinButton.setOnClick(std::bind(&MultiPlayerMenu::onJoin, this));
    buttonHBox.addWidget(&joinButton, 0.1);
    buttonHBox.addWidget(HSpacer::create(90));

    // Start Network Manager
    SDL_Log("Starting network...");
    try {
        pNetworkManager = std::make_unique<NetworkManager>(settings.network.serverPort, settings.network.metaServer);
        LANGameFinderAndAnnouncer* pLANGFAA = pNetworkManager->getLANGameFinderAndAnnouncer();
        pLANGFAA->setOnNewServer(std::bind(&MultiPlayerMenu::onNewLANServer, this, std::placeholders::_1));
        pLANGFAA->setOnUpdateServer(std::bind(&MultiPlayerMenu::onUpdateLANServer, this, std::placeholders::_1));
        pLANGFAA->setOnRemoveServer(std::bind(&MultiPlayerMenu::onRemoveLANServer, this, std::placeholders::_1));
        pLANGFAA->refreshServerList();
        SDL_Log("Network initialization completed.");
    } catch (const std::exception& e) {
        SDL_Log("Network initialization failed: %s", e.what());
        SDL_Log("Multiplayer functionality may be limited until network permissions are granted.");
        // Continue without crashing - the NetworkManager constructor should handle this gracefully now
    }

    onGameTypeChange(0);
}


MultiPlayerMenu::~MultiPlayerMenu() {
    // Save player name on exit (even if just going back)
    savePlayerNameToConfig();
    SDL_Log("Stopping network...");
    pNetworkManager.reset();
}


bool MultiPlayerMenu::validateAndSavePlayerName() {
    std::string name = playerNameTextBox.getText();
    
    // Trim whitespace
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start == std::string::npos) {
        name = "";
    } else {
        name = name.substr(start, end - start + 1);
    }
    
    if (name.empty()) {
        openWindow(MsgBox::create(_("Please enter a player name.")));
        return false;
    }
    
    // Update settings and save
    settings.general.playerName = name;
    playerNameTextBox.setText(name);  // Update with trimmed version
    savePlayerNameToConfig();
    return true;
}

void MultiPlayerMenu::savePlayerNameToConfig() {
    std::string name = playerNameTextBox.getText();
    
    // Trim whitespace
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start != std::string::npos) {
        name = name.substr(start, end - start + 1);
    }
    
    if (!name.empty() && name != settings.general.playerName) {
        settings.general.playerName = name;
        
        // Save to config file
        INIFile myINIFile(getConfigFilepath());
        myINIFile.setStringValue("General", "Player Name", settings.general.playerName);
        myINIFile.saveChangesTo(getConfigFilepath());
    }
}


/**
    This method is called, when the child window is about to be closed.
    This child window will be closed after this method returns.
    \param  pChildWindow    The child window that will be closed
*/
void MultiPlayerMenu::onChildWindowClose(Window* pChildWindow) {
    // Connection canceled
    // TODO
}

void MultiPlayerMenu::onCreateLANGame() {
    if (!validateAndSavePlayerName()) {
        return;
    }
    CustomGameMenu(true, true).showMenu();
}


void MultiPlayerMenu::onCreateInternetGame() {
    if (!validateAndSavePlayerName()) {
        return;
    }
    CustomGameMenu(true, false).showMenu();
}


void MultiPlayerMenu::onConnect() {
    if (!validateAndSavePlayerName()) {
        return;
    }
    if (!pNetworkManager) {
        openWindow(MsgBox::create(_("Network not available. Please grant network permissions and restart the game.")));
        return;
    }
    std::string hostname = connectHostTextBox.getText();
    int port = atol(connectPortTextBox.getText().c_str());

    pNetworkManager->setOnReceiveGameInfo(std::bind(&MultiPlayerMenu::onReceiveGameInfo, this, std::placeholders::_1, std::placeholders::_2));
    pNetworkManager->setOnPeerDisconnected(std::bind(&MultiPlayerMenu::onPeerDisconnected, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    pNetworkManager->connect(hostname, port, settings.general.playerName);

    openWindow(MsgBox::create(_("Connecting...")));
}


void MultiPlayerMenu::onQuit() {
    SDL_Event quitEvent;
    quitEvent.type = SDL_QUIT;
    SDL_PushEvent(&quitEvent);
}


void MultiPlayerMenu::onPeerDisconnected(const std::string& playername, bool bHost, int cause) {
    if(bHost && pNetworkManager) {
        pNetworkManager->setOnReceiveGameInfo(std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
        closeChildWindow();

        showDisconnectMessageBox(cause);
    }
}

void MultiPlayerMenu::onJoin() {
    if (!validateAndSavePlayerName()) {
        return;
    }
    if (!pNetworkManager) {
        openWindow(MsgBox::create(_("Network not available. Please grant network permissions and restart the game.")));
        return;
    }
    int selectedEntry = gameList.getSelectedIndex();
    if(selectedEntry >= 0) {
        GameServerInfo* pGameServerInfo = static_cast<GameServerInfo*>(gameList.getEntryPtrData(selectedEntry));
        
        // Smart NAT detection: For internet games, decide whether to use external or local IP
        // - If found via LAN broadcast, use LAN address (same network confirmed)
        // - Otherwise, use external IP from metaserver (the default for internet games)
        // Note: The local IP is only useful if both players are behind the same router,
        // which would be detected via LAN broadcast anyway.
        ENetAddress connectAddress = pGameServerInfo->serverAddress;
        bool foundOnLAN = false;
        
        if(internetGamesButton.getToggleState()) {
            // Check if this game is also on LAN (UDP broadcast discovery)
            for(const GameServerInfo& lanGame : LANGameList) {
                if(lanGame.serverName == pGameServerInfo->serverName &&
                   lanGame.serverAddress.port == pGameServerInfo->serverAddress.port &&
                   lanGame.mapName == pGameServerInfo->mapName) {
                    // Found via LAN broadcast - use that address (most reliable)
                    SDL_Log("Smart NAT: Game found via LAN broadcast, using %s:%d",
                            Address2String(lanGame.serverAddress).c_str(), lanGame.serverAddress.port);
                    connectAddress = lanGame.serverAddress;
                    foundOnLAN = true;
                    break;
                }
            }
            
            if(!foundOnLAN) {
                // Not found on LAN - check if we're behind the same NAT (same external IP)
                // If so, use local IP to avoid hairpin NAT issues
                std::string clientExternalIP;
                uint16_t clientStunPort = 0;
                bool sameNAT = false;
                
                if (pNetworkManager->performStunQueryFull(clientExternalIP, clientStunPort)) {
                    // Get server's external IP from the address
                    std::string serverExternalIP = Address2String(pGameServerInfo->serverAddress);
                    
                    if (clientExternalIP == serverExternalIP && !pGameServerInfo->localIP.empty()) {
                        // Same external IP = behind same NAT, use local IP
                        SDL_Log("Same-NAT detected: client=%s, server=%s - using local IP %s",
                                clientExternalIP.c_str(), serverExternalIP.c_str(), 
                                pGameServerInfo->localIP.c_str());
                        
                        if (enet_address_set_host(&connectAddress, pGameServerInfo->localIP.c_str()) == 0) {
                            // Keep the same port (local port = external port for most routers)
                            sameNAT = true;
                        } else {
                            SDL_Log("Same-NAT: Failed to resolve local IP %s, falling back",
                                    pGameServerInfo->localIP.c_str());
                        }
                    }
                }
                
                // If not same-NAT, try hole punch if available
                if (!sameNAT && pGameServerInfo->holePunchAvailable && !pGameServerInfo->sessionId.empty()) {
                    SDL_Log("NAT Hole Punch: Attempting hole punch for internet game");
                    if (clientStunPort > 0) {
                        MetaServerClient* pMetaServer = pNetworkManager->getMetaServerClient();
                        if (pMetaServer) {
                            // Step 2: Request hole punch coordination
                            std::string clientId = pMetaServer->requestHolePunch(
                                pGameServerInfo->sessionId, clientStunPort);
                            
                            if (!clientId.empty()) {
                                // Step 3: Poll for punch readiness (with timeout)
                                openWindow(MsgBox::create(_("Establishing NAT connection...")));
                                
                                std::string hostIP;
                                uint16_t hostPort = 0;
                                int waitSeconds = 0;
                                bool punchReady = false;
                                
                                Uint32 pollStart = SDL_GetTicks();
                                constexpr Uint32 PUNCH_TIMEOUT_MS = 10000;  // 10 second timeout
                                
                                while (SDL_GetTicks() - pollStart < PUNCH_TIMEOUT_MS) {
                                    if (pMetaServer->pollPunchStatus(pGameServerInfo->sessionId, clientId,
                                                                     hostIP, hostPort, waitSeconds)) {
                                        punchReady = true;
                                        break;
                                    }
                                    SDL_Delay(500);  // Poll every 500ms
                                }
                                
                                closeChildWindow();
                                
                                if (punchReady) {
                                    SDL_Log("NAT Hole Punch: Ready - host at %s:%d, waiting %d sec",
                                            hostIP.c_str(), hostPort, waitSeconds);
                                    
                                    // Step 4: Wait coordinated time
                                    if (waitSeconds > 0) {
                                        SDL_Delay(waitSeconds * 1000);
                                    }
                                    
                                    // Step 5: Send punch packets
                                    pNetworkManager->sendHolePunchPackets(hostIP, hostPort, 10, 50);
                                    
                                    // Step 6: Update connect address to punched address
                                    if (enet_address_set_host(&connectAddress, hostIP.c_str()) == 0) {
                                        connectAddress.port = hostPort;
                                        SDL_Log("NAT Hole Punch: Connecting to punched address %s:%d",
                                                hostIP.c_str(), hostPort);
                                    }
                                } else {
                                    SDL_Log("NAT Hole Punch: Timeout - falling back to direct connect");
                                }
                            } else {
                                SDL_Log("NAT Hole Punch: Failed to get client ID - falling back to direct connect");
                            }
                        }
                    } else {
                        SDL_Log("NAT Hole Punch: STUN query failed - falling back to direct connect");
                    }
                } else {
                    // No hole punch available - use direct connection
                    SDL_Log("Connecting to internet game via external IP: %s:%d",
                            Address2String(pGameServerInfo->serverAddress).c_str(), pGameServerInfo->serverAddress.port);
                }
            }
        }

        pNetworkManager->setOnReceiveGameInfo(std::bind(&MultiPlayerMenu::onReceiveGameInfo, this, std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnPeerDisconnected(std::bind(&MultiPlayerMenu::onPeerDisconnected, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pNetworkManager->connect(connectAddress, settings.general.playerName);

        openWindow(MsgBox::create(_("Connecting...")));
    }
}


void MultiPlayerMenu::onGameTypeChange(int buttonID) {
    if (!pNetworkManager) {
        // Network not available - just update button states
        LANGamesButton.setToggleState(buttonID == 0);
        internetGamesButton.setToggleState(buttonID == 1);
        return;
    }
    MetaServerClient* pMetaServerClient = pNetworkManager->getMetaServerClient();
    if((buttonID == 0) && internetGamesButton.getToggleState() == true) {
        // LAN Games

        gameList.clearAllEntries();
        InternetGameList.clear();

        for(GameServerInfo& gameServerInfo : LANGameList) {
            std::string description = gameServerInfo.serverName + " (" + Address2String(gameServerInfo.serverAddress) + " : " + std::to_string(gameServerInfo.serverAddress.port) + ") - "
                                        + gameServerInfo.mapName + " (" + std::to_string(gameServerInfo.numPlayers) + "/" + std::to_string(gameServerInfo.maxPlayers) + ")";
            gameList.addEntry(description, &gameServerInfo);
        }

        // stop listening on internet games
        pMetaServerClient->setOnGameServerInfoList(std::function<void (std::list<GameServerInfo>&)>());
        pMetaServerClient->setOnMetaServerError(std::function<void (int, const std::string&)>());
    } else if((buttonID == 1) && (LANGamesButton.getToggleState() == true)) {
        // Internet Games

        gameList.clearAllEntries();
        InternetGameList.clear();

        // start listening on internet games
        pMetaServerClient->setOnGameServerInfoList(std::bind(&MultiPlayerMenu::onGameServerInfoList, this, std::placeholders::_1));
        pMetaServerClient->setOnMetaServerError(std::bind(&MultiPlayerMenu::onMetaServerError, this, std::placeholders::_1, std::placeholders::_2));
    }

    LANGamesButton.setToggleState(buttonID == 0);
    internetGamesButton.setToggleState(buttonID == 1);

}


void MultiPlayerMenu::onGameListSelectionChange(bool bInteractive) {
}


void MultiPlayerMenu::onNewLANServer(GameServerInfo gameServerInfo) {
    LANGameList.push_back(gameServerInfo);
    std::string description = gameServerInfo.serverName + " (" + Address2String(gameServerInfo.serverAddress) + " : " + std::to_string(gameServerInfo.serverAddress.port) + ") - "
                                + gameServerInfo.mapName + " (" + std::to_string(gameServerInfo.numPlayers) + "/" + std::to_string(gameServerInfo.maxPlayers) + ")";
    gameList.addEntry(description, &LANGameList.back());
}

void MultiPlayerMenu::onUpdateLANServer(GameServerInfo gameServerInfo) {
    size_t index = 0;
    for(GameServerInfo& curGameServerInfo : LANGameList) {
        if(curGameServerInfo == gameServerInfo) {
            curGameServerInfo = gameServerInfo;
            break;
        }

        index++;
    }

    if(index < LANGameList.size()) {
        std::string description = gameServerInfo.serverName + " (" + Address2String(gameServerInfo.serverAddress) + " : " + std::to_string(gameServerInfo.serverAddress.port) + ") - "
                                    + gameServerInfo.mapName + " (" + std::to_string(gameServerInfo.numPlayers) + "/" + std::to_string(gameServerInfo.maxPlayers) + ")";

        gameList.setEntry(index, description);
    }
}

void MultiPlayerMenu::onRemoveLANServer(GameServerInfo gameServerInfo) {
    for(int i=0;i<gameList.getNumEntries();i++) {
        GameServerInfo* pGameServerInfo = static_cast<GameServerInfo*>(gameList.getEntryPtrData(i));
        if(*pGameServerInfo == gameServerInfo) {
            gameList.removeEntry(i);
            break;
        }
    }

    LANGameList.remove(gameServerInfo);
}

void MultiPlayerMenu::onGameServerInfoList(const std::list<GameServerInfo>& gameServerInfoList) {
    // remove all game servers from the list that are not included in the sent list
    std::list<GameServerInfo>::iterator oldListIter = InternetGameList.begin();
    int index = 0;
    while(oldListIter != InternetGameList.end()) {
        GameServerInfo& gameServerInfo = *oldListIter;

        if(std::find(gameServerInfoList.begin(), gameServerInfoList.end(), gameServerInfo) == gameServerInfoList.end()) {
            // not found => remove
            gameList.removeEntry(index);
            oldListIter = InternetGameList.erase(oldListIter);
        } else {
            // found => move on
            ++oldListIter;
            ++index;
        }
    }

    // now add all servers that are included for the first time and update the others
    for(const GameServerInfo& gameServerInfo : gameServerInfoList) {
        size_t oldListIndex = 0;
        std::list<GameServerInfo>::iterator oldListIter;
        for(oldListIter = InternetGameList.begin(); oldListIter != InternetGameList.end(); ++oldListIter) {
            if(*oldListIter == gameServerInfo) {
                // found => update
                *oldListIter = gameServerInfo;
                break;
            }

            oldListIndex++;
        }


        if(oldListIndex >= InternetGameList.size()) {
            // not found => add at the end
            InternetGameList.push_back(gameServerInfo);
            std::string description = gameServerInfo.serverName + " (" + Address2String(gameServerInfo.serverAddress) + " : " + std::to_string(gameServerInfo.serverAddress.port) + ") - "
                                + gameServerInfo.mapName + " (" + std::to_string(gameServerInfo.numPlayers) + "/" + std::to_string(gameServerInfo.maxPlayers) + ")";
            gameList.addEntry(description, &InternetGameList.back());
        } else {
            // found => update
            std::string description = gameServerInfo.serverName + " (" + Address2String(gameServerInfo.serverAddress) + " : " + std::to_string(gameServerInfo.serverAddress.port) + ") - "
                                    + gameServerInfo.mapName + " (" + std::to_string(gameServerInfo.numPlayers) + "/" + std::to_string(gameServerInfo.maxPlayers) + ")";

            gameList.setEntry(oldListIndex, description);
        }

    }
}

void MultiPlayerMenu::onMetaServerError(int errorcause, const std::string& errorMessage) {
    switch(errorcause) {
        case METASERVERCOMMAND_ADD: {
            openWindow(MsgBox::create("MetaServer error on adding game server:\n" + errorMessage));
        } break;

        case METASERVERCOMMAND_UPDATE: {
            openWindow(MsgBox::create("MetaServer error on updating game server:\n" + errorMessage));
        } break;

        case METASERVERCOMMAND_REMOVE: {
            openWindow(MsgBox::create("MetaServer error on removing game server:\n" + errorMessage));
        } break;

        case METASERVERCOMMAND_LIST : {
            openWindow(MsgBox::create("MetaServer error on list game servers:\n" + errorMessage));
        } break;

        case METASERVERCOMMAND_EXIT:
        default: {
            openWindow(MsgBox::create("MetaServer error:\n" + errorMessage));
        } break;
    }
}

void MultiPlayerMenu::onReceiveGameInfo(const GameInitSettings& gameInitSettings, const ChangeEventList& changeEventList) {
    closeChildWindow();

    if (pNetworkManager) {
        pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
    }

    auto pCustomGamePlayers = std::make_unique<CustomGamePlayers>(gameInitSettings, false);
    pCustomGamePlayers->onReceiveChangeEventList(changeEventList);
    int ret = pCustomGamePlayers->showMenu();
    pCustomGamePlayers.reset();

    switch(ret) {
        case MENU_QUIT_DEFAULT: {
            // nothing
        } break;

        case MENU_QUIT_GAME_FINISHED: {
            quit(MENU_QUIT_GAME_FINISHED);
        } break;

        default: {
            showDisconnectMessageBox(ret);
        } break;
    }
}

void MultiPlayerMenu::showDisconnectMessageBox(int cause) {
    switch(cause) {
        case NETWORKDISCONNECT_QUIT: {
            openWindow(MsgBox::create(_("The game host quit the game!")));
        } break;

        case NETWORKDISCONNECT_TIMEOUT: {
            openWindow(MsgBox::create(_("The server timed out!")));
        } break;

        case NETWORKDISCONNECT_PLAYER_EXISTS: {
            openWindow(MsgBox::create(_("A player with the same name as yours already exists!")));
        } break;

        case NETWORKDISCONNECT_GAME_FULL: {
            openWindow(MsgBox::create(_("There is no free player slot in this game left!")));
        } break;

        case NETWORKDISCONNECT_PROTOCOL_MISMATCH: {
            openWindow(MsgBox::create(_("The game host uses an incompatible network protocol version. Please use the same Dune City version.")));
        } break;

        default: {
            openWindow(MsgBox::create(_("The connection to the game host was lost!")));
        } break;
    }
}
