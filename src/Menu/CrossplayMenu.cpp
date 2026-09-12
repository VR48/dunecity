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

#include <Network/NetworkManager.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <config.h>
#include <globals.h>
#include <main.h>
#include <misc/FileSystem.h>
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
        // Something could not be hashed locally. Send nothing rather than something malformed;
        // the lobby's own config exchange still catches a genuine mismatch.
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

    captionLabel.setText(_("Play Online"));
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(16));

    playerNameLabel.setText(_("Player Name:"));
    playerNameHBox.addWidget(&playerNameLabel, 120);
    playerNameTextBox.setText(settings.general.playerName);
    playerNameTextBox.setMaximumTextLength(20);
    playerNameHBox.addWidget(&playerNameTextBox, 220);
    playerNameHBox.addWidget(Spacer::create());
    mainVBox.addWidget(&playerNameHBox, 28);

    mainVBox.addWidget(VSpacer::create(16));

    statusLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&statusLabel, 56);

    roomCodeLabel.setAlignment(Alignment_HCenter);
    roomCodeLabel.setTextFontSize(24);
    mainVBox.addWidget(&roomCodeLabel, 34);

    mainVBox.addWidget(VSpacer::create(16));

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

    mainVBox.addWidget(VSpacer::create(16));

    joinLabel.setText(_("Game Code:"));
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

    mainVBox.addWidget(Spacer::create(), 0.8);

    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&CrossplayMenu::onBack, this));
    buttonHBox.addWidget(HSpacer::create(70));
    buttonHBox.addWidget(&backButton, 0.1);
    buttonHBox.addWidget(Spacer::create(), 0.9);
    mainVBox.addWidget(&buttonHBox, 24);

    // Say plainly why online play is not offered, rather than failing later.
    if(settings.network.activeRelayEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        stage = Stage::Finished;
    } else {
        const RelayWebSocketSupport support = relayWebSocketSupport();
        if(!support.available) {
            setStatus(support.reason);
            stage = Stage::Finished;
        } else {
            setStatus(_("Host a game and share the code, or type a friend's code to join."));
        }
    }

    refreshControls();
}

CrossplayMenu::~CrossplayMenu() {
    admission.cancel();
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
    joinButton.setEnabled(idle);
    joinCodeTextBox.setEnabled(idle);
    playerNameTextBox.setEnabled(idle);

    // Once in a room as the host, the two host buttons become "what do you want to play".
    if(stage == Stage::HostReady) {
        hostCustomGameButton.setEnabled(true);
        hostCoopButton.setEnabled(true);
        hostCustomGameButton.setText(_("Choose a Map"));
        hostCoopButton.setText(_("Choose a Campaign Mission"));
    }

    const bool showCode = !roomCode.empty()
        && (stage == Stage::HostReady || stage == Stage::ClientWaiting);
    roomCodeLabel.setText(showCode ? (_("Game code: ") + roomCode) : std::string());

    backButton.setEnabled(!busy);
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

void CrossplayMenu::beginAdmission(bool hosting) {
    if(!validateAndSavePlayerName()) {
        return;
    }
    if(settings.network.activeRelayEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        return;
    }

    AdmissionRequest request;
    request.baseUrl = settings.network.activeRelayEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    request.contentHash = contentFingerprint();
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.hosting = hosting;
    if(hosting) {
        // Co-op is a two-player arrangement; a custom game uses the lobby's own limit.
        request.mode = hostingCoop ? "coop" : "custom";
        request.maxPeers = hostingCoop ? 2 : 4;
    } else {
        request.roomCode = joinCodeTextBox.getText();
    }

    pendingHosting = hosting;
    stage = Stage::Requesting;
    setStatus(hosting ? _("Creating a game...") : _("Looking for that game..."));
    refreshControls();

    admission.begin(request);
}

void CrossplayMenu::openRelaySession() {
    RoomRelayClient::Config config;
    config.socketUrl   = grantedRoom.socketUrl;
    config.grant       = grantedRoom.grant;
    config.displayName = settings.general.playerName;
    config.appVersion  = VERSIONSTRING;
    config.contentHash = contentFingerprint();
    config.gameProtocolVersion = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    config.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
#ifdef __EMSCRIPTEN__
    config.runtime = "browser";
    // The browser sets Origin itself and does not let a page choose one.
    config.origin.clear();
#else
    config.runtime = "native";
    config.origin.clear();
#endif

    try {
        pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::RoomRelay);
    } catch(const std::exception& error) {
        setStatus(error.what());
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    std::string failure;
    if(!pNetworkManager->startRelaySession(config, failure)) {
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
    stage = Stage::Connecting;
    setStatus(_("Connecting..."));
    refreshControls();
}

void CrossplayMenu::teardownSession(const std::string& reason) {
    admission.cancel();
    if(pNetworkManager != nullptr && pNetworkManager->isRelaySession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager->disconnect();
        pNetworkManager.reset();
    }

    roomCode.clear();
    if(!reason.empty()) {
        setStatus(reason);
        stage = Stage::Finished;
    } else {
        stage = Stage::Choosing;
    }
    hostCustomGameButton.setText(_("Host a Game"));
    hostCoopButton.setText(_("Host Campaign Co-op"));
    refreshControls();
}

void CrossplayMenu::update() {
    admission.update();

    if(stage == Stage::Requesting) {
        switch(admission.status()) {
            case RoomAdmissionClient::Status::Succeeded:
                grantedRoom = admission.response();
                admission.cancel();
                openRelaySession();
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

    RoomRelayClient* relay = pNetworkManager->getRelayClient();
    if(relay == nullptr) {
        return;
    }

    if(stage == Stage::Connecting && relay->isJoined()) {
        roomCode = relay->roomCode();
        if(pendingHosting) {
            stage = Stage::HostReady;
            setStatus(_("Your game is open. Give the code below to a friend, then choose what to play."));
        } else {
            stage = Stage::ClientWaiting;
            setStatus(_("Joined. Waiting for the host to choose a map..."));
        }
        refreshControls();
        return;
    }

    if(relay->status() == RoomRelayClient::Status::Closed
       && (stage == Stage::Connecting || stage == Stage::HostReady
           || stage == Stage::ClientWaiting)) {
        teardownSession(relay->statusMessage().empty()
            ? std::string(_("The connection to the game was lost."))
            : relay->statusMessage());
    }
}

void CrossplayMenu::onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                                      const ChangeEventList& changeEventList) {
    if(pendingHosting) {
        return;     // a host does not take a lobby from anybody
    }

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
    teardownSession(message);
}
