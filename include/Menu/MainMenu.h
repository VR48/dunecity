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

#ifndef MAINMENU_H
#define MAINMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Spacer.h>
#include <GUI/PictureLabel.h>
#include <GUI/Label.h>
#include <Network/VersionChecker.h>

#include <memory>

class MainMenu final : public MenuBase {
public:
    MainMenu();
    virtual ~MainMenu();

    MainMenu(const MainMenu &) = delete;
    MainMenu(MainMenu &&) = delete;
    MainMenu& operator=(const MainMenu &) = delete;
    MainMenu& operator=(MainMenu &&) = delete;

    virtual int showMenu() override;
    virtual void update() override;
    virtual void onChildWindowClose(Window* pChildWindow) override;

private:
    void onSinglePlayer() const;
    void onMultiPlayer() const;
    void onMapEditor() const;
    void onMods() const;
    void onOptions();
    void onAbout() const;
    void onQuit();

    /// Refresh the bottom-left mod/version watermark from the current
    /// active mod. Cheap; no-op when the displayed mod hasn't changed.
    void refreshModVersionLabel();

    StaticContainer windowWidget;
    VBox            MenuButtons;

    TextButton      singlePlayerButton;
    TextButton      multiPlayerButton;
    TextButton      mapEditorButton;
    TextButton      modsButton;
    TextButton      optionsButton;
    TextButton      aboutButton;
    TextButton      quitButton;

    PictureLabel    planetPicture;
    PictureLabel    duneLegacy;
    PictureLabel    buttonBorder;

    Label           modVersionLabel; ///< Bottom-right "<active mod>\nv<VERSION>" watermark.
    std::string     lastShownModName; ///< Tracks last mod name written to modVersionLabel; avoids redundant setText.

    // Version checking
    std::unique_ptr<VersionChecker> pVersionChecker;
    bool bVersionCheckStarted = false;
    bool bUpdateDialogShown = false;
    std::string latestVersion;
    std::string downloadURL;
};

#endif // MAINMENU_H
