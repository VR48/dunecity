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

#ifndef REFINERY_H
#define REFINERY_H

#include <structures/StructureBase.h>
#include <ObjectPointer.h>

// forward declarations
class Harvester;
class TrackedUnit;
class UnitBase;
class Carryall;

class Refinery final : public StructureBase
{
public:
    explicit Refinery(House* newOwner);
    explicit Refinery(InputStream& stream);
    void init();
    virtual ~Refinery();

    void save(OutputStream& stream) const override;

    ObjectInterface* getInterfaceContainer() override;

    void assignHarvester(TrackedUnit* newHarvester);
    void deployHarvester(Carryall* pCarryall = nullptr);
    void startAnimate();
    void stopAnimate();

    inline void book() {
        bookings++;
        startAnimate();
    }
    inline void unBook() {
        bookings--;
        if(bookings == 0) {
            stopAnimate();
        }
    }
    inline bool isFree() const { return !extractingSpice; }
    inline int getNumBookings() const { return bookings; }  //number of units goings there
    inline UnitBase* getContainedHarvester() { return harvester.getUnitPointer(); }
    inline const UnitBase* getContainedHarvester() const { return harvester.getUnitPointer(); }
    inline const Harvester* getHarvester() const  { return reinterpret_cast<Harvester*>(harvester.getObjPointer()); }
    inline Harvester* getHarvester() { return reinterpret_cast<Harvester*>(harvester.getObjPointer()); }

    bool acceptsHarvesterDropoff() const override { return true; }
    bool isHarvesterDropoffFree() const override { return isFree(); }
    int getHarvesterDropoffBookings() const override { return getNumBookings(); }
    void bookHarvesterDropoff() override { book(); }
    void unbookHarvesterDropoff() override { unBook(); }
    void startHarvesterDropoffAnimation() override { startAnimate(); }
    bool receiveHarvester(TrackedUnit* unit) override { assignHarvester(unit); return true; }
    void deployContainedHarvester(Carryall* carryall = nullptr) override { deployHarvester(carryall); }
    UnitBase* getContainedHarvesterUnit() override { return getContainedHarvester(); }
    const UnitBase* getContainedHarvesterUnit() const override { return getContainedHarvester(); }

protected:
    /**
        Used for updating things that are specific to that particular structure. Is called from
        StructureBase::update() before the check if this structure is still alive.
    */
    void updateStructureSpecificStuff() override;

private:

    bool            extractingSpice;    ///< Currently extracting spice?
    ObjectPointer   harvester;          ///< The harverster currently in the refinery
    Uint32          bookings;           ///< How many bookings?

    bool    firstRun;       ///< On first deploy of a harvester we tell it to the user
};

#endif // REFINERY_H
