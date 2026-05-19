# Bugfix: Residential Growth/Shrink — Port SimCity Valve Semantics

**Date:** 2026-05-19
**Status:** Implemented, awaiting live-game observation

## Problem

Residential zones grew unchecked even when R=-2000 (extreme oversupply).
The RCI demand valves were computed AFTER the growth loop, making them
display-only. Growth decisions never consulted the valves, so residential
density climbed to L3 regardless of whether there were enough commercial/
industrial jobs to support the population.

User-visible: in-game HUD showed R=-2000, C=+2000, I=+2000, yet
residential zones kept growing rapidly.

## Root Cause

In `CityEffectsRuntime.cpp::runZoneGrowth()`:
1. Growth decisions (lines 578-664) checked `powered && level < max`
   but never consulted `resValve_/comValve_/indValve_`.
2. Valve computation (lines 708-725) ran AFTER growth, not before.
3. Decline only triggered on power starvation, never on demand oversupply.

SimCity Classic computes `zscore = globalValve + localEvaluation` and
gates both growth (zscore > -350) and decline (zscore < 350) on this
combined score.

## Fix

### CityEffects.h — new pure helpers
- `computeLocalEval(role, landValue, pollution, hasRoad)` — local zone
  evaluation inspired by SC's evalRes/evalCom/evalInd
- `computeZscore(valve, localEval, powered)` — combined score for
  growth/decline gating
- `shouldZoneDecline(zscore, level, roll)` — probabilistic decline
  based on zscore severity
- Constants: `kZscoreGrowthGate = -350`, `kZscoreDeclineGate = 350`

### CityEffectsRuntime.cpp — restructured runZoneGrowth()
1. **Valve computation moved before growth loop** — valves now reflect
   pre-growth state and gate decisions.
2. **Growth gated on zscore** — `zscore > kZscoreGrowthGate` required.
   With R=-2000 even the best neighborhood (lv=250) gives zscore=-1000,
   well below -350.
3. **Demand-based decline added** — zones probabilistically shrink when
   zscore is negative. Probability tiers: 25% (zscore≤-1000), 12.5%
   (≤-500), 6.25% (<0). L1→L0 protected unless zscore≤-1500.
4. **Road check precomputed** — feeds both zscore (via localEval) and
   the existing growth gate. Lack of road now hurts residential zscore
   by -300, making decline more likely.
5. **Power-starved decline preserved** as a separate guaranteed path.
6. **C/I growth also gated** on comValve_/indValve_ via the same
   zscore mechanism.

### Tests added
- `computeLocalEval` for all three roles
- `computeZscore` powered/unpowered
- Regression: R=-2000 always blocks residential growth
- `shouldZoneDecline` at all probability tiers
- L1 protection unless extreme
- Buffer zone [0, 350) no decline

## What's proven
- Unit tests pass for all pure helpers
- Build compiles cleanly
- Valve computation runs before growth decisions
- R=-2000 mathematically cannot produce a zscore above the growth gate

## What needs live-game observation
- Equilibrium dynamics: do R/C/I balance reach a stable state?
- Growth rate feel: is it too slow or too fast?
- Decline rate: does residential visibly shrink under oversupply?
- Bootstrap: do freshly placed zones still grow quickly to L1?
- Palace behavior: does Palace obey the same shrink rules?
