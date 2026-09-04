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
#include <Menu/DisplayMenu.h>
#include <misc/MenuLayout.h>
#include <Menu/ModMenu.h>
#include <Menu/Dune2REditorMenu.h>
#include <Menu/AboutMenu.h>
#include <Menu/HowToPlayMenu.h>

#include <GUI/QstBox.h>
#include <misc/DiscordManager.h>
#include <misc/fnkdat.h>
#include <mod/ModManager.h>
#include <mod/ModInfo.h>
#include <config.h>

#include <cstdio>
#include <fstream>

namespace {
// Marker file under the user config dir. Once written, the first-launch
// "Enable city-sim mod?" prompt is suppressed forever.
std::string firstLaunchMarkerPath() {
    char tmp[FILENAME_MAX];
    if (fnkdat("dunecity-first-launch.done", tmp, FILENAME_MAX,
               FNKDAT_USER | FNKDAT_CREAT) < 0) {
        return std::string();
    }
    return std::string(tmp);
}

bool firstLaunchMarkerExists() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return true; // fail closed: don't pester
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

void writeFirstLaunchMarker() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return;
    std::ofstream out(p);
    out << "Dune City " << VERSION << "\n";
}
} // namespace

MainMenu::MainMenu()
{
    // Update Discord Rich Presence
    DiscordManager::instance().setMainMenu();
    
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    singlePlayerButton.setText(_("SINGLE PLAYER"));
    singlePlayerButton.setOnClick(std::bind(&MainMenu::onSinglePlayer, this));
    singlePlayerButton.setActive();
    multiPlayerButton.setText(_("MULTIPLAYER"));
    multiPlayerButton.setOnClick(std::bind(&MainMenu::onMultiPlayer, this));
    mapEditorButton.setText(_("MAP EDITOR"));
    mapEditorButton.setOnClick(std::bind(&MainMenu::onMapEditor, this));
    modsButton.setText(_("MODS"));
    modsButton.setOnClick(std::bind(&MainMenu::onMods, this));
    dune2rEditorButton.setText("DUNE2R ASSETS");
    dune2rEditorButton.setOnClick(std::bind(&MainMenu::onDune2REditor, this));
    optionsButton.setText(_("OPTIONS"));
    optionsButton.setOnClick(std::bind(&MainMenu::onOptions, this));
    displayButton.setText(_("DISPLAY"));
    displayButton.setOnClick(std::bind(&MainMenu::onDisplay, this));
    howToPlayButton.setText(_("HOW TO PLAY"));
    howToPlayButton.setOnClick(std::bind(&MainMenu::onHowToPlay, this));
    aboutButton.setText(_("ABOUT"));
    aboutButton.setOnClick(std::bind(&MainMenu::onAbout, this));
    quitButton.setText(_("QUIT"));
    quitButton.setOnClick(std::bind(&MainMenu::onQuit, this));
    const StartMenuLayout layout{getSize().x, getSize().y, 5};
    planetPicture.setTexture(pGFXManager->getUIGraphic(UI_PlanetBackground));
    planetPicture.setFitToSize(true);
    windowWidget.addWidget(&planetPicture, layout.planetBounds());
    logoPicture.setTexture(pGFXManager->getUIGraphic(UI_DuneLegacy));
    logoPicture.setFitToSize(true);
    windowWidget.addWidget(&logoPicture, layout.logoBounds());
    TextButton* buttons[] = {&singlePlayerButton, &multiPlayerButton, &modsButton, &optionsButton,
        &displayButton, &howToPlayButton, &mapEditorButton, &dune2rEditorButton, &aboutButton, &quitButton};
    for(int i = 0; i < 10; ++i) windowWidget.addWidget(buttons[i], layout.button(i));
    refreshDune2REditorButton();
    modVersionLabel.setTextFontSize(14);
    modVersionLabel.setAlignment(Alignment_HCenter);
    refreshModVersionLabel();
    windowWidget.addWidget(&modVersionLabel, Point(24, getSize().y - 30), Point(getSize().x - 48, 24));
}

void MainMenu::refreshModVersionLabel()
{
    std::string activeModName;
    std::string modDisplayName = "Vanilla";
    ModManager& modManager = ModManager::instance();
    if (modManager.isInitialized()) {
        activeModName = modManager.getActiveModName();
        // v1.0.510: defensive null guard. Tornie's ModInfo.displayName was
        // observed empty on some mod bundles (Tornie was registered but
        // the ModInfo was never populated past init). Reading an empty
        // string then concatenating with "\nv" was crashing in some
        // label rendering paths downstream. Fall back to the raw mod
        // name in that case.
        try {
            ModInfo info = modManager.getModInfo(activeModName);
            if (!info.displayName.empty()) {
                modDisplayName = info.displayName;
            } else if (!info.name.empty()) {
                modDisplayName = info.name;
            } else if (!activeModName.empty()) {
                modDisplayName = activeModName;
            }
        } catch (const std::exception& e) {
            SDL_Log("MainMenu: refreshModVersionLabel failed: %s — using raw mod name", e.what());
            modDisplayName = activeModName.empty() ? "Unknown" : activeModName;
        }
    }

    if (activeModName == lastShownModName && !modVersionLabel.getText().empty()) {
        return;
    }
    lastShownModName = activeModName;
    try {
        modVersionLabel.setText(modDisplayName + "  v" + std::string(VERSION));
    } catch (const std::exception& e) {
        SDL_Log("MainMenu: setText failed: %s", e.what());
    }
}

MainMenu::~MainMenu() = default;

int MainMenu::showMenu()
{
    int menuResult = -1;
    try {
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

        menuResult = MenuBase::showMenu();
    } catch(const std::exception& e) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: %s — returning to caller with code -1", e.what());
    } catch(...) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: unknown exception — returning to caller with code -1");
    }
    return menuResult;
}

void MainMenu::update()
{
    // Mod can be switched from any sub-menu (ModMenu, CustomGameMenu,
    // CustomGamePlayers); refresh the watermark on every tick so it
    // tracks the live ModManager state when control returns here.
    refreshModVersionLabel();
    refreshDune2REditorButton();

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

    // First-launch "Enable city-sim mod?" prompt. Runs after the update
    // dialog so we don't stack two QstBoxes on top of each other.
    showFirstLaunchCityPromptIfNeeded();
}

void MainMenu::onChildWindowClose(Window* pChildWindow)
{
    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
    if (pQstBox == nullptr) return;

    if (bFirstLaunchPromptOpen) {
        // This QstBox was the first-launch "Enable city-sim mod?" prompt.
        bFirstLaunchPromptOpen = false;
        writeFirstLaunchMarker(); // record the user's decision either way

        if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
            ModManager& mm = ModManager::instance();
            if (mm.setActiveMod("dunecity")) {
                // Reinitialize so all subsystems pick up the new mod's
                // ObjectData.ini, QuantBot Config.ini, and game options.
                quit(MENU_QUIT_REINITIALIZE);
            }
        }
        return;
    }

    if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
        // User clicked "Download" - open the download URL
        if (!downloadURL.empty()) {
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

void MainMenu::onDune2REditor() const
{
    if(ModManager::instance().isInitialized()
       && ModManager::instance().getActiveModName() == "Dune2R") {
        Dune2REditorMenu editor;
        editor.showMenu();
    }
}

void MainMenu::refreshDune2REditorButton()
{
    const bool available = ModManager::instance().isInitialized()
                           && ModManager::instance().getActiveModName() == "Dune2R";
    if(dune2rButtonLayoutInitialized && dune2rEditorButton.isVisible() == available) return;
    dune2rButtonLayoutInitialized = true;
    dune2rEditorButton.setVisible(available);
    dune2rEditorButton.setEnabled(available);
    const StartMenuLayout layout{getSize().x, getSize().y, 5};
    const auto aboutBounds = layout.button(available ? 8 : 7);
    windowWidget.setWidgetGeometry(&aboutButton, Point(aboutBounds.x, aboutBounds.y), Point(aboutBounds.w, aboutBounds.h));
    auto quitBounds = layout.button(available ? 9 : 8);
    if(!available) quitBounds.x = (getSize().x - quitBounds.w) / 2;
    windowWidget.setWidgetGeometry(&quitButton, Point(quitBounds.x, quitBounds.y), Point(quitBounds.w, quitBounds.h));
}

void MainMenu::onOptions() {
    OptionsMenu  optionsMenu;
    int ret = optionsMenu.showMenu();

    if(ret == MENU_QUIT_REINITIALIZE) {
        quit(MENU_QUIT_REINITIALIZE);
    }
}

void MainMenu::onDisplay() {
    if(DisplayMenu().showMenu() == MENU_QUIT_REINITIALIZE) quit(MENU_QUIT_REINITIALIZE);
}

void MainMenu::onAbout() const
{
    AboutMenu myAbout;
    myAbout.showMenu();
}

void MainMenu::onHowToPlay() const
{
    HowToPlayMenu menu;
    menu.showMenu();
}

void MainMenu::onQuit() {
    quit();
}

void MainMenu::showFirstLaunchCityPromptIfNeeded()
{
    if (bFirstLaunchPromptChecked) return;
    bFirstLaunchPromptChecked = true;

    // Don't compete with the version-update dialog.
    if (bUpdateDialogShown || pChildWindow != nullptr) {
        // Reschedule on next tick by un-flagging.
        bFirstLaunchPromptChecked = false;
        return;
    }

    ModManager& mm = ModManager::instance();
    if (!mm.isInitialized()) return;

    // Already on a city-sim mod -> nothing to prompt.
    if (mm.isCityModeActive()) {
        writeFirstLaunchMarker();
        return;
    }

    // Already shown previously -> respect the user's choice.
    if (firstLaunchMarkerExists()) return;

    // Need the dunecity mod to exist before we can offer to activate it.
    if (!mm.modExists("dunecity")) return;

    bFirstLaunchPromptOpen = true;

    std::string message = _("Welcome to Dune City!");
    message += "\n\n";
    message += _("Build a city on Arrakis with districts, roads,\npower and public services.");
    message += "\n\n";
    message += _("Enable Dune City now?\nYou can change this later in MODS.");

    auto* prompt = QstBox::create(message, _("Enable now"), _("Later"), QSTBOX_BUTTON1);
    openWindow(prompt);
}


