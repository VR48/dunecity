#include <catch2/catch_test_macros.hpp>
#include <dunecity/CityTrafficPolicy.h>
#include <string>

using namespace DuneCity;

TEST_CASE("Traffic follows one route instead of flooding searched side streets", "[city][traffic]") {
    CityTraffic::RouteFinder finder;
    // BFS explores both the north branch and lower loop before the distant goal.
    const std::vector<std::string> grid={"..#......","..#......","S#######G","..#...#..","..#####.."};
    const auto road=[&](int x,int y){return grid[y][x]!='.';};
    const auto goal=[&](int x,int y){return grid[y][x]=='G';};
    REQUIRE(finder.find(9,5,{0,2},8,road,goal));
    REQUIRE(finder.route().size()==9);
    for(int x=0;x<9;++x) REQUIRE(finder.route()[x]==CityTraffic::Point{x,2});
    CityMapLayer<uint8_t> density; density.init(9,5,2);
    CityTraffic::addJourney(density,finder.route(),road);
    REQUIRE(density.get(1,0)==0); // north spur was searched, never driven
    REQUIRE(density.get(1,2)==0); // lower loop was searched, never driven
    REQUIRE(density.get(0,1)==0); // no stamp on start/first move
    for(int x=1;x<5;++x) REQUIRE(density.get(x,1)==50); // once, not per visited tile
    REQUIRE_FALSE(finder.find(9,5,{0,2},7,road,goal));
    REQUIRE(finder.route().empty()); // failed route cannot reuse previous success
}

TEST_CASE("Traffic route search stays deterministic and handles missing roads and map resize", "[city][traffic]") {
    CityTraffic::RouteFinder finder;
    const auto road=[](int,int){return true;};
    const auto goal=[](int x,int y){return x==2&&y==2;};
    REQUIRE(finder.find(4,4,{1,1},2,road,goal));
    const auto first=finder.route();
    REQUIRE(first==std::vector<CityTraffic::Point>{{1,1},{2,1},{2,2}});
    for(int n=0;n<20;++n) {
        REQUIRE(finder.find(4,4,{1,1},2,road,goal));
        REQUIRE(finder.route()==first);
    }
    // Same cell count but different dimensions must not reuse visited state.
    REQUIRE(finder.find(8,2,{0,0},7,road,[](int x,int y){return x==7&&y==0;}));
    REQUIRE(finder.route().size()==8);
    REQUIRE_FALSE(finder.find(4,4,{1,1},2,[](int,int){return false;},goal));
    REQUIRE(finder.route().empty());
    REQUIRE_FALSE(finder.find(4,4,{-1,0},2,road,goal));
    REQUIRE(finder.find(1,1,{0,0},0,road,[](int,int){return true;}));
    REQUIRE(finder.route().size()==1);
    finder.clear();
    REQUIRE(finder.route().empty());
}

TEST_CASE("Micropolis trip sampling counts moves two four six and caps traffic at 240", "[city][traffic]") {
    CityMapLayer<uint8_t> density; density.init(8,2,2);
    const std::vector<CityTraffic::Point> route={{0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0}};
    const auto road=[](int,int){return true;};
    CityTraffic::addJourney(density,route,road);
    REQUIRE(density.get(0,0)==0);
    REQUIRE(density.get(1,0)==50);
    REQUIRE(density.get(2,0)==50);
    REQUIRE(density.get(3,0)==50); // odd final move is not an extra sample
    for(int n=0;n<10;++n) CityTraffic::addJourney(density,route,road);
    REQUIRE(density.get(3,0)==240);
    CityTraffic::decay(density,8,2);
    REQUIRE(density.get(3,0)==206);
    CityTraffic::addJourney(density,route,road);
    REQUIRE(density.get(3,0)==240); // real repeated journeys can still congest

    density.init(8,2,2);
    CityTraffic::addJourney(density,route,[](int x,int){return x!=4;});
    REQUIRE(density.get(2,0)==0); // non-road connector doesn't itself receive cars
    REQUIRE(density.get(1,0)==50);
    REQUIRE(density.get(3,0)==50);
}

TEST_CASE("Micropolis traffic decays per cell and quiet roads eventually empty", "[city][traffic]") {
    REQUIRE(CityTraffic::decayed(0)==0);
    REQUIRE(CityTraffic::decayed(24)==0);
    REQUIRE(CityTraffic::decayed(25)==1);
    REQUIRE(CityTraffic::decayed(200)==176);
    REQUIRE(CityTraffic::decayed(201)==167);
    REQUIRE(CityTraffic::decayed(240)==206);
    REQUIRE(CityTraffic::decayed(255)==221); // tolerate old inflated values
    CityMapLayer<uint8_t> density; density.init(5,3,2);
    density.set(0,0,100); density.set(2,1,240);
    CityTraffic::decay(density,5,3);
    REQUIRE(density.get(0,0)==76); // once per cell, irrespective of roads inside
    REQUIRE(density.get(2,1)==206); // partial cells at map edge included
    for(int n=0;n<10;++n) CityTraffic::decay(density,5,3);
    for(int y=0;y<2;++y) for(int x=0;x<3;++x) REQUIRE(density.get(x,y)==0);
}

TEST_CASE("Journey frequency follows Micropolis occupancy probabilities", "[city][traffic]") {
    constexpr int days=36000;
    for (bool residential : {false,true}) for (int pop : {0,1,4,8,16,24,32,40}) {
        int trips=0;
        for (int day=0;day<days;++day) trips+=CityTraffic::journeyDue(residential,pop,12,18,day);
        const int expected=days*std::min(pop,residential?36:6)/(residential?36:6);
        INFO("residential="<<residential<<" population="<<pop);
        REQUIRE(std::abs(trips-expected)<days/100);
    }
    int different=0;
    for (int day=0;day<1000;++day)
        different+=CityTraffic::journeyDue(false,1,12,18,day)!=CityTraffic::journeyDue(false,1,15,18,day);
    REQUIRE(different>200); // nearby grid-aligned sites do not emit in lockstep
}

TEST_CASE("Sparse occupied roads show light traffic while busy shared routes can congest", "[city][traffic]") {
    CityMapLayer<uint8_t> traffic; traffic.init(8,2,2);
    const std::vector<CityTraffic::Point> route={{0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0}};
    const auto road=[](int,int){return true;};
    int none=0,light=0,heavy=0;
    for (int day=0;day<10000;++day) {
        CityTraffic::decay(traffic,8,2);
        // Two early industrial employers and a two-house residential lot.
        for (int site=0;site<3;++site)
            if (CityTraffic::journeyDue(site==2,site==2?2:1,site*3,6,day))
                CityTraffic::addJourney(traffic,route,road);
        const int value=traffic.get(1,0);
        none+=value<64; light+=value>=64&&value<192; heavy+=value>=192;
    }
    REQUIRE(none>5000);
    REQUIRE(light>1000);
    REQUIRE(heavy<1000);
    for (int day=0;day<20;++day) {
        CityTraffic::decay(traffic,8,2);
        for (int site=0;site<8;++site)
            if (CityTraffic::journeyDue(true,40,site*3,6,day))
                CityTraffic::addJourney(traffic,route,road);
    }
    REQUIRE(traffic.get(1,0)==240);
    for (int day=0;day<10;++day) CityTraffic::decay(traffic,8,2);
    REQUIRE(traffic.get(1,0)==0);
}
