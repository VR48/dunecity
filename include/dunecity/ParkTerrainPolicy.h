#ifndef DUNECITY_PARKTERRAINPOLICY_H
#define DUNECITY_PARKTERRAINPOLICY_H
#include <dunecity/CityMapLayer.h>
#include <algorithm>
#include <cstdlib>

namespace DuneCity {
// Micropolis terrain blocks span four tiles (3-tile zone + 1-tile road).
// DuneCity uses three (2-tile zone + the same 1-tile road). Land-value
// storage remains 2x2; only the park source grid uses this spacing.
constexpr int kParkTerrainBlockSize = 3;
constexpr int kParkTerrainSearchRadius = 2 * kParkTerrainBlockSize;

class ParkTerrainPolicy {
public:
    void init(int width, int height) {
        width_ = width; height_ = height;
        sources_.init(width, height, kParkTerrainBlockSize);
    }
    void addSource(int x, int y, int strength) {
        if (!inside(x,y)) return;
        const int bx=x/kParkTerrainBlockSize, by=y/kParkTerrainBlockSize;
        sources_.set(bx,by,std::clamp(sources_.get(bx,by)+strength,0,255));
    }
    int valueAt(int x, int y) const {
        if (!inside(x,y)) return 0;
        const int bx=x/kParkTerrainBlockSize, by=y/kParkTerrainBlockSize;
        return smooth(sources_.get(bx,by), neighbours(bx,by));
    }
    int landValueContribution(int x, int y, int landBlockSize=2) const {
        if (!inside(x,y)) return 0;
        const int x0=x/landBlockSize*landBlockSize, y0=y/landBlockSize*landBlockSize;
        int sum=0,count=0;
        // The 3x3 terrain grid and 2x2 land-value grid do not align. Average
        // the covered tiles rather than losing an odd-coordinate source.
        for (int wy=y0;wy<std::min(height_,y0+landBlockSize);++wy)
            for (int wx=x0;wx<std::min(width_,x0+landBlockSize);++wx) {
                sum+=valueAt(wx,wy); ++count;
            }
        return count ? sum/count : 0;
    }
    // Exact marginal change after aggregating existing/planned sources,
    // smoothing and resampling. Bounded local reads, no flood/allocation.
    int marginalGain(int cx,int cy,int strength,int px,int py,int landBlockSize=2) const {
        if (!inside(cx,cy) || !inside(px,py) || strength<=0) return 0;
        const int sx=cx/kParkTerrainBlockSize, sy=cy/kParkTerrainBlockSize;
        const int added=std::min(strength,255-sources_.get(sx,sy));
        const int x0=px/landBlockSize*landBlockSize, y0=py/landBlockSize*landBlockSize;
        int before=0,after=0,count=0;
        for (int y=y0;y<std::min(height_,y0+landBlockSize);++y)
            for (int x=x0;x<std::min(width_,x0+landBlockSize);++x) {
                const int bx=x/kParkTerrainBlockSize, by=y/kParkTerrainBlockSize;
                const int distance=std::abs(sx-bx)+std::abs(sy-by);
                const int center=sources_.get(bx,by), adjacent=neighbours(bx,by);
                before+=smooth(center,adjacent);
                after+=smooth(center+(distance==0?added:0),adjacent+(distance==1?added:0));
                ++count;
            }
        return count ? after/count-before/count : 0;
    }
private:
    bool inside(int x,int y) const { return x>=0 && y>=0 && x<width_ && y<height_; }
    int neighbours(int x,int y) const {
        return sources_.get(x-1,y)+sources_.get(x+1,y)
             + sources_.get(x,y-1)+sources_.get(x,y+1);
    }
    // Micropolis smoothTerrain(), non-dither branch: one pass, with
    // integer rounding after the neighbour average and after halving.
    static int smooth(int center,int adjacent) { return (center+adjacent/4)/2; }
    int width_=0,height_=0;
    CityMapLayer<int> sources_;
};
}
#endif
