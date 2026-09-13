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

#include <Menu/CrossplayMenu.h>

#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/SinglePlayerSkirmishMenu.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <GUI/MsgBox.h>

#include <Network/DirectPeerConnection.h>
#include <Network/DirectRoomTransport.h>
#include <Network/RoomSessionTransport.h>
#include <Network/NetworkManager.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <config.h>
#include <globals.h>
#include <main.h>
#include <misc/FileSystem.h>
#include <misc/WebRuntime.h>
#include <players/QuantBotConfig.h>

#include <algorithm>

namespace {

/// A run of exactly 16 lowercase hex characters, which is what the content checksums are.
bool isChecksumToken(const std::string& value) {
    if(value.size() != 16) {
        return false;
    }
    return RoomRelay::isLowercaseHex(value);
}

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if(first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

std::string CrossplayMenu::contentFingerprint() {
    // The relay compares this between the host and anybody joining, so a mismatched install is
    // reported before a socket is opened. It is the same material the lobby exchanges in its own
    // config check, which stays the authority.
    const std::string quantBot = getQuantBotConfig().getConfigHash();
    const std::string objectData = getObjectDataHash();
    if(!isChecksumToken(quantBot) || !isChecksumToken(objectData)) {
        // Something could not be hashed locally. An empty fingerprint is not a wildcard and must
        // never be sent as one: callers treat it as "this install cannot be checked" and refuse
        // to go online, because two installs that both failed to hash themselves would otherwise
        // match each other and neither would have verified anything.
        return std::string();
    }
    return quantBot + objectData;
}

CrossplayMenu::CrossplayMenu() : MenuBase() {
    SDL_Texture* pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));
    setWindowWidget(&windowWidget);

    windowWidget.addWidget(&mainVBox, Point(24, 23),
                           Point(getRendererWidth() - 48, getRendererHeight() - 46));

    captionLabel.setText(_("Online Lobby"));
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(8));

    playerNameLabel.setText(_("Player Name:"));
    playerNameHBox.addWidget(&playerNameLabel, 120);
    playerNameTextBox.setText(settings.general.playerName);
    playerNameTextBox.setMaximumTextLength(20);
    playerNameHBox.addWidget(&playerNameTextBox, 220);
    playerNameHBox.addWidget(HSpacer::create(12));
    confirmNameButton.setText(_("Confirm name for chat"));
    confirmNameButton.setOnClick([this]() { confirmChatName(); });
    playerNameTextBox.setOnReturn([this]() { confirmChatName(); });
    playerNameHBox.addWidget(&confirmNameButton, 210);
    playerNameHBox.addWidget(Spacer::create());
    mainVBox.addWidget(&playerNameHBox, 28);

    statusLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&statusLabel, 36);

    directoryLabel.setText(_("Public games - no code needed"));
    directoryHBox.addWidget(&directoryLabel, 1.0);
    refreshGamesButton.setText(_("Refresh"));
    refreshGamesButton.setOnClick([this]() { refreshPublicGames(); });
    directoryHBox.addWidget(&refreshGamesButton, 90);
    moreGamesButton.setText(_("More"));
    moreGamesButton.setOnClick([this]() { refreshPublicGames(nextDirectoryPage); });
    directoryHBox.addWidget(&moreGamesButton, 80);
    joinPublicButton.setText(_("Join Game"));
    joinPublicButton.setOnClick([this]() { joinPublicGame(); });
    mainVBox.addWidget(&directoryHBox, 28);
    publicGameList.setOnSelectionChange([this](bool) { refreshControls(); });
    mainVBox.addWidget(&publicGameList, 0.5);
    mainVBox.addWidget(VSpacer::create(8));
    joinPublicHBox.addWidget(Spacer::create(), 0.5);
    joinPublicHBox.addWidget(&joinPublicButton, 260);
    joinPublicHBox.addWidget(Spacer::create(), 0.5);
    mainVBox.addWidget(&joinPublicHBox, 48);
    mainVBox.addWidget(VSpacer::create(8));
    chatLabel.setText(_("Public lobby chat - confirm your name above to chat"));
    mainVBox.addWidget(&chatLabel, 24);
    mainVBox.addWidget(&chatHistory, 0.5);
    chatInput.setMaximumTextLength(120);
    chatInput.setOnReturn([this]() { sendLobbyChat(); });
    chatInputHBox.addWidget(&chatInput, 1.0);
    chatSendButton.setText(_("Send"));
    chatSendButton.setOnClick([this]() { sendLobbyChat(); });
    chatInputHBox.addWidget(&chatSendButton, 90);
    mainVBox.addWidget(&chatInputHBox, 28);
    mainVBox.addWidget(VSpacer::create(8));

    visibilityLabel.setText(_("Host visibility:"));
    visibilityHBox.addWidget(&visibilityLabel, 160);
    visibilityChoice.addEntry(_("Public - anyone can join"));
    visibilityChoice.addEntry(_("Private - invite by code"));
    visibilityChoice.setSelectedItem(0);
    visibilityChoice.setOnSelectionChange([this](bool interactive) {
        if(interactive) changeVisibility();
    });
    visibilityHBox.addWidget(&visibilityChoice, 300);
    visibilityHBox.addWidget(Spacer::create());
    privateInviteButton.setText(_("Join with invite code"));
    privateInviteButton.setOnClick([this]() {
        showPrivateJoin = !showPrivateJoin;
        refreshControls();
    });
    visibilityHBox.addWidget(&privateInviteButton, 220);
    mainVBox.addWidget(&visibilityHBox, 28);

    roomCodeLabel.setAlignment(Alignment_HCenter);
    roomCodeLabel.setTextFontSize(24);
    roomCodeHBox.addWidget(Spacer::create(), 0.2);
    roomCodeHBox.addWidget(&roomCodeLabel, 0.6);
    copyCodeButton.setText(_("Copy code"));
    copyCodeButton.setOnClick([this]() {
        if(roomCode.empty()) return;
        copyCodeButton.setText(WebRuntime::copyText(roomCode) ? _("Copied!") : _("Try again"));
    });
    roomCodeHBox.addWidget(&copyCodeButton, 130);
    roomCodeHBox.addWidget(Spacer::create(), 0.2);

    hostCustomGameButton.setText(_("Host a Game"));
    hostCustomGameButton.setOnClick(std::bind(&CrossplayMenu::onHostCustomGame, this));
    hostHBox.addWidget(Spacer::create(), 0.25);
    hostHBox.addWidget(&hostCustomGameButton, 200);
    hostHBox.addWidget(HSpacer::create(16));
    hostCoopButton.setText(_("Host Campaign Co-op"));
    hostCoopButton.setOnClick(std::bind(&CrossplayMenu::onHostCampaignCoop, this));
    hostHBox.addWidget(&hostCoopButton, 220);
    hostHBox.addWidget(Spacer::create(), 0.25);
    mainVBox.addWidget(&hostHBox, 28);
    mainVBox.addWidget(&roomCodeHBox, 34);

    joinLabel.setText(_("Invite code:"));
    joinHBox.addWidget(Spacer::create(), 0.25);
    joinHBox.addWidget(&joinLabel, 120);
    joinCodeTextBox.setMaximumTextLength(16);
    joinHBox.addWidget(&joinCodeTextBox, 200);
    joinHBox.addWidget(HSpacer::create(16));
    joinButton.setText(_("Join Game"));
    joinButton.setOnClick(std::bind(&CrossplayMenu::onJoin, this));
    joinHBox.addWidget(&joinButton, 140);
    joinHBox.addWidget(Spacer::create(), 0.25);
    mainVBox.addWidget(&joinHBox, 28);

    mainVBox.addWidget(VSpacer::create(8));

    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&CrossplayMenu::onBack, this));
    buttonHBox.addWidget(HSpacer::create(70));
    buttonHBox.addWidget(&backButton, 0.1);
    buttonHBox.addWidget(Spacer::create(), 0.9);
    mainVBox.addWidget(&buttonHBox, 24);

    // Say plainly why online play is not offered, rather than failing later.
    if(settings.network.activeDirectEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        stage = Stage::Finished;
    } else {
        // Direct play needs a WebRTC backend, not a WebSocket: a build without one says so here
        // rather than looking like it is connecting and never arriving.
        if(!isDirectPeerConnectionAvailable()) {
            setStatus(_("This copy of the game cannot open direct connections to other players."));
            stage = Stage::Finished;
        } else {
            setStatus(_("Choose your player name, then join a public game or host your own."));
        }
    }

    refreshControls();
    if(stage == Stage::Choosing) refreshPublicGames();
}

CrossplayMenu::~CrossplayMenu() {
    directory.cancel();
    chat.cancel();
    visibilityUpdate.cancel();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRelaySession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager.reset();
    }
}

void CrossplayMenu::setStatus(const std::string& message) {
    statusText = message;
    statusLabel.setText(message);
}

void CrossplayMenu::refreshControls() {
    const bool idle = (stage == Stage::Choosing);
    const bool busy = (stage == Stage::Requesting) || (stage == Stage::Connecting);

    hostCustomGameButton.setEnabled(idle);
    hostCoopButton.setEnabled(idle);
    const bool invite = idle && showPrivateJoin;
    joinLabel.setVisible(invite);
    joinButton.setVisible(invite);
    joinCodeTextBox.setVisible(invite);
    privateInviteButton.setVisible(idle);
    privateInviteButton.setText(showPrivateJoin ? _("Hide private invite") : _("Join with invite code"));
    joinButton.setEnabled(invite);
    joinCodeTextBox.setEnabled(invite);
    playerNameTextBox.setEnabled(idle && chatSession.empty() && !chatPending);
    confirmNameButton.setEnabled(idle || (chatSession.empty() && !chatPending
        && (stage == Stage::HostReady || stage == Stage::ClientWaiting)));
    confirmNameButton.setText(chatSession.empty() ? _("Confirm name for chat") : _("Change name"));
    chatInput.setEnabled(!chatSession.empty());
    chatSendButton.setEnabled(!chatSession.empty() && !chatPending);
    visibilityChoice.setEnabled(idle || (stage == Stage::HostReady
        && !visibilityPending && !grantedRoom.controlToken.empty()));
    // A background directory refresh must not steal selection/keyboard focus.
    // Joining a cached listing is safe: admission validates that it is still open.
    publicGameList.setEnabled(idle);
    refreshGamesButton.setEnabled(idle && !directoryPending);
    moreGamesButton.setEnabled(idle && !directoryPending && nextDirectoryPage > 0);
    const int selected = publicGameList.getSelectedIndex();
    joinPublicButton.setText(_("Join Game"));
    joinPublicButton.setEnabled(idle && selected >= 0
        && static_cast<std::size_t>(selected) < publicGames.size());

    // Once in a room as the host, the two host buttons become "what do you want to play".
    if(stage == Stage::HostReady) {
        hostCustomGameButton.setEnabled(!hostingCoop && !visibilityPending);
        hostCoopButton.setEnabled(hostingCoop && !visibilityPending);
        hostCustomGameButton.setText(_("Choose a Map"));
        hostCoopButton.setText(_("Choose a Campaign Mission"));
    }

    const bool showCode = !publicRoom && !roomCode.empty() && stage == Stage::HostReady;
    roomCodeLabel.setText(showCode ? (_("Game code: ") + roomCode) : std::string());
    copyCodeButton.setVisible(showCode);
    copyCodeButton.setEnabled(showCode);
    copyCodeButton.setText(_("Copy code"));

    backButton.setEnabled(!busy);
}

void CrossplayMenu::refreshPublicGames(unsigned offset) {
    if((stage != Stage::Choosing && stage != Stage::HostReady && stage != Stage::ClientWaiting)
       || directoryPending) return;
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        directoryLabel.setText(_("Cannot check game content"));
        return;
    }
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.listing = true;
    request.listOffset = offset;
    directoryPending = true;
    directoryLabel.setText(_("Finding public games..."));
    directory.begin(request);
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    refreshControls();
}

void CrossplayMenu::joinPublicGame() {
    const int index = publicGameList.getSelectedIndex();
    if(stage != Stage::Choosing || directoryPending || index < 0
       || static_cast<std::size_t>(index) >= publicGames.size()) return;
    beginAdmission(false, true);
}

AdmissionRequest CrossplayMenu::lobbyRequest() const {
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = contentFingerprint();
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    return request;
}

void CrossplayMenu::changeVisibility() {
    if(stage == Stage::Choosing) { publicRoom = visibilityChoice.getSelectedIndex() == 0; return; }
    if(stage != Stage::HostReady || visibilityPending || grantedRoom.controlToken.empty()) return;
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::Visibility;
    request.roomCode = roomCode;
    request.controlToken = grantedRoom.controlToken;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 0;
    visibilityPending = true;
    setStatus(_("Updating game visibility..."));
    visibilityUpdate.begin(request);
    refreshControls();
}

void CrossplayMenu::confirmChatName() {
    if(stage == Stage::Choosing && !chatSession.empty()) {
        chat.cancel();
        chatPending = false;
        chatSession.clear();
        chatLabel.setText(_("Enter a name above, then confirm it to chat."));
        refreshControls();
        return;
    }
    if(chatPending || !chatSession.empty() || !validateAndSavePlayerName()) return;
    auto request = lobbyRequest();
    if(request.contentHash.empty()) { chatLabel.setText(_("Cannot check game content.")); return; }
    request.operation = AdmissionOperation::ChatEnter;
    request.displayName = playerNameTextBox.getText();
    chatAction = request.operation;
    chatPending = true;
    chatLabel.setText(_("Confirming your name..."));
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::sendLobbyChat() {
    if(chatSession.empty() || chatPending) return;
    const auto text = trimmed(chatInput.getText());
    if(text.empty()) return;
    if(text.size() > 120) { chatLabel.setText(_("Message is too long. Please shorten it.")); return; }
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::ChatSay;
    request.chatSession = chatSession;
    request.chatText = text;
    chatAction = request.operation;
    chatPending = true;
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::updateLobbyChat() {
    chat.update();
    if(chatPending && chat.status() != RoomAdmissionClient::Status::InProgress) {
        chatPending = false;
        if(chat.status() == RoomAdmissionClient::Status::Succeeded) {
            const auto& response = chat.response();
            if(chatAction == AdmissionOperation::ChatEnter) {
                chatSession = response.chatSession;
                chatCursor = response.chatCursor;
            } else if(chatAction == AdmissionOperation::ChatSay) {
                chatInput.setText("");
            } else if(chatAction == AdmissionOperation::ChatPoll) {
                if(response.chatCursor < chatCursor) {
                    chatSession.clear();
                    chatLabel.setText(_("Chat restarted. Confirm your name again."));
                } else {
                    if(response.chatGap) chatLines.push_back(_("Older lobby messages have expired."));
                    for(const auto& message : response.messages) {
                        if(message.id > chatCursor) chatLines.push_back(message.name + ": " + message.text);
                    }
                    chatCursor = response.chatCursor;
                    if(chatLines.size() > 60) chatLines.erase(chatLines.begin(), chatLines.end() - 60);
                    if(!response.messages.empty() || response.chatGap) {
                        std::string text;
                        for(const auto& line : chatLines) text += line + "\n";
                        chatHistory.setText(text);
                        chatHistory.scrollToEnd();
                    }
                }
            }
            if(!chatSession.empty()) chatLabel.setText(_("Public lobby chat - ") + playerNameTextBox.getText());
            nextChatPoll = SDL_GetTicks() + (chatAction == AdmissionOperation::ChatPoll ? 5000 : 0);
        } else {
            chatLabel.setText(chat.errorMessage());
            if(chat.response().errorCode == "session_expired") chatSession.clear();
            nextChatPoll = SDL_GetTicks() + 15000;
        }
        chat.cancel();
        refreshControls();
    }
    if(!chatPending && !chatSession.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), nextChatPoll)) {
        auto request = lobbyRequest();
        request.operation = AdmissionOperation::ChatPoll;
        request.chatSession = chatSession;
        request.chatCursor = chatCursor;
        chatAction = request.operation;
        chatPending = true;
        chat.begin(request);
        refreshControls();
    }
}

bool CrossplayMenu::validateAndSavePlayerName() {
    const std::string name = trimmed(playerNameTextBox.getText());
    if(name.empty() || !RoomRelay::isAcceptableDisplayName(name)) {
        openWindow(MsgBox::create(_("Please enter a player name.")));
        return false;
    }

    playerNameTextBox.setText(name);
    if(name != settings.general.playerName) {
        settings.general.playerName = name;
        INIFile configFile(getConfigFilepath());
        configFile.setStringValue("General", "Player Name", settings.general.playerName);
        configFile.saveChangesTo(getConfigFilepath());
    }
    return true;
}

void CrossplayMenu::onHostCustomGame() {
    if(stage == Stage::HostReady) {
        // Already in a room: pick a map and carry the same session into the lobby.
        const int result = CustomGameMenu(true, false).showMenu();
        if(result != MENU_QUIT_DEFAULT) {
            quit(result);
        } else if(pNetworkManager == nullptr || !pNetworkManager->isRelaySession()) {
            teardownSession(_("The online game ended."));
        }
        return;
    }

    hostingCoop = false;
    beginAdmission(true);
}

void CrossplayMenu::onHostCampaignCoop() {
    if(stage == Stage::HostReady) {
        SinglePlayerSkirmishMenu(true).showMenu();
        if(pNetworkManager == nullptr || !pNetworkManager->isRelaySession()) {
            teardownSession(_("The online game ended."));
        }
        return;
    }

    hostingCoop = true;
    beginAdmission(true);
}

void CrossplayMenu::onJoin() {
    std::string normalized;
    if(!RoomRelay::normalizeRoomCode(joinCodeTextBox.getText(), normalized)) {
        openWindow(MsgBox::create(_("That game code is not valid. Codes look like ABCD-EFGH-JKMN.")));
        return;
    }
    joinCodeTextBox.setText(normalized);
    beginAdmission(false);
}

void CrossplayMenu::onBack() {
    teardownSession(std::string());
    quit();
}

void CrossplayMenu::beginAdmission(bool hosting, bool publicJoin) {
    if(!validateAndSavePlayerName()) {
        return;
    }
    if(settings.network.activeDirectEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        return;
    }

    // Fail closed. Going online without being able to describe our own content would ask the
    // game service to match us against a fingerprint we never computed, and would leave the
    // lobby with nothing to compare either.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online. Reinstalling the game usually fixes this."));
        return;
    }

    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.hosting = hosting;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 0;
    if(hosting) {
        // Co-op is a two-player arrangement; a custom game uses the lobby's own limit.
        request.mode = hostingCoop ? "coop" : "custom";
        request.maxPeers = hostingCoop ? 2 : 4;
    } else {
        request.publicOnly = publicJoin;
        request.roomCode = publicJoin ? publicGames[publicGameList.getSelectedIndex()].roomCode
                                      : joinCodeTextBox.getText();
    }

    pendingHosting = hosting;
    directory.cancel();
    directoryPending = false;
    stage = Stage::Requesting;
    setStatus(hosting ? _("Creating a game...") : _("Looking for that game..."));
    refreshControls();

    admission.begin(request);
}

void CrossplayMenu::openDirectSession() {
    // The fingerprint is recomputed rather than remembered: admission and the handshake must
    // describe the same install, and anything that changed in between has to be caught here.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online."));
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    // Direct only. The address comes from this installation's settings, never from the admission
    // answer: a service that could hand out a gameplay endpoint could move the match back onto a
    // server, which is the whole thing this transport exists to stop. grantedRoom.socketUrl is
    // deliberately ignored.
    DirectRoomTransport::Config config;
    config.signalingBaseUrl = settings.network.activeDirectEndpoint();
    config.grant       = grantedRoom.grant;
    config.roomCode    = grantedRoom.roomCode;
    config.displayName = settings.general.playerName;
    config.appVersion  = VERSIONSTRING;
    config.contentHash = fingerprint;
    config.gameProtocolVersion = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    config.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
#ifdef __EMSCRIPTEN__
    config.runtime = "browser";
#else
    config.runtime = "native";
#endif

    try {
        pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    } catch(const std::exception& error) {
        setStatus(error.what());
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    std::string failure;
    if(!pNetworkManager->startDirectSession(config, failure)) {
        pNetworkManager.reset();
        setStatus(failure);
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    pNetworkManager->setOnReceiveGameInfo(
        std::bind(&CrossplayMenu::onReceiveGameInfo, this,
                  std::placeholders::_1, std::placeholders::_2));
    pNetworkManager->setOnPeerDisconnected(
        std::bind(&CrossplayMenu::onPeerDisconnected, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    roomCode = grantedRoom.roomCode;
    publicRoom = grantedRoom.visibility == "public";
    pNetworkManager->setPublicRelayRoom(publicRoom);
    stage = Stage::Connecting;
    setStatus(_("Connecting..."));
    refreshControls();
}

void CrossplayMenu::teardownSession(std::string reason) {
    // Own the reason before releasing the relay whose status may contain it.
    pendingGameInfo.reset();
    pendingLobbyChanges = ChangeEventList();
    pendingDisconnectReason.clear();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRelaySession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager->disconnect();
        pNetworkManager.reset();
    }

    roomCode.clear();
    grantedRoom = AdmissionResponse();
    stage = Stage::Choosing;
    if(!reason.empty()) setStatus(reason);
    hostCustomGameButton.setText(_("Host a Game"));
    hostCoopButton.setText(_("Host Campaign Co-op"));
    refreshControls();
}

void CrossplayMenu::update() {
    updateLobbyChat();
    visibilityUpdate.update();
    if(visibilityPending && visibilityUpdate.status() != RoomAdmissionClient::Status::InProgress) {
        visibilityPending = false;
        if(visibilityUpdate.status() == RoomAdmissionClient::Status::Succeeded) {
            publicRoom = visibilityUpdate.response().visibility == "public";
            if(!visibilityUpdate.response().roomCode.empty()) {
                roomCode = visibilityUpdate.response().roomCode;
                grantedRoom.roomCode = roomCode;
                if(pNetworkManager && pNetworkManager->getRelayClient())
                    pNetworkManager->getRelayClient()->updateInvitationCode(roomCode);
            }
            if(pNetworkManager) pNetworkManager->setPublicRelayRoom(publicRoom);
            setStatus(publicRoom ? _("Your public game is listed. Players can join from the lobby.")
                                 : _("Your private game is unlisted. Share its invitation code."));
        } else {
            setStatus(_("Visibility could not be confirmed. Choose it again to confirm your game and invitation code."));
        }
        visibilityChoice.setSelectedItem(publicRoom ? 0 : 1);
        visibilityUpdate.cancel();
        refreshControls();
    }
    directory.update();
    if(directoryPending && (directory.status() == RoomAdmissionClient::Status::Succeeded
                            || directory.status() == RoomAdmissionClient::Status::Failed)) {
        const int selected = publicGameList.getSelectedIndex();
        const std::string selectedRoom = selected >= 0 && static_cast<std::size_t>(selected) < publicGames.size()
            ? publicGames[selected].roomCode : std::string();
        publicGameList.clearAllEntries();
        publicGames.clear();
        nextDirectoryPage = 0;
        directoryPending = false;
        if(directory.status() == RoomAdmissionClient::Status::Succeeded) {
            publicGames = directory.response().games;
            nextDirectoryPage = directory.response().nextPage;
            for(std::size_t i = 0; i < publicGames.size(); ++i) {
                const auto& game = publicGames[i];
                publicGameList.addEntry(game.hostName + " - "
                    + (game.mode == "coop" ? _("Campaign co-op") : _("Custom game"))
                    + " - " + std::to_string(game.players) + "/" + std::to_string(game.maxPeers));
                if(game.roomCode == selectedRoom) publicGameList.setSelectedItem(static_cast<int>(i));
            }
            directoryLabel.setText(publicGames.empty() ? _("No open public games") : _("Public games - no code needed"));
        } else {
            directoryLabel.setText(_("Public list unavailable"));
            setStatus(directory.errorMessage());
        }
        directory.cancel();
        refreshControls();
    }
    if((stage == Stage::Choosing || stage == Stage::HostReady || stage == Stage::ClientWaiting)
       && !directoryPending
       && SDL_TICKS_PASSED(SDL_GetTicks(), nextDirectoryRefresh)) refreshPublicGames();
    // Network callbacks run inside NetworkManager::update(). Defer menu loops and
    // manager destruction until that dispatch has returned to MenuBase.
    if(!pendingDisconnectReason.empty()) {
        teardownSession(pendingDisconnectReason);
        return;
    }
    if(pendingGameInfo) {
        auto gameInfo = std::move(pendingGameInfo);
        auto changes = std::move(pendingLobbyChanges);
        enterReceivedLobby(*gameInfo, changes);
        return;
    }
    admission.update();

    if(stage == Stage::Requesting) {
        switch(admission.status()) {
            case RoomAdmissionClient::Status::Succeeded:
                grantedRoom = admission.response();
                admission.cancel();
                openDirectSession();
                break;
            case RoomAdmissionClient::Status::Failed:
                setStatus(admission.errorMessage());
                admission.cancel();
                stage = Stage::Choosing;
                refreshControls();
                break;
            default:
                break;
        }
        return;
    }

    if(pNetworkManager == nullptr || !pNetworkManager->isRelaySession()) {
        return;
    }

    RoomSessionTransport* relay = pNetworkManager->getRelayClient();
    if(relay == nullptr) {
        return;
    }

    if(stage == Stage::Connecting && relay->isJoined()) {
        roomCode = relay->roomCode();
        if(pendingHosting) {
            stage = Stage::HostReady;
            setStatus(publicRoom
                ? _("Your public game is listed. Choose a map or campaign while players join.")
                : _("Your private game is open. Share the invite code, then choose what to play."));
        } else {
            stage = Stage::ClientWaiting;
            setStatus(_("Joined. Waiting for the host to choose a map..."));
        }
        refreshControls();
        refreshPublicGames();
        return;
    }

    if(relay->status() == RoomSessionTransport::Status::Closed
       && (stage == Stage::Connecting || stage == Stage::HostReady
           || stage == Stage::ClientWaiting)) {
        teardownSession(relay->statusMessage().empty()
            ? std::string(_("The connection to the game was lost."))
            : relay->statusMessage());
    }
}

void CrossplayMenu::onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                                      const ChangeEventList& changeEventList) {
    if(pendingHosting || pendingGameInfo || !pendingDisconnectReason.empty()) {
        return;     // a host does not take a lobby from anybody
    }

    pendingGameInfo = std::make_unique<GameInitSettings>(gameInitSettings);
    pendingLobbyChanges = changeEventList;
}

void CrossplayMenu::enterReceivedLobby(const GameInitSettings& gameInitSettings,
                                       const ChangeEventList& changeEventList) {

    setStatus(_("Joining the game..."));

    auto pCustomGamePlayers = std::make_unique<CustomGamePlayers>(gameInitSettings, false);
    pCustomGamePlayers->onReceiveChangeEventList(changeEventList);
    const int result = pCustomGamePlayers->showMenu();
    pCustomGamePlayers.reset();

    switch(result) {
        case MENU_QUIT_DEFAULT:
            teardownSession(_("You left the game."));
            break;
        case MENU_QUIT_GAME_FINISHED:
            quit(MENU_QUIT_GAME_FINISHED);
            break;
        default:
            teardownSession(_("The connection to the game was lost."));
            break;
    }
}

void CrossplayMenu::onPeerDisconnected(const std::string& playerName, bool isHost, int cause) {
    if(!isHost) {
        return;
    }

    if(pNetworkManager && pNetworkManager->getRelayClient()) {
        const auto* relay = pNetworkManager->getRelayClient();
        if(relay->status() == RoomSessionTransport::Status::Closed && !relay->statusMessage().empty()) {
            pendingDisconnectReason = relay->statusMessage();
            return;
        }
    }
    std::string message;
    switch(cause) {
        case NETWORKDISCONNECT_TIMEOUT:
            message = _("The connection stopped responding.");
            break;
        case NETWORKDISCONNECT_GAME_FULL:
            message = _("There is no free player slot in this game left!");
            break;
        case NETWORKDISCONNECT_PROTOCOL_MISMATCH:
            message = _("That game was created by a different version of Dune City.");
            break;
        default:
            message = playerName.empty() ? std::string(_("The online game ended."))
                                         : (playerName + _(" left the game."));
            break;
    }
    // A host leaving arrives twice: once as that peer departing, once as the room closing. The
    // first carries the name and is the more useful sentence, so it is the one that is kept.
    if(pendingDisconnectReason.empty()) {
        pendingDisconnectReason = std::move(message);
    }
}
