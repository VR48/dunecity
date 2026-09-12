#ifndef REDEVELOPMENT_POLICY_H
#define REDEVELOPMENT_POLICY_H
#include <algorithm>
namespace RedevelopmentPolicy {
// R's demand ceiling is2000; C/I1500. Compare equal fractions equally.
inline int displacementCost(int density, int landValue, int demand, int demandCeiling) {
    const int normalized = std::clamp(demand*1000/std::max(1,demandCeiling),-1000,1000);
    return (normalized+1000)*20 + (density>0 ? 10000:0) + density*1000
        + std::max(0,landValue);
}
}
#endif
