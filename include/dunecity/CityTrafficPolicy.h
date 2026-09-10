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

// Preserve the existing deterministic N/E/S/W BFS connectivity rule. Keep
// parents so traffic follows one successful route, never the explored branches.
class RouteFinder {
public:
    void clear() { route_.clear(); }
    const std::vector<Point>& route() const { return route_; }

    template<class IsRoad, class IsDestination>
    bool find(int width, int height, Point start, int maxDistance,
              IsRoad isRoad, IsDestination isDestination) {
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
                const int x = p.x+dx[d], y = p.y+dy[d];
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

// route[0] is the perimeter start. Original tryDrive saves moves 2,4,6,...
// for its 2x2 traffic cells; addToTrafficDensityMap adds 50, capped at 240.
// Sample the route, not every world tile or every explored search branch.
template<class IsRoad>
void addJourney(CityMapLayer<uint8_t>& density, const std::vector<Point>& route, IsRoad isRoad) {
    const int bs = density.getBlockSize();
    for (size_t i = 2; i < route.size(); i += 2) {
        const auto p = route[i];
        if (!isRoad(p.x,p.y)) continue;
        const int x = p.x/bs, y = p.y/bs;
        density.set(x,y,static_cast<uint8_t>(std::min(240,density.get(x,y)+50)));
    }
}
} // namespace DuneCity::CityTraffic
#endif
