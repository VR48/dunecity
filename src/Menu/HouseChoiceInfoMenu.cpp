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

#include <Menu/HouseChoiceInfoMenu.h>

#include <globals.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/Palfile.h>
#include <misc/draw_util.h>

#include <algorithm>

HouseChoiceInfoMenu::HouseChoiceInfoMenu(int newHouse) : MentatMenu(HOUSE_INVALID)
{
    disableQuiting(true);

    house = newHouse;

    Animation* anim = nullptr;
    const HOUSETYPE displayHouse = getHouseFallbackHouse(static_cast<HOUSETYPE>(house));

    switch(displayHouse) {
        case HOUSE_HARKONNEN:   anim = pGFXManager->getAnimation(Anim_HarkonnenPlanet); break;
        case HOUSE_ATREIDES:    anim = pGFXManager->getAnimation(Anim_AtreidesPlanet);  break;
        case HOUSE_ORDOS:       anim = pGFXManager->getAnimation(Anim_OrdosPlanet);     break;
        case HOUSE_FREMEN:      anim = pGFXManager->getAnimation(Anim_FremenPlanet);    break;
        case HOUSE_SARDAUKAR:   anim = pGFXManager->getAnimation(Anim_SardaukarPlanet); break;
        case HOUSE_MERCENARY:   anim = pGFXManager->getAnimation(Anim_MercenaryPlanet); break;
        case HOUSE_NEUTRAL:     anim = pGFXManager->getAnimation(Anim_NeutralPlanet);   break;
        case HOUSE_REBELS:      anim = pGFXManager->getAnimation(Anim_RebelsPlanet);    break;
        default: {
            THROW(std::invalid_argument, "HouseChoiceInfoMenu::HouseChoiceInfoMenu(): Invalid house id '%d'.", newHouse);
        } break;
    }

    if(newHouse == HOUSE_CUSTOM && anim != nullptr) {
        SDL_Surface* registeredHerald = pGFXManager->getUIGraphicSurface(UI_Herald_ColoredLarge, HOUSE_CUSTOM);
        auto opaqueHerald = copySurface(registeredHerald);
        if(opaqueHerald != nullptr) {
            SDL_SetColorKey(opaqueHerald.get(), SDL_FALSE, 0);
            customPlanetAnimation = std::make_unique<Animation>();
            for(const auto& frame : anim->getFrames()) {
                auto customFrame = copySurface(frame.get());
                if(customFrame == nullptr) {
                    continue;
                }

                SDL_Rect sourceRect{0, 0,
                                    std::min(opaqueHerald->w, customFrame->w - 12),
                                    std::min(126, opaqueHerald->h)};
                SDL_Rect destinationRect{12, 66, sourceRect.w, sourceRect.h};
                if(sourceRect.w > 0 && sourceRect.h > 0) {
                    SDL_BlitSurface(opaqueHerald.get(), &sourceRect, customFrame.get(), &destinationRect);
                }
                customPlanetAnimation->addFrame(std::move(customFrame));
            }
            customPlanetAnimation->setFrameDurationTime(anim->getFrameDurationTime());
            customPlanetAnimation->setNumLoops(anim->getLoopsLeft());
            anim = customPlanetAnimation.get();
        }
    }

    planetAnimation.setAnimation(anim);
    windowWidget.addWidget(&planetAnimation, Point(256,96), planetAnimation.getMinimumSize());

    SDL_Texture* pQuestionTexture = pGFXManager->getUIGraphic(UI_MentatHouseChoiceInfoQuestion, newHouse);
    questionLabel.setTexture(pQuestionTexture);
    windowWidget.addWidget(&questionLabel, Point(0,0), getTextureSize(pQuestionTexture));
    questionLabel.setVisible(false);

    // init textbox but skip first line (this line contains "House ???")
    std::string desc = pTextManager->getBriefingText(0,MISSION_DESCRIPTION,house);
    const auto linebreak = desc.find("\n", 0);
    if(linebreak == std::string::npos) {
        setText(desc);
    } else {
        setText(desc.substr(linebreak + 1, desc.length() - linebreak - 1));
    }

    SDL_Texture* pMentatYes = pGFXManager->getUIGraphic(UI_MentatYes);
    SDL_Texture* pMentatYesPressed = pGFXManager->getUIGraphic(UI_MentatYes_Pressed);

    yesButton.setTextures(pMentatYes, pMentatYesPressed);
    yesButton.setEnabled(false);
    yesButton.setVisible(false);
    yesButton.setOnClick(std::bind(&HouseChoiceInfoMenu::onYes, this));
    windowWidget.addWidget(&yesButton,Point(370,340), getTextureSize(pMentatYes));

    SDL_Texture* pMentatNo = pGFXManager->getUIGraphic(UI_MentatNo);
    SDL_Texture* pMentatNoPressed = pGFXManager->getUIGraphic(UI_MentatNo_Pressed);

    noButton.setTextures(pMentatNo, pMentatNoPressed);
    noButton.setEnabled(false);
    noButton.setVisible(false);
    noButton.setOnClick(std::bind(&HouseChoiceInfoMenu::onNo, this));
    windowWidget.addWidget(&noButton,Point(480,340), getTextureSize(pMentatNo));
}

HouseChoiceInfoMenu::~HouseChoiceInfoMenu() = default;

void HouseChoiceInfoMenu::onMentatTextFinished() {
    yesButton.setEnabled(true);
    yesButton.setVisible(true);
    noButton.setEnabled(true);
    noButton.setVisible(true);

    questionLabel.setVisible(true);
}

void HouseChoiceInfoMenu::drawSpecificStuff() {
    MentatMenu::drawSpecificStuff();
}

void HouseChoiceInfoMenu::onYes() {
    quit(MENU_QUIT_HOUSECHOICE_YES);
}

void HouseChoiceInfoMenu::onNo() {
    quit(MENU_QUIT_DEFAULT);
}
