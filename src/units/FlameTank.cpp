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

#include <units/FlameTank.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <ScreenBorder.h>
#include <SoundPlayer.h>


FlameTank::FlameTank(House* newOwner) : TrackedUnit(newOwner) {
    FlameTank::init();

    setHealth(getMaxHealth());
}

FlameTank::FlameTank(InputStream& stream) : TrackedUnit(stream) {
    FlameTank::init();
}

void FlameTank::init() {
    itemID = Unit_FlameTank;
    owner->incrementUnits(itemID);

    numWeapons = 1;
    bulletType = Bullet_Flame;

    graphicID = ObjPic_FlameTank;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

FlameTank::~FlameTank() = default;

void FlameTank::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        Coord realPos(lround(realX), lround(realY));
        Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2});
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

        // Scatter flame explosions around the wreck
        for(int i = 0; i < 3; i++) {
            Coord flamePos = realPos;
            flamePos.x += currentGame->randomGen.rand(-TILESIZE/2, TILESIZE/2);
            flamePos.y += currentGame->randomGen.rand(-TILESIZE/2, TILESIZE/2);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Flames, flamePos, owner->getHouseID()));
        }

        if(isVisible(getOwner()->getTeamID())) {
            screenborder->shakeScreen(12);
            soundPlayer->playSoundAt(Sound_ExplosionMedium, location);
        }
    }

    TrackedUnit::destroy();
}

void FlameTank::playAttackSound() {
    soundPlayer->playSoundAt(Sound_Sonic, location);
}
