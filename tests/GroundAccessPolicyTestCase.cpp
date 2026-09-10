#include <catch2/catch_test_macros.hpp>
#include <players/GroundAccessPolicy.h>
#include <players/CityPlacementPolicy.h>

namespace {
struct AccessMap {
    int w=24,h=20;
    std::vector<uint8_t> tiles=std::vector<uint8_t>(w*h,1);
    void block(int x,int y,int width,int height) {
        for(int yy=y;yy<y+height;++yy) for(int xx=x;xx<x+width;++xx) tiles[yy*w+xx]=0;
    }
    bool allows(GroundAccessPolicy::Rect r,bool factory=true,bool diagonal=false) const {
        return GroundAccessPolicy::allows(r,factory,[&](int x,int y){
            return x>=0 && y>=0 && x<w && y<h && tiles[y*w+x];
        },diagonal);
    }
};
}
TEST_CASE("Local placement leaves an open perimeter", "[ai][placement]") {
    AccessMap m;
    REQUIRE(m.allows({8,8,2,2}));
    REQUIRE(m.allows({0,0,3,2})); // map edge needs no off-map road
    m.block(7,7,5,4);
    REQUIRE_FALSE(m.allows({8,8,3,2}));
}
TEST_CASE("Buildings can share a wall without sealing the surrounding passage", "[ai][placement]") {
    AccessMap m;m.block(5,5,2,2);
    REQUIRE(m.allows({7,5,2,2}));
    REQUIRE(m.allows({5,7,2,2}));
}
TEST_CASE("Local placement cannot close a lane between existing blocks", "[ai][placement]") {
    AccessMap m;m.block(5,3,2,10);m.block(8,3,2,10);
    REQUIRE_FALSE(m.allows({7,6,1,1},false));
    REQUIRE(m.allows({11,6,2,2}));
}
TEST_CASE("Reservations and mountains use the same local occupancy rules", "[ai][placement]") {
    AccessMap m;m.block(5,3,2,10);
    REQUIRE(m.allows({7,6,1,1},false));
    m.block(8,3,2,10); // reserved neighbouring building
    REQUIRE_FALSE(m.allows({7,6,1,1},false));
}
TEST_CASE("Rocket junctions retain diagonal movement around the turret", "[ai][placement]") {
    AccessMap m;
    REQUIRE(m.allows({8,8,1,1},false,true));
    m.block(7,7,1,3);m.block(9,7,1,3);
    REQUIRE_FALSE(m.allows({8,8,1,1},false,true));
}
TEST_CASE("Placement inspection is bounded independently of map size", "[ai][placement]") {
    int reads=0;
    REQUIRE(GroundAccessPolicy::allows({500,500,3,2},true,[&](int x,int y){
        REQUIRE(x>=499);REQUIRE(x<=503);REQUIRE(y>=499);REQUIRE(y<=502);
        ++reads;return true;
    }));
    REQUIRE(reads==20);
}
TEST_CASE("Residential gaps outrank expansion but retain safety preference", "[ai][placement]") {
    using CityPlacementPolicy::preferCitySite;
    REQUIRE(preferCitySite(true,2,0,0,true,0,2,500));
    REQUIRE_FALSE(preferCitySite(false,3,2,500,true,0,0,0));
    REQUIRE_FALSE(preferCitySite(true,0,2,500,true,2,0,0));
}
