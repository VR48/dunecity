# City traffic balance (1.0.643)

## Comparison with Micropolis

Primary reference inspected locally: MicropolisCore/src/MicropolisEngine/src/
traffic.cpp (addToTrafficDensityMap, tryGo), zone.cpp (R/C/I trip gates), and
simulate.cpp (decTrafficMap). Dune City's rendering thresholds were already
correct: below 64 no cars, 64–191 light, 192 and above heavy. Moving those
thresholds would hide the problem without fixing traffic pollution.

Both engines use 2×2 density cells, sample every second successful-route move,
cap density at 240, and decay once per city update by 24 (34 above 200).
Occupancy gates are R population /36 and C/I population /6, capped at one.
Micropolis adds 50 per sampled cell and varies the road walk randomly. Our
bounded BFS repeatedly used the same route and added trips for mapped Dune
infrastructure as well as actual R/C/I zones. A single guaranteed daily trip
adding 50 exceeds both decay rates, so even one repeated route saturates.
Turns could also stamp two sampled road tiles in the same density cell.

Our zones are 2×2 rather than 3×3, while roads remain 1×1. With shared roads,
regular block pitch shrinks from four to three tiles: there can be roughly
16/9 as many plots in a map area. This makes direct transplantation of the
original traffic weight particularly aggressive. The exact road load also
varies with layout, occupancy and routing; there is no universal geometric
conversion to an exact Micropolis traffic level.

## Implemented adaptation

- Only actual R/C/I zones emit zone-style trips. Dune infrastructure retains
  employment and destination/connectivity roles, without extra trip generation
  from special-building updates. This does not alter tax or job populations.
- Use 22 per journey/cell (50 × 4/9, rounded down), a deliberate balance choice
  based on plot footprint, not a claim of exact Micropolis equivalence. It is
  below the normal 24 decay so a single persistent daily route cannot become
  permanently congested simply through elapsed time. Multiple busy sources
  sharing a road can still exceed decay and congest it.
- Rotate equal shortest-path direction preferences by site and simulated day.
  Reachability and maximum distance remain unchanged. No gameplay RNG calls,
  extra route searches, map floods or saved state are introduced. This spreads
  equal-route choices; it is not a port of Micropolis's random road walk.
- Count each journey once in each sampled density cell, including turns.
- Preserve Micropolis decay, saturation and animation thresholds. Old inflated
  traffic is allowed to decay naturally after loading with the new executable.

## Verification and limits

CityTrafficPolicyTestCase covers the original 50-unit reference separately,
then tests the production 22-unit adaptation: a single mature plot stays quiet
for 1,000 updates; two half-occupied employers sharing an access route exhibit
quiet and light traffic over 10,000 updates rather than permanent heavy;
four mature plots sharing a bottleneck still produce heavy traffic; quiet
roads clear after ten decay updates. Route variation is deterministic and
retains the distance bound; bends do not double count density cells.

The 1.0.642 live log corroborates excessive traffic: at cycle 172225, 206/249
nonzero traffic cells were heavy. These fixtures validate model behaviour,
not a measured post-change replay of that entire live game. Road topology and
large concentrations of mature plots can still create legitimate hotspots.
