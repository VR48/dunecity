#include <catch2/catch_test_macros.hpp>
#include <players/GroundAccessPolicy.h>

namespace {
struct AccessMap {
    int w=24,h=20;
    std::vector<uint8_t> tiles=std::vector<uint8_t>(w*h,1);
    void block(int x,int y,int width,int height) {
        for (int yy=y;yy<y+height;++yy) for (int xx=x;xx<x+width;++xx) tiles[yy*w+xx]=0;
    }
    GroundAccessPolicy policy() const { GroundAccessPolicy p; p.reset(w,h,tiles); return p; }
};
}
TEST_CASE("Factories need an outside route, not an empty courtyard", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,10,1);m.block(0,0,1,9);m.block(9,0,1,9);m.block(0,8,10,1);
    auto p=m.policy();
    REQUIRE_FALSE(p.allows({3,3,3,2},true));
    REQUIRE(p.allows({13,0,3,2},true)); // map-edge rear factory, with an exit
    REQUIRE(p.allows({13,12,3,2},true));
}
TEST_CASE("Later buildings cannot plug a factory escape lane", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,1,9);m.block(9,0,1,9);m.block(0,8,10,1);
    m.tiles[8*m.w+5]=1; // only exit from this map-edge pocket
    m.block(3,2,3,2);
    auto p=m.policy();p.protectExits({3,2,3,2});
    REQUIRE_FALSE(p.allows({5,8,1,1},false)); // even a turret seals it
    REQUIRE(p.allows({15,12,3,2},false));
}
TEST_CASE("Produced units retain a route even away from a factory", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,1,9);m.block(9,0,1,9);m.block(0,8,10,1);
    m.tiles[8*m.w+5]=1;
    auto p=m.policy();p.protectUnit(4,4);
    REQUIRE_FALSE(p.allows({5,8,1,1},false));
}
TEST_CASE("Reserved footprints and mountains count as barriers", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,1,9);m.block(9,0,1,9);m.block(0,8,10,1);
    m.tiles[8*m.w+5]=1;
    auto open=m.policy();
    REQUIRE(open.allows({2,1,2,2},true));
    m.block(5,8,1,1); // same occupancy input for another yard's reservation
    auto reserved=m.policy();
    REQUIRE_FALSE(reserved.allows({2,1,2,2},true));
}
TEST_CASE("A factory needs one connected deployment side", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,1,10);m.block(4,0,1,10);m.block(0,9,5,1);
    m.tiles[9*m.w+2]=1;
    auto p=m.policy();
    REQUIRE(p.allows({1,5,3,2},true)); // south side still reaches outside
    p.protectUnit(2,2);
    REQUIRE_FALSE(p.allows({1,5,3,2},true)); // but cannot seal an existing unit behind it
}
TEST_CASE("Existing isolated units do not freeze construction elsewhere", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,6,1);m.block(0,0,1,6);m.block(5,0,1,6);m.block(0,5,6,1);
    auto p=m.policy();p.protectUnit(2,2);p.protectUnit(-1,-1);
    REQUIRE(p.allows({12,12,3,2},true));
    REQUIRE_FALSE(p.allows({23,19,3,2},true));
}
TEST_CASE("Access planning is repeatable and handles a completely blocked map", "[ai][placement]") {
    AccessMap m;
    auto a=m.policy(),b=m.policy();
    a.protectExits({3,3,3,2});b.protectExits({3,3,3,2});
    for(int y=0;y<18;++y) for(int x=0;x<22;++x)
        REQUIRE(a.allows({x,y,3,2},true)==b.allows({x,y,3,2},true));
    m.block(0,0,m.w,m.h);
    auto p=m.policy();REQUIRE_FALSE(p.allows({3,3,3,2},true));
}
TEST_CASE("Even a spacious courtyard must retain its outside connection", "[ai][placement]") {
    AccessMap m;
    m.block(0,0,1,10);m.block(10,0,1,10);m.block(0,9,11,1);
    m.tiles[9*m.w+5]=1;
    auto p=m.policy();p.protectUnit(4,4);
    REQUIRE_FALSE(p.allows({5,9,1,1},false));
}
TEST_CASE("One factory exit can be covered when another remains", "[ai][placement]") {
    AccessMap m;m.block(3,3,3,2);
    auto p=m.policy();p.protectExits({3,3,3,2});
    REQUIRE(p.allows({6,5,1,1},false));
}

TEST_CASE("Open ground paths may detour around a new building", "[ai][placement]") {
    AccessMap m;m.block(3,3,3,2);
    auto p=m.policy();p.protectExits({3,3,3,2});p.protectUnit(4,7);
    REQUIRE(p.allows({3,9,5,2},false));
    REQUIRE_FALSE(p.allows({4,7,1,1},false));
}
TEST_CASE("Long alternate paths preserve factory connectivity", "[ai][placement]") {
    AccessMap m;
    m.block(0,9,24,1);m.tiles[9*m.w+4]=1;m.tiles[9*m.w+19]=1;
    m.block(3,2,3,2);
    auto p=m.policy();p.protectExits({3,2,3,2});p.protectUnit(4,5);
    REQUIRE(p.allows({4,9,1,1},false)); // far opening remains
    m.block(19,9,1,1); // e.g. another yard's reservation
    p=m.policy();p.protectExits({3,2,3,2});p.protectUnit(4,5);
    REQUIRE_FALSE(p.allows({4,9,1,1},false));
}
TEST_CASE("Unrelated construction in isolated space leaves existing access alone", "[ai][placement]") {
    AccessMap m;m.block(0,0,6,1);m.block(0,0,1,6);m.block(5,0,1,6);m.block(0,5,6,1);
    auto p=m.policy();p.protectUnit(15,15);
    REQUIRE(p.allows({2,2,1,1},false));
    REQUIRE_FALSE(p.allows({2,2,1,1},true));
}
