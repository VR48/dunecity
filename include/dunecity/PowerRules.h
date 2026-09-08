#ifndef DUNECITY_POWER_RULES_H
#define DUNECITY_POWER_RULES_H
#include <string_view>
#include <fixmath/FixPoint.h>
namespace DuneCity {
inline int generatorOutput(int nominal, FixPoint health, int maxHealth, bool scalesWithHealth) {
    if (health <= 0 || nominal <= 0) return 0;
    if (!scalesWithHealth) return nominal;
    return maxHealth > 0 ? lround(health / maxHealth * nominal) : 0;
}
inline bool rocketTurretPowered(bool requiresPower, int produced, int required) {
    return !requiresPower || produced >= required;
}
inline bool powerRulesEnabled(bool citySim, std::string_view mod) {
    return citySim || (!mod.empty() && mod != "vanilla");
}
}
#endif
