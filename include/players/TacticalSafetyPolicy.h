#ifndef TACTICAL_SAFETY_POLICY_H
#define TACTICAL_SAFETY_POLICY_H
#include <algorithm>
#include <vector>
#include <tuple>
#include <data.h>
#include <players/CityPlacementPolicy.h>

namespace TacticalSafetyPolicy {
inline bool needsRefineryRefuge(bool threatened,bool unsafeJob,bool returning,bool hasCargo) {
    // An empty vehicle already in safety needs a new field or a safe hold,
    // not another unload/deploy loop caused by its old dangerous spice job.
    return threatened || (hasCargo && (unsafeJob || returning));
}

inline int harvesterThreatRadius(int item, int range) {
    if (isInfantryUnit(item)) return -1; // Harvesters can crush foot troops.
    return std::max(1,range)+(item==Unit_Launcher ? 3 : 0);
}

inline bool productionFactory(int type) {
    return type == Structure_HeavyFactory || type == Structure_LightFactory
        || type == Structure_HighTechFactory || type == Structure_Barracks || type == Structure_WOR;
}
// Distance beyond known weapon reach (including the construction safety buffer).
// Two deterministic passes cost O(map area), shared by every candidate. Beyond
// twelve clear tiles, normal placement preferences decide rather than map edges.
inline std::vector<int> enemyClearance(const std::vector<int>& danger, int w, int h) {
    constexpr int sufficient = 12;
    std::vector<int> result(w*h, sufficient);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        auto& distance=result[y*w+x];
        if (danger[y*w+x]>0) { distance=0; continue; }
        if (x>0) distance=std::min(distance,result[y*w+x-1]+1);
        if (y>0) for (int dx=-1; dx<=1; ++dx)
            if (x+dx>=0 && x+dx<w) distance=std::min(distance,result[(y-1)*w+x+dx]+1);
    }
    for (int y=h-1; y>=0; --y) for (int x=w-1; x>=0; --x) {
        auto& distance=result[y*w+x];
        if (x+1<w) distance=std::min(distance,result[y*w+x+1]+1);
        if (y+1<h) for (int dx=-1; dx<=1; ++dx)
            if (x+dx>=0 && x+dx<w) distance=std::min(distance,result[(y+1)*w+x+dx]+1);
    }
    return result;
}
inline int footprintClearance(const std::vector<int>& clearance, int w, int h,
                              int x, int y, int sx, int sy) {
    if (x<0 || y<0 || x+sx>w || y+sy>h || clearance.size()!=static_cast<size_t>(w*h)) return 0;
    int result=12;
    for (int py=y; py<y+sy; ++py) for (int px=x; px<x+sx; ++px)
        result=std::min(result,clearance[py*w+px]);
    return result;
}
// Avoid known loss sites first, then prefer distance from live threats. This
// ranks legal alternatives; it never bans the only available factory site.
inline auto factorySiteRank(int lossRisk, int clearance, int tier, int score) {
    return std::make_tuple(lossRisk==0,clearance,tier,score);
}
inline bool protectedReactorNeighbour(int type) {
    return type == Structure_NuclearPlant || type == Structure_HeavyFactory
        || type == Structure_ConstructionYard || type == Structure_RepairYard
        || type == Structure_HighTechFactory || type == Structure_IX
        || type == Structure_Palace || type == Structure_StarPort || type == Structure_Refinery;
}
// Four clear tiles between footprints, exceeding the reactor's 3.66-tile radius.
inline bool blastClearance(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
    return CityPlacementPolicy::footprintDistance(x,y,w,h,bx,by,bw,bh) >= 5;
}
inline int lossStrength(unsigned age, unsigned lifetime) {
    if (!lifetime || age >= lifetime) return 0;
    return 1 + 100 * (lifetime-age) / lifetime;
}
// Skip the first tile when escaping a firing zone. For outbound spice journeys,
// the caller separately checks the vehicle's current tile before using this.
template<class Danger>
int corridorDanger(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int result = 0;
    for (int i = 2; i <= steps; ++i)
        result = std::max(result, danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps));
    return result;
}
// Escape an existing firing zone without entering stronger danger or re-entering
// danger after reaching safety. This is a corridor estimate, not a path proof.
template<class Danger>
bool escapeCorridor(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int previous = danger(x,y);
    for (int i=1; i<=steps; ++i) {
        const int next = danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps);
        if (next > previous) return false;
        previous = next;
    }
    return previous == 0;
}

}
#endif
