#ifndef CITY_PLANNING_POLICY_H
#define CITY_PLANNING_POLICY_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace CityPlanningPolicy {
// Fixed simulation work, never a wall-clock budget. Every tile is visited once
// per sweep, including the final partial batch. Absolute cycles make the sweep
// reproducible after loading without adding save state.
struct ScanWindow {
    static constexpr int tilesPerPass = 4096;
    int begin = 0, end = 0;
    ScanWindow(int width, int height, uint32_t cycle, unsigned house, unsigned owners = 1) {
        const int cells = width * height;
        if (cells <= 0) return;
        const int batches = (cells + tilesPerPass - 1) / tilesPerPass;
        begin = int((uint64_t(cycle / 100 / std::max(1u,owners)) + house) % batches) * tilesPerPass;
        end = std::min(cells, begin + tilesPerPass);
    }
};

// Waiting-to-place yards are sorted before idle yards by the caller. Rotate
// within each yard group so a blocked ready item cannot consume every pass.
inline void rotateYards(std::vector<std::pair<int,uint32_t>>& order, uint32_t cycle) {
    for (auto first=order.begin();first!=order.end() && first->first>=2;) {
        const auto last=std::find_if(first,order.end(),[&](const auto& row){return row.first!=first->first;});
        std::rotate(first,first+(cycle/100)%std::distance(first,last),last);
        first=last;
    }
}

// Share positive AND negative results within one house's build pass. Geometry
// changes invalidate results without replenishing work: later yards defer to
// the next pass rather than triggering another city-wide scan.
template<class Key, class Result> class PassSearch {
public:
    void reset() { spent = false; cached.reset(); }
    void invalidate() { cached.reset(); }
    const Result* get(const Key& key) const {
        return cached && cached->first == key ? &cached->second : nullptr;
    }
    bool start(const Key& key) {
        if (spent) return false;
        spent = true;
        cached.emplace(key, Result{});
        return true;
    }
    Result& result() { return cached->second; }
private:
    bool spent = false;
    std::optional<std::pair<Key, Result>> cached;
};
}
#endif
