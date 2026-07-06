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

#include <Menu/HouseChoiceMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <GUI/Spacer.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <Menu/HouseChoiceInfoMenu.h>
#include <SoundPlayer.h>


static const int houseOrder[] = { HOUSE_ATREIDES, HOUSE_ORDOS, HOUSE_HARKONNEN, HOUSE_MERCENARY, HOUSE_FREMEN, HOUSE_SARDAUKAR };

namespace {
const char* const kSupportPlayerClasses[] = {
    "",
    "qBotSupportEasy",
    "qBotSupportMedium",
    "qBotSupportHard",
    "qBotSupportBrutal"
};

constexpr int kSupportOptionCount = sizeof(kSupportPlayerClasses) / sizeof(kSupportPlayerClasses[0]);

const char* const kEnemyAIClasses[] = {
    "CampaignAIPlayer",
    "qBotEasy",
    "qBotMedium",
    "qBotHard",
    "qBotBrutal"
};

constexpr int kEnemyAIOptionCount = sizeof(kEnemyAIClasses) / sizeof(kEnemyAIClasses[0]);
}

// Static member definitions
int HouseChoiceMenu::s_supportBotIndex = 0;
int HouseChoiceMenu::s_enemyAIIndex = 0;
SettingsClass::GameOptionsClass HouseChoiceMenu::s_currentGameOptions;

HouseChoiceMenu::HouseChoiceMenu() : MenuBase()
{
    currentHouseChoiceScrollPos = 0;
    s_currentGameOptions = effectiveGameOptions;  // Use mod-aware effective options

    // set up window
    int xpos = std::max(0,(getRendererWidth() - 640)/2);
    int ypos = std::max(0,(getRendererHeight() - 560)/2);

    setCurrentPosition(xpos,ypos,640,560);

    setTransparentBackground(true);

    setWindowWidget(&windowWidget);


    selectYourHouseLabel.setTexture(pGFXManager->getUIGraphic(UI_SelectYourHouseLarge));
    windowWidget.addWidget(&selectYourHouseLabel, Point(0,0), Point(100, 640));

    // set up buttons
    house1Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 0));
    windowWidget.addWidget(&house1Button, Point(40,108),    Point(168,182));

    house2Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 1));
    windowWidget.addWidget(&house2Button, Point(235,108),   Point(168,182));

    house3Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 2));
    windowWidget.addWidget(&house3Button, Point(430,108),   Point(168,182));

    SDL_Texture *pArrowLeft = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftLarge);
    SDL_Texture *pArrowLeftHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftHighlightLarge);
    houseLeftButton.setTextures(pArrowLeftHighlight, pArrowLeftHighlight, pArrowLeftHighlight);
    houseLeftButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseLeft, this));
    houseLeftButton.setVisible(true);
    windowWidget.addWidget( &houseLeftButton, Point(320 - getWidth(pArrowLeft) - 85, 360), getTextureSize(pArrowLeft));

    SDL_Texture *pArrowRight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightLarge);
    SDL_Texture *pArrowRightHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightHighlightLarge);
    houseRightButton.setTextures(pArrowRightHighlight, pArrowRightHighlight, pArrowRightHighlight);
    houseRightButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseRight, this));
    houseRightButton.setVisible(true);
    windowWidget.addWidget( &houseRightButton, Point(320 + 85, 360), getTextureSize(pArrowRight));

    // Add AI options below the house selection and arrows
    // Position these centered below the arrows (at Y=390)
    int optionsX = 240;  // Centered at 320 (half of 640) - 80 (half of 160)
    int optionsY = 390;
    
    // AI Support label
    Label* supportLabel = Label::create(_("AI support: help you fight"));
    supportLabel->setTextColor(COLOR_WHITE);
    supportLabel->setTextFontSize(12);
    supportLabel->setAlignment(Alignment_HCenter);
    windowWidget.addWidget(supportLabel, Point(optionsX, optionsY - 11), Point(160, 16));
    
    // AI Support dropdown (gap increased by 3 pixels = 29 pixel gap total)
    supportBotDropDown.addEntry(_("AI Support: None"), 0);
    supportBotDropDown.addEntry(_("AI Support: Easy"), 1);
    supportBotDropDown.addEntry(_("AI Support: Medium"), 2);
    supportBotDropDown.addEntry(_("AI Support: Hard"), 3);
    supportBotDropDown.addEntry(_("AI Support: Brutal"), 4);
    supportBotDropDown.setSelectedItem(s_supportBotIndex);
    supportBotDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onSupportBotSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&supportBotDropDown, Point(optionsX, optionsY + 18), Point(160, 20));

    // Enemy AI label
    Label* enemyAILabel = Label::create(_("Enemy AI"));
    enemyAILabel->setTextColor(COLOR_WHITE);
    enemyAILabel->setTextFontSize(12);
    enemyAILabel->setAlignment(Alignment_HCenter);
    windowWidget.addWidget(enemyAILabel, Point(optionsX, optionsY + 43), Point(160, 16));
    
    // Enemy AI dropdown (gap increased by 3 pixels = 29 pixel gap total)
    enemyAIDropDown.addEntry(_("Enemy AI: Campaign AI"), 0);
    enemyAIDropDown.addEntry(_("Enemy AI: QuantBot Easy"), 1);
    enemyAIDropDown.addEntry(_("Enemy AI: QuantBot Medium"), 2);
    enemyAIDropDown.addEntry(_("Enemy AI: QuantBot Hard"), 3);
    enemyAIDropDown.addEntry(_("Enemy AI: QuantBot Brutal"), 4);
    enemyAIDropDown.setSelectedItem(s_enemyAIIndex);
    enemyAIDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onEnemyAISelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&enemyAIDropDown, Point(optionsX, optionsY + 72), Point(160, 20));

    // Game Options button
    gameOptionsButton.setText(_("Game Options"));
    gameOptionsButton.setOnClick(std::bind(&HouseChoiceMenu::onGameOptions, this));
    windowWidget.addWidget(&gameOptionsButton, Point(optionsX, optionsY + 100), Point(160, 20));

    updateHouseChoice();
}

HouseChoiceMenu::~HouseChoiceMenu() = default;

void HouseChoiceMenu::onChildWindowClose(Window* pChildWindow) {
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        s_currentGameOptions = pGameOptionsWindow->getGameOptions();
    }
}

void HouseChoiceMenu::onGameOptions() {
    openWindow(GameOptionsWindow::create(s_currentGameOptions));
}

void HouseChoiceMenu::onSupportBotSelectionChanged(bool /*interactive*/) {
    int entry = supportBotDropDown.getSelectedEntryIntData();
    s_supportBotIndex = (entry >= 0 && entry < kSupportOptionCount) ? entry : 0;
}

void HouseChoiceMenu::onEnemyAISelectionChanged(bool /*interactive*/) {
    int entry = enemyAIDropDown.getSelectedEntryIntData();
    s_enemyAIIndex = (entry >= 0 && entry < kEnemyAIOptionCount) ? entry : 0;
}

void HouseChoiceMenu::onHouseButton(int button) {
    int selectedHouse = houseOrder[currentHouseChoiceScrollPos+button];

    switch(selectedHouse) {
        case HOUSE_HARKONNEN:   soundPlayer->playVoice(HouseHarkonnen, selectedHouse);     break;
        case HOUSE_ATREIDES:    soundPlayer->playVoice(HouseAtreides, selectedHouse);      break;
        case HOUSE_ORDOS:       soundPlayer->playVoice(HouseOrdos, selectedHouse);         break;
        default:                /* no sounds for the other houses avail.*/  break;

    }

    int ret = HouseChoiceInfoMenu(selectedHouse).showMenu();
    quit(ret == MENU_QUIT_DEFAULT ? MENU_QUIT_DEFAULT : selectedHouse);
}


void HouseChoiceMenu::updateHouseChoice() {
    // House1 button
    house1Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+0]));

    // House2 button
    house2Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+1]));

    // House3 button
    house3Button.setTextures(pGFXManager->getUIGraphic(UI_Herald_ColoredLarge, houseOrder[currentHouseChoiceScrollPos+2]));
}

void HouseChoiceMenu::onHouseLeft()
{
    if(currentHouseChoiceScrollPos > 0) {
        currentHouseChoiceScrollPos--;
        updateHouseChoice();
    }
}

void HouseChoiceMenu::onHouseRight()
{
    if(currentHouseChoiceScrollPos < 3) {
        currentHouseChoiceScrollPos++;
        updateHouseChoice();
    }
}
