#ifndef DUNECITY_CITYDEMANDNOTICEPOLICY_H
#define DUNECITY_CITYDEMANDNOTICEPOLICY_H
#include <cstdint>

namespace DuneCity {
// UI-only state: no random numbers, simulation effects or save-format changes.
struct CityDemandNoticePolicy {
    uint8_t announced = 0, pending = 0;
    uint32_t nextCycle = 0;

    uint8_t update(uint8_t missing, uint8_t blocked, uint32_t cycle, uint32_t spacing) {
        announced &= missing;
        pending = (pending | (blocked & ~announced)) & missing;
        if (!pending || cycle < nextCycle) return 0;
        const uint8_t notice = pending & -pending;
        pending &= ~notice;
        announced |= notice;
        nextCycle = cycle + spacing;
        return notice;
    }
};
}
#endif
