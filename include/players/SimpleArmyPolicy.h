#ifndef SIMPLE_ARMY_POLICY_H
#define SIMPLE_ARMY_POLICY_H
#include <algorithm>
#include <cstdint>
#include <vector>
#include <optional>
#include <utility>
namespace SimpleArmyPolicy {
struct Responder { uint32_t id; int value; int distance; };
inline int responseValue(int threat) { return std::max(0,threat) + (std::max(0,threat)+3)/4; }
// Prefer the nearest usable troops; existing responders count against the budget.
// The last unit may overshoot, but a small incident cannot requisition the army.
inline std::vector<uint32_t> reinforcements(int threat, int committed, std::vector<Responder> candidates) {
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.distance!=b.distance ? a.distance<b.distance : a.id<b.id;
    });
    std::vector<uint32_t> selected;
    int value=std::max(0,committed);
    for (const auto& candidate:candidates) {
        if (value>=responseValue(threat)) break;
        if (candidate.value<=0) continue;
        selected.push_back(candidate.id); value+=candidate.value;
    }
    return selected;
}
// A proactive clearing mission needs a complete local response. Do not trickle
// a few distant troops into a defended spice field just because it is dangerous.
inline std::vector<uint32_t> clearingForce(int threat,int committed,std::vector<Responder> candidates,int radius) {
    candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& c) {
        return c.distance>radius;
    }),candidates.end());
    int available=std::max(0,committed);
    for (const auto& c:candidates) available+=std::max(0,c.value);
    if (available<responseValue(threat)) return {};
    return reinforcements(threat,committed,std::move(candidates));
}
// Bounded local scatter, never a flood fill or an occupied centre fallback.
template<class Usable>
std::optional<std::pair<int,int>> rallyOffset(uint32_t id,int radius,Usable usable) {
    const int r=std::max(2,radius),width=2*r+1;
    for (int attempt=0;attempt<8;++attempt) {
        const int x=int((id*17+attempt*7)%width)-r,y=int((id*31+attempt*11)%width)-r;
        if (usable(x,y)) return std::pair<int,int>{x,y};
    }
    return std::nullopt;
}
}
#endif
