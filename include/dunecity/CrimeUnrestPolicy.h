#ifndef DUNECITY_CRIME_UNREST_POLICY_H
#define DUNECITY_CRIME_UNREST_POLICY_H
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace DuneCity {
// Sustained district crime controls timing; accumulated dangerous building-time
// controls strength. A sudden new cluster cannot inherit a fully grown force.
struct CrimeUnrestDistrict {
    uint32_t progress = 0;
    uint64_t buildingExposure = 0;
    void reset() { progress=0; buildingExposure=0; }
    int advance(int rate, int dangerousBuildings, uint32_t cycles, uint32_t threshold) {
        if (rate<=0 || dangerousBuildings<=0) { reset(); return 0; }
        const uint32_t step=cycles*static_cast<uint32_t>(rate);
        progress+=step;
        buildingExposure+=uint64_t(step)*dangerousBuildings;
        if (progress<threshold) return 0;
        const auto sustainedBuildings=buildingExposure/progress;
        const int strength=std::max(12,3*static_cast<int>(std::min<uint64_t>(20,sustainedBuildings)));
        reset();
        return strength;
    }
};
// Spread three-trooper groups across the district, including when the force
// cap means there are more dangerous buildings than groups.
inline size_t crimeSpawnOrigin(int trooper, int force, size_t origins) {
    if (!origins) return 0;
    const size_t groups=std::min(origins,static_cast<size_t>((force+2)/3));
    return (static_cast<size_t>(trooper/3)%groups)*origins/groups;
}
template<class Stream>
void saveCrimeUnrest(Stream& stream, const std::vector<CrimeUnrestDistrict>& districts) {
    stream.writeUint32(static_cast<uint32_t>(districts.size()));
    for (const auto& district:districts) stream.writeUint32(district.progress);
    for (const auto& district:districts) stream.writeUint64(district.buildingExposure);
}
template<class Stream>
void loadCrimeUnrest(Stream& stream, std::vector<CrimeUnrestDistrict>& districts, int version) {
    if (stream.readUint32()!=districts.size()) throw std::runtime_error("Invalid city unrest district count");
    for (auto& district:districts) district.progress=stream.readUint32();
    if (version>=9833) {
        for (auto& district:districts) district.buildingExposure=stream.readUint64();
    } else {
        // Legacy timers have no building-time evidence and much shorter waits.
        for (auto& district:districts) district.reset();
    }
}
}
#endif
