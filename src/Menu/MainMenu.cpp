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

#include <Menu/MainMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <MapEditor/MapEditor.h>

#include <Menu/SinglePlayerMenu.h>
#include <Menu/MultiPlayerMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/ModMenu.h>
#include <Menu/AboutMenu.h>

#include <GUI/QstBox.h>
#include <misc/DiscordManager.h>
#include <mod/ModManager.h>
#include <mod/ModInfo.h>
#include <config.h>

MainMenu::MainMenu()
{
    // Update Discord Rich Presence
    DiscordManager::instance().setMainMenu();
    
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    // set up pictures in the background
    // set up pictures in the background
    SDL_Texture* pPlanetBackground = pGFXManager->getUIGraphic(UI_PlanetBackground);
    planetPicture.setTexture(pPlanetBackground);
    SDL_Rect dest1 = calcAlignedDrawingRect(pPlanetBackground);
    dest1.y = dest1.y - getHeight(pPlanetBackground)/2 + 10;
    windowWidget.addWidget(&planetPicture, dest1);

    SDL_Texture* pDuneLegacy = pGFXManager->getUIGraphic(UI_DuneLegacy);
    duneLegacy.setTexture(pDuneLegacy);
    SDL_Rect dest2 = calcAlignedDrawingRect(pDuneLegacy);
    dest2.y = dest2.y + getHeight(pDuneLegacy)/2 + 28;
    windowWidget.addWidget(&duneLegacy, dest2);

    SDL_Texture* pMenuButtonBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
    buttonBorder.setTexture(pMenuButtonBorder);
    SDL_Rect dest3 = calcAlignedDrawingRect(pMenuButtonBorder);
    dest3.y = dest3.y + getHeight(pMenuButtonBorder)/2 + 59;
    windowWidget.addWidget(&buttonBorder, dest3);

    // set up menu buttons
    windowWidget.addWidget(&MenuButtons,Point((getRendererWidth() - 160)/2,getRendererHeight()/2 + 64),Point(160,128));

    singlePlayerButton.setText(_("SINGLE PLAYER"));
    singlePlayerButton.setOnClick(std::bind(&MainMenu::onSinglePlayer, this));
    MenuButtons.addWidget(&singlePlayerButton);
    singlePlayerButton.setActive();

    MenuButtons.addWidget(VSpacer::create(3));

    multiPlayerButton.setText(_("MULTIPLAYER"));
    multiPlayerButton.setOnClick(std::bind(&MainMenu::onMultiPlayer, this));
    MenuButtons.addWidget(&multiPlayerButton);

    MenuButtons.addWidget(VSpacer::create(3));

//    MenuButtons.addWidget(VSpacer::create(16));
    mapEditorButton.setText(_("MAP EDITOR"));
    mapEditorButton.setOnClick(std::bind(&MainMenu::onMapEditor, this));
    MenuButtons.addWidget(&mapEditorButton);

    MenuButtons.addWidget(VSpacer::create(3));

    modsButton.setText(_("MODS"));
    modsButton.setOnClick(std::bind(&MainMenu::onMods, this));
    MenuButtons.addWidget(&modsButton);

    MenuButtons.addWidget(VSpacer::create(3));

    optionsButton.setText(_("OPTIONS"));
    optionsButton.setOnClick(std::bind(&MainMenu::onOptions, this));
    MenuButtons.addWidget(&optionsButton);

    MenuButtons.addWidget(VSpacer::create(3));

    aboutButton.setText(_("ABOUT"));
    aboutButton.setOnClick(std::bind(&MainMenu::onAbout, this));
    MenuButtons.addWidget(&aboutButton);

    MenuButtons.addWidget(VSpacer::create(3));

    quitButton.setText(_("QUIT"));
    quitButton.setOnClick(std::bind(&MainMenu::onQuit, this));
    MenuButtons.addWidget(&quitButton);

    // Bottom-right watermark: <active mod display name> stacked over v<VERSION>.
    {
        modVersionLabel.setTextFontSize(16);
        modVersionLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        modVersionLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
        refreshModVersionLabel();

        const int labelWidth  = 220;
        const int labelHeight = 50;
        const int marginX     = 12;
        const int marginY     = 8;
        windowWidget.addWidget(&modVersionLabel,
                               Point(marginX,
                                     getRendererHeight() - labelHeight - marginY),
                               Point(labelWidth, labelHeight));
    }
}

void MainMenu::refreshModVersionLabel()
{
    std::string activeModName;
    std::string modDisplayName = "Vanilla";
    ModManager& modManager = ModManager::instance();
    if (modManager.isInitialized()) {
        activeModName = modManager.getActiveModName();
        ModInfo info = modManager.getModInfo(activeModName);
        if (!info.displayName.empty()) {
            modDisplayName = info.displayName;
        } else if (!info.name.empty()) {
            modDisplayName = info.name;
        }
    }

    if (activeModName == lastShownModName) {
        return;
    }
    lastShownModName = activeModName;
    modVersionLabel.setText(modDisplayName + "\nv" + std::string(VERSION));
}

MainMenu::~MainMenu() = default;

int MainMenu::showMenu()
{
    musicPlayer->changeMusic(MUSIC_MENU);

    // Start version check in background (only once)
    if(!bVersionCheckStarted) {
        bVersionCheckStarted = true;

        pVersionChecker = std::make_unique<VersionChecker>(settings.network.metaServer);
        pVersionChecker->setOnVersionCheckComplete([this](const VersionInfo& info) {
            if(info.updateAvailable && !bUpdateDialogShown) {
                latestVersion = info.latestVersion;
                downloadURL = info.downloadURL;
                // Show dialog in update() when safe (not during callback)
            }
        });
        pVersionChecker->checkForUpdates();
    }

    return MenuBase::showMenu();
}

void MainMenu::update()
{
    // Mod can be switched from any sub-menu (ModMenu, CustomGameMenu,
    // CustomGamePlayers); refresh the watermark on every tick so it
    // tracks the live ModManager state when control returns here.
    refreshModVersionLabel();

    // Process version check results
    if(pVersionChecker) {
        pVersionChecker->update();
    }

    // Show update dialog if new version available and not already shown
    if(!latestVersion.empty() && !bUpdateDialogShown && !pChildWindow) {
        bUpdateDialogShown = true;

        std::string message = _("A new version of Dune City is available!");
        message += "\n\n";
        message += _("Current: ");
        message += VERSION;
        message += "\n";
        message += _("Latest: ");
        message += latestVersion;
        message += "\n\n";
        message += _("Would you like to visit the download page?");

        openWindow(QstBox::create(message, _("Download"), _("Later"), QSTBOX_BUTTON1));
    }
}

void MainMenu::onChildWindowClose(Window* pChildWindow)
{
    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
    if(pQstBox != nullptr && pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
        // User clicked "Download" - open the download URL
        if(!downloadURL.empty()) {
            SDL_OpenURL(downloadURL.c_str());
        }
    }
}

void MainMenu::onSinglePlayer() const
{
    SinglePlayerMenu singlePlayerMenu;
    singlePlayerMenu.showMenu();
}

void MainMenu::onMultiPlayer() const
{
    MultiPlayerMenu multiPlayerMenu;
    multiPlayerMenu.showMenu();
}

void MainMenu::onMapEditor() const
{
    MapEditor mapEditor;
    mapEditor.RunEditor();
}

void MainMenu::onMods() const
{
    ModMenu modMenu;
    modMenu.showMenu();
}

void MainMenu::onOptions() {
    OptionsMenu  optionsMenu;
    int ret = optionsMenu.showMenu();

    if(ret == MENU_QUIT_REINITIALIZE) {
        quit(MENU_QUIT_REINITIALIZE);
    }
}

void MainMenu::onAbout() const
{
    AboutMenu myAbout;
    myAbout.showMenu();
}

void MainMenu::onQuit() {
    quit();
}
