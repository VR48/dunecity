#ifndef DUNECITY_AIR_PATROL_CYCLE_H
#define DUNECITY_AIR_PATROL_CYCLE_H
#include <algorithm>
namespace DuneCity {
struct AirPatrolCycle {
    int remainingCycles;
    int pendingAircraft = 2;
    explicit AirPatrolCycle(int cooldown) : remainingCycles(cooldown) {}
    void tick() { if (remainingCycles > 0) --remainingCycles; }
    bool ready() const { return remainingCycles == 0; }
    void deployed(int cooldown) {
        if (--pendingAircraft == 0) { pendingAircraft = 2; remainingCycles = cooldown; }
    }
    void restore(int remaining, int pending, int cooldown) {
        remainingCycles = std::clamp(remaining,0,cooldown);
        pendingAircraft = std::clamp(pending,1,2);
    }
};
}
#endif
