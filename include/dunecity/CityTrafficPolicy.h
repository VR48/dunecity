#ifndef DUNECITY_CITYTRAFFICPOLICY_H
#define DUNECITY_CITYTRAFFICPOLICY_H

#include <dunecity/CityMapLayer.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace DuneCity::CityTraffic {
struct Point {
    int x, y;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

// Bounded BFS preserves connectivity; rotate equal-length route choices. Keep
// parents so traffic follows one successful route, never the explored branches.
class RouteFinder {
public:
    void clear() { route_.clear(); }
    const std::vector<Point>& route() const { return route_; }

    template<class IsRoad, class IsDestination>
    bool find(int width, int height, Point start, int maxDistance,
              IsRoad isRoad, IsDestination isDestination, unsigned directionOffset = 0) {
        route_.clear(); queue_.clear();
        if (width <= 0 || height <= 0 || maxDistance < 0 || start.x < 0 || start.y < 0
            || start.x >= width || start.y >= height || !isRoad(start.x,start.y)) return false;
        const auto size = static_cast<size_t>(width) * height;
        if (visited_.size() != size) { visited_.assign(size,0); generation_ = 0; }
        if (++generation_ == 0) { std::fill(visited_.begin(),visited_.end(),0); ++generation_; }
        // Reused generation stamps avoid clearing a whole map for every zone.
        visited_[start.y*width+start.x] = generation_;
        queue_.push_back({start,-1,0});
        constexpr int dx[] = {0,1,0,-1}, dy[] = {-1,0,1,0};
        for (size_t head = 0; head < queue_.size(); ++head) {
            const Node node = queue_[head]; // append can reallocate the queue
            const Point p = node.point;
            if (isDestination(p.x,p.y)) {
                for (int index = static_cast<int>(head); index >= 0; index = queue_[index].parent)
                    route_.push_back(queue_[index].point);
                std::reverse(route_.begin(),route_.end());
                return true;
            }
            if (node.distance >= maxDistance) continue;
            for (int d = 0; d < 4; ++d) {
                const int direction = (d + directionOffset) & 3;
                const int x = p.x+dx[direction], y = p.y+dy[direction];
                if (x < 0 || y < 0 || x >= width || y >= height) continue;
                const int index = y*width+x;
                if (visited_[index] == generation_ || !isRoad(x,y)) continue;
                visited_[index] = generation_;
                queue_.push_back({{x,y},static_cast<int>(head),node.distance+1});
            }
        }
        return false;
    }
private:
    struct Node { Point point; int parent, distance; };
    std::vector<Node> queue_;
    std::vector<uint32_t> visited_;
    std::vector<Point> route_;
    uint32_t generation_ = 0;
};

// Micropolis simulate.cpp::decTrafficMap, once per traffic-map cell.
inline uint8_t decayed(int value) {
    return static_cast<uint8_t>(value <= 24 ? 0 : value - (value > 200 ? 34 : 24));
}
inline void decay(CityMapLayer<uint8_t>& density, int width, int height) {
    const int bs = density.getBlockSize();
    for (int y = 0; y < (height+bs-1)/bs; ++y)
        for (int x = 0; x < (width+bs-1)/bs; ++x)
            density.set(x,y,decayed(density.get(x,y)));
}

// Micropolis: R population > random(35), C/I population > random(5).
// Mix site and day independently so neighbouring trips do not synchronise.
// Deterministic sampling avoids consuming the combat RNG or adding save state.
inline bool journeyDue(bool residential, int population, int x, int y, uint32_t day) {
    const unsigned range = residential ? 36u : 6u;
    if (population <= 0) return false;
    if (population >= int(range)) return true;
    uint32_t hash = uint32_t(x)*0x9e3779b9u ^ uint32_t(y)*0x85ebca6bu ^ day*0xc2b2ae35u;
    hash ^= hash >> 16; hash *= 0x7feb352du;
    hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
    return hash % range < unsigned(population);
}

// Balance adaptation, not an original Micropolis constant: a 2x2 plot emits
// 4/9 of a 3x3 plot's 50-unit load (22, rounded down). Roads stay one tile
// wide. Keep the original decay and display thresholds: one daily trip must
// not permanently saturate its fixed access road just because time passes.
inline constexpr int kJourneyLoad = 50 * 4 / 9;

// route[0] is the perimeter start. Original tryDrive saves moves 2,4,6,...
// for its 2x2 traffic cells; addToTrafficDensityMap adds 50, capped at 240.
// Sample the route, not every world tile or every explored search branch.
template<class IsRoad>
void addJourney(CityMapLayer<uint8_t>& density, const std::vector<Point>& route, IsRoad isRoad, int load = kJourneyLoad) {
    const int bs = density.getBlockSize();
    for (size_t i = 2; i < route.size(); i += 2) {
        const auto p = route[i];
        if (!isRoad(p.x,p.y)) continue;
        const int x = p.x/bs, y = p.y/bs;
        // A bend can put separate sampled road tiles in the same 2x2 cell.
        // Count that journey once, not twice in the shared density cell.
        bool alreadySampled = false;
        for (size_t j = 2; j < i; j += 2)
            if (route[j].x/bs == x && route[j].y/bs == y && isRoad(route[j].x,route[j].y)) {
                alreadySampled = true;
                break;
            }
        if (!alreadySampled)
            density.set(x,y,static_cast<uint8_t>(std::min(240,density.get(x,y)+load)));
    }
}
} // namespace DuneCity::CityTraffic
#endif
