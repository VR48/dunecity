#ifndef GROUND_SQUAD_POLICY_H
#define GROUND_SQUAD_POLICY_H
#include <algorithm>
#include <vector>
#include <utility>
namespace GroundSquadPolicy {
// Resolve the object once: an ObjectPointer can retain a dead object's ID.
template<class Unit, class InRange>
bool engaged(const Unit* unit, InRange inRange) {
    const auto* target = unit->getTarget();
    return target && target->getHealth() > 0 && unit->canAttack(target) && inRange(target);
}

inline int formationRadius(int members) {
    int width = 1;
    while (width * width < std::max(1, members) * 2) ++width;
    return width / 2 + 3;
}

// Unique, connected destinations. Friendly unit occupancy must not be included
// in walkable: an assembling army is not a permanent terrain obstruction.
template<class Walkable>
std::vector<std::pair<int,int>> rallySlots(int x, int y, int radius, Walkable walkable) {
    std::vector<std::pair<int,int>> slots;
    if (!walkable(x,y)) return slots;
    const int width = radius * 2 + 1;
    std::vector<bool> seen(width * width, false);
    slots.emplace_back(x,y);
    seen[radius * width + radius] = true;
    for (size_t i = 0; i < slots.size(); ++i) {
        const auto p = slots[i];
        for (const auto offset : {std::pair<int,int>{0,-1}, {1,0}, {0,1}, {-1,0}}) {
            const int nx=p.first+offset.first, ny=p.second+offset.second;
            const int dx=nx-x+radius, dy=ny-y+radius;
            if (dx<0 || dy<0 || dx>=width || dy>=width) continue;
            const int index=dy*width+dx;
            if (seen[index]) continue;
            seen[index]=true;
            if (walkable(nx,ny)) slots.emplace_back(nx,ny);
        }
    }
    return slots;
}

// Cohesion limits the leading edge, not every member of an imperfect formation.
// A majority must remain together; stragglers catch up without recalling the core.
inline bool holdCore(int compact, int members) { return compact * 100 < members * 70; }
enum class Assembly { Wait, Launch, Abort };
inline int committedCount(int available) { return std::max(0,available)*80/100; }
inline Assembly assembly(int ready,int members,int original,bool deadline) {
    if (members<6 || members*2<original) return Assembly::Abort;
    if (ready*100>=members*85) return Assembly::Launch;
    if (!deadline) return Assembly::Wait;
    return ready>=6 && ready*100>=original*70 ? Assembly::Launch : Assembly::Abort;
}
inline bool waitForBody(int unitToTarget,int centreToTarget,int radius,bool engaged) {
    return !engaged && unitToTarget+radius/2<centreToTarget;
}
}
#endif
