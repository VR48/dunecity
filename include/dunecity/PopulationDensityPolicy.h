#ifndef DUNECITY_POPULATION_DENSITY_POLICY_H
#define DUNECITY_POPULATION_DENSITY_POLICY_H
#include <dunecity/CityMapLayer.h>
#include <algorithm>

namespace DuneCity {
// Micropolis populationDensityScan: point sources, three non-dithered
// (centre + four neighbours) / 4 passes, then the original byte-map doubling.
inline void smoothPopulationDensity(CityMapLayer<uint8_t>& density, int width, int height) {
    const int bs = density.getBlockSize();
    const int cols = (width+bs-1)/bs, rows = (height+bs-1)/bs;
    for (int pass=0; pass<3; ++pass) {
        const auto previous = density;
        for (int y=0;y<rows;++y) for (int x=0;x<cols;++x) {
            const int sum = previous.get(x,y)+previous.get(x-1,y)+previous.get(x+1,y)
                +previous.get(x,y-1)+previous.get(x,y+1);
            density.set(x,y,static_cast<uint8_t>(std::min(255,sum/4)));
        }
    }
    for (int y=0;y<rows;++y) for (int x=0;x<cols;++x)
        density.set(x,y,static_cast<uint8_t>(density.get(x,y)*2));
}
}
#endif
