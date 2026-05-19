#ifndef DUNECITY_CITYEFFECTS_H
#define DUNECITY_CITYEFFECTS_H

#include <data.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DuneCity {

// =============================================================================
// City effects spec — pure functions, fully unit-testable.
//
// Every "city role" structure (a zone or an existing Dune building that maps
// to a SimCity-Classic role) carries an integer `level` 0..maxLevel. For
// zones that's tile->getCityZoneDensity(). For non-zone city-role structures
// (Refinery, Silo, Radar, Factories, Repair Yard, House IX, HighTech) it's
// pStruct->getCityOccupancy().
//
// Zones grow up from level 0 (vacant). Non-zone city-role structures are
// player-built, so the city sim treats their effective level as max(1,
// cityOccupancy) — they immediately provide tier-1 jobs the moment they
// finish construction, then grow to higher tiers via the usual demand
// gates. The level-1 floor lives in CityEffectsRuntime.cpp and avoids the
// chicken-and-egg where a Refinery can't level up without residents and
// residents can't move in without jobs.
//
// All supply / pollution / population numbers below are "at level L". A
// structure at level 0 contributes nothing; level 3 contributes the full
// amount listed for its role tier. This keeps the model uniform across
// hand-placed Dune buildings and player-zoned R/C/I.
//
// Mapping reference (as agreed in spec discussion):
//   Slab/Slab4         -> Road
//   WindTrap           -> Coal Power (no pollution)
//   Nuclear Plant      -> Nuclear Power (no pollution, no meltdown)
//   Refinery           -> Industrial low (small factory, density 1 only)
//   Light Factory      -> Industrial medium
//   Heavy Factory      -> Industrial high
//   Repair Yard        -> Industrial high
//   Spice Silo         -> Commercial low
//   Radar              -> Commercial medium
//   HighTech Factory   -> Commercial high
//   House IX           -> Commercial high
//   Starport           -> Shipyard / Industrial high (jobs, economic role)
//   Palace             -> Super Residential (3x pop, slow growth)
//   Stadium            -> Civic amenity (large land-value boost)
//   Airport            -> Commercial high (transport/trade boost)
//   Barracks           -> Police Station
//   WOR                -> Police Station (HQ)
//   Gun/Rocket Turret  -> Park bonus + 1/4 Police
//   Wall               -> Park bonus only
//   Sand (terrain)     -> Water (land-value bonus)
// =============================================================================

// --- Radii (in tiles, distances measured Chebyshev / square) -----------------

constexpr int kPollutionRadius   = 5;  // emission falls off linearly to this
// SimCity Classic stored police in an 8-tile-block map and ran three smooth
// passes, so a single station's influence spread ~3 blocks = ~24 tiles —
// roughly 8 zone-widths in SC's 3x3-footprint zones. Our zones are 2x2 so a
// "zone-width" is two tiles here; 8 zone-widths = 16 tiles. Linear falloff
// over that range preserves SC's "one station noticeably affects the whole
// neighbourhood" feel without over-reaching for our smaller gameplay grid.
constexpr int kPoliceRadius      = 16;
constexpr int kParkBonusRadius   = 3;  // Park / Wall / Turret land-value reach
// Sand-as-water reach. Each open sand tile stamps a falloff into the land-
// value map, bypassing the /8 dilution of the SC-style smoothing pass.
// Tuned so a zone with one full sand-block neighbour lands solidly inside
// tier 2 (value ≥ 80) and a zone with sand on two-plus sides reaches tier 3
// (value ≥ 150) when reasonably close to the city centre.
constexpr int kSandBonusRadius   = 5;
// Supply visibility radius. SimCity Classic does NOT use a Chebyshev
// supply check — it does a road-network drive of up to
// MAX_TRAFFIC_DISTANCE = 30 tiles (see SC traffic.cpp). We use a much
// simpler straight-line lookup, but bump the radius to 16 so the reach
// is in the same ballpark as a typical SC drive (allowing for some
// road-detour overhead). Smaller values caused commercial zones to
// permanently stall at level 1 when industrial sat in a separate
// cluster — commercial growth gates on local industrial supply.
constexpr int kSupplyRadius      = 16;

// --- Effect strengths --------------------------------------------------------

constexpr int kParkLandValueBonus       = 15;  // SC Classic Park land-value lift
constexpr int kStadiumLandValueBonus   = 40;  // Stadium provides a much larger boost
constexpr int kSandLandValueBonus       = 8;   // Per-tile direct stampFalloff
/// Per-tile contribution to the block-smoothed terrain feature map. Box-
/// smoothed twice and added to the SC-style land-value base, this is the
/// "close-adjacency" lift sand provides — distinct from the long-reach
/// stampFalloff above (kSandLandValueBonus). Larger than SC's +15 because
/// Dune-sized maps don't tolerate as much smoothing dilution.
constexpr int kSandTerrainRawBonus      = 40;
constexpr int kPoliceCoverageFull       = 100; // Barracks / WOR
constexpr int kPoliceCoverageGunTurret  =  25; // Gun Turret (1/4 strength)
constexpr int kPoliceCoverageRocketTurret = 10; // Rocket Turret (less than guns)

constexpr int kMaxLandValue = 250;
constexpr int kMaxCrime     = 250;
constexpr int kMaxPollution = 250;

/// SimCity Classic feedback: when crime exceeds this threshold on a block,
/// the next land-value scan subtracts kCrimeLandValuePenalty from that
/// block's base. Source: MicropolisEngine scan.cpp `pollutionTerrainLand
/// ValueScan`, line 279 — `if (crimeRateMap.get(x, y) > 190) dis -= 20;`.
constexpr int kCrimeLandValueThreshold = 190;
constexpr int kCrimeLandValuePenalty   = 20;

/// SimCity Classic crime-base midpoint. `crime = 128 - landValue + pop`.
/// 128 means lv ≈ 128 yields zero base crime — typical mid-range blocks
/// sit near neutral. Source: scan.cpp `crimeScan`, line 427.
constexpr int kCrimeBaseMidpoint = 128;
/// Intermediate clamp before subtracting police coverage (SC line 430).
constexpr int kCrimePrePoliceClamp = 300;

// --- City roles --------------------------------------------------------------

enum class CityRole {
    None,
    Residential,
    Commercial,
    Industrial,
};

/// What SC-Classic role this Dune structure plays in the city sim, if any.
/// Palace → super residential (high-capacity, caps at 1000 people, slow growth).
/// StarPort → shipyard / industrial high (jobs + economic role).
/// Airport → commercial high (transport/trade boost).
inline CityRole getStructureCityRole(int itemID) {
    switch (itemID) {
        case Structure_ZoneResidential:
        case Structure_Palace:
            return CityRole::Residential;
        case Structure_ZoneCommercial:
        case Structure_Silo:
        case Structure_Radar:
        case Structure_HighTechFactory:
        case Structure_IX:
        case Structure_Airport:
            return CityRole::Commercial;
        case Structure_ZoneIndustrial:
        case Structure_LightFactory:
        case Structure_HeavyFactory:
        case Structure_RepairYard:
        case Structure_Refinery:
        case Structure_StarPort:
            return CityRole::Industrial;
        default:
            return CityRole::None;
    }
}

/// Maximum "level" this city-role structure can reach. Zones go 1..3.
/// Non-zone city-role structures cap at the tier they map to (a Silo is
/// inherently low-density commercial, so it tops out at level 1).
inline int getStructureMaxLevel(int itemID) {
    switch (itemID) {
        case Structure_ZoneResidential:
        case Structure_ZoneCommercial:
        case Structure_ZoneIndustrial:
        case Structure_Palace:          // super residential — caps at 1000 people, grows slowly
            return 3;
        case Structure_Silo:
        case Structure_Refinery:
            return 1;  // low (Silo = C-low, Refinery = I-low / small factory)
        case Structure_Radar:
        case Structure_LightFactory:
            return 2;  // medium
        case Structure_HighTechFactory:
        case Structure_IX:
        case Structure_HeavyFactory:
        case Structure_RepairYard:
        case Structure_StarPort:        // shipyard — industrial high
        case Structure_Airport:         // transport hub — commercial high
            return 3;  // high
        default:
            return 0;
    }
}

// --- Pollution emission ------------------------------------------------------

/// Per-source pollution emission (0-100 scale), scaled by current level.
/// Industrial-role structures pollute proportionally to their level; all
/// other roles emit zero.
inline int getPollutionEmission(int itemID, int level) {
    if (level <= 0) return 0;
    if (level > 3) level = 3;
    if (getStructureCityRole(itemID) != CityRole::Industrial) return 0;

    // Starport is Industrial for jobs/demand but does not pollute (it's a
    // trade hub, not a factory). Per spec override.
    if (itemID == Structure_StarPort) return 0;

    // Industrial pollution scales with level: 10 / 25 / 50.
    static constexpr int kIndustrialPollution[3] = { 10, 25, 50 };
    return kIndustrialPollution[level - 1];
}

// --- Supply (jobs/residents available within reach) --------------------------

namespace detail {
inline int supplyForLevel(int level) {
    if (level <= 0) return 0;
    if (level > 3) level = 3;
    static constexpr int kTable[3] = { 10, 25, 50 };
    return kTable[level - 1];
}
}

inline int getCommercialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Commercial)
        ? detail::supplyForLevel(level) : 0;
}

inline int getIndustrialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Industrial)
        ? detail::supplyForLevel(level) : 0;
}

inline int getResidentialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Residential)
        ? detail::supplyForLevel(level) : 0;
}

// --- Police coverage (crime reduction in radius) -----------------------------
//
// Police Station (DuneCity city-mode building) is the sole source of full
// coverage. Barracks and WOR are pure infantry-production buildings and
// no longer reduce crime — putting a barracks down should not magically
// turn your slums into a tier-3 luxury neighborhood. Gun and Rocket
// turrets retain their fractional coverage as a "garrison adjacency"
// effect, mirroring SC Classic's reduced-coverage outposts.

inline int getPoliceCoverage(int itemID) {
    switch (itemID) {
        case Structure_PoliceStation:   return kPoliceCoverageFull;
        case Structure_GunTurret:       return kPoliceCoverageGunTurret;
        case Structure_RocketTurret:    return kPoliceCoverageRocketTurret;
        default:                        return 0;
    }
}

// --- Police annual cost (city budget) ----------------------------------------

/// Annual upkeep in city credits for each police-role structure. Mirrors
/// the coverage strength so a turret that protects 1/4 of an HQ's area
/// also costs 1/4 to operate. Cost is paid out of city totalFunds_ once
/// per city year; if the city has under-funded police, coverage scales
/// down proportionally (handled at scan time, not here).
///
/// PoliceStation upkeep equals its build price (500), matching SimCity
/// Classic's TOOL_POLICESTATION (tool.cpp gCostOf[4] = 500).
constexpr int kPoliceCostPoliceStation = 500;
// Gun and Rocket turrets contribute fractional police coverage as a
// "garrison adjacency" effect, but no longer cost anything to operate.
// They're defensive structures first — the police effect is a bonus,
// and the player shouldn't be charged a city-budget bill for their own
// base defenses.
constexpr int kPoliceCostGunTurret     =   0;
constexpr int kPoliceCostRocketTurret  =   0;

inline int getPoliceAnnualCost(int itemID) {
    switch (itemID) {
        case Structure_PoliceStation:   return kPoliceCostPoliceStation;
        case Structure_GunTurret:       return kPoliceCostGunTurret;
        case Structure_RocketTurret:    return kPoliceCostRocketTurret;
        default:                        return 0;
    }
}

// --- Park-style land-value bonus (Wall / Turrets) ----------------------------

inline int getParkLandValueBonus(int itemID) {
    switch (itemID) {
        case Structure_Wall:            return kParkLandValueBonus;
        case Structure_GunTurret:       return kParkLandValueBonus;
        case Structure_RocketTurret:    return kParkLandValueBonus;
        case Structure_Stadium:         return kStadiumLandValueBonus;
        case Structure_Palace:          return kParkLandValueBonus;
        default:                        return 0;
    }
}

// --- Linear distance falloff helper ------------------------------------------

/// Returns a coverage strength scaled by (radius - distance) / radius.
/// Returns 0 for distance >= radius. Distances are in tiles.
inline int falloff(int strength, int distance, int radius) {
    if (distance >= radius || radius <= 0) return 0;
    if (distance < 0) distance = 0;
    return (strength * (radius - distance)) / radius;
}

// --- SC-Classic base land-value formula --------------------------------------
//
// Pure helper extracted from runEffectsScans so it can be unit-tested.
// Mirrors SimCity Classic's `pollutionTerrainLandValueScan` in
// MicropolisEngine/src/scan.cpp (lines 274-282 in the MicropolisCore tree):
//
//     dis = 34 - getCityCenterDistance(worldX, worldY) / 2;
//     dis = dis << 2;                              // dis *= 4
//     dis += terrainDensityMap.get(x>>1, y>>1);    // smoothed water/tree
//     dis -= pollutionDensityMap.get(x, y);
//     if (crimeRateMap.get(x, y) > 190) dis -= 20; // crime feedback
//     dis = clamp(dis, 1, 250);
//
// Distance is Manhattan (xDis + yDis) from the city centroid, clamped to
// 64 (SC line 408). Block-coordinate distances would compress the falloff
// and land everyone in tier 0.
constexpr int kLandValueDistanceClamp = 64;  ///< SC scan.cpp line 408

inline int computeBaseLandValue(int distanceFromCenter,
                                int terrainBonus,
                                int pollution,
                                int crime = 0) {
    int dist = distanceFromCenter;
    if (dist < 0)                          dist = 0;
    if (dist > kLandValueDistanceClamp)    dist = kLandValueDistanceClamp;
    int v = (34 - dist / 2) * 4;
    v += terrainBonus;
    v -= pollution;
    if (crime > kCrimeLandValueThreshold) {
        v -= kCrimeLandValuePenalty;
    }
    if (v < 1)             v = 1;
    if (v > kMaxLandValue) v = kMaxLandValue;
    return v;
}

// --- Crime formula -----------------------------------------------------------
//
// Mirrors SimCity Classic's `crimeScan` (scan.cpp lines 425-432):
//
//     z = landValueMap.worldGet(x, y);
//     if (z > 0) {
//         z = 128 - z;                       // invert around midpoint
//         z += populationDensityMap.worldGet(x, y);   // pop fuels crime
//         z = min(z, 300);
//         z -= policeStationMap.worldGet(x, y);
//         z = clamp(z, 0, 250);
//     }
//
// `computeBaseCrime` is the pre-police half (the police-coverage stamp
// happens in the runtime via stampFalloff). `populationDensity` defaults
// to 0 so legacy callers continue to work; the runtime passes the
// per-block population density when SC fidelity is desired.

inline int computeBaseCrime(int landValue, int populationDensity = 0) {
    if (landValue <= 0) {
        // SC's `if (z > 0)` guard: undeveloped blocks have zero crime.
        return 0;
    }
    int crime = kCrimeBaseMidpoint - landValue + populationDensity;
    if (crime > kCrimePrePoliceClamp) crime = kCrimePrePoliceClamp;
    if (crime < 0)                    crime = 0;
    if (crime > kMaxCrime)            crime = kMaxCrime;
    return crime;
}

// --- Sprite-atlas indexing ---------------------------------------------------

/// Compute the cell index inside a zone's sprite atlas given the tile's
/// current density (column) and the sampled land-value tier (row).
/// Atlas layout is (numDensity columns × numTiers rows), so the linear
/// index used by `blitToScreen` is `density + tier * numDensity`.
/// Returned values are clamped to the atlas bounds so callers can't
/// over-read in case density transiently exceeds numDensity (which can
/// happen during the transition from a higher-tier sprite atlas to a
/// lower-tier one in mid-growth).
inline int computeZoneSpriteFrame(int density, int valueTier,
                                  int numDensity, int numTiers) {
    if (density   < 0)            density   = 0;
    if (density   >= numDensity)  density   = numDensity - 1;
    if (valueTier < 0)            valueTier = 0;
    if (valueTier >= numTiers)    valueTier = numTiers - 1;
    return density + valueTier * numDensity;
}

// --- Pollution growth suppression --------------------------------------------
//
// High pollution suppresses residential and commercial growth. Industrial
// zones tolerate pollution (they produce it). This is the threshold above
// which the growth stochastic roll is halved.

constexpr int kPollutionGrowthThreshold = 80;  // pollution > this slows R/C growth
constexpr int kPollutionGrowthBlock     = 160; // pollution > this blocks R/C growth entirely

/// Returns true if pollution at this block is too high for the given role
/// to grow to the requested level.
inline bool isPollutionBlockingGrowth(int pollution, CityRole role, int /*targetLevel*/) {
    if (role == CityRole::Industrial) return false;  // industry tolerates pollution
    if (role == CityRole::None)       return false;
    return pollution >= kPollutionGrowthBlock;
}

/// Returns true if pollution should halve the growth probability for R/C.
inline bool isPollutionSlowingGrowth(int pollution, CityRole role) {
    if (role == CityRole::Industrial) return false;
    if (role == CityRole::None)       return false;
    return pollution >= kPollutionGrowthThreshold;
}

// --- Demand model for zone growth --------------------------------------------

/// Minimum residential supply (population) required for an Industrial or
/// Commercial zone to grow to the requested level. Indexed 1..3. Tuned so
/// L1→L2 needs one adjacent supplier and L2→L3 needs a small cluster,
/// matching the small footprints typical of Dune-sized maps.
inline int getDemandResidentialThreshold(int level) {
    if (level <= 1) return 0;
    if (level == 2) return 10;
    return 30;  // level 3
}

/// Minimum (industrial + commercial) supply combined required for a
/// Residential zone to grow to the requested level.
inline int getDemandJobsThreshold(int level) {
    if (level <= 1) return 0;
    if (level == 2) return 10;
    return 30;
}

/// Land-value floor (out of kMaxLandValue) below which a zone refuses to
/// grow to the requested level. We keep zeros across the board so density
/// is allowed to climb to L3 even on rock with no parks — value tier
/// already differentiates rich vs poor neighbourhoods visually, and
/// gatekeeping density behind park-spam felt punishing on Dune-sized maps.
inline int getDemandLandValueFloor(int /*level*/) {
    return 0;
}

/// Map a land-value sample (0..kMaxLandValue) to a Micropolis-style value
/// tier. Used by ZoneStructure to pick which "row" of its sprite atlas to
/// draw (rich neighbourhoods show fancier buildings even at the same
/// density). `numTiers` clamps the result to what the zone type ships —
/// Industrial only has two value tiers, R/C have four.
///
/// Thresholds match SimCity Classic's getLandPollutionValue() (zone.cpp):
///   < 30  -> tier 0 (poor)
///   < 80  -> tier 1
///   < 150 -> tier 2
///   else  -> tier 3 (rich)
/// SC computes these from (landValue - pollution); our scan already bakes
/// pollution into landValue, so we pass the stored value straight through.
inline int getZoneValueTier(int landValue, int numTiers) {
    if (numTiers <= 1) return 0;
    int tier;
    if      (landValue <  30) tier = 0;
    else if (landValue <  80) tier = 1;
    else if (landValue < 150) tier = 2;
    else                      tier = 3;
    if (tier >= numTiers) tier = numTiers - 1;
    return tier;
}

// --- Population accounting ---------------------------------------------------

/// People (R) or jobs (C/I) contributed by one city-role structure at the
/// given level. Used by CitySimulation to recompute resPop_/comPop_/indPop_
/// each scan. Returns 0 for non-role itemIDs and level==0.
inline int getZonePopulation(int itemID, int level) {
    if (level <= 0) return 0;
    if (level > 3) level = 3;

    // Palace is a super residential — high-capacity civic residence that
    // grows slowly via the same occupancy path. Caps at 1000 people.
    if (itemID == Structure_Palace) {
        static constexpr int kPalace[3] = { 250, 500, 1000 };
        return kPalace[level - 1];
    }

    // Residential is people; Commercial/Industrial are jobs. R skews higher
    // so a city feels inhabited before it feels saturated with jobs.
    // Numbers are tuned so 20-30 zones on a typical Dune map produce
    // SC-Classic-style totals (tens of thousands of residents); Dune maps
    // hold ~10x fewer zones than SC's 120x100 grid so per-zone has to do
    // proportionally more work.
    static constexpr int kResidential[3] = { 100, 300, 800 };
    static constexpr int kCommercial [3] = {  60, 200, 500 };
    static constexpr int kIndustrial [3] = {  60, 200, 500 };

    switch (getStructureCityRole(itemID)) {
        case CityRole::Residential: return kResidential[level - 1];
        case CityRole::Commercial:  return kCommercial [level - 1];
        case CityRole::Industrial:  return kIndustrial [level - 1];
        default:                    return 0;
    }
}

// --- Traffic connectivity result ---------------------------------------------

enum class TrafficResult {
    NoRoad,          // no perimeter road found around the structure
    NoDestination,   // road found but cannot reach the needed complementary zone
    Connected,       // reachable destination found via road network
};

/// Maximum BFS distance (in road tiles) for a traffic attempt.
/// Micropolis uses MAX_TRAFFIC_DISTANCE = 30; we use 20 for smaller maps.
constexpr int kMaxTrafficDistance = 20;

// --- Zone score for growth/decline decisions ---------------------------------
//
// SimCity Classic computes zscore = globalValve + localEval per zone.
// Growth requires zscore > -350; decline is likely when zscore < 350.
// When unpowered, zscore = -500 (hard-coded in SC). DuneCity ports these
// semantics so that negative RCI demand valves actively suppress growth
// and trigger residential shrink.

/// Zone-score threshold above which growth attempts are allowed.
/// SimCity Classic uses -350 (doResidential guard in zone.cpp).
constexpr int kZscoreGrowthGate  = -350;

/// Zone-score threshold below which decline can trigger.
/// SimCity Classic: decline roll starts when zscore < 350.
constexpr int kZscoreDeclineGate = 350;

/// Local zone evaluation inspired by SimCity's evalRes/evalCom/evalInd.
/// Now consumes traffic connectivity result alongside land value, pollution,
/// and crime. Each zone type weights these inputs differently:
///
/// Residential: high sensitivity to pollution and crime, needs traffic to
///   commercial, land value helps. evalRes = clamp((lv - poll)*32, 0, 6000)
///   - 3000; traffic failure = -3000. Scaled down by /4 for DuneCity's
///   +-2000 valve range.
///
/// Commercial: land value and traffic to industrial matter, crime hurts,
///   pollution hurts moderately. Uses comRate-style calculation.
///
/// Industrial: traffic to residential dominates, pollution is output not
///   blocker, local conditions mostly irrelevant.
inline int computeLocalEval(CityRole role, int landValue, int pollution,
                            int crime, TrafficResult traffic) {
    switch (role) {
        case CityRole::Residential: {
            // SC evalRes: clamp((lv - poll) * 32, 0, 6000) - 3000
            // Scale /4 to fit DuneCity's valve range: max +750, min -750
            int raw = (landValue - pollution) * 32;
            if (raw < 0)    raw = 0;
            if (raw > 6000) raw = 6000;
            int v = (raw - 3000) / 4;
            // Crime penalty: high crime areas are undesirable for residents
            if (crime > 150) v -= 200;
            else if (crime > 100) v -= 100;
            // Traffic: no road = moderate penalty, no destination = large
            if (traffic == TrafficResult::NoRoad) v -= 300;
            else if (traffic == TrafficResult::NoDestination) v -= 600;
            return v;
        }
        case CityRole::Commercial: {
            // Commercial values: land value, connectivity, low crime
            int v = landValue * 2;
            if (crime > 120) v -= 150;
            else if (crime > 80) v -= 75;
            // Pollution hurts moderately (less than residential)
            if (pollution > 100) v -= 100;
            else if (pollution > 50) v -= 50;
            // Traffic to industrial is important
            if (traffic == TrafficResult::NoRoad) v -= 200;
            else if (traffic == TrafficResult::NoDestination) v -= 400;
            return v;
        }
        case CityRole::Industrial: {
            // Industrial: traffic to residential dominates
            // Pollution is output, not a self-blocker
            int v = 0;
            if (traffic == TrafficResult::NoRoad) v -= 200;
            else if (traffic == TrafficResult::NoDestination) v -= 300;
            return v;
        }
        default:
            return 0;
    }
}

/// Commercial-rate adjustment: adjusts the base commercial local eval
/// with supply-side and infrastructure factors not available to the pure
/// local-eval function. Matches the spec's "computeCommercialRate(node)"
/// helper (Section D).
///
/// nearbyRes: residential supply within kSupplyRadius
/// nearbyInd: industrial supply within kSupplyRadius
/// hasAirport: whether an Airport exists in the city
///
/// Returns an additive adjustment to the commercial zscore.
inline int computeCommercialRate(int nearbyRes, int nearbyInd, bool hasAirport) {
    int adj = 0;
    // Commercial zones thrive near residential population (customers)
    // and industrial output (goods to sell). Each 10 supply adds +10.
    adj += std::min(200, nearbyRes);
    adj += std::min(100, nearbyInd / 2);
    // Airport unlocks trade capacity — flat bonus when present.
    if (hasAirport) adj += 100;
    return adj;
}

/// Backward-compatible overload for tests that don't supply traffic/crime.
inline int computeLocalEval(CityRole role, int landValue, int pollution,
                            bool hasRoad) {
    TrafficResult tr = hasRoad ? TrafficResult::Connected : TrafficResult::NoRoad;
    return computeLocalEval(role, landValue, pollution, 0, tr);
}

/// Compute the zone score for growth/decline decisions.
/// zscore = globalValve + localEval; if unpowered, returns -500.
inline int computeZscore(int globalValve, int localEval, bool powered) {
    if (!powered) return -500;
    return globalValve + localEval;
}

/// Determine if a zone should decline based on its zscore and current level.
/// The roll parameter should be a deterministic hash in [0, 16).
///
/// Probability tiers (per tick):
///   zscore <= -1000: 25% (roll < 4)
///   zscore <= -500:  12.5% (roll < 2)
///   zscore < 0:      6.25% (roll == 0)
///   zscore in [0, kZscoreDeclineGate): no decline (buffer zone)
///
/// L1->L0 (back to vacant) only fires when zscore <= -1500, avoiding
/// flicker on freshly placed zones during the bootstrap grace period.
inline bool shouldZoneDecline(int zscore, int level, uint32_t roll) {
    if (level <= 0) return false;
    if (zscore >= kZscoreDeclineGate) return false;

    bool decline = false;
    if      (zscore <= -1000) decline = (roll < 4);   // 25%
    else if (zscore <= -500)  decline = (roll < 2);   // 12.5%
    else if (zscore < 0)      decline = (roll == 0);  // 6.25%

    // Protect L1 from declining to vacant unless conditions are extreme.
    if (decline && level == 1 && zscore > -1500) decline = false;

    return decline;
}

// --- Micropolis-shaped demand valve computation ------------------------------
//
// SimCity Classic's setValves() derives demand from employment, migration,
// births, labour base, internal/external market, and tax. DuneCity adapts
// this for its smaller maps and slower cadence.

/// Micropolis tax table (21 entries, indexed by min(cityTax + gameLevel, 20)).
/// Values range from 200 (low tax) to -600 (high tax). Source: simulate.cpp.
inline int getTaxTableEntry(int taxRate) {
    static constexpr int kTaxTable[21] = {
        200, 150, 120, 100, 80, 50, 30, 0, -10, -40,
        -100, -150, -200, -250, -300, -350, -400, -450, -500, -550, -600
    };
    if (taxRate < 0) taxRate = 0;
    if (taxRate > 20) taxRate = 20;
    return kTaxTable[taxRate];
}

/// Compute RCI demand valves from population state, matching Micropolis'
/// setValves() shape. Returns valves clamped to [-2000, 2000].
///
/// resPop/comPop/indPop: current population counts (from zone scan)
/// taxRate: city tax percentage (0-20)
/// hasStadium/hasPalace/hasAirport/hasStarport: civic building presence
/// popThreshold: population above which missing civics caps positive demand
struct ValveInputs {
    int resPop = 0;
    int comPop = 0;
    int indPop = 0;
    int taxRate = 7;
    bool hasStadium = false;
    bool hasPalace  = false;
    bool hasAirport = false;
    bool hasStarport = false;
    int popThreshold = 2000;  // when civic caps start mattering
};

struct ValveOutputs {
    int16_t resValve = 0;
    int16_t comValve = 0;
    int16_t indValve = 0;
};

inline ValveOutputs computeDemandValves(const ValveInputs& in) {
    auto clampValve = [](int v) -> int16_t {
        if (v >  2000) v =  2000;
        if (v < -2000) v = -2000;
        return static_cast<int16_t>(v);
    };

    const int totalPop = in.resPop + in.comPop + in.indPop;

    // Bootstrap: empty or near-empty city gets small positive R demand to
    // attract initial settlers. Without this the employment/migration formula
    // produces negative demand for a city that doesn't exist yet.
    if (totalPop < 100) {
        const int taxAdj = getTaxTableEntry(in.taxRate);
        return { clampValve(200 + taxAdj),
                 clampValve(100 + taxAdj),
                 clampValve(100 + taxAdj) };
    }

    // Normalize residential population (SC uses /8)
    const double normResPop = std::max(1.0, static_cast<double>(in.resPop) / 8.0);

    // Employment ratio: jobs / normalized residents
    const double totalJobs = static_cast<double>(in.comPop + in.indPop);
    const double employment = totalJobs / normResPop;

    // Migration and births (SC model)
    const double migration = normResPop * (employment - 1.0);
    const double births = normResPop * 0.02;
    const double projectedResPop = normResPop + migration + births;

    // Labour base (capped at 1.3)
    double laborBase = 1.0;
    if (totalJobs > 0) {
        laborBase = static_cast<double>(in.resPop) / totalJobs;
        if (laborBase > 1.3) laborBase = 1.3;
        if (laborBase < 0.0) laborBase = 0.0;
    }

    // Internal market
    const double internalMarket = (normResPop + in.comPop + in.indPop) / 3.7;

    // Projected populations
    const double projectedComPop = internalMarket * laborBase;
    const double projectedIndPop = std::max(5.0, static_cast<double>(in.indPop)) * laborBase;

    // Growth ratios (clamped to max 2.0)
    double resRatio = (normResPop > 0) ? projectedResPop / normResPop : 1.0;
    double comRatio = (in.comPop > 0) ? projectedComPop / in.comPop : 1.0;
    double indRatio = (in.indPop > 0) ? projectedIndPop / in.indPop : 1.0;
    if (resRatio > 2.0) resRatio = 2.0;
    if (comRatio > 2.0) comRatio = 2.0;
    if (indRatio > 2.0) indRatio = 2.0;

    // Tax adjustment. Multiplier is 1500 (not SC's 600) because DuneCity
    // computes valves fresh each tick rather than accumulating like SC.
    // SC accumulates over many cycles; we need a single computation to
    // produce a valve strong enough that extreme imbalance (R >> C+I)
    // overcomes max local eval (+750) and stays below growth gate (-350).
    // Requires M > 1342 for the 10k-R/100-C/100-I invariant.
    const int taxAdj = getTaxTableEntry(in.taxRate);
    int resRaw = static_cast<int>((resRatio - 1.0) * 1500.0) + taxAdj;
    int comRaw = static_cast<int>((comRatio - 1.0) * 1500.0) + taxAdj;
    int indRaw = static_cast<int>((indRatio - 1.0) * 1500.0) + taxAdj;

    // Civic caps: when population exceeds threshold and civic building is
    // missing, clamp positive demand to a soft ceiling. This makes Stadium,
    // Airport, and Starport matter for growth without hard-blocking.
    constexpr int kCivicCapCeiling = 200;  // soft cap when civic missing

    if (in.resPop > in.popThreshold) {
        if (!in.hasStadium && !in.hasPalace && resRaw > kCivicCapCeiling) {
            resRaw = kCivicCapCeiling;
        }
    }
    if (in.comPop > in.popThreshold / 2) {
        if (!in.hasAirport && comRaw > kCivicCapCeiling) {
            comRaw = kCivicCapCeiling;
        }
    }
    if (in.indPop > in.popThreshold / 2) {
        if (!in.hasStarport && indRaw > kCivicCapCeiling) {
            indRaw = kCivicCapCeiling;
        }
    }

    return { clampValve(resRaw), clampValve(comRaw), clampValve(indRaw) };
}

// --- Traffic pollution -------------------------------------------------------
//
// SC Classic's traffic density feeds into the pollution scan: road segments
// with heavy traffic contribute pollution proportional to density. Light
// traffic (density < threshold) contributes a small amount; heavy traffic
// (density >= threshold) contributes more. This closes the feedback loop:
// zones that generate traffic also generate pollution along the roads they
// use, penalizing dense residential areas near busy corridors.

constexpr int kTrafficPollutionLightThreshold = 64;   // density >= this → light traffic pollution
constexpr int kTrafficPollutionHeavyThreshold = 150;  // density >= this → heavy traffic pollution
constexpr int kTrafficPollutionLight = 2;   // per-block pollution contribution from light traffic
constexpr int kTrafficPollutionHeavy = 6;   // per-block pollution contribution from heavy traffic

/// Compute traffic pollution contribution for a block given its traffic density.
inline int getTrafficPollution(int trafficDensity) {
    if (trafficDensity >= kTrafficPollutionHeavyThreshold) return kTrafficPollutionHeavy;
    if (trafficDensity >= kTrafficPollutionLightThreshold) return kTrafficPollutionLight;
    return 0;
}

// --- Growth rate tracking ----------------------------------------------------

/// Growth-rate delta to apply when a zone grows or declines. Positive for
/// growth, negative for decline. The rate map decays toward zero each scan.
constexpr int kGrowthRateIncrement = 4;
constexpr int kGrowthRateDecrement = -4;

/// Decay amount subtracted from absolute growth rate each city day.
constexpr int kGrowthRateDecay = 1;

// --- Tax / city-year cadence -------------------------------------------------

/// Number of game cycles per simulated city year. At GAMESPEED_DEFAULT
/// (16 ms/cycle), 60 s of wall-clock = 60000/16 = 3750 cycles. The year
/// is the budget unit (taxes/police paid annually).
constexpr uint32_t kCyclesPerCityYear = 3750;

/// SimCity Classic divides a year into 48 cityTime units (4 weeks × 12
/// months). Each unit is a "day tick" — the cadence at which we run
/// effects scans and roll for zone growth. With kCyclesPerCityYear=3750,
/// that's ~78 cycles ≈ 1.25 s per day. Zones get ~48 growth attempts
/// per year, gated stochastically inside runZoneGrowth() so population
/// climbs gradually rather than in big steps.
constexpr int      kCityDaysPerYear   = 48;
constexpr uint32_t kCyclesPerCityDay  = kCyclesPerCityYear / kCityDaysPerYear;

/// Compute annual tax revenue (in credits) for a city of `totalPopulation`
/// at the given tax rate (0-100, but realistically 1-20 in-game).
inline int32_t computeAnnualTaxRevenue(int totalPopulation, int taxRatePct) {
    if (totalPopulation <= 0 || taxRatePct <= 0) return 0;
    return (totalPopulation * taxRatePct) / 100;
}

} // namespace DuneCity

#endif // DUNECITY_CITYEFFECTS_H
