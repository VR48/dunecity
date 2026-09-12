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
    uint64_t readyCycles = 0; // 9834 reuses the old 64-bit exposure slot.
    void reset() { progress=0; readyCycles=0; }
    int advance(int rate, int troopStrength, uint32_t cycles, uint32_t threshold) {
        if (rate<=0 || troopStrength<=0) { reset(); return 0; }
        const bool alreadyReady=progress>=threshold;
        progress=static_cast<uint32_t>(std::min<uint64_t>(threshold,uint64_t(progress)+uint64_t(cycles)*rate));
        if (progress<threshold) return 0;
        if (alreadyReady) readyCycles+=cycles;
        return troopStrength;
    }
};
// Mature neighbouring districts share one outbreak. Hold readiness briefly to
// catch staggered neighbours, without maturing any district early. Components
// never cross house boundaries or wrap around a row of the district grid.
inline std::vector<std::vector<size_t>> crimeOutbreakGroups(
        const std::vector<CrimeUnrestDistrict>& districts,const std::vector<int>& strength,
        int width,int height,uint32_t threshold,uint32_t gatheringCycles) {
    std::vector<std::vector<size_t>> groups;
    if(width<=0 || height<=0 || strength.size()!=districts.size()) return groups;
    const size_t perHouse=static_cast<size_t>(width)*height;
    std::vector<bool> seen(districts.size());
    auto ready=[&](size_t i) { return strength[i]>0 && districts[i].progress>=threshold; };
    for(size_t seed=0;seed<districts.size();++seed) {
        if(seen[seed] || !ready(seed)) continue;
        std::vector<size_t> group{seed}; seen[seed]=true;
        bool due=false;
        const size_t houseStart=(seed/perHouse)*perHouse;
        for(size_t cursor=0;cursor<group.size();++cursor) {
            const auto i=group[cursor];
            due=due || districts[i].readyCycles>=gatheringCycles;
            const int x=(i-houseStart)%width,y=(i-houseStart)/width;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const int nx=x+dx,ny=y+dy;
                if(nx<0 || ny<0 || nx>=width || ny>=height) continue;
                const size_t next=houseStart+ny*width+nx;
                if(next>=districts.size() || seen[next] || !ready(next)) continue;
                seen[next]=true; group.push_back(next);
            }
        }
        if(due) groups.push_back(std::move(group));
    }
    return groups;
}
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
    for (const auto& district:districts) stream.writeUint64(district.readyCycles);
}
template<class Stream>
void loadCrimeUnrest(Stream& stream, std::vector<CrimeUnrestDistrict>& districts, int version) {
    if (stream.readUint32()!=districts.size()) throw std::runtime_error("Invalid city unrest district count");
    for (auto& district:districts) district.progress=stream.readUint32();
    if (version>=9833) {
        for (auto& district:districts) {
            const auto saved=stream.readUint64();
            district.readyCycles=version>=9834 ? saved : 0;
        }
    } else {
        // Legacy timers have no building-time evidence and much shorter waits.
        for (auto& district:districts) district.reset();
    }
}
}
#endif
