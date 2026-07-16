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

#ifndef TECHCENTER_H
#define TECHCENTER_H

#include <structures/StructureBase.h>

/// TechCenter (Tornie mod) — Palace-equivalent building.
///
/// Per Tornie spec: behaves like a Palace but instead of producing a
/// special weapon (Death Hand / Saboteur / Fremen), spawns 1-3 random
/// vehicles around itself when House IX is unlocked for that house.
/// The spawned units default to Area Guard behaviour. TechLevel 9 —
/// this is the last building unlocked in the Tornie tech tree.
///
/// Stats come from ObjectData.ini under [Tech Center]. Production
/// of vehicles is gated by the house's unlock state; until the
/// matching house reaches its per-house IX-unlock (TechLevel(R) = 9
/// typically), no vehicles spawn.
class TechCenter final : public StructureBase
{
public:
    explicit TechCenter(House* newOwner);
    explicit TechCenter(InputStream& stream);
    void init();
    virtual ~TechCenter();

    void save(OutputStream& stream) const override;

    ObjectInterface* getInterfaceContainer() override;

    bool canBeCaptured() const override { return false; }

    /// Production cycle status for the UI (mirrors Palace::getPercentComplete).
    inline int getPercentComplete() const {
        return spawnTimer * 100 / getMaxSpawnTimer();
    }
    inline bool isSpawnReady() const { return spawnTimer == 0; }
    bool canSpawnVehicles() const;
    void handleSpawnClick();
    void doSpawnVehicles();
    inline int getSpawnTimer() const { return spawnTimer; }
    inline int getMaxSpawnTimer() const {
        // Same cadence as Palace special weapon: 10 min for Harkonnen/Sardaukar,
        // 5 min otherwise.
        if(originalHouseID == HOUSE_HARKONNEN || originalHouseID == HOUSE_SARDAUKAR) {
            return MILLI2CYCLES(10*60*1000);
        } else {
            return MILLI2CYCLES(5*60*1000);
        }
    }

protected:
    void updateStructureSpecificStuff() override;

private:
    /// Returns true if the owning house has reached its IX-unlock (TechLevel(R) >= 9).
    bool houseHasIxUnlocked() const;

    /// Spawns 1-3 random vehicles of mixed types around the Tech Center.
    /// Uses currentGame->randomGen for selection. Returns the count spawned.
    int spawnRandomVehicles(int count);

    /// Per-house IX-unlock state — set by the house itself, not saved.
    /// Tracked locally so we don't repeatedly try to spawn.
    Sint32  spawnTimer;          ///< Counts down to 0; spawn 1-3 vehicles on hit.
};

#endif // TECHCENTER_H
