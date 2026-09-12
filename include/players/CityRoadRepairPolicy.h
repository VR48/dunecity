#ifndef CITY_ROAD_REPAIR_POLICY_H
#define CITY_ROAD_REPAIR_POLICY_H
#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace CityRoadRepairPolicy {
struct Footprint { int x,y,width,height; };
// Consider surviving buildings throughout the city, not a radius around the
// base centre. Prefer broken through-roads, then junctions and connected edges.
// Callbacks reject occupied terrain/reserved construction and recognise roads
// under turret intersections as connections too.
template<class CanPlace, class HasRoad>
std::vector<std::pair<int,int>> candidates(const std::vector<Footprint>& buildings,
                                         CanPlace canPlace, HasRoad hasRoad) {
    std::set<std::pair<int,int>> perimeter;
    for (const auto& b:buildings) {
        for (int x=b.x-1;x<=b.x+b.width;++x) {
            perimeter.emplace(x,b.y-1); perimeter.emplace(x,b.y+b.height);
        }
        for (int y=b.y;y<b.y+b.height;++y) {
            perimeter.emplace(b.x-1,y); perimeter.emplace(b.x+b.width,y);
        }
    }
    struct Site { int x,y,score; };
    std::vector<Site> ranked;
    for (const auto& p:perimeter) {
        const int x=p.first,y=p.second;
        if (!canPlace(x,y)) continue;
        const bool n=hasRoad(x,y-1),s=hasRoad(x,y+1),e=hasRoad(x+1,y),w=hasRoad(x-1,y);
        const int connections=int(n)+int(s)+int(e)+int(w);
        if (connections==0) continue;
        ranked.push_back({x,y,connections+((n&&s)||(e&&w) ? 8 : 0)});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b) { return a.score>b.score; });
    std::vector<std::pair<int,int>> result;
    for (const auto& p:ranked) result.emplace_back(p.x,p.y);
    return result;
}
}
#endif
