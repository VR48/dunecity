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

#ifndef FLAMETANK_H
#define FLAMETANK_H

#include <units/TrackedUnit.h>

/// Flame Tank (Tornie mod) - Rebels Heavy Factory unit unlocked by House IX.
/// Fires Bullet_Flame: launcher-like projectile with flame impact and area damage.
/// Anti-infantry focused, no deviation effect, no aircraft damage.
class FlameTank final : public TrackedUnit
{
public:
    explicit FlameTank(House* newOwner);
    explicit FlameTank(InputStream& stream);
    void init();
    virtual ~FlameTank();

    void blitToScreen() override;
    void destroy() override;
    void playAttackSound() override;

private:
    zoomable_texture turretGraphic{};
    int              gunGraphicID;
};

#endif // FLAMETANK_H
