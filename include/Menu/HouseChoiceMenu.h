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

#ifndef HOUSECHOICEMENU_H
#define HOUSECHOICEMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/PictureLabel.h>
#include <GUI/PictureButton.h>
#include <DataTypes.h>
#include <GUI/TextView.h>
#include <mod/ModInfo.h>

class HouseChoiceMenu : public MenuBase {
public:
    HouseChoiceMenu();
    virtual ~HouseChoiceMenu();

    void onChildWindowClose(Window* pChildWindow) override;

    // Static accessors for AI settings (so SinglePlayerMenu can read them)
    static int getStartLevel() { return s_startLevel; }
    static int getSupportBotIndex() { return s_supportBotIndex; }
    static int getEnemyAIIndex() { return s_enemyAIIndex; }
    static const SettingsClass::GameOptionsClass& getGameOptions() { return s_currentGameOptions; }

private:
    void onHouseButton(int button);
    void onModSelectionChanged(bool interactive);
    void updateModDescription();
    void updateHouseChoice();

    void onHouseLeft();
    void onHouseRight();
    
    void onGameOptions();
    void onSupportBotSelectionChanged(bool interactive);
    void onEnemyAISelectionChanged(bool interactive);

    StaticContainer windowWidget;
    VBox            optionsVBox;

    PictureLabel    selectYourHouseLabel;

    PictureButton   house1Button;
    PictureButton   house2Button;
    PictureButton   house3Button;

    PictureButton   houseLeftButton;
    PictureButton   houseRightButton;

    DropDownBox     startLevelDropDown;
    DropDownBox     modDropDown;
    TextView        modDescription;
    std::vector<ModInfo> availableMods;
    TextButton      backButton;
    Label          supportDescription;
    DropDownBox     supportBotDropDown;
    DropDownBox     enemyAIDropDown;
    TextButton hostCoopButton;
    TextButton      gameOptionsButton;

    int currentHouseChoiceScrollPos;

    // Static storage for AI settings
    static int s_startLevel;
    static int s_supportBotIndex;
    static int s_enemyAIIndex;
    static SettingsClass::GameOptionsClass s_currentGameOptions;
};

#endif // HOUSECHOICEMENU_H
