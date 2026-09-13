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
#include <Menu/SinglePlayerSkirmishMenu.h>
#include <mod/ModManager.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <GUI/Spacer.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <Menu/HouseChoiceInfoMenu.h>
#include <SoundPlayer.h>


namespace {
const int houseOrder[] = {
    HOUSE_ATREIDES,
    HOUSE_ORDOS,
    HOUSE_HARKONNEN,
    HOUSE_MERCENARY,
    HOUSE_FREMEN,
    HOUSE_SARDAUKAR,
    HOUSE_NEUTRAL,
    HOUSE_REBELS,
    HOUSE_CUSTOM
};

constexpr int kVisibleHouseButtons = 3;
int getHouseChoiceCount() {
    const int capacity = sizeof(houseOrder) / sizeof(houseOrder[0]);
    return isHouseAvailable(HOUSE_CUSTOM) ? capacity : capacity - 1;
}

int getMaxHouseScrollPos() {
    return getHouseChoiceCount() - kVisibleHouseButtons;
}

const char* const kSupportPlayerClasses[] = {
    "",
    "qBotSupportEasy",
    "qBotSupportMedium",
    "qBotSupportHard",
    "qBotSupportBrutal",
    "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend"
};

constexpr int kSupportOptionCount = sizeof(kSupportPlayerClasses) / sizeof(kSupportPlayerClasses[0]);

const char* const kEnemyAIClasses[] = {
    "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend", "CampaignAIPlayer"
};

constexpr int kEnemyAIOptionCount = sizeof(kEnemyAIClasses) / sizeof(kEnemyAIClasses[0]);
}

// Static member definitions
int HouseChoiceMenu::s_startLevel = 1;
int HouseChoiceMenu::s_supportBotIndex = 0;
int HouseChoiceMenu::s_enemyAIIndex = 0;
SettingsClass::GameOptionsClass HouseChoiceMenu::s_currentGameOptions;

HouseChoiceMenu::HouseChoiceMenu() : MenuBase()
{
    currentHouseChoiceScrollPos = 0;
    s_currentGameOptions = effectiveGameOptions;  // Use mod-aware effective options

    // set up window
    int xpos = std::max(0,(getRendererWidth() - 640)/2);
    int ypos = std::max(0,(getRendererHeight() - 480)/2);

    setCurrentPosition(xpos,ypos,640,480);

    setTransparentBackground(true);

    setWindowWidget(&windowWidget);


    selectYourHouseLabel.setTexture(pGFXManager->getUIGraphic(UI_SelectYourHouseLarge));
    selectYourHouseLabel.setFitToSize(true);
    windowWidget.addWidget(&selectYourHouseLabel, Point(0,0), Point(640, 56));

    // set up buttons
    house1Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 0));
    windowWidget.addWidget(&house1Button, Point(40,60),    Point(168,182));

    house2Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 1));
    windowWidget.addWidget(&house2Button, Point(235,60),   Point(168,182));

    house3Button.setOnClick(std::bind(&HouseChoiceMenu::onHouseButton, this, 2));
    windowWidget.addWidget(&house3Button, Point(430,60),   Point(168,182));

    SDL_Texture *pArrowLeft = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftLarge);
    SDL_Texture *pArrowLeftHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftHighlightLarge);
    houseLeftButton.setTextures(pArrowLeftHighlight, pArrowLeftHighlight, pArrowLeftHighlight);
    houseLeftButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseLeft, this));
    houseLeftButton.setVisible(true);
    windowWidget.addWidget( &houseLeftButton, Point(320 - getWidth(pArrowLeft) - 85, 250), getTextureSize(pArrowLeft));

    SDL_Texture *pArrowRight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightLarge);
    SDL_Texture *pArrowRightHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightHighlightLarge);
    houseRightButton.setTextures(pArrowRightHighlight, pArrowRightHighlight, pArrowRightHighlight);
    houseRightButton.setOnClick(std::bind(&HouseChoiceMenu::onHouseRight, this));
    houseRightButton.setVisible(true);
    windowWidget.addWidget( &houseRightButton, Point(320 + 85, 250), getTextureSize(pArrowRight));

    auto label = [this](const char* text, int x, int y) {
        auto* item = Label::create(_(text));
        item->setTextFontSize(12);
        item->setTextColor(COLOR_WHITE);
        item->setAlignment(Alignment_Left);
        windowWidget.addWidget(item, Point(x, y), Point(256, 18));
    };
    label("Start from level", 48, 294);
    for(int level = 1; level <= 9; ++level)
        startLevelDropDown.addEntry(_("Level ") + std::to_string(level), level);
    startLevelDropDown.setSelectedItem(s_startLevel - 1);
    startLevelDropDown.setOnSelectionChange([this](bool) {
        s_startLevel = std::clamp(startLevelDropDown.getSelectedEntryIntData(), 1, 9);
    });
    windowWidget.addWidget(&startLevelDropDown, Point(48, 315), Point(256, 22));
    label("Begin here, then continue the campaign.", 48, 339);

    label("Campaign mod", 48, 365);
    availableMods = ModManager::instance().listMods();
    int activeIndex = 0;
    for(size_t i = 0; i < availableMods.size(); ++i) {
        const auto& mod = availableMods[i];
        modDropDown.addEntry(mod.displayName.empty() ? mod.name : mod.displayName, static_cast<int>(i));
        if(mod.name == ModManager::instance().getActiveModName()) activeIndex = static_cast<int>(i);
    }
    modDropDown.setSelectedItem(activeIndex);
    modDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onModSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&modDropDown, Point(48, 386), Point(256, 22));
    modDescription.setTextFontSize(11);
    modDescription.setTextColor(COLOR_WHITE);
    windowWidget.addWidget(&modDescription, Point(48, 411), Point(256, 38));
    updateModDescription();

    label("AI partner", 336, 294);
    supportBotDropDown.addEntry(_("None"), 0);
    supportBotDropDown.addEntry(_("AI Support (Easy)"), 1);
    supportBotDropDown.addEntry(_("AI Support (Medium)"), 2);
    supportBotDropDown.addEntry(_("AI Support (Hard)"), 3);
    supportBotDropDown.addEntry(_("AI Support (Brutal)"), 4);
    supportBotDropDown.addEntry(_("QuantBot Easy"), 5);
    supportBotDropDown.addEntry(_("QuantBot Medium"), 6);
    supportBotDropDown.addEntry(_("QuantBot Hard"), 7);
    supportBotDropDown.addEntry(_("QuantBot Brutal"), 8);
    supportBotDropDown.addEntry(_("QuantBot Defend"), 9);
    supportBotDropDown.setSelectedItem(s_supportBotIndex);
    supportBotDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onSupportBotSelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&supportBotDropDown, Point(336, 315), Point(256, 22));
    supportDescription.setTextFontSize(10);
    supportDescription.setTextColor(COLOR_WHITE);
    supportDescription.setAlignment(Alignment_Left);
    windowWidget.addWidget(&supportDescription, Point(336, 339), Point(256, 30));
    onSupportBotSelectionChanged(false);

    label("Enemy AI", 336, 377);
    enemyAIDropDown.addEntry(_("QuantBot Easy"), 0);
    enemyAIDropDown.addEntry(_("QuantBot Medium"), 1);
    enemyAIDropDown.addEntry(_("QuantBot Hard"), 2);
    enemyAIDropDown.addEntry(_("QuantBot Brutal"), 3);
    enemyAIDropDown.addEntry(_("QuantBot Defend"), 4);
    enemyAIDropDown.addEntry(_("Campaign AI"), 5);
    enemyAIDropDown.setSelectedItem(s_enemyAIIndex);
    enemyAIDropDown.setOnSelectionChange(std::bind(&HouseChoiceMenu::onEnemyAISelectionChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&enemyAIDropDown, Point(336, 398), Point(256, 22));
    label("Choose how your opponents fight.", 336, 425);

    gameOptionsButton.setText(_("Game Options"));
    gameOptionsButton.setOnClick(std::bind(&HouseChoiceMenu::onGameOptions, this));
    windowWidget.addWidget(&gameOptionsButton, Point(80, 455), Point(150, 22));
    hostCoopButton.setText(_("Host Co-op"));
    hostCoopButton.setTooltipText(_("Open the co-op lobby to configure a shared campaign."));
    hostCoopButton.setOnClick([] { SinglePlayerSkirmishMenu(true).showMenu(); });
    windowWidget.addWidget(&hostCoopButton, Point(245, 455), Point(150, 22));
    backButton.setText(_("Back"));
    backButton.setOnClick([this] { quit(); });
    windowWidget.addWidget(&backButton, Point(410, 455), Point(150, 22));
    updateHouseChoice();
}

HouseChoiceMenu::~HouseChoiceMenu() = default;

void HouseChoiceMenu::onChildWindowClose(Window* pChildWindow) {
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        s_currentGameOptions = pGameOptionsWindow->getGameOptions();
        // Choices made here become the new defaults, the same as in Options.
        saveGameOptionsAsDefaults(s_currentGameOptions);
    }
}

void HouseChoiceMenu::onGameOptions() {
    openWindow(GameOptionsWindow::create(s_currentGameOptions));
}

void HouseChoiceMenu::onSupportBotSelectionChanged(bool /*interactive*/) {
    int entry = supportBotDropDown.getSelectedEntryIntData();
    s_supportBotIndex = (entry >= 0 && entry < kSupportOptionCount) ? entry : 0;
    supportDescription.setText(s_supportBotIndex >= 5
        ? _("QuantBot plays for you: economy, building\nand unit control (including combat).")
        : _("AI Support: economy and construction.\nYou command combat units."));
}

void HouseChoiceMenu::onEnemyAISelectionChanged(bool /*interactive*/) {
    int entry = enemyAIDropDown.getSelectedEntryIntData();
    s_enemyAIIndex = (entry >= 0 && entry < kEnemyAIOptionCount) ? entry : 0;
}

void HouseChoiceMenu::onHouseButton(int button) {
    int selectedHouse = houseOrder[currentHouseChoiceScrollPos+button];

    const HOUSETYPE selectedIdentity =
        getHouseFactionIdentity(static_cast<HOUSETYPE>(selectedHouse));
    switch(selectedIdentity) {
        case HOUSE_HARKONNEN:   soundPlayer->playVoice(HouseHarkonnen, selectedHouse); break;
        case HOUSE_ATREIDES:    soundPlayer->playVoice(HouseAtreides, selectedHouse);  break;
        case HOUSE_ORDOS:       soundPlayer->playVoice(HouseOrdos, selectedHouse);     break;
        case HOUSE_FREMEN:      soundPlayer->playVoice(HouseAtreides, selectedHouse);  break;
        case HOUSE_SARDAUKAR:   soundPlayer->playVoice(HouseHarkonnen, selectedHouse); break;
        case HOUSE_MERCENARY:   soundPlayer->playVoice(HouseOrdos, selectedHouse);     break;
        case HOUSE_NEUTRAL:
        case HOUSE_WILDSPADE:
            soundPlayer->playVoice(HouseAtreides, selectedHouse);
            break;
        case HOUSE_REBELS:
        case HOUSE_KLESHMERSH:
            soundPlayer->playVoice(HouseHarkonnen, selectedHouse);
            break;
        case HOUSE_CUSTOM:
        case HOUSE_THARPIQUE: {
            const HOUSETYPE fallbackHouse =
                getHouseFallbackHouse(static_cast<HOUSETYPE>(selectedHouse));
            switch(fallbackHouse) {
                case HOUSE_ATREIDES:
                case HOUSE_FREMEN:
                case HOUSE_NEUTRAL:
                    soundPlayer->playVoice(HouseAtreides, selectedHouse);
                    break;
                case HOUSE_ORDOS:
                case HOUSE_MERCENARY:
                    soundPlayer->playVoice(HouseOrdos, selectedHouse);
                    break;
                default:
                    soundPlayer->playVoice(HouseHarkonnen, selectedHouse);
                    break;
            }
        } break;
        default:
            break;
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
    if(currentHouseChoiceScrollPos < getMaxHouseScrollPos()) {
        currentHouseChoiceScrollPos++;
        updateHouseChoice();
    }
}

void HouseChoiceMenu::updateModDescription() {
    const int index = modDropDown.getSelectedEntryIntData();
    if(index < 0 || index >= static_cast<int>(availableMods.size())) return;
    const auto& mod = availableMods[index];
    const std::string description = mod.description.empty()
        ? _("Use this mod's campaign content and rules.") : mod.description;
    modDescription.setText(description);
}

void HouseChoiceMenu::onModSelectionChanged(bool interactive) {
    if(!interactive) return;
    const int index = modDropDown.getSelectedEntryIntData();
    if(index < 0 || index >= static_cast<int>(availableMods.size())) return;
    auto& manager = ModManager::instance();
    if(availableMods[index].name != manager.getActiveModName()) {
        if(!manager.setActiveMod(availableMods[index].name)) {
            for(size_t i = 0; i < availableMods.size(); ++i)
                if(availableMods[i].name == manager.getActiveModName())
                    modDropDown.setSelectedItem(static_cast<int>(i));
            openWindow(MsgBox::create(_("Could not load that mod. The previous mod is still selected.")));
        } else {
            effectiveGameOptions = manager.loadEffectiveGameOptions(settings.gameOptions);
            s_currentGameOptions = effectiveGameOptions;
        }
    }
    currentHouseChoiceScrollPos = std::min(currentHouseChoiceScrollPos, getMaxHouseScrollPos());
    updateHouseChoice();
    updateModDescription();
}
