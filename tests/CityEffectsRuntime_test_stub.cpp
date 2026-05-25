/*
 *  CityEffectsRuntime_test_stub.cpp
 *
 *  Empty replacements for CitySimulation::runEffectsScans / runZoneGrowth
 *  in the unit-test build. The real implementations live in
 *  src/dunecity/CityEffectsRuntime.cpp and link against Map / Tile /
 *  ZoneStructure / House — heavy dependencies the test target deliberately
 *  doesn't include. Tests don't exercise the runtime scans (those are
 *  integration-tier behavior verified in-game), so empty bodies are correct.
 */

#include <dunecity/CitySimulation.h>

namespace DuneCity {

int32_t CitySimulation::getTotalFunds() const { return totalFunds_; }

bool CitySimulation::spendCityFunds(int32_t amount) {
    if (totalFunds_ >= amount) {
        totalFunds_ -= amount;
        return true;
    }
    return false;
}

void CitySimulation::runEffectsScans()    {}
void CitySimulation::runZoneGrowth()      {}
void CitySimulation::runDailyBudget()     {}
void CitySimulation::decayGrowthRateMap() {}

}  // namespace DuneCity
