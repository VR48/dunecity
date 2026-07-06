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

#include <Bullet.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <SoundPlayer.h>
#include <ObjectBase.h>
#include <Game.h>
#include <Map.h>
#include <House.h>
#include <Explosion.h>

#include <misc/draw_util.h>
#include <misc/exceptions.h>

#include <algorithm>


Bullet::Bullet(Uint32 shooterID, Coord* newRealLocation, Coord* newRealDestination, Uint32 bulletID, int damage, bool air, const ObjectBase* pTarget)
{
    airAttack = air;

    this->shooterID = shooterID;

    this->owner = currentGame->getObjectManager().getObject(shooterID)->getOwner();

    this->bulletID = bulletID;

    this->damage = damage;

    target.pointTo(pTarget);

    Bullet::init();

    if(bulletID == Bullet_TurretRocket) {
        const ObjectBase* pInitialTarget = target.getObjPointer();
        if(pInitialTarget && pInitialTarget->isAFlyingUnit()) {
            detonationTimer = 120;
        } else {
            detonationTimer = 60;
        }
    } else if((bulletID == Bullet_Rocket || bulletID == Bullet_DRocket || bulletID == Bullet_SmallRocket)
              && detonationTimer > 0) {
        const ObjectBase* pInitialTarget = target.getObjPointer();
        if(pInitialTarget && pInitialTarget->isAFlyingUnit()) {
            // Air targets: Dynasty uses 120 frames @ 20Hz movement = 0.8 seconds
            // (120 frames ÷ 3 ticks/frame ÷ 60Hz = 0.8s)
            // Legacy: 50 cycles @ 62.5Hz = 0.8 seconds (50 × 16ms = 800ms)
            detonationTimer = 50;
        }
    }

    destination = *newRealDestination;

    if(bulletID == Bullet_Sonic) {
        int diffX = destination.x - newRealLocation->x;
        int diffY = destination.y - newRealLocation->y;

        int weaponrange = currentGame->objectData.data[Unit_SonicTank][owner->getHouseID()].weaponrange;

        if((diffX == 0) && (diffY == 0)) {
            diffY = weaponrange*TILESIZE;
        }

        FixPoint square_root = FixPoint::sqrt(diffX*diffX + diffY*diffY);
        FixPoint ratio = (weaponrange*TILESIZE)/square_root;
        destination.x = newRealLocation->x + floor(diffX*ratio);
        destination.y = newRealLocation->y + floor(diffY*ratio);
    } else if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {
        // Dynasty scatter algorithm - applies to both ground AND air targets
        FixPoint distance = distanceFrom(*newRealLocation, *newRealDestination);
        const int distanceInTiles = std::max(0, lround(distance / TILESIZE));

        // Pick scatter limit: 15/16 chance → (tiles + 8), 1/16 chance → rand(255) + 8
        int scatterLimit;
        if((currentGame->randomGen.rand(0, 15)) != 0) {
            scatterLimit = distanceInTiles + 8;
        } else {
            scatterLimit = currentGame->randomGen.rand(0, 255) + 8;
        }

        // Pick radius and repeatedly halve until under limit (biases toward large values)
        int radius = currentGame->randomGen.rand(0, 255);
        while(radius > scatterLimit) {
            radius /= 2;
        }

        // Apply scatter in random direction
        // Dynasty formula: offset = (k_stepX[angle] * radius) / 128
        // where k_stepX ∈ [-128,128], so normalized = cos(angle)
        const FixPoint randAngle = 2 * FixPt_PI * currentGame->randomGen.randFixPoint();

        destination.x += lround(FixPoint::cos(randAngle) * radius);
        destination.y -= lround(FixPoint::sin(randAngle) * radius);

    }

    realX = newRealLocation->x;
    realY = newRealLocation->y;
    source.x = newRealLocation->x;
    source.y = newRealLocation->y;
    location.x = newRealLocation->x/TILESIZE;
    location.y = newRealLocation->y/TILESIZE;

    // v0.96.4: Launch toward original target, rockets steer toward scattered destination during flight
    FixPoint angleRad =  destinationAngleRad(*newRealLocation, *newRealDestination);
    angle = RadToDeg256(angleRad);
    drawnAngle = lround(numFrames*angle/256) % numFrames;

    xSpeed = speed * FixPoint::cos(angleRad);
    ySpeed = speed * -FixPoint::sin(angleRad);
}

Bullet::Bullet(InputStream& stream)
{
    bulletID = stream.readUint32();

    airAttack = stream.readBool();
    target.load(stream);
    damage = stream.readSint32();

    shooterID = stream.readUint32();
    Uint32 x = stream.readUint32();
    if(x < NUM_HOUSES) {
        owner = currentGame->getHouse(x);
    } else {
        owner = currentGame->getHouse(0);
    }

    source.x = stream.readSint32();
    source.y = stream.readSint32();
    destination.x = stream.readSint32();
    destination.y = stream.readSint32();
    location.x = stream.readSint32();
    location.y = stream.readSint32();
    realX = stream.readFixPoint();
    realY = stream.readFixPoint();

    xSpeed = stream.readFixPoint();
    ySpeed = stream.readFixPoint();

    drawnAngle = stream.readSint8();
    angle = stream.readFixPoint();

    Bullet::init();

    detonationTimer = stream.readSint8();
}

void Bullet::init()
{
    explodesAtGroundObjects = false;

    int houseID = owner->getHouseID();

    switch(bulletID) {
        case Bullet_DRocket: {
            damageRadius = TILESIZE/2;
            speed = 19.2_fix;  // Dynasty 240 px/s × 5.0 = 1,200 px/s ÷ 62.5Hz = 19.2
            detonationTimer = 19;
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

        case Bullet_LargeRocket: {
            damageRadius = TILESIZE;
            speed = 32.0_fix;
            detonationTimer = -1;
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_LargeRocket, houseID);
        } break;

        case Bullet_Rocket: {
            damageRadius = TILESIZE/2;
            speed = 19.2_fix;  // Dynasty 240 px/s × 5.0 = 1,200 px/s ÷ 62.5Hz = 19.2
            detonationTimer = 22;
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

        case Bullet_TurretRocket: {
            damageRadius = TILESIZE/2;
            speed = 19.2_fix;  // Match launcher: Dynasty 240 px/s × 5.0 = 1,200 px/s ÷ 62.5Hz = 19.2
            // Dynasty: fireDistance 60, doubled to 120 for air targets
            // Dynasty fireDelay decrements every 3 ticks @ 60Hz = 120 * 3/60 = 6.0 seconds
            // Legacy: 6000ms / 16ms per cycle = 375 cycles (at default game speed)
            detonationTimer = MILLI2CYCLES(6000);  // 6 seconds max flight time
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

        case Bullet_ShellSmall: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Small, houseID);
        } break;

        case Bullet_ShellMedium: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Medium, houseID);
        } break;

        case Bullet_ShellLarge: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Large, houseID);
        } break;

        case Bullet_ShellTurret: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Medium, houseID);
        } break;

        case Bullet_SmallRocket: {
            damageRadius = TILESIZE/2;
            speed = 23.04_fix;
            detonationTimer = 7;
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_SmallRocket, houseID);
        } break;

        case Bullet_Sonic: {
            damageRadius = (TILESIZE*3)/4;
            speed = 6;  // For Sonic bullets this is only half the actual speed; see Bullet::update()
            numFrames = 1;
            detonationTimer = 45;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Sonic, HOUSE_HARKONNEN);    // no color remapping
        } break;

        case Bullet_Sandworm: {
            THROW(std::domain_error, "Cannot init 'Bullet_Sandworm': Not allowed!");
        } break;

        default: {
            THROW(std::domain_error, "Unknown Bullet type %d!", bulletID);
        } break;
    }
}


Bullet::~Bullet() = default;

void Bullet::save(OutputStream& stream) const
{
    stream.writeUint32(bulletID);

    stream.writeBool(airAttack);
    target.save(stream);
    stream.writeSint32(damage);

    stream.writeUint32(shooterID);
    stream.writeUint32(owner->getHouseID());

    stream.writeSint32(source.x);
    stream.writeSint32(source.y);
    stream.writeSint32(destination.x);
    stream.writeSint32(destination.y);
    stream.writeSint32(location.x);
    stream.writeSint32(location.y);
    stream.writeFixPoint(realX);
    stream.writeFixPoint(realY);

    stream.writeFixPoint(xSpeed);
    stream.writeFixPoint(ySpeed);

    stream.writeSint8(drawnAngle);
    stream.writeFixPoint(angle);

    stream.writeSint8(detonationTimer);
}


void Bullet::blitToScreen() const
{
    int imageW = getWidth(graphic[currentZoomlevel])/numFrames;
    int imageH = getHeight(graphic[currentZoomlevel]);

    if(screenborder->isInsideScreen( Coord(lround(realX), lround(realY)), Coord(imageW, imageH)) == false) {
        return;
    }

    SDL_Rect dest = calcSpriteDrawingRect(graphic[currentZoomlevel], screenborder->world2screenX(realX), screenborder->world2screenY(realY), numFrames, 1, HAlign::Center, VAlign::Center);

    if(bulletID == Bullet_Sonic) {
        static const int shimmerOffset[]  = { 1, 3, 2, 5, 4, 3, 2, 1 };

        SDL_Texture* shimmerTex = pGFXManager->getZoomedObjPic(ObjPic_Bullet_SonicTemp, currentZoomlevel);
        SDL_Texture* shimmerMaskTex = pGFXManager->getZoomedObjPic(ObjPic_Bullet_Sonic, currentZoomlevel);

        // switch to texture 'shimmerTex' for rendering
        SDL_Texture* oldRenderTarget = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, shimmerTex);

        // copy complete mask
        // contains solid black (0,0,0,255) for pixels to take from screen
        // and transparent (0,0,0,0) for pixels that should not be copied over
        SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, shimmerMaskTex, nullptr, nullptr);
        SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_BLEND);

        // now copy r,g,b colors from screen but don't change alpha values in mask
        SDL_SetTextureBlendMode(screenTexture, SDL_BLENDMODE_ADD);
        SDL_Rect source = dest;
        int shimmerOffsetIndex = ((currentGame->getGameCycleCount() + getBulletID()) % 24)/3;
        source.x += shimmerOffset[shimmerOffsetIndex%8]*2;
        SDL_RenderCopy(renderer, screenTexture, &source, nullptr);
        SDL_SetTextureBlendMode(screenTexture, SDL_BLENDMODE_NONE);

        // switch back to old rendering target (from texture 'shimmerTex')
        SDL_SetRenderTarget(renderer, oldRenderTarget);

        // now blend shimmerTex to screen (= make use of alpha values in mask)
        SDL_SetTextureBlendMode(shimmerTex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, shimmerTex, nullptr, &dest);
    } else {
        SDL_Rect source = calcSpriteSourceRect(graphic[currentZoomlevel], (numFrames > 1) ? drawnAngle: 0, numFrames);
        SDL_RenderCopy(renderer, graphic[currentZoomlevel], &source, &dest);
    }
}


void Bullet::update()
{
    if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {

        // Dynasty: Launcher/Deviator rockets track ornithopters (small, fast targets)
        // Carryalls are NOT tracked - they use static scattered destination
        ObjectBase* pTarget = target.getObjPointer();
        
        if(pTarget != nullptr && pTarget->getItemID() == Unit_Ornithopter) {
            // Track the moving ornithopter's current position (for both steering AND proximity check)
            destination = pTarget->getCenterPoint();
        }
        // Carryalls & ground targets: use the static scattered destination (no tracking)

        FixPoint angleToDestinationRad = destinationAngleRad(Coord(lround(realX), lround(realY)), destination);
        FixPoint angleToDestination = RadToDeg256(angleToDestinationRad);

        FixPoint angleDifference = angleToDestination - angle;
        if(angleDifference > 128) {
            angleDifference -= 256;
        } else if(angleDifference < -128) {
            angleDifference += 256;
        }

        // v0.96.4: FixPt(4,5) = 4.5°/tick
        static const FixPoint turnSpeed = 4.5_fix;

        if(angleDifference >= turnSpeed) {
            angleDifference = turnSpeed;
        } else if(angleDifference <= -turnSpeed) {
            angleDifference = -turnSpeed;
        }

        angle += angleDifference;

        if(angle < 0) {
            angle += 256;
        } else if(angle >= 256) {
            angle -= 256;
        }

        xSpeed = speed * FixPoint::cos(Deg256ToRad(angle));
        ySpeed = speed * -FixPoint::sin(Deg256ToRad(angle));

        drawnAngle = lround(numFrames*angle/256) % numFrames;
    } else if(bulletID == Bullet_TurretRocket) {

        // Dynasty: Turret rockets actively track their moving target
        ObjectBase* pTarget = target.getObjPointer();
        
        if(pTarget != nullptr) {
            // Update destination to track the moving target's current position
            destination = pTarget->getCenterPoint();
        }
        // If target destroyed, continue to last known position (destination unchanged)

        FixPoint angleToDestinationRad = destinationAngleRad(Coord(lround(realX), lround(realY)), destination);
        FixPoint angleToDestination = RadToDeg256(angleToDestinationRad);

        FixPoint angleDifference = angleToDestination - angle;
        if(angleDifference > 128) {
            angleDifference -= 256;
        } else if(angleDifference < -128) {
            angleDifference += 256;
        }

        // v0.96.4: FixPt(4,5) = 4.5°/tick (same as launcher/deviator rockets)
        static const FixPoint turnSpeed = 4.5_fix;

        if(angleDifference >= turnSpeed) {
            angleDifference = turnSpeed;
        } else if(angleDifference <= -turnSpeed) {
            angleDifference = -turnSpeed;
        }

        angle += angleDifference;

        if(angle < 0) {
            angle += 256;
        } else if(angle >= 256) {
            angle -= 256;
        }

        xSpeed = speed * FixPoint::cos(Deg256ToRad(angle));
        ySpeed = speed * -FixPoint::sin(Deg256ToRad(angle));

        drawnAngle = lround(numFrames*angle/256) % numFrames;
    }


    FixPoint oldDistanceToDestination = distanceFrom(realX, realY, destination.x, destination.y);

    realX += xSpeed;  //keep the bullet moving by its current speeds
    realY += ySpeed;
    location.x = floor(realX/TILESIZE);
    location.y = floor(realY/TILESIZE);

    if((location.x < -5) || (location.x >= currentGameMap->getSizeX() + 5) || (location.y < -5) || (location.y >= currentGameMap->getSizeY() + 5)) {
        // it's off the map => delete it
        bulletList.remove(this);
        delete this;
        return;
    } else {
        FixPoint newDistanceToDestination = distanceFrom(realX, realY, destination.x, destination.y);

        if(detonationTimer > 0) {
            detonationTimer--;
        }

        // Dynasty: All rockets with expired timers detonate mid-flight
        if((bulletID == Bullet_TurretRocket) && detonationTimer == 0) {
            destroy();
            return;
        }

        if(bulletID == Bullet_Sonic) {

            if(detonationTimer == 0) {
                destroy();
                return;
            }

            FixPoint weaponDamage = currentGame->objectData.data[Unit_SonicTank][owner->getHouseID()].weapondamage;

            FixPoint startDamage = (weaponDamage / 4 + 1) / 4.5_fix;
            FixPoint endDamage = ((weaponDamage-9) / 4 + 1) / 4.5_fix;

            FixPoint damageDecrease = - (startDamage-endDamage)/(45 * 2 * speed);
            FixPoint dist = distanceFrom(source.x, source.y, realX, realY);

            FixPoint currentDamage = dist*damageDecrease + startDamage;

            Coord realPos = Coord(lround(realX), lround(realY));
            currentGameMap->damage(shooterID, owner, realPos, bulletID, currentDamage/2, damageRadius, false);

            realX += xSpeed;  //keep the bullet moving by its current speeds
            realY += ySpeed;

            realPos = Coord(lround(realX), lround(realY));
            currentGameMap->damage(shooterID, owner, realPos, bulletID, currentDamage/2, damageRadius, false);
        } else if( explodesAtGroundObjects
                    && currentGameMap->tileExists(location)
                    && currentGameMap->getTile(location)->hasAGroundObject()
                    && currentGameMap->getTile(location)->getGroundObject()->isAStructure()
                    && ((bulletID != Bullet_ShellTurret) || (currentGameMap->getTile(location)->getGroundObject()->getOwner() != owner))) {
            destroy();
            return;
        }

        if(oldDistanceToDestination < newDistanceToDestination || newDistanceToDestination < 4)  {

            if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {
                // Check if targeting a flying unit
                ObjectBase* pTarget = target.getObjPointer();
                bool isAirTarget = (pTarget != nullptr && pTarget->isAFlyingUnit());
                
                if(isAirTarget) {
                    // Dynasty-style: Air targets detonate immediately on proximity (no timer check)
                    // Scatter was already applied at launch, tracking during flight
                    destroy();
                    return;
                } else {
                    // v0.96.4: Ground targets only detonate if timer has expired
                    // This creates the point-blank miss behavior
                    if(detonationTimer == 0) {
                destroy();
                return;
                    }
                }
            } else if(bulletID == Bullet_TurretRocket) {
                // Dynasty/v0.96.4: Turret rockets (anti-air) detonate IMMEDIATELY upon reaching destination
                // No timer check needed - they should always hit their target on proximity
                // MULTIPLAYER-SAFE: Track proximity detonation
                ObjectBase* pTarget = target.getObjPointer();
                if(pTarget && pTarget->getItemID() == Unit_Ornithopter) {
                    currentGame->combatStats.turretRocketsProximityDetonated++;
                }
                realX = destination.x;
                realY = destination.y;
                destroy();
                return;
            } else {
                realX = destination.x;
                realY = destination.y;
                destroy();
                return;
            }
        }

        // v0.96.4: Launcher rockets do NOT explode when timer expires mid-flight
        // They only explode when reaching destination AND timer == 0 (checked above)
    }
}


void Bullet::destroy()
{
    Coord position = Coord(lround(realX), lround(realY));

    int houseID = owner->getHouseID();

    switch(bulletID) {
        case Bullet_DRocket: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            soundPlayer->playSoundAt(Sound_ExplosionGas, position);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Gas,position,houseID));
        } break;

        case Bullet_LargeRocket: {
            soundPlayer->playSoundAt(Sound_ExplosionLarge, position);

            for(int i = 0; i < 5; i++) {
                for(int j = 0; j < 5; j++) {
                    if (((i != 0) && (i != 4)) || ((j != 0) && (j != 4))) {
                        position.x = lround(realX) + (i - 2)*TILESIZE;
                        position.y = lround(realY) + (j - 2)*TILESIZE;

                        currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);

                        Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Large1,Explosion_Large2});
                        currentGame->getExplosionList().push_back(new Explosion(explosionID,position,houseID));
                        screenborder->shakeScreen(22);
                    }
                }
            }
        } break;

        case Bullet_Rocket:
        case Bullet_TurretRocket:
        case Bullet_SmallRocket: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Small,position,houseID));
        } break;

        case Bullet_ShellSmall: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellSmall,position,houseID));
        } break;

        case Bullet_ShellMedium: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellMedium,position,houseID));
        } break;

        case Bullet_ShellLarge: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellLarge,position,houseID));
        } break;

        case Bullet_ShellTurret: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellMedium,position,houseID));
        } break;

        case Bullet_Sonic:
        case Bullet_Sandworm:
        default: {
            // do nothing
        } break;
    }

    bulletList.remove(this);
    delete this;
}
