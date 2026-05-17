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

#ifndef ZONESTRUCTURE_H
#define ZONESTRUCTURE_H

#include <structures/StructureBase.h>
#include <dunecity/CityConstants.h>

// Forward declarations
class House;
class Game;

/// A base class for city zone structures (Residential, Commercial, Industrial)
class ZoneStructure : public StructureBase {
public:
    explicit ZoneStructure(House* newOwner, DuneCity::ZoneType zoneType);
    explicit ZoneStructure(InputStream& stream);
    virtual ~ZoneStructure() override;

    void save(OutputStream& stream) const override;

    void setLocation(int xPos, int yPos) override;

    /// Enables placement on sand tiles
    bool canBePlacedAt(int x, int y, bool torch = false) const;

    DuneCity::ZoneType getZoneType() const { return zoneType_; }

    ObjectInterface* getInterfaceContainer() override;

    void destroy() override;

    /// Recompute zone power from current tile density and apply the delta to
    /// the owner's House::powerRequirement. Idempotent; safe to call from
    /// setLocation, density-change hooks, and load.
    void refreshZonePowerDraw();

    /// Update curAnimFrame from current tile density (column in the atlas)
    /// and the sampled land-value tier (row). Called every tick so the
    /// sprite tracks growth and changes in neighbourhood quality.
    void updateStructureSpecificStuff() override;

private:
    DuneCity::ZoneType zoneType_;  // The type of zone this structure represents
    int registeredZonePower_ = 0;  // Power last reported into the House pool.
};

/// A residential zone structure
class ResidentialZone : public ZoneStructure {
public:
    explicit ResidentialZone(House* newOwner);
    explicit ResidentialZone(InputStream& stream);
    virtual ~ResidentialZone() override = default;
private:
    void init();
};

/// A commercial zone structure
class CommercialZone : public ZoneStructure {
public:
    explicit CommercialZone(House* newOwner);
    explicit CommercialZone(InputStream& stream);
    virtual ~CommercialZone() override = default;
private:
    void init();
};

/// An industrial zone structure
class IndustrialZone : public ZoneStructure {
public:
    explicit IndustrialZone(House* newOwner);
    explicit IndustrialZone(InputStream& stream);
    virtual ~IndustrialZone() override = default;
private:
    void init();
};

#endif // ZONESTRUCTURE_H
