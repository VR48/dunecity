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
#include <Menu/CrossplayMenu.h>
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
#include <cctype>
#include <fstream>
#include <vector>

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

class ModesMenu final : public MenuBase {
public:
    ModesMenu() {
        SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
        setBackground(background);
        resize(getTextureSize(background));
        setWindowWidget(&windowWidget);

        singlePlayerButton.setText(_("SINGLE PLAYER"));
        singlePlayerButton.setOnClick([this]() { SinglePlayerMenu().showMenu(); });
        singlePlayerButton.setActive();
#ifdef __EMSCRIPTEN__
        // A browser has no UDP socket, so the LAN and direct-Internet choices in the mesh menu
        // could never work there. Offer only the thing that does: the crossplay room.
        multiPlayerButton.setText(_("PLAY ONLINE"));
        multiPlayerButton.setOnClick([this]() { CrossplayMenu().showMenu(); });
#else
        multiPlayerButton.setText(_("MULTIPLAYER"));
        multiPlayerButton.setOnClick([this]() { MultiPlayerMenu().showMenu(); });
#endif
        mapEditorButton.setText(_("MAP EDITOR"));
        mapEditorButton.setOnClick([this]() { MapEditor().RunEditor(); });
        modsButton.setText(_("MODS"));
        modsButton.setOnClick([this]() { ModMenu().showMenu(); });
        backButton.setText(_("BACK"));
        backButton.setOnClick([this]() { quit(); });

        SDL_Texture* planet = pGFXManager->getUIGraphic(UI_PlanetBackground);
        SDL_Texture* logo = pGFXManager->getUIGraphic(UI_DuneLegacy);
        planetPicture.setTexture(planet);
        logoPicture.setTexture(logo);

        TextButton* buttons[] = {&singlePlayerButton, &multiPlayerButton, &mapEditorButton,
                                 &modsButton, &backButton};
        if(validatedStartMenuMode(settings.video.startMenuMode) == 1) {
            const StartMenuLayout layout{getSize().x, getSize().y, 5};
            planetPicture.setFitToSize(true);
            windowWidget.addWidget(&planetPicture, layout.planetBounds());
            logoPicture.setFitToSize(true);
            windowWidget.addWidget(&logoPicture, layout.logoBounds());

            SDL_Texture* border = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
            buttonBorder.setTexture(border);
            buttonBorder.setStretchToSize(true);
            windowWidget.addWidget(&buttonBorder, layout.borderBounds());
            for(int i = 0; i < 5; ++i) windowWidget.addWidget(buttons[i], layout.button(i));
        } else {
            SDL_Rect planetBounds = calcAlignedDrawingRect(planet);
            planetBounds.y = planetBounds.y - getHeight(planet) / 2 + 10;
            windowWidget.addWidget(&planetPicture, planetBounds);

            SDL_Rect logoBounds = calcAlignedDrawingRect(logo);
            logoBounds.y = logoBounds.y + getHeight(logo) / 2 + 28;
            windowWidget.addWidget(&logoPicture, logoBounds);

            SDL_Texture* border = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
            buttonBorder.setTexture(border);
            SDL_Rect borderBounds = calcAlignedDrawingRect(border);
            borderBounds.y = borderBounds.y + getHeight(border) / 2 + 59;
            windowWidget.addWidget(&buttonBorder, borderBounds);

            constexpr int listHeight = 111;
            constexpr int gap = 3;
            constexpr int buttonHeight = (listHeight - 4 * gap) / 5;
            const int x = (getSize().x - 160) / 2;
            const int y = getSize().y / 2 + 64;
            for(int i = 0; i < 5; ++i) {
                windowWidget.addWidget(buttons[i], Point(x, y + i * (buttonHeight + gap)),
                                       Point(160, buttonHeight));
            }
        }
    }

private:
    StaticContainer windowWidget;
    PictureLabel planetPicture;
    PictureLabel logoPicture;
    PictureLabel buttonBorder;
    TextButton singlePlayerButton;
    TextButton multiPlayerButton;
    TextButton mapEditorButton;
    TextButton modsButton;
    TextButton backButton;
};
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
    enlargedStartMenus = validatedStartMenuMode(settings.video.startMenuMode) == 1;

    modesButton.setText(_("MODES"));
    modesButton.setOnClick(std::bind(&MainMenu::onModes, this));
    modesButton.setActive();
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
    SDL_Texture* pPlanet = pGFXManager->getUIGraphic(UI_PlanetBackground);
    SDL_Texture* pLogo = pGFXManager->getUIGraphic(UI_DuneLegacy);
    planetPicture.setTexture(pPlanet);
    logoPicture.setTexture(pLogo);

    if(enlargedStartMenus) {
        const StartMenuLayout layout{getSize().x, getSize().y, 6};
        planetPicture.setFitToSize(true);
        windowWidget.addWidget(&planetPicture, layout.planetBounds());
        logoPicture.setFitToSize(true);
        windowWidget.addWidget(&logoPicture, layout.logoBounds());

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        buttonBorder.setStretchToSize(true);
        windowWidget.addWidget(&buttonBorder, layout.borderBounds());
    } else {
        SDL_Rect planetBounds = calcAlignedDrawingRect(pPlanet);
        planetBounds.y = planetBounds.y - getHeight(pPlanet) / 2 + 10;
        windowWidget.addWidget(&planetPicture, planetBounds);

        SDL_Rect logoBounds = calcAlignedDrawingRect(pLogo);
        logoBounds.y = logoBounds.y + getHeight(pLogo) / 2 + 28;
        windowWidget.addWidget(&logoPicture, logoBounds);

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        SDL_Rect borderBounds = calcAlignedDrawingRect(pBorder);
        borderBounds.y = borderBounds.y + getHeight(pBorder) / 2 + 59;
        windowWidget.addWidget(&buttonBorder, borderBounds);

    }
    TextButton* allButtons[] = {&modesButton, &optionsButton, &displayButton, &howToPlayButton,
                                &dune2rEditorButton, &aboutButton, &quitButton};
    for(TextButton* button : allButtons) {
        windowWidget.addWidget(button, Point(0, 0), Point(1, 1));
    }
    // The generic product logo must not imply DuneCity rules when Vanilla is active.
    logoPicture.setVisible(false);
    activeModLabel.setTextFontSize(24);
    // Same-colour shadow supplies an extra pixel of weight to the lettering.
    activeModLabel.setTextColor(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
    activeModLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    windowWidget.addWidget(&activeModLabel, Point(0, 0), Point(1, 1));
    refreshContextButtons();
    modVersionLabel.setTextFontSize(14);
    modVersionLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    modVersionLabel.setAlignment(enlargedStartMenus
        ? Alignment_HCenter
        : static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
    refreshModVersionLabel();
    if(enlargedStartMenus) {
        windowWidget.addWidget(&modVersionLabel, Point(24, getSize().y - 30), Point(getSize().x - 48, 24));
    } else {
        windowWidget.addWidget(&modVersionLabel, Point(12, getSize().y - 58), Point(220, 50));
    }
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
        std::transform(modDisplayName.begin(), modDisplayName.end(), modDisplayName.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string bannerText = "MOD: " + modDisplayName;
        int bannerFontSize = 24;
        const int bannerWidth = std::min(getSize().x - 48, 420);
        while (bannerFontSize > 12 && GUIStyle::getInstance().getMinimumLabelSize(bannerText, bannerFontSize).x > bannerWidth)
            --bannerFontSize;
        activeModLabel.setTextFontSize(bannerFontSize);
        activeModLabel.setText(bannerText);
        modVersionLabel.setText("v" + std::string(VERSION));
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
    refreshContextButtons();

    // Process version check results
#ifndef __EMSCRIPTEN__
    if(pVersionChecker) {
        pVersionChecker->update();
    }
#endif

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

void MainMenu::onModes() const
{
    ModesMenu().showMenu();
}

void MainMenu::onDune2REditor() const
{
    if(ModManager::instance().isInitialized()
       && ModManager::instance().getActiveModName() == "Dune2R") {
        Dune2REditorMenu editor;
        editor.showMenu();
    }
}

void MainMenu::refreshContextButtons()
{
    ModManager& modManager = ModManager::instance();
    const std::string activeMod = modManager.isInitialized() ? modManager.getActiveModName() : std::string();
    const bool showDune2R = activeMod == "Dune2R";
    const bool showHowToPlay = activeMod == "dunecity";
    dune2rEditorButton.setVisible(showDune2R);
    dune2rEditorButton.setEnabled(showDune2R);
    howToPlayButton.setVisible(showHowToPlay);
    howToPlayButton.setEnabled(showHowToPlay);

    std::vector<TextButton*> buttons{&modesButton, &optionsButton, &displayButton};
    if(showHowToPlay) buttons.push_back(&howToPlayButton);
    if(showDune2R) buttons.push_back(&dune2rEditorButton);
    buttons.push_back(&aboutButton);
    buttons.push_back(&quitButton);

    if(enlargedStartMenus) {
        const StartMenuLayout layout{getSize().x, getSize().y, static_cast<int>(buttons.size())};
        const auto planetBounds = layout.planetBounds();
        const auto logoBounds = layout.logoBounds();
        const auto borderBounds = layout.borderBounds();
        windowWidget.setWidgetGeometry(&planetPicture, Point(planetBounds.x, planetBounds.y),
                                       Point(planetBounds.w, planetBounds.h));
        windowWidget.setWidgetGeometry(&logoPicture, Point(logoBounds.x, logoBounds.y),
                                       Point(logoBounds.w, logoBounds.h));
        const int bannerWidth = std::min(getSize().x - 48, 420);
        windowWidget.setWidgetGeometry(&activeModLabel,
            Point((getSize().x - bannerWidth) / 2, logoBounds.y - 8), Point(bannerWidth, 38));
        windowWidget.setWidgetGeometry(&buttonBorder, Point(borderBounds.x, borderBounds.y),
                                       Point(borderBounds.w, borderBounds.h));
        for(size_t i = 0; i < buttons.size(); ++i) {
            const auto bounds = layout.button(static_cast<int>(i));
            windowWidget.setWidgetGeometry(buttons[i], Point(bounds.x, bounds.y), Point(bounds.w, bounds.h));
        }
    } else {
        const int bannerWidth = std::min(getSize().x - 48, 420);
        windowWidget.setWidgetGeometry(&activeModLabel,
            Point((getSize().x - bannerWidth) / 2, getSize().y / 2 + 20), Point(bannerWidth, 38));
        constexpr int listHeight = 128;
        constexpr int gap = 3;
        const int buttonHeight = (listHeight - (static_cast<int>(buttons.size()) - 1) * gap)
                                 / static_cast<int>(buttons.size());
        const int x = (getSize().x - 160) / 2;
        const int y = getSize().y / 2 + 64;
        for(size_t i = 0; i < buttons.size(); ++i) {
            windowWidget.setWidgetGeometry(buttons[i], Point(x, y + static_cast<int>(i) * (buttonHeight + gap)),
                                           Point(160, buttonHeight));
        }
    }
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


