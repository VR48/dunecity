#ifndef DUNECITY_POLICECOVERAGEPOLICY_H
#define DUNECITY_POLICECOVERAGEPOLICY_H
#include <dunecity/CityEffects.h>
#include <dunecity/CityMapLayer.h>
#include <array>
#include <map>
#include <tuple>
namespace DuneCity {
// Two (zone + road) pitches: Micropolis 8 tiles becomes 6 here.
constexpr int kPoliceMapBlockSize = 6;
inline int policeSourceStrength(int strength,int funding,bool powered,bool road) {
    int result=strength*std::clamp(funding,0,100)/100;
    if(!powered) result/=2;
    if(!road) result/=2;
    return result;
}
struct PoliceSource { int x,y,strength; };
template<class MapType>
PoliceSource policeSource(const MapType& map,int x,int y,int sx,int sy,
                          int strength,int funding,bool powered) {
    auto road=[&](int wx,int wy) {
        if(wx<0||wy<0||wx>=map.getSizeX()||wy>=map.getSizeY()) return false;
        const auto* tile=map.getTile(wx,wy);
        return tile && tile->isRoad();
    };
    if(sx==1&&sy==1&&road(x,y))
        return {x,y,policeSourceStrength(strength,funding,powered,true)};
    for(int yy:{y-1,y+sy}) for(int xx=x-1;xx<=x+sx;++xx)
        if(road(xx,yy)) return {xx,yy,policeSourceStrength(strength,funding,powered,true)};
    for(int yy=y;yy<y+sy;++yy) for(int xx:{x-1,x+sx})
        if(road(xx,yy)) return {xx,yy,policeSourceStrength(strength,funding,powered,true)};
    return {x,y,policeSourceStrength(strength,funding,powered,false)};
}
inline void addPoliceCoverage(CityMapLayer<int32_t>& raw,int width,int height,
                              int x,int y,int strength) {
    if(x<0||y<0||x>=width||y>=height||strength<=0) return;
    const int bs=raw.getBlockSize();
    raw.set(x/bs,y/bs,raw.get(x/bs,y/bs)+strength);
}
inline void smoothPoliceCoverage(CityMapLayer<int32_t>& layer,int width,int height) {
    const int bs=layer.getBlockSize(),w=(width+bs-1)/bs,h=(height+bs-1)/bs;
    // Micropolis smoothStationMap: sum every source BEFORE three passes.
    // Overlaps are additive; no placement penalty enters this calculation.
    for(int pass=0;pass<3;++pass) {
        const auto previous=layer;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const int n=previous.get(x-1,y)+previous.get(x+1,y)
                +previous.get(x,y-1)+previous.get(x,y+1);
            layer.set(x,y,(previous.get(x,y)+n/4)/2);
        }
    }
}
// Isolated-source AI estimate. Runtime smooths all sources together, retaining
// integer rounding carries between overlapping sources.
inline int policeCoverageAt(int cx,int cy,int x,int y,int /*crimeBlockSize*/,
                            int strength,int width=100000,int height=100000) {
    const int bs=kPoliceMapBlockSize,dx=x/bs-cx/bs,dy=y/bs-cy/bs;
    if(std::abs(dx)+std::abs(dy)>3||strength<=0) return 0;
    const int l=std::min(3,cx/bs),t=std::min(3,cy/bs);
    const int r=std::min(3,(width-1)/bs-cx/bs),b=std::min(3,(height-1)/bs-cy/bs);
    using Key=std::tuple<int,int,int,int,int>;
    static thread_local std::map<Key,std::array<int,49>> cache;
    const Key key{strength,l,t,r,b};
    auto found=cache.find(key);
    if(found==cache.end()) {
        std::array<int,49> field{}; field[24]=strength;
        for(int pass=0;pass<3;++pass) {
            const auto old=field;
            for(int yy=-t;yy<=b;++yy) for(int xx=-l;xx<=r;++xx) {
                const int i=(yy+3)*7+xx+3;
                const int n=(xx>-l?old[i-1]:0)+(xx<r?old[i+1]:0)
                    +(yy>-t?old[i-7]:0)+(yy<b?old[i+7]:0);
                field[i]=(old[i]+n/4)/2;
            }
        }
        found=cache.emplace(key,field).first;
    }
    return found->second[(dy+3)*7+dx+3];
}
inline int marginalCrimeReduction(int base,int coverage,int added) {
    return std::clamp(base-coverage,0,250)-std::clamp(base-coverage-added,0,250);
}
}
#endif
