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

#include <Menu/OptionsMenu.h>

#include <globals.h>

#include <GUI/Label.h>
#include <GUI/Spacer.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/MsgBox.h>

#include <main.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/music/MusicPlayer.h>

#include <SoundPlayer.h>

#include <players/PlayerFactory.h>

#include <misc/Scaler.h>
#include <misc/FileSystem.h>
#include <misc/format.h>

#include <algorithm>

OptionsMenu::OptionsMenu() : MenuBase()
{
    determineAvailableScreenResolutions();

    std::list<std::string> languagesList = getFileNamesList(getDuneLegacyDataDir() + "/locale", "po", true, FileListOrder_Name_Asc);
    if(languagesList.empty()) {
        languagesList = getFileNamesList(getDuneLegacyDataDir() + "/data/locale", "po", true, FileListOrder_Name_Asc);
    }
    availLanguages = std::vector<std::string>(languagesList.begin(), languagesList.end());

    currentGameOptions = effectiveGameOptions;  // Use mod-aware effective options

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    const int left = (getSize().x - 540) / 2;
    title.setText(_("OPTIONS"));
    title.setTextFontSize(22);
    title.setTextColor(COLOR_BLACK, COLOR_TRANSPARENT);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(left, 18), Point(540, 30));
    const char* tabNames[] = {"GENERAL", "DISPLAY", "AUDIO / NETWORK"};
    for(int page = 0; page < 3; ++page) {
        pageButtons[page].setText(_(tabNames[page]));
        pageButtons[page].setToggleButton(true);
        pageButtons[page].setOnClick([this, page] { showPage(page); });
        windowWidget.addWidget(&pageButtons[page], Point(left + page * 182, 58), Point(176, 36));
        windowWidget.addWidget(&pages[page], Point(left, 110), Point(540, getSize().y - 192));
    }
    auto optionLabel = [](const std::string& text) {
        auto* label = Label::create(text);
        label->setTextColor(COLOR_BLACK, COLOR_TRANSPARENT);
        label->setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
        return label;
    };

    NameHBox.addWidget(Spacer::create(), 0.5);
    NameHBox.addWidget(optionLabel(_("Player Name")), 190);
    nameTextBox.setMaximumTextLength(MAX_PLAYERNAMELENGHT);
    nameTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    NameHBox.addWidget(&nameTextBox, 290);
    NameHBox.addWidget(Spacer::create(), 0.5);
    nameTextBox.setText(settings.general.playerName);

    pages[0].addWidget(&NameHBox, 40);
    pages[0].addWidget(VSpacer::create(8));

    gameOptionsHBox.addWidget(Spacer::create(), 0.5);

    gameOptionsHBox.addWidget(optionLabel(_("Default Game Options")), 190);
    gameOptionsButton.setText(_("Change..."));
    gameOptionsButton.setOnClick(std::bind(&OptionsMenu::onGameOptions, this));
    gameOptionsHBox.addWidget(&gameOptionsButton, 130);

    gameOptionsHBox.addWidget(Spacer::create(), 160);

    gameOptionsHBox.addWidget(Spacer::create(), 0.5);

    pages[0].addWidget(&gameOptionsHBox, 40);
    pages[0].addWidget(VSpacer::create(8));

    languageHBox.addWidget(Spacer::create(), 0.5);
    languageHBox.addWidget(optionLabel(_("Language")), 190);

    for(size_t i = 0; i < availLanguages.size(); i++) {
        languageDropDownBox.addEntry(availLanguages[i].substr(0, availLanguages[i].size()-6), i);
        if(availLanguages[i].substr(availLanguages[i].size()-5,2) == settings.general.language) {
            languageDropDownBox.setSelectedItem(i);
        }
    }

    languageDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    languageHBox.addWidget(&languageDropDownBox, 100);
    languageHBox.addWidget(Spacer::create(), 190);
    languageHBox.addWidget(Spacer::create(), 0.5);

    pages[0].addWidget(&languageHBox, 40);
    pages[0].addWidget(VSpacer::create(8));

    generalHBox.addWidget(Spacer::create(), 0.5);
    generalHBox.addWidget(optionLabel(_("Campaign AI")), 190);
    int visibleIndex = 0;
    int selectedVisibleIndex = -1;
    for(unsigned int i=1; i<PlayerFactory::getList().size(); i++) {
        const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(i);
        if(playerData == nullptr) {
            continue;
        }
        const std::string& playerClass = playerData->getPlayerClass();
        if(playerClass.rfind("qBotSupport", 0) == 0) {
            continue;
        }
        aiDropDownBox.addEntry(playerData->getName(), i);
        if(playerClass == settings.ai.campaignAI) {
            selectedVisibleIndex = visibleIndex;
        }
        visibleIndex++;
    }
    if(selectedVisibleIndex >= 0) {
        aiDropDownBox.setSelectedItem(selectedVisibleIndex);
    } else if(visibleIndex > 0) {
        aiDropDownBox.setSelectedItem(0);
    }
    aiDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    generalHBox.addWidget(&aiDropDownBox, 290);
    introCheckbox.setText(_("Play Intro"));
    introCheckbox.setChecked(settings.general.playIntro);
    introCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    introHBox.addWidget(Spacer::create(), 0.5);
    introHBox.addWidget(&introCheckbox, 480);
    introHBox.addWidget(Spacer::create(), 0.5);
    pages[0].addWidget(&introHBox, 40);
    pages[0].addWidget(VSpacer::create(8));
    generalHBox.addWidget(Spacer::create(), 0.5);

    pages[0].addWidget(&generalHBox, 40);
    pages[0].addWidget(VSpacer::create(8));

    resolutionHBox.addWidget(Spacer::create(), 0.5);
#ifdef __ANDROID__
    resolutionHBox.addWidget(optionLabel(_("Interface Resolution")), 190);
#else
    resolutionHBox.addWidget(optionLabel(_("Video Resolution")), 190);
#endif

    int i = 0;
    for(const Coord& coord : availScreenRes) {
        int factor = getLogicalToPhysicalResolutionFactor(coord.x, coord.y);
#ifdef __ANDROID__
        factor = 1;
#endif
        if(factor > 1) {
            resolutionDropDownBox.addEntry(fmt::sprintf("%d x %d @ %dx", coord.x, coord.y, factor), i);
        } else {
            resolutionDropDownBox.addEntry(fmt::sprintf("%d x %d", coord.x, coord.y), i);
        }
        if(
#ifdef __ANDROID__
            coord.y == settings.video.interfaceHeight
#else
            coord.x == settings.video.physicalWidth && coord.y == settings.video.physicalHeight
#endif
        ) {
            resolutionDropDownBox.setSelectedItem(i);
        }
        i++;
    }
    resolutionDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&resolutionDropDownBox, 130);
    resolutionHBox.addWidget(Spacer::create(), 5);
    zoomlevelDropDownBox.addEntry("Zoom 1x", 0);
    zoomlevelDropDownBox.addEntry("Zoom 2x", 1);
    zoomlevelDropDownBox.addEntry("Zoom 3x", 2);
    zoomlevelDropDownBox.setSelectedItem(settings.video.preferredZoomLevel);
    zoomlevelDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&zoomlevelDropDownBox, 72);
    resolutionHBox.addWidget(Spacer::create(), 5);
    for(int i = 0; i < Scaler::NumScaler; i++) {
        scalerDropDownBox.addEntry(Scaler::getScalerName((Scaler::ScalerType) i));
    }
    Scaler::ScalerType currentScaler = Scaler::getScalerByName(settings.video.scaler);
    scalerDropDownBox.setSelectedItem(currentScaler >= 0 ? currentScaler : Scaler::ScaleHD);
    scalerDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&scalerDropDownBox, 78);

    resolutionHBox.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&resolutionHBox, 40);
    pages[1].addWidget(VSpacer::create(8));

    videoHBox.addWidget(Spacer::create(), 0.5);
    fullScreenCheckbox.setText(_("Full Screen"));
    fullScreenCheckbox.setChecked(settings.video.fullscreen);
    fullScreenCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    videoHBox.addWidget(&fullScreenCheckbox, 240);
    frameLimitCheckbox.setText(_("Enable VSync"));
    frameLimitCheckbox.setChecked(settings.video.frameLimit);
    frameLimitCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    videoHBox.addWidget(&frameLimitCheckbox, 240);
    showTutorialHintsCheckbox.setText(_("Show Tutorial Hints"));
    showTutorialHintsCheckbox.setChecked(settings.general.showTutorialHints);
    showTutorialHintsCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    flagsHBox.addWidget(Spacer::create(), 0.5);
    flagsHBox.addWidget(&showTutorialHintsCheckbox, 240);
    videoHBox.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&videoHBox, 40);
    pages[1].addWidget(VSpacer::create(8));

    videoHBox2.addWidget(Spacer::create(), 0.5);
    showWatermarkCheckbox.setText(_("Show Watermark"));
    showWatermarkCheckbox.setChecked(settings.video.showWatermark);
    showWatermarkCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    flagsHBox.addWidget(&showWatermarkCheckbox, 240);
    flagsHBox.addWidget(Spacer::create(), 0.5);
    pages[1].addWidget(&flagsHBox, 40);
    pages[1].addWidget(VSpacer::create(8));
    videoHBox2.addWidget(optionLabel(_("Cursor")), 190);
    cursorVisibilityDropDownBox.addEntry(_("Auto"), 0);
    cursorVisibilityDropDownBox.addEntry(_("Hidden"), 1);
    cursorVisibilityDropDownBox.addEntry(_("Visible"), 2);
    int cursorVisibilityIndex = settings.video.cursorVisibility >= 0 && settings.video.cursorVisibility <= 2
        ? settings.video.cursorVisibility : 0;
    cursorVisibilityDropDownBox.setSelectedItem(cursorVisibilityIndex);
    cursorVisibilityDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    videoHBox2.addWidget(&cursorVisibilityDropDownBox, 130);
    videoHBox2.addWidget(optionLabel(_("Scale")), 90);
    cursorScaleDropDownBox.addEntry(_("Auto"), 0);
    cursorScaleDropDownBox.addEntry("1x", 1);
    cursorScaleDropDownBox.addEntry("2x", 2);
    cursorScaleDropDownBox.addEntry("3x", 3);
    cursorScaleDropDownBox.addEntry("4x", 4);
    int cursorScaleIndex = settings.video.cursorScale >= 0 && settings.video.cursorScale <= 4 ? settings.video.cursorScale : 0;
    cursorScaleDropDownBox.setSelectedItem(cursorScaleIndex);
    cursorScaleDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    videoHBox2.addWidget(&cursorScaleDropDownBox, 70);
    videoHBox2.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&videoHBox2, 40);
    pages[1].addWidget(VSpacer::create(8));

    audioHBox.addWidget(Spacer::create(), 0.5);
    playSFXCheckbox.setText(_("Play SFX"));
    playSFXCheckbox.setChecked(settings.audio.playSFX);
    playSFXCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox.addWidget(&playSFXCheckbox, 240);
    playMusicCheckbox.setText(_("Play Music"));
    playMusicCheckbox.setChecked(settings.audio.playMusic);
    playMusicCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox.addWidget(&playMusicCheckbox, 240);
    audioHBox.addWidget(Spacer::create(), 0.5);

    pages[2].addWidget(&audioHBox, 40);
    pages[2].addWidget(VSpacer::create(8));

    audioHBox2.addWidget(Spacer::create(), 0.5);
    playCreditsSFXCheckbox.setText(_("Play Credits SFX"));
    playCreditsSFXCheckbox.setChecked(settings.audio.playCreditsSFX);
    playCreditsSFXCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox2.addWidget(&playCreditsSFXCheckbox, 240);
    audioHBox2.addWidget(Spacer::create(), 240);
    audioHBox2.addWidget(Spacer::create(), 0.5);

    pages[2].addWidget(&audioHBox2, 40);
    pages[2].addWidget(VSpacer::create(8));

    networkPortHBox.addWidget(Spacer::create(), 0.5);
    networkPortHBox.addWidget(optionLabel(_("Port")), 190);
    portTextBox.setMaximumTextLength(5);
    portTextBox.setAllowedChars("0123456789");
    portTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    networkPortHBox.addWidget(&portTextBox, 100);
    portTextBox.setText(std::to_string(settings.network.serverPort));
    networkPortHBox.addWidget(Spacer::create(), 190);
    networkPortHBox.addWidget(Spacer::create(), 0.5);
    pages[2].addWidget(&networkPortHBox, 40);
    pages[2].addWidget(VSpacer::create(8));

    networkMetaServerHBox.addWidget(Spacer::create(), 0.5);
    networkMetaServerHBox.addWidget(optionLabel(_("MetaServer")), 190);
    metaServerTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    networkMetaServerHBox.addWidget(&metaServerTextBox, 290);
    metaServerTextBox.setText(settings.network.metaServer);
    networkMetaServerHBox.addWidget(Spacer::create(), 0.5);
    pages[2].addWidget(&networkMetaServerHBox, 40);
    pages[2].addWidget(VSpacer::create(8));

    restoreDefaultsHBox.addWidget(Spacer::create(), 0.5);
    restoreDefaultsButton.setText(_("Restore Config Defaults"));
    restoreDefaultsButton.setOnClick(std::bind(&OptionsMenu::onRestoreDefaults, this));
    restoreDefaultsHBox.addWidget(&restoreDefaultsButton, 320);
    restoreDefaultsHBox.addWidget(Spacer::create(), 0.5);
    pages[0].addWidget(&restoreDefaultsHBox, 40);
    pages[0].addWidget(VSpacer::create(8));

    backButton.setText(_("BACK"));
    backButton.setOnClick(std::bind(&OptionsMenu::onOptionsCancel, this));


    acceptButton.setText(_("APPLY"));
    acceptButton.setEnabled(false);
    acceptButton.setOnClick(std::bind(&OptionsMenu::onOptionsOK, this));


    windowWidget.addWidget(&backButton, Point(left + 30, getSize().y - 64), Point(220, 40));
    windowWidget.addWidget(&acceptButton, Point(left + 290, getSize().y - 64), Point(220, 40));
    for(auto* checkbox : {&introCheckbox, &fullScreenCheckbox, &frameLimitCheckbox,
            &showTutorialHintsCheckbox, &showWatermarkCheckbox, &playSFXCheckbox,
            &playMusicCheckbox, &playCreditsSFXCheckbox})
        checkbox->setTextColor(COLOR_BLACK, COLOR_TRANSPARENT);
    for(auto* box : {&nameTextBox, &portTextBox, &metaServerTextBox})
        box->setTextColor(COLOR_BLACK, COLOR_TRANSPARENT);
    for(auto* box : {&languageDropDownBox, &aiDropDownBox, &resolutionDropDownBox,
            &zoomlevelDropDownBox, &scalerDropDownBox, &cursorVisibilityDropDownBox, &cursorScaleDropDownBox})
        box->setColor(COLOR_BLACK);
#ifdef __ANDROID__
    fullScreenCheckbox.setEnabled(false);
#endif
    showPage(0);
}

OptionsMenu::~OptionsMenu()
{
    ;
}

void OptionsMenu::showPage(int page) {
    for(int i = 0; i < 3; ++i) {
        pages[i].setVisible(i == page);
        pages[i].setEnabled(i == page);
        pageButtons[i].setToggleState(i == page);
    }
}

void OptionsMenu::onChangeOption(bool bInteractive) {
    bool bChanged = false;

    bChanged |= (settings.general.playerName != nameTextBox.getText());
    int languageIndex = languageDropDownBox.getSelectedEntryIntData();
    if(languageIndex >= 0) {
        std::string languageFilename = availLanguages[languageIndex];
        bChanged |= (settings.general.language != languageFilename.substr(languageFilename.size()-5,2));
    }
    const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByIndex(aiDropDownBox.getSelectedEntryIntData());
    bChanged |= ((pPlayerData == nullptr) || (settings.ai.campaignAI != pPlayerData->getPlayerClass()));
    bChanged |= (settings.general.playIntro != introCheckbox.isChecked());
    bChanged |= (settings.general.showTutorialHints != showTutorialHintsCheckbox.isChecked());

    int selectedResolution = resolutionDropDownBox.getSelectedEntryIntData();
    if(selectedResolution >= 0) {
#ifdef __ANDROID__
        bChanged |= settings.video.interfaceHeight != availScreenRes[selectedResolution].y;
#else
        bChanged |= (settings.video.physicalWidth != availScreenRes[selectedResolution].x);
        bChanged |= (settings.video.physicalHeight != availScreenRes[selectedResolution].y);
#endif
    }
    bChanged |= (settings.video.preferredZoomLevel != zoomlevelDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.fullscreen != fullScreenCheckbox.isChecked());
    bChanged |= (settings.video.frameLimit != frameLimitCheckbox.isChecked());
    bChanged |= (settings.video.scaler != scalerDropDownBox.getSelectedEntry());
    bChanged |= (settings.video.showWatermark != showWatermarkCheckbox.isChecked());
    bChanged |= (settings.video.cursorVisibility != cursorVisibilityDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.cursorScale != cursorScaleDropDownBox.getSelectedEntryIntData());

    bChanged |= (settings.audio.playSFX != playSFXCheckbox.isChecked());
    bChanged |= (settings.audio.playMusic != playMusicCheckbox.isChecked());
    bChanged |= (settings.audio.playCreditsSFX != playCreditsSFXCheckbox.isChecked());

    bChanged |= (settings.gameOptions != currentGameOptions);

    bChanged |= (settings.network.serverPort != atoi(portTextBox.getText().c_str()));
    bChanged |= (settings.network.metaServer != metaServerTextBox.getText());

    acceptButton.setEnabled(bChanged);
}

void OptionsMenu::onOptionsOK() {
    std::string playername = nameTextBox.getText();
    if(playername.empty()) {
        openWindow(MsgBox::create(_("Please enter a Player Name.")));
        return;
    }

    int serverport;
    if(!parseString(portTextBox.getText(), serverport) || serverport <= 0 || serverport > 65535) {
        openWindow(MsgBox::create(fmt::sprintf(_("Server Port must be between 1 and 65535!\nDefault Server Port is %d!"), DEFAULT_PORT)));
        return;
    }

    std::string metaserver = metaServerTextBox.getText();
    if(metaserver.empty()) {
        openWindow(MsgBox::create(_("Please enter a MetaServer.")));
        return;
    }

    settings.general.playerName = playername;
    std::string languageFilename = (languageDropDownBox.getSelectedEntryIntData() < 0) ? "English.en.po" : availLanguages[languageDropDownBox.getSelectedEntryIntData()];
    settings.general.language = languageFilename.substr(languageFilename.size()-5,2);
    settings.general.playIntro = introCheckbox.isChecked();
    settings.general.showTutorialHints = showTutorialHintsCheckbox.isChecked();

    const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByIndex(aiDropDownBox.getSelectedEntryIntData());
    settings.ai.campaignAI = ((pPlayerData != nullptr) ? pPlayerData->getPlayerClass() : DEFAULTAIPLAYERCLASS);

    int selectedResolution = resolutionDropDownBox.getSelectedEntryIntData();
#ifdef __ANDROID__
    if(selectedResolution >= 0) settings.video.interfaceHeight = availScreenRes[selectedResolution].y;
#else
    settings.video.physicalWidth = (selectedResolution >= 0) ? availScreenRes[selectedResolution].x : 0;
    settings.video.physicalHeight = (selectedResolution >= 0) ? availScreenRes[selectedResolution].y : 0;
#endif
    
    // Validate resolution settings
    if(settings.video.physicalWidth < SCREEN_MIN_WIDTH || settings.video.physicalHeight < SCREEN_MIN_HEIGHT) {
        openWindow(MsgBox::create(_("Invalid resolution selected. Please choose a valid resolution.")));
        return;
    }
    
    int factor = getLogicalToPhysicalResolutionFactor(settings.video.physicalWidth, settings.video.physicalHeight);
    // Prevent division by zero and ensure minimum dimensions
    if(factor <= 0) {
        factor = 1;
    }
    settings.video.width = settings.video.physicalWidth / factor;
    settings.video.height = settings.video.physicalHeight / factor;
    
    // Ensure minimum dimensions
    if(settings.video.width < SCREEN_MIN_WIDTH) settings.video.width = SCREEN_MIN_WIDTH;
    if(settings.video.height < SCREEN_MIN_HEIGHT) settings.video.height = SCREEN_MIN_HEIGHT;

    settings.video.preferredZoomLevel = zoomlevelDropDownBox.getSelectedEntryIntData();
    settings.video.scaler = scalerDropDownBox.getSelectedEntry();
    settings.video.fullscreen = fullScreenCheckbox.isChecked();
    settings.video.frameLimit = frameLimitCheckbox.isChecked();
    settings.video.showWatermark = showWatermarkCheckbox.isChecked();
    settings.video.cursorVisibility = cursorVisibilityDropDownBox.getSelectedEntryIntData();
    settings.video.cursorScale = cursorScaleDropDownBox.getSelectedEntryIntData();

    settings.audio.playSFX = playSFXCheckbox.isChecked();
    settings.audio.playMusic = playMusicCheckbox.isChecked();
    settings.audio.playCreditsSFX = playCreditsSFXCheckbox.isChecked();

    settings.gameOptions = currentGameOptions;

    settings.network.serverPort = serverport;
    settings.network.metaServer = metaserver;

    saveConfiguration2File();

    // sound is not reinitialized when restarting
    // => music and sound player do not reload settings
    soundPlayer->setSound(settings.audio.playSFX);
    if(musicPlayer->isMusicOn() != settings.audio.playMusic) {
        musicPlayer->setMusic(settings.audio.playMusic);
        musicPlayer->changeMusic(MUSIC_INTRO);
    }

    quit(MENU_QUIT_REINITIALIZE);
}

void OptionsMenu::onOptionsCancel() {
    quit();
}

void OptionsMenu::onGameOptions() {
    openWindow(GameOptionsWindow::create(currentGameOptions));
}

void OptionsMenu::onRestoreDefaults() {
    // Restore config files
    if (restoreDefaultConfigs()) {
        std::string successMessage = 
            "Config files restored successfully!\n\n"
            "ObjectData.ini and QuantBot Config.ini have been\n"
            "reset to default values.\n\n"
            "IMPORTANT: Changes will take effect on next game start.\n"
            "Please restart the game.";
        MsgBox* pMsgBox = MsgBox::create(successMessage);
        openWindow(pMsgBox);
    } else {
        std::string errorMessage = 
            "ERROR: Failed to restore config files!\n\n"
            "Check the log file for details.";
        MsgBox* pMsgBox = MsgBox::create(errorMessage);
        pMsgBox->setTextColor(COLOR_RED);
        openWindow(pMsgBox);
    }
}

void OptionsMenu::saveConfiguration2File() {
    INIFile myINIFile(getConfigFilepath());

    myINIFile.setBoolValue("General","Play Intro",settings.general.playIntro);
    myINIFile.setBoolValue("General","Show Tutorial Hints",settings.general.showTutorialHints);

    myINIFile.setIntValue("Video","Physical Width",settings.video.physicalWidth);
    myINIFile.setIntValue("Video","Physical Height",settings.video.physicalHeight);
    myINIFile.setIntValue("Video","Width",settings.video.width);
    myINIFile.setIntValue("Video","Height",settings.video.height);
    myINIFile.setIntValue("Video","Interface Height",settings.video.interfaceHeight);
    myINIFile.setBoolValue("Video","Fullscreen",settings.video.fullscreen);
    myINIFile.setBoolValue("Video","FrameLimit",settings.video.frameLimit);
    myINIFile.setIntValue("Video","Preferred Zoom Level",settings.video.preferredZoomLevel);
    myINIFile.setStringValue("Video","Scaler",settings.video.scaler);
    myINIFile.setBoolValue("Video","RotateUnitGraphics",settings.video.rotateUnitGraphics);
    myINIFile.setBoolValue("Video","Show Watermark",settings.video.showWatermark);
    myINIFile.setIntValue("Video","Cursor Visibility",settings.video.cursorVisibility);
    myINIFile.setIntValue("Video","Cursor Scale",settings.video.cursorScale);

    myINIFile.setStringValue("General","Player Name",settings.general.playerName);
    myINIFile.setStringValue("General","Language",settings.general.language);

    myINIFile.setStringValue("AI","Campaign AI",settings.ai.campaignAI);

    myINIFile.setBoolValue("Audio","Play SFX",settings.audio.playSFX);
    myINIFile.setBoolValue("Audio","Play Music",settings.audio.playMusic);
    myINIFile.setBoolValue("Audio","Play Credits SFX",settings.audio.playCreditsSFX);

    myINIFile.setIntValue("Game Options","Game Speed",settings.gameOptions.gameSpeed);
    myINIFile.setBoolValue("Game Options","Concrete Required",settings.gameOptions.concreteRequired);
    myINIFile.setBoolValue("Game Options","Structures Degrade On Concrete",settings.gameOptions.structuresDegradeOnConcrete);
    myINIFile.setBoolValue("Game Options","Fog of War",settings.gameOptions.fogOfWar);
    myINIFile.setBoolValue("Game Options","Start with Explored Map",settings.gameOptions.startWithExploredMap);
    myINIFile.setBoolValue("Game Options","Instant Build",settings.gameOptions.instantBuild);
    myINIFile.setBoolValue("Game Options","Only One Palace",settings.gameOptions.onlyOnePalace);
    myINIFile.setBoolValue("Game Options","Rocket-Turrets Need Power",settings.gameOptions.rocketTurretsNeedPower);
    myINIFile.setBoolValue("Game Options","Sandworms Respawn",settings.gameOptions.sandwormsRespawn);
    myINIFile.setBoolValue("Game Options","Killed Sandworms Drop Spice",settings.gameOptions.killedSandwormsDropSpice);
    myINIFile.setBoolValue("Game Options","Manual Carryall Drops",settings.gameOptions.manualCarryallDrops);
    myINIFile.setIntValue("Game Options","Maximum Number of Units Override",settings.gameOptions.maximumNumberOfUnitsOverride);
    myINIFile.setIntValue("Game Options","Maximum Number of Harvesters Override",settings.gameOptions.maximumNumberOfHarvestersOverride);
    myINIFile.setBoolValue("Game Options","Immortal Human Player",settings.gameOptions.immortalHumanPlayer);

    myINIFile.setIntValue("Network","ServerPort",settings.network.serverPort);
    myINIFile.setStringValue("Network","MetaServer",settings.network.metaServer);

    myINIFile.saveChangesTo(getConfigFilepath());
}

void OptionsMenu::determineAvailableScreenResolutions() {
    availScreenRes.clear();
#ifdef __ANDROID__
    availScreenRes.emplace_back(640, 480);
    availScreenRes.emplace_back(800, 600);
    availScreenRes.emplace_back(1024, 768);
    return;
#endif

    // Safety check: ensure window exists before trying to get display index
    if(window == nullptr) {
        SDL_Log("Warning: Window not available, using fallback resolutions");
        // Use fallback resolutions
        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1280, 720 );   // WXGA (16:9)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        return;
    }

    SDL_DisplayMode displayMode;
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    
    // Safety check: ensure display index is valid
    if(displayIndex < 0) {
        SDL_Log("Warning: Invalid display index, using fallback resolutions");
        displayIndex = 0; // Use primary display
    }
    
    int numDisplayModes = SDL_GetNumDisplayModes(displayIndex);
    
    // Safety check: ensure we have display modes
    if(numDisplayModes <= 0) {
        SDL_Log("Warning: No display modes available, using fallback resolutions");
        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1280, 720 );   // WXGA (16:9)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        return;
    }
    
    for(int i = numDisplayModes-1; i >=0; i--) {
        if(SDL_GetDisplayMode(displayIndex, i, &displayMode) == 0) {
            Coord screenRes(displayMode.w, displayMode.h);
            if(screenRes.x >= SCREEN_MIN_WIDTH && screenRes.y >= SCREEN_MIN_HEIGHT) {
                if(std::find(availScreenRes.begin(), availScreenRes.end(), screenRes) == availScreenRes.end()) {
                    // not yet in the list (might happen if e.g. multiple refresh rates are reported)
                    availScreenRes.push_back(screenRes);
                }
            }
        }
    }

    if(availScreenRes.empty()) {
        // Not possible or not available
        // try some standard resolutions

        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 480 );    // WVGA (5:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(960, 540 );    // Quarter HD (16:9)
        availScreenRes.emplace_back(960, 720 );    // ? (4:3)
        availScreenRes.emplace_back(1024, 576 );   // WSVGA (16:9)
        availScreenRes.emplace_back(1024, 640 );   // ? (16:10)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1152, 864 );   // XGA+ (4:3)
        availScreenRes.emplace_back(1280, 720 );  // WXGA (16:9)
        availScreenRes.emplace_back(1280, 768 );   // WXGA (5:3)
        availScreenRes.emplace_back(1280, 800 );   // WXGA (16:10)
        availScreenRes.emplace_back(1280, 960 );   // SXGA- (4:3)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1366, 768 );   // HDTV 720p (~16:9)
        availScreenRes.emplace_back(1400, 1050 );  // SXGA+ (4:3)
        availScreenRes.emplace_back(1440, 900 );   // WXGA+ (16:10)
        availScreenRes.emplace_back(1440, 1080 );  // ? (4:3)
        availScreenRes.emplace_back(1600, 900 );   // WSXGA (16:9)
        availScreenRes.emplace_back(1600, 1200 );  // UXGA (4:3)
        availScreenRes.emplace_back(1680, 1050 );  // WSXGA+ (16:10)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        availScreenRes.emplace_back(1920, 1200 );  // WUXGA (16:10)
        availScreenRes.emplace_back(2560, 1440 );  // WQHD (16:9)
        availScreenRes.emplace_back(2560, 1600 );  // WQXGA (16:10)
        availScreenRes.emplace_back(3840, 2160 );  // 2160p (16:9)
    }

    Coord currentRes(settings.video.physicalWidth, settings.video.physicalHeight);

    if(std::find(availScreenRes.begin(), availScreenRes.end(), currentRes) == availScreenRes.end()) {
        // not yet in the list
        availScreenRes.insert(availScreenRes.begin(), currentRes);
    }
}

void OptionsMenu::onChildWindowClose(Window* pChildWindow) {
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        currentGameOptions = pGameOptionsWindow->getGameOptions();

        onChangeOption(true);
    }
}
