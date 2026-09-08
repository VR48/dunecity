#ifndef DUNECITY_CRIME_UNREST_POLICY_H
#define DUNECITY_CRIME_UNREST_POLICY_H
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <utility>

namespace DuneCity {
// Current occupied density determines wave strength. Vacant/non-city buildings
// supply no rebels; city levels 1/2/3 correspond to low/medium/high density.
inline int crimeRebelsForDensity(int level) { return std::clamp(level,0,3); }
struct CrimeUnrestDistrict {
    uint32_t progress = 0;
    uint64_t buildingExposure = 0; // Reserved: retain the 9833 save layout.
    void reset() { progress=0; buildingExposure=0; }
    int advance(int rate, int troopStrength, uint32_t cycles, uint32_t threshold) {
        if (rate<=0 || troopStrength<=0) { reset(); return 0; }
        progress+=cycles*static_cast<uint32_t>(rate);
        if (progress<threshold) return 0;
        reset();
        return troopStrength;
    }
};
// Compact, deterministic nearest-first placement around one hotspot. The runtime
// fills each infantry tile before moving on, and never rescans exhausted slots.
inline std::vector<std::pair<int,int>> crimeSpawnSites(int cx,int cy,int width,int height) {
    std::vector<std::pair<int,int>> sites;
    constexpr int radius=16;
    for (int y=std::max(0,cy-radius);y<std::min(height,cy+radius+1);++y)
        for (int x=std::max(0,cx-radius);x<std::min(width,cx+radius+1);++x)
            sites.emplace_back(x,y);
    std::stable_sort(sites.begin(),sites.end(),[&](const auto& a,const auto& b) {
        auto distance=[&](const auto& p) { return (p.first-cx)*(p.first-cx)+(p.second-cy)*(p.second-cy); };
        return distance(a)<distance(b);
    });
    return sites;
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
