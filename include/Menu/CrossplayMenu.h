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

#ifndef CROSSPLAYMENU_H
#define CROSSPLAYMENU_H

/**
    Hosting and joining a game that desktop and browser players can share.

    A host creates a room and gets a short code to pass to a friend; a friend types the code in.
    There is no address to enter and no router to configure, because both sides only ever make
    one outbound connection to the same game service.
*/

#include <GUI/Checkbox.h>
#include <GUI/DropDownBox.h>
#include <GUI/HBox.h>
#include <GUI/Label.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextBox.h>
#include <GUI/TextButton.h>
#include <GUI/Spacer.h>
#include <GUI/VBox.h>

#include <Network/ChangeEventList.h>
#include <Network/RoomAdmissionClient.h>

#include <GameInitSettings.h>

#include <memory>
#include <string>

#include "MenuBase.h"

class CrossplayMenu : public MenuBase {
public:
    CrossplayMenu();
    ~CrossplayMenu() override;

    void update() override;

private:
    enum class Stage {
        Choosing,       ///< nothing in flight
        Requesting,     ///< waiting for the game service to answer
        Connecting,     ///< opening the game connection
        HostReady,      ///< in the room as host; may now choose what to play
        ClientWaiting,  ///< in the room as a guest; waiting for the host's lobby
        Finished        ///< the session ended; the reason is on screen
    };

    void onHostCustomGame();
    void onHostCampaignCoop();
    void onJoin();
    void onBack();

    void beginAdmission(bool hosting);
    void openRelaySession();
    void teardownSession(std::string reason);
    void enterReceivedLobby(const GameInitSettings& gameInitSettings,
                            const ChangeEventList& changeEventList);
    void setStatus(const std::string& message);
    void refreshControls();

    bool validateAndSavePlayerName();

    void onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                           const ChangeEventList& changeEventList);
    void onPeerDisconnected(const std::string& playerName, bool isHost, int cause);

    /// Fingerprint of the bundled content, as the relay and the lobby both understand it.
    static std::string contentFingerprint();

    Stage       stage = Stage::Choosing;
    bool        hostingCoop = false;
    bool        pendingHosting = false;
    std::string roomCode;
    std::string statusText;
    std::unique_ptr<GameInitSettings> pendingGameInfo;
    ChangeEventList pendingLobbyChanges;
    std::string pendingDisconnectReason;

    RoomAdmissionClient admission;
    AdmissionResponse   grantedRoom;

    StaticContainer windowWidget;
    VBox            mainVBox;
    Label           captionLabel;

    HBox            playerNameHBox;
    Label           playerNameLabel;
    TextBox         playerNameTextBox;

    Label           statusLabel;
    Label           roomCodeLabel;
    HBox            roomCodeHBox;
    TextButton      copyCodeButton;

    HBox            hostHBox;
    TextButton      hostCustomGameButton;
    TextButton      hostCoopButton;

    HBox            joinHBox;
    Label           joinLabel;
    TextBox         joinCodeTextBox;
    TextButton      joinButton;

    HBox            buttonHBox;
    TextButton      backButton;
};

#endif // CROSSPLAYMENU_H
