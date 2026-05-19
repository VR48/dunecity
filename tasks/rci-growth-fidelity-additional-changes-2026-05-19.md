# RCI Growth Fidelity Additional Changes

Date: 2026-05-19
Status: Spec ready; implement after the active residential shrink/unemployment job lands.

## Context

The current DuneCity city simulation has most of the visible ingredients: R/C/I roles, population levels, land value, pollution, crime, traffic overlay, police funding, and budget hooks. The bug report shows the coupling is still wrong: residential can keep growing while the R valve is pinned negative, unemployment is high, and commercial/industrial demand is positive.

Micropolis does not treat growth as a simple local supply threshold. It runs:

- global R/C/I valves from population, employment, migration, births, tax, and caps
- local evaluation per zone type
- traffic attempts between complementary zone types
- pollution/crime/value scans feeding the local score
- explicit out-migration/downgrade paths when traffic, power, or score is bad

The DuneCity implementation needs to move toward that model instead of adding more one-off gates.

## Source References

Original Micropolis:

- `../simcity/MicropolisCore/MicropolisEngine/src/simulate.cpp`
  - `setValves()`: derives residential demand from employment/unemployment, migration, births, labour base, tax, and growth caps.
  - `decTrafficMap()`, `decRateOfGrowthMap()`: traffic/growth overlays decay rather than being rebuilt as static snapshots only.
- `../simcity/MicropolisCore/MicropolisEngine/src/zone.cpp`
  - `doResidential()`, `evalRes()`, `doResIn()`, `doResOut()`
  - `doCommercial()`, `evalCom()`, `doComIn()`, `doComOut()`
  - `doIndustrial()`, `evalInd()`, `doIndIn()`, `doIndOut()`
  - `incRateOfGrowth()`
- `../simcity/MicropolisCore/MicropolisEngine/src/scan.cpp`
  - `pollutionTerrainLandValueScan()`
  - `crimeScan()`
  - `getPollutionValue()`
- `../simcity/MicropolisCore/MicropolisEngine/src/traffic.cpp`
  - `makeTraffic()`, `findPerimeterRoad()`, `tryDrive()`, `addToTrafficDensityMap()`

Current DuneCity:

- `include/dunecity/CityEffects.h`
- `src/dunecity/CityEffectsRuntime.cpp`
- `src/dunecity/CitySimulation.cpp`
- `include/dunecity/CitySimulation.h`
- `include/dunecity/ZoneSimulation.h`
- `src/dunecity/ZoneSimulation.cpp`

## Existing Gaps

### 1. Valves are computed too late and too simply

Current DuneCity recomputes `resValve_`, `comValve_`, and `indValve_` after growth from direct supply totals:

- R = jobs minus residents
- C = residents minus commercial
- I = residents minus industrial

That makes the UI look plausible but does not reliably drive the growth decision that already happened. It also misses Micropolis' employment model:

- `Employment = (ComHis[1] + IndHis[1]) / NormResPop`
- `Migration = NormResPop * (Employment - 1)`
- `Births = NormResPop * 0.02`
- projected residential population drives the residential valve

### 2. Traffic is not a growth input

DuneCity currently builds a traffic density overlay by stamping city-role structures and letting roads absorb some value. Micropolis uses traffic as an actual success/failure condition:

- residential tries traffic to commercial
- commercial tries traffic to industrial
- industrial tries traffic to residential
- no perimeter road returns `-1`
- no reachable destination returns `0`
- failed traffic can immediately trigger zone decline
- successful trips add to the traffic density map

### 3. Local evaluation is incomplete

Micropolis combines global valve + local score:

- Residential: `resValve + evalRes()`
  - `evalRes = clamp((landValue - pollution) * 32, 0, 6000) - 3000`
  - traffic failure is `-3000`
  - pollution above 128 blocks immigration
- Commercial: `comValve + evalCom()`
  - `evalCom` reads `comRateMap`
  - commercial growth is also capped by local land value
- Industrial: `indValve + evalInd()`
  - `evalInd` is mostly traffic/power/global demand; local pollution is a consequence more than a blocker

DuneCity currently uses land-value floors, local supply thresholds, and pollution gates, but does not have a proper zone-type score contract.

### 4. Decline is too narrow

DuneCity mainly declines on power-starvation. Micropolis declines when:

- traffic cannot be made
- z-score is bad enough
- power is off
- residential can downgrade high density, clear back to empty, or remove houses
- commercial/industrial step down by density and eventually clear

DuneCity needs a level-based equivalent:

- level 3 -> 2 -> 1 -> 0
- zones can become vacant again
- non-zone city-role buildings should not be destroyed, but their city occupancy can fall back toward their floor
- Palace can shrink occupancy like residential but should not vanish

### 5. Civic caps are missing from demand

Micropolis caps positive demand when infrastructure is missing:

- residential cap: stadium-style civic capacity
- commercial cap: airport-style trade capacity
- industrial cap: seaport/shipyard capacity

DuneCity has Stadium, Airport, Starport, and Palace now. They should matter to the demand ceiling, not just exist as objects.

## Proposed Design

### A. Add a pre-growth valve pass

Create a dedicated `CitySimulation::updateDemandValves()` and call it before `runZoneGrowth()`.

Inputs:

- normalized residential population
- commercial jobs
- industrial jobs
- current tax rate
- employment ratio
- migration
- births
- internal market
- external market proxy
- civic caps

Output:

- `resValve_`, `comValve_`, `indValve_` clamped to `[-2000, 2000]`

Rules:

- If residents exceed C+I jobs, residential demand must fall.
- If jobs exceed residents, residential demand should rise.
- If C/I demand is high and R is negative, residential zones should not grow just because nearby C/I exists; the valve must be part of the grow/shrink score.
- Tax should suppress all three valves at high rates, matching Micropolis' `taxTable` influence.

Acceptance:

- With R = -2000, residential level must not increase.
- With R < -350 and poor local score, residential can shrink.
- C/I can remain positive while R is negative without forcing residential growth.

### B. Replace threshold growth with zone-type z-score decisions

Introduce helper functions:

- `evaluateResidentialNode(...)`
- `evaluateCommercialNode(...)`
- `evaluateIndustrialNode(...)`
- `maybeGrowNode(...)`
- `maybeDeclineNode(...)`

Each node computes:

- global valve for its role
- local evaluation score
- traffic result
- powered state
- pollution/crime/value penalties where applicable
- deterministic multiplayer-safe roll

The decision should mirror Micropolis' shape, adapted to DuneCity levels:

- score <= strong negative: decline
- score near neutral: usually hold
- score positive: may grow
- power failure forces decline pressure
- traffic failure triggers decline pressure

Do not copy Micropolis' exact random signed-16 thresholds blindly. Preserve the behaviour curve, but tune for DuneCity's slower day/year cadence and 0..3 levels.

### C. Implement traffic attempts as connectivity, not overlay-only

Add a deterministic road-network query:

- find a perimeter road around the structure footprint
- breadth-first search along road tiles up to a fixed max distance
- destination succeeds when reaching a perimeter road adjacent to a target role

Role destinations:

- Residential -> Commercial
- Commercial -> Industrial
- Industrial -> Residential

Return:

- `NoRoad` when no perimeter road exists
- `NoDestination` when a road exists but cannot reach the needed role
- `Connected` when reachable

On `Connected`, increment/decay `trafficDensityMap_` along the path. Traffic density should feed pollution like Micropolis road traffic:

- light traffic contributes pollution
- heavy traffic contributes more pollution

Acceptance:

- A residential zone with no nearby road may bootstrap to level 1, but cannot keep growing and can decline under bad demand.
- A road connected to nowhere is not enough for stable higher-density growth.
- Traffic overlay changes because actual trips happened, not because buildings merely exist.

### D. Add a commercial desirability map

Micropolis uses `comRateMap`; DuneCity does not currently have an equivalent.

Implement either:

1. a `commercialRateMap_` layer, or
2. a pure `computeCommercialRate(node)` helper if a full overlay is not needed yet.

Inputs:

- distance to city centre / market centre
- traffic connectivity
- land value
- nearby residential supply
- nearby industrial supply
- airport/trade bonus
- crime penalty
- pollution penalty, softer than residential

Acceptance:

- Commercial should prefer connected, valuable, populated areas.
- High crime/low value commercial zones should stall or decline even when the global C valve is positive.
- Airport should materially improve commercial potential/cap, not just spawn aircraft.

### E. Keep pollution, crime, and value as first-class feedback

Current land-value/crime/pollution scans are close enough to keep, but growth must consume them consistently:

- Residential:
  - land value helps
  - pollution hurts strongly
  - pollution > 128 blocks immigration
  - crime hurts via land value and may add a direct penalty if needed
- Commercial:
  - land value and traffic help
  - crime hurts
  - pollution hurts moderately
- Industrial:
  - traffic and demand dominate
  - industrial pollution is an output, not a major self-blocker

Traffic pollution must be included in the pollution scan once real trips exist.

### F. Add civic cap rules

Add cap state to the demand pass:

- residential cap:
  - no Stadium or Palace: cap positive residential demand at a lower ceiling once population crosses a threshold
  - Stadium increases cap
  - Palace increases cap or acts as high-cap residential civic anchor
- commercial cap:
  - Airport increases or unlocks high commercial demand
- industrial cap:
  - Starport acts as shipyard/seaport equivalent and unlocks higher industrial demand

Use soft caps first rather than hard stops, so the sim does not feel broken before the player understands the missing civic building.

Acceptance:

- In larger cities, missing civic infrastructure visibly flattens the matching RCI demand.
- Adding Stadium/Airport/Starport changes the relevant valve within one scan cycle.

### G. Add growth-rate tracking

Port a small DuneCity equivalent of Micropolis `incRateOfGrowth()` and `decRateOfGrowthMap()`.

Use it for:

- debugging why a block is growing or shrinking
- future overlay UI
- balancing growth pace

Acceptance:

- Growth increments positive rate.
- Decline increments negative rate.
- Rate decays over time.
- Debug logs can show local growth pressure without reading save internals.

## Implementation Slices

### Slice 1: Demand and decline correctness

Depends on active job: `residential-shrink-unemployment`.

- Move valve calculation before growth.
- Use role valve in growth decisions.
- Add residential shrink when R is negative and local score is bad.
- Add tests for R=-2000 preventing and reversing residential growth.

### Slice 2: Connectivity traffic

- Implement road BFS traffic attempts.
- Replace the current residential nearby-road gate with traffic result.
- Wire traffic failure into decline for all R/C/I roles.
- Update traffic overlay from successful paths.

### Slice 3: Local evaluation parity

- Add residential/commercial/industrial evaluation helpers.
- Add commercial-rate calculation.
- Tune thresholds for 0..3 DuneCity density levels.

### Slice 4: Pollution/crime/value consumption

- Make every role consume the same local scan outputs through the evaluation helpers.
- Add traffic pollution.
- Verify high pollution and high crime can suppress or reverse growth.

### Slice 5: Civic caps

- Add Stadium/Palace residential-cap influence.
- Add Airport commercial-cap influence.
- Add Starport industrial-cap influence.
- Surface cap pressure in debug output and, later, the UI if useful.

### Slice 6: Regression harness

Add deterministic tests or scripted scenarios for:

- R=-2000, C/I positive: residential does not grow and can shrink.
- Residential with no road: bootstraps at most to level 1, then stalls/declines.
- Connected residential + connected jobs + low pollution: grows over time.
- High pollution > 128: residential immigration blocked.
- High crime: land value falls and growth stalls.
- Airport raises commercial demand/cap.
- Starport raises industrial demand/cap.
- Stadium/Palace raises residential cap/value.

## Non-Goals

- Do not change R/C/I footprint size. DuneCity settled constraint: gameplay zones stay 2x2.
- Do not destroy Dune structures during city decline. Only reduce city occupancy/level unless the object is an actual zone.
- Do not make traffic simulation expensive enough to affect multiplayer performance. Cache or cap BFS work if needed.
- Do not remove the existing Dune gameplay economy; city sim should sit on top of it.

## Verification

Required before claiming implementation complete:

- Relevant unit tests pass.
- Local app rebuilt at `build/bin/dunecity.app`.
- Manual or scripted scenario demonstrates residential shrink under unemployment/negative R demand.
- Manual or scripted scenario demonstrates traffic connectivity changing growth outcome.
- Logs or overlay evidence show land value, pollution, crime, traffic, and RCI valve values used in the decision.

