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

#include <GameInterface.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <House.h>
#include <Game.h>

#include <ObjectBase.h>
#include <GUI/ObjectInterfaces/ObjectInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>

#include <misc/draw_util.h>
#include <misc/SDL2pp.h>
#include <misc/format.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityBudget.h>
#include <dunecity/CityEvaluation.h>

#include <mod/ModManager.h>
#include <mod/ModInfo.h>
#include <config.h>

#include <algorithm>
#include <cstdlib>
#include <string>

GameInterface::GameInterface() : Window(0,0,0,0) {
    pObjectContainer = nullptr;
    objectID = NONE_ID;
    showCityStatsOverlay = false;

    setTransparentBackground(true);

    setCurrentPosition(0,0,getRendererWidth(),getRendererHeight());

    setWindowWidget(&windowWidget);

    // top bar
    SDL_Texture* pTopBarTex = pGFXManager->getUIGraphic(UI_TopBar, pLocalHouse->getHouseID());
    topBar.setTexture(pTopBarTex);
    windowWidget.addWidget(&topBar,Point(0,0),Point(getWidth(pTopBarTex),getHeight(pTopBarTex) - 12));

    // side bar
    SDL_Texture* pSideBarTex = pGFXManager->getUIGraphic(UI_SideBar, pLocalHouse->getHouseID());
    sideBar.setTexture(pSideBarTex);
    SDL_Rect dest = calcAlignedDrawingRect(pSideBarTex, HAlign::Right, VAlign::Top);
    windowWidget.addWidget(&sideBar, dest);

    // add buttons
    windowWidget.addWidget(&topBarHBox,Point(5,5),
                            Point(getRendererWidth() - sideBar.getSize().x, topBar.getSize().y - 10));

    topBarHBox.addWidget(&newsticker);

    topBarHBox.addWidget(Spacer::create());

    optionsButton.setTextures(  pGFXManager->getUIGraphic(UI_Options, pLocalHouse->getHouseID()),
                                pGFXManager->getUIGraphic(UI_Options_Pressed, pLocalHouse->getHouseID()));
    optionsButton.setOnClick(std::bind(&Game::onOptions, currentGame));
    topBarHBox.addWidget(&optionsButton);

    topBarHBox.addWidget(Spacer::create());

    mentatButton.setTextures(   pGFXManager->getUIGraphic(UI_Mentat, pLocalHouse->getHouseID()),
                                pGFXManager->getUIGraphic(UI_Mentat_Pressed, pLocalHouse->getHouseID()));
    mentatButton.setOnClick(std::bind(&Game::onMentat, currentGame));
    topBarHBox.addWidget(&mentatButton);

    topBarHBox.addWidget(Spacer::create());

    // City sim only: a "Budget" text button next to Mentat. There's no
    // dedicated UI texture so we use a TextButton; it's hidden in
    // non-city games via setVisible() in draw().
    budgetButton.setText(_("Budget"));
    budgetButton.setTooltipText(_("Open city budget (Shift+B)"));
    budgetButton.setOnClick(std::bind(&Game::onCityBudget, currentGame));
    topBarHBox.addWidget(&budgetButton);

    topBarHBox.addWidget(Spacer::create());

    // add radar
    const Point radarOrigin(getRendererWidth() - sideBar.getSize().x + SIDEBAR_COLUMN_WIDTH, 0);
    const Point radarSize = radarView.getMinimumSize();
    windowWidget.addWidget(&radarView, radarOrigin, radarSize);
    radarView.setOnRadarClick(std::bind(&Game::onRadarClick, currentGame, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // add ornithopter selection shortcut button just below the radar
    ornithopterSelectButton.setText(_("Ornithopter"));
    ornithopterSelectButton.setTooltipText(_("Select all ornithopters (Hotkey: O)"));
    ornithopterSelectButton.setOnClick(std::bind(&Game::selectAllOrnithopters, currentGame));

    const int ornithopterButtonWidth = sideBar.getSize().x - 25;
    const int ornithopterButtonHeight = std::max(ornithopterSelectButton.getMinimumSize().y, 36);
    const Point ornithopterButtonSize(ornithopterButtonWidth, ornithopterButtonHeight);
    const Point ornithopterButtonPos(
        getRendererWidth() - sideBar.getSize().x + 24,
        146
    );
    ornithopterSelectButton.resize(ornithopterButtonSize.x, ornithopterButtonSize.y);
    windowWidget.addWidget(&ornithopterSelectButton, ornithopterButtonPos, ornithopterButtonSize);

    // add chat manager
    windowWidget.addWidget(&chatManager, Point(20, 60), Point(getRendererWidth() - sideBar.getSize().x, 360));

    // Always-visible population pill (city sim mode only). Sits just below
    // the top bar, left-aligned, so the player sees their city growing
    // without having to open the Shift+C stats overlay.
    {
        populationLabel.setText("Pop: 0");
        populationLabel.setTextFontSize(14);
        populationLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        populationLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));

        const int labelWidth  = 140;
        const int labelHeight = 22;
        windowWidget.addWidget(&populationLabel,
                               Point(12, topBar.getSize().y - 4),
                               Point(labelWidth, labelHeight));

        // RCI demand readout immediately below the population pill. Same
        // city-sim gating as populationLabel — only visible in city mode.
        rciDemandLabel.setText("R --  C --  I --");
        rciDemandLabel.setTextFontSize(12);
        rciDemandLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        rciDemandLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));

        const int rciWidth  = 200;
        const int rciHeight = 18;
        windowWidget.addWidget(&rciDemandLabel,
                               Point(12, topBar.getSize().y - 4 + labelHeight),
                               Point(rciWidth, rciHeight));
    }

    // Bottom-right watermark: <active mod display name> over v<VERSION>.
    // Mirrors the main menu so the active mod is visible mid-game too.
    {
        std::string modDisplayName = "Vanilla";
        ModManager& modManager = ModManager::instance();
        if (modManager.isInitialized()) {
            ModInfo info = modManager.getModInfo(modManager.getActiveModName());
            if (!info.displayName.empty()) {
                modDisplayName = info.displayName;
            } else if (!info.name.empty()) {
                modDisplayName = info.name;
            }
        }

        modVersionLabel.setText(modDisplayName + "\nv" + std::string(VERSION));
        modVersionLabel.setTextFontSize(14);
        modVersionLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        modVersionLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));

        const int labelWidth  = 200;
        const int labelHeight = 44;
        const int marginX     = 8;
        const int marginY     = 6;
        windowWidget.addWidget(&modVersionLabel,
                               Point(marginX,
                                     getRendererHeight() - labelHeight - marginY),
                               Point(labelWidth, labelHeight));

        if (!settings.video.showWatermark) {
            modVersionLabel.setVisible(false);
        }
    }
}

GameInterface::~GameInterface() {
    removeOldContainer();
}

void GameInterface::draw(Point position) {
    // Refresh the population pill from the live city sim. We only show the
    // label when city sim is active; outside city mode it would just be
    // visual noise reading "Pop: 0".
    {
        auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
        if (citySim && citySim->isInitialized()) {
            const int displayPop = citySim->getDisplayTotalPop();
            if (displayPop != lastShownPopulation) {
                populationLabel.setText(fmt::sprintf("Pop: %d", displayPop));
                lastShownPopulation = displayPop;
            }
            populationLabel.setVisible(true);

            // RCI demand line. Valves are signed: positive means demand
            // (zones want to grow), negative means oversupply (zones may
            // decline). Show with arrows so the player can read the
            // direction at a glance without parsing magnitudes.
            const int r = citySim->getResValve();
            const int c = citySim->getComValve();
            const int i = citySim->getIndValve();
            if (r != lastShownResValve || c != lastShownComValve || i != lastShownIndValve) {
                auto fmtValve = [](int v) {
                    const char* arrow = (v > 0) ? "+" : ((v < 0) ? "-" : "=");
                    return std::string(arrow) + std::to_string(std::abs(v));
                };
                rciDemandLabel.setText("R " + fmtValve(r) +
                                       "  C " + fmtValve(c) +
                                       "  I " + fmtValve(i));
                lastShownResValve = r;
                lastShownComValve = c;
                lastShownIndValve = i;
            }
            rciDemandLabel.setVisible(true);
            budgetButton.setVisible(true);
        } else {
            populationLabel.setVisible(false);
            rciDemandLabel.setVisible(false);
            budgetButton.setVisible(false);
        }
    }

    Window::draw(position);

    // Update and draw disaster notifications (top center, stacked vertically)
    {
        const int notificationWidth = 320;
        const int notificationHeight = 44;
        const int notificationSpacing = 4;
        const int stackX = (getRendererWidth() - notificationWidth) / 2;
        int stackY = 4;

        // Remove dismissed notifications
        disasterNotifications_.erase(
            std::remove_if(disasterNotifications_.begin(), disasterNotifications_.end(),
                [](const std::unique_ptr<DisasterNotification>& n) { return n->isDismissed(); }),
            disasterNotifications_.end()
        );

        for (auto& notification : disasterNotifications_) {
            notification->update();
            notification->draw(Point(stackX, stackY));
            stackY += notificationHeight + notificationSpacing;
        }
    }

    // draw Power Indicator and Spice indicator

    SDL_Rect powerIndicatorPos = {  getRendererWidth() - sideBar.getSize().x + 14, 146, 4, getRendererHeight() - 146 - 2 };
    renderFillRect(renderer, &powerIndicatorPos, COLOR_BLACK);

    SDL_Rect spiceIndicatorPos = {  getRendererWidth() - sideBar.getSize().x + 20, 146, 4, getRendererHeight() - 146 - 2 };
    renderFillRect(renderer, &spiceIndicatorPos, COLOR_BLACK);

    int xCount = 0, yCount = 0;
    int yCount2 = 0;

    //draw power level indicator
    if (pLocalHouse->getPowerRequirement() == 0)    {
        if (pLocalHouse->getProducedPower() > 0) {
            yCount2 = powerIndicatorPos.h + 1;
        } else {
            yCount2 = powerIndicatorPos.h/2;
        }
    } else {
        yCount2 = lround((double)pLocalHouse->getProducedPower()/(double)pLocalHouse->getPowerRequirement()*(double)(powerIndicatorPos.h/2));
    }

    if (yCount2 > powerIndicatorPos.h + 1) {
        yCount2 = powerIndicatorPos.h + 1;
    }

    setRenderDrawColor(renderer, COLOR_GREEN);
    for (yCount = 0; yCount < yCount2; yCount++) {
        for (xCount = 1; xCount < powerIndicatorPos.w - 1; xCount++) {
            if(((yCount/2) % 3) != 0) {
                SDL_RenderDrawPoint(renderer, xCount + powerIndicatorPos.x, powerIndicatorPos.y + powerIndicatorPos.h - yCount);
            }
        }
    }

    //draw spice level indicator
    if (pLocalHouse->getCapacity() == 0) {
        yCount2 = 0;
    } else {
        yCount2 = lround((pLocalHouse->getStoredCredits()/pLocalHouse->getCapacity())*spiceIndicatorPos.h);
    }

    if (yCount2 > spiceIndicatorPos.h + 1) {
        yCount2 = spiceIndicatorPos.h + 1;
    }

    setRenderDrawColor(renderer, COLOR_ORANGE);
    for (yCount = 0; yCount < yCount2; yCount++) {
        for (xCount = 1; xCount < spiceIndicatorPos.w - 1; xCount++) {
            if(((yCount/2) % 3) != 0) {
                SDL_RenderDrawPoint(renderer, xCount + spiceIndicatorPos.x, spiceIndicatorPos.y + spiceIndicatorPos.h - yCount);
            }
        }
    }

    //draw credits
    const auto credits = pLocalHouse->getCredits();
    const auto CreditsBuffer = std::to_string((credits < 0) ? 0 : credits);
    const auto NumDigits = CreditsBuffer.length();
    SDL_Texture* digitsTex = pGFXManager->getUIGraphic(UI_CreditsDigits);

    for(int i=NumDigits-1; i>=0; i--) {
        SDL_Rect source = calcSpriteSourceRect(digitsTex, CreditsBuffer[i] - '0', 10);
        SDL_Rect dest = calcSpriteDrawingRect(digitsTex, getRendererWidth() - sideBar.getSize().x + 49 + (6 - NumDigits + i)*10, 135, 10);
        SDL_RenderCopy(renderer, digitsTex, &source, &dest);
    }

    if(showCityStatsOverlay) {
        drawCityStatsOverlay();
    }
}

void GameInterface::updateObjectInterface() {
    const auto& selection = currentGame->getSelectedList();

    if(selection.empty()) {
        ornithopterSelectButton.setVisible(true);
        removeOldContainer();
        return;
    }

    ornithopterSelectButton.setVisible(false);

    if(selection.size() == 1) {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(*selection.begin());
        Uint32 newObjectID = pObject->getObjectID();

        if(newObjectID != objectID) {
            removeOldContainer();

            pObjectContainer = pObject->getInterfaceContainer();

            if(pObjectContainer != nullptr) {
                objectID = newObjectID;

                windowWidget.addWidget(pObjectContainer,
                                        Point(getRendererWidth() - sideBar.getSize().x + 24, 146),
                                        Point(sideBar.getSize().x - 25,getRendererHeight() - 148));

            }

        } else {
            if(pObjectContainer->update() == false) {
                removeOldContainer();
            }
        }
    } else {

        if((pObjectContainer == nullptr) || (objectID != NONE_ID)) {
            // either there was nothing selected before or exactly one unit

            if(pObjectContainer != nullptr) {
                removeOldContainer();
            }

            pObjectContainer = MultiUnitInterface::create();

            windowWidget.addWidget(pObjectContainer,
                                    Point(getRendererWidth() - sideBar.getSize().x + 24, 146),
                                    Point(sideBar.getSize().x - 25,getRendererHeight() - 148));
        } else {
            if(pObjectContainer->update() == false) {
                removeOldContainer();
            }
        }
    }
}

void GameInterface::removeOldContainer() {
    if(pObjectContainer != nullptr) {
        delete pObjectContainer;
        pObjectContainer = nullptr;
        objectID = NONE_ID;
    }
}

void GameInterface::drawCityStatsOverlay() {
    auto* citySim = currentGame->getCitySimulation();
    if(!citySim || !citySim->isInitialized()) {
        return;
    }

    const int overlayWidth = 250;
    const int overlayHeight = 260;
    const int overlayX = getRendererWidth() - overlayWidth - 10;
    const int overlayY = 10;
    const int padding = 8;
    const int lineHeight = 16;
    const int barHeight = 12;

    SDL_Rect bgRect = { overlayX, overlayY, overlayWidth, overlayHeight };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &bgRect);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer, &bgRect);

    int textX = overlayX + padding;
    int textY = overlayY + padding;

    auto drawText = [&](const std::string& text, unsigned int fontSize, Uint32 color = COLOR_WHITE) {
        sdl2::texture_ptr textTexture = pFontManager->createTextureWithText(text, color, fontSize);
        if(textTexture) {
            SDL_Rect destRect = { textX, textY, 0, 0 };
            SDL_QueryTexture(textTexture.get(), nullptr, nullptr, &destRect.w, &destRect.h);
            SDL_RenderCopy(renderer, textTexture.get(), nullptr, &destRect);
        }
        textY += lineHeight;
    };

    auto drawBar = [&](int value, int maxValue, uint8_t r, uint8_t g, uint8_t b) {
        int barWidth = overlayWidth - 2 * padding;
        int fillWidth = maxValue > 0 ? (value * barWidth) / maxValue : 0;
        
        SDL_Rect barBg = { textX, textY, barWidth, barHeight };
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &barBg);
        
        if (fillWidth > 0) {
            SDL_Rect barFill = { textX, textY, fillWidth, barHeight };
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            SDL_RenderFillRect(renderer, &barFill);
        }
        
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDrawRect(renderer, &barBg);
        
        textY += barHeight + 4;
    };

    drawText("=== CITY STATS ===", 12);
    textY += 4;

    int resPop = citySim->getDisplayResPop();
    int comPop = citySim->getDisplayComPop();
    int indPop = citySim->getDisplayIndPop();
    int totalPop = citySim->getDisplayTotalPop();

    int16_t resValve = citySim->getResValve();
    int16_t comValve = citySim->getComValve();
    int16_t indValve = citySim->getIndValve();

    const char* resArrow = resValve > 100 ? " ↑" : (resValve < -100 ? " ↓" : "");
    const char* comArrow = comValve > 100 ? " ↑" : (comValve < -100 ? " ↓" : "");
    const char* indArrow = indValve > 100 ? " ↑" : (indValve < -100 ? " ↓" : "");

    drawText(fmt::sprintf("Population: %d", totalPop), 12);
    drawText(fmt::sprintf("  R: %d%s  C: %d%s  I: %d%s", resPop, resArrow, comPop, comArrow, indPop, indArrow), 10);
    
    if (totalPop > 0) {
        drawBar(resPop, totalPop, 0, 200, 0);
        drawBar(comPop, totalPop, 0, 100, 255);
        drawBar(indPop, totalPop, 200, 200, 0);
    }

    auto& budget = citySim->getCityBudget();
    int32_t lastRevenue = budget.getLastTaxRevenue();
    
    drawText(fmt::sprintf("Treasury: %d", citySim->getTotalFunds()), 12);
    drawText(fmt::sprintf("Tax Revenue: %d", lastRevenue), 10);

    int totalTiles = citySim->getMapWidth() * citySim->getMapHeight();
    int poweredTiles = 0;
    const auto& powerMap = citySim->getPowerGridMap();
    for(int y = 0; y < citySim->getMapHeight(); y++) {
        for(int x = 0; x < citySim->getMapWidth(); x++) {
            if(powerMap.get(x, y) > 0) {
                poweredTiles++;
            }
        }
    }
    int powerPercent = totalTiles > 0 ? (poweredTiles * 100) / totalTiles : 0;
    const char* powerIcon = powerPercent >= 80 ? "✓" : (powerPercent >= 50 ? "!" : "✗");
    Uint32 powerColor = powerPercent >= 80 ? COLOR_GREEN : (powerPercent >= 50 ? COLOR_YELLOW : COLOR_RED);
    drawText(fmt::sprintf("Power: %d%% %s", powerPercent, powerIcon), 12, powerColor);

    drawText(fmt::sprintf("Year: %d", citySim->getCityYear()), 12);

    auto overlayMode = currentGame->getCityOverlayMode();
    if (overlayMode != DuneCity::CityOverlayMode::None) {
        const char* overlayName = "Unknown";
        switch (overlayMode) {
            case DuneCity::CityOverlayMode::PowerGrid:      overlayName = "Power Grid"; break;
            case DuneCity::CityOverlayMode::TrafficDensity: overlayName = "Traffic"; break;
            case DuneCity::CityOverlayMode::Pollution:      overlayName = "Pollution"; break;
            case DuneCity::CityOverlayMode::LandValue:      overlayName = "Land Value"; break;
            case DuneCity::CityOverlayMode::CrimeRate:      overlayName = "Crime"; break;
            case DuneCity::CityOverlayMode::Population:     overlayName = "Population"; break;
            default: break;
        }
        drawText(fmt::sprintf("Overlay: %s", overlayName), 10);

        // Draw color legend bar for the active overlay
        const int legendBarWidth = overlayWidth - 2 * padding;
        const int legendBarHeight = 12;
        const int numStops = 10;

        // Select gradient endpoints based on overlay type
        uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
        const char* lowLabel = "";
        const char* highLabel = "";
        switch (overlayMode) {
            case DuneCity::CityOverlayMode::PowerGrid:
                r1 = 200; g1 = 0;   b1 = 0;   r2 = 0;   g2 = 255; b2 = 0;
                lowLabel = "Unpowered"; highLabel = "Powered";
                break;
            case DuneCity::CityOverlayMode::TrafficDensity:
                r1 = 0;   g1 = 255; b1 = 0;   r2 = 255; g2 = 0;   b2 = 0;
                lowLabel = "Low"; highLabel = "High";
                break;
            case DuneCity::CityOverlayMode::Pollution:
                r1 = 0;   g1 = 255; b1 = 0;   r2 = 150; g2 = 0;   b2 = 200;
                lowLabel = "Clean"; highLabel = "Polluted";
                break;
            case DuneCity::CityOverlayMode::LandValue:
                r1 = 255; g1 = 0;   b1 = 0;   r2 = 0;   g2 = 255; b2 = 0;
                lowLabel = "Low"; highLabel = "High";
                break;
            case DuneCity::CityOverlayMode::CrimeRate:
                r1 = 0;   g1 = 255; b1 = 0;   r2 = 255; g2 = 0;   b2 = 0;
                lowLabel = "Safe"; highLabel = "Dangerous";
                break;
            case DuneCity::CityOverlayMode::Population:
                r1 = 50;  g1 = 50;  b1 = 50;  r2 = 255; g2 = 255; b2 = 255;
                lowLabel = "Sparse"; highLabel = "Dense";
                break;
            default:
                break;
        }

        // Draw gradient bar as a series of colored vertical stripes
        int stripWidth = legendBarWidth / numStops;
        for (int i = 0; i < numStops; i++) {
            float t = static_cast<float>(i) / (numStops - 1);
            uint8_t sr = static_cast<uint8_t>(r1 + (r2 - r1) * t);
            uint8_t sg = static_cast<uint8_t>(g1 + (g2 - g1) * t);
            uint8_t sb = static_cast<uint8_t>(b1 + (b2 - b1) * t);
            SDL_Rect strip = { textX + i * stripWidth, textY, stripWidth, legendBarHeight };
            SDL_SetRenderDrawColor(renderer, sr, sg, sb, 255);
            SDL_RenderFillRect(renderer, &strip);
        }
        // Border around bar
        SDL_Rect barBorder = { textX, textY, legendBarWidth, legendBarHeight };
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDrawRect(renderer, &barBorder);

        textY += legendBarHeight + 2;

        // Draw low/high labels below the bar
        sdl2::texture_ptr lowTex = pFontManager->createTextureWithText(lowLabel, COLOR_WHITE, 9);
        if (lowTex) {
            SDL_Rect d = { textX, textY, 0, 0 };
            SDL_QueryTexture(lowTex.get(), nullptr, nullptr, &d.w, &d.h);
            SDL_RenderCopy(renderer, lowTex.get(), nullptr, &d);
        }
        sdl2::texture_ptr highTex = pFontManager->createTextureWithText(highLabel, COLOR_WHITE, 9);
        if (highTex) {
            SDL_Rect d = { textX + legendBarWidth - 80, textY, 80, 0 };
            SDL_QueryTexture(highTex.get(), nullptr, nullptr, &d.w, &d.h);
            SDL_RenderCopy(renderer, highTex.get(), nullptr, &d);
        }
        textY += 14;
    }

    textY += 4;
    drawText("Press C to toggle", 10);
}

void GameInterface::addDisasterNotification(DisasterType type, const std::string& message, int durationSeconds, int affectedCount) {
    auto notification = std::make_unique<DisasterNotification>();
    notification->setDisaster(type, message, durationSeconds, affectedCount);
    disasterNotifications_.push_back(std::move(notification));
}
