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

#include <Menu/SinglePlayerMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <misc/fnkdat.h>
#include <misc/MenuLayout.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>

#include <Menu/CustomGameMenu.h>
#include <Menu/SinglePlayerSkirmishMenu.h>
#include <Menu/HouseChoiceMenu.h>

#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/MsgBox.h>

#include <Game.h>
#include <GameInitSettings.h>
#include <sand.h>

SinglePlayerMenu::SinglePlayerMenu() : MenuBase() {
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    campaignButton.setText(_("CAMPAIGN"));
    campaignButton.setOnClick(std::bind(&SinglePlayerMenu::onCampaign, this));
    campaignButton.setActive();
    customButton.setText(_("CUSTOM GAME"));
    customButton.setOnClick(std::bind(&SinglePlayerMenu::onCustom, this));
    skirmishButton.setText(_("SKIRMISH"));
    skirmishButton.setOnClick(std::bind(&SinglePlayerMenu::onSkirmish, this));
    loadSavegameButton.setText(_("LOAD GAME"));
    loadSavegameButton.setOnClick(std::bind(&SinglePlayerMenu::onLoadSavegame, this));
    loadReplayButton.setText(_("LOAD REPLAY"));
    loadReplayButton.setOnClick(std::bind(&SinglePlayerMenu::onLoadReplay, this));
    cancelButton.setText(_("BACK"));
    cancelButton.setOnClick(std::bind(&SinglePlayerMenu::onCancel, this));
    const StartMenuLayout layout{getSize().x, getSize().y, 3};
    planetPicture.setTexture(pGFXManager->getUIGraphic(UI_PlanetBackground));
    planetPicture.setFitToSize(true);
    windowWidget.addWidget(&planetPicture, layout.planetBounds());
    logoPicture.setTexture(pGFXManager->getUIGraphic(UI_DuneLegacy));
    logoPicture.setFitToSize(true);
    windowWidget.addWidget(&logoPicture, layout.logoBounds());
    TextButton* buttons[] = {&campaignButton, &customButton, &skirmishButton,
                             &loadSavegameButton, &loadReplayButton, &cancelButton};
    for(int i = 0; i < 6; ++i) windowWidget.addWidget(buttons[i], layout.button(i));
}

SinglePlayerMenu::~SinglePlayerMenu() = default;

void SinglePlayerMenu::onCampaign() {
    int player = HouseChoiceMenu().showMenu();

    if(player < 0) {
        return;
    }

    // Get AI settings from HouseChoiceMenu static storage
    int supportBotIndex = HouseChoiceMenu::getSupportBotIndex();
    int enemyAIIndex = HouseChoiceMenu::getEnemyAIIndex();
    SettingsClass::GameOptionsClass gameOptions = HouseChoiceMenu::getGameOptions();

    const char* const kSupportPlayerClasses[] = {
        "",
        "qBotSupportEasy",
        "qBotSupportMedium",
        "qBotSupportHard",
        "qBotSupportBrutal"
    };

    const char* const kEnemyAIClasses[] = {
        "CampaignAIPlayer",
        "qBotEasy",
        "qBotMedium",
        "qBotHard",
        "qBotBrutal"
    };

    const bool supportSelected = (supportBotIndex > 0);
    const char* supportPlayerClass = supportSelected ? kSupportPlayerClasses[supportBotIndex] : nullptr;
    const char* enemyAIClass = kEnemyAIClasses[enemyAIIndex];

    GameInitSettings init((HOUSETYPE) player, gameOptions);
    if(supportSelected) {
        init.setMultiplePlayersPerHouse(true);
    }

    for(int houseID = 0; houseID < NUM_HOUSES; houseID++) {
        if(!isHouseAvailable(static_cast<HOUSETYPE>(houseID))) {
            continue;
        }
        if(houseID == player) {
            GameInitSettings::HouseInfo humanHouseInfo((HOUSETYPE) player, 1);
            humanHouseInfo.addPlayerInfo( GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS) );

            if(supportSelected && supportPlayerClass != nullptr && *supportPlayerClass != '\0') {
                std::string allyName = getHouseNameByNumber((HOUSETYPE) houseID) + " " + _("(AI Support)");
                humanHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(allyName, supportPlayerClass));
            }

            init.addHouseInfo(humanHouseInfo);
        } else {
            GameInitSettings::HouseInfo aiHouseInfo((HOUSETYPE) houseID, 2);
            aiHouseInfo.addPlayerInfo( GameInitSettings::PlayerInfo(getHouseNameByNumber( (HOUSETYPE) houseID), enemyAIClass) );
            init.addHouseInfo(aiHouseInfo);
        }
    }

    startSinglePlayerGame(init);

    quit();
}

void SinglePlayerMenu::onCustom() {
    CustomGameMenu(false).showMenu();
}

void SinglePlayerMenu::onSkirmish() {
    SinglePlayerSkirmishMenu().showMenu();
}

void SinglePlayerMenu::onLoadSavegame() {
    char tmp[FILENAME_MAX];
    fnkdat("save/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Game"), savepath, "dls"));
}

void SinglePlayerMenu::onLoadReplay() {
    char tmp[FILENAME_MAX];
    fnkdat("replay/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string replaypath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Replay"), replaypath, "rpl"));
}

void SinglePlayerMenu::onCancel() {
    quit();
}

void SinglePlayerMenu::onChildWindowClose(Window* pChildWindow) {
    std::string filename = "";
    std::string extension = "";
    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        filename = pLoadSaveWindow->getFilename();
        extension = pLoadSaveWindow->getExtension();
    }

    if(filename != "") {
        if(extension == "dls") {

            try {
                startSinglePlayerGame( GameInitSettings(filename) );
            } catch (std::exception& e) {
                // most probably the savegame file is not valid or from a different dune legacy version
                openWindow(MsgBox::create(e.what()));
            }
        } else if(extension == "rpl") {
            startReplay(filename);
        }
    }
}
