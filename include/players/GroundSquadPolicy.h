#ifndef GROUND_SQUAD_POLICY_H
#define GROUND_SQUAD_POLICY_H
#include <algorithm>
namespace GroundSquadPolicy {
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
