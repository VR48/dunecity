# City tax and spice economics (1.0.637, 2026-09-11)

## Active tax formula

Stefan authorized the Micropolis easy restructuring, with Palace as an explicit
R+C exception to the government tax exemption. Only actual R/C/I zones and the
Palace generate direct city tax. Other government buildings retain their jobs,
demand and pollution roles, but generate no tax.

Annual gross tax = `(R/8 + C + I) * averageLandValue/120 * taxPercent * 1.4`.
Use taxable populations only; Palace contributes both its R and C portions.
The census stores eighths (`R + 8*C + 8*I`) to retain partial residential houses.
Annual totals are rounded once per house and paid fractionally each simulation
cycle. This follows Micropolis easy's weights/rate while avoiding its intermediate
integer truncation of tiny cities. Explicit zero land value earns zero; AI
forecasts can assume 128 for unknown future land. All values are deterministic
integer arithmetic. No per-lot tax calculation or new save-format field.

One city year = 3,750 cycles = 60 simulated seconds. Annual credits therefore
also equal credits per simulated minute in DuneCity. Simulation speed changes
both taxes and harvesting; do not use wall-clock FPS to compare them.

## Income readout

At 7% tax and house-average land value 128, gross credits per simulated minute:

| Tax-producing building | Low | Medium | High |
| --- | ---: | ---: | ---: |
| Residential zone | 20.91 | 31.36 | 52.27 |
| Commercial zone | 10.45 | 31.36 | 52.27 |
| Industrial zone | 10.45 | 31.36 | 41.81 |
| Palace (R+C) | 31.36 | 62.72 | 104.53 |

Approximate contributions before city-total rounding and upkeep. Empty zones
pay zero. A single developed house within an R lot adds population 2, about 2.61
credits/minute; eight houses total 20.91 before the next density stage.
At land value 64, all amounts halve; at 192, multiply by 1.5. Rates scale with tax.

Before 1.0.637, high-density R/C/I yielded 186.67/23.33/18.67 at the same settings.
The change is R -72%, C/I +124%. Palace was exempt in 1.0.634–636; now both halves
pay tax (rather than the old pre-634 mismatch between R-only payout and R+C UI).

## Government roles

| Infrastructure | Role / maximum density | Direct tax |
| --- | --- | ---: |
| WindTrap | Power only; no industry | 0 |
| Light Factory, Spice Silo | Low I | 0 |
| Refinery, Heavy Factory, High Tech Factory, Repair Yard | Medium I | 0 |
| House IX | High C | 0 |
| Starport | Seaport / existing high-I employment and demand gate | 0 |
| Construction Yard | Existing high I | 0 |
| Radar, Airport | Existing medium/high C respectively | 0 |
| Barracks, WOR | Existing high R garrison | 0 |
| Palace | R+C, up to high density | See income table |

All remaining non-zone structures also have zero direct tax. Silo, WindTrap,
IX, Starport and Construction Yard stay clean. Light Factory emissions cap 10;
Refinery/Heavy/HighTech/Repair cap 25. Jobs/population/emissions and loaded
occupancy are clamped to mapped density; UI labels agree.

## Roads and other costs

Road upkeep is removed in 1.0.637 at every population, including heavy traffic.
There is no per-house road census or charge in the runtime, UI or AI forecasts.
The billing-specific ownership additions from 647b98c are reverted: automatic
frontage roads and the city road-overlay command preserve underlying tile
ownership. Load no longer infers owners from neighbouring structures. Existing
saved tile ownership is preserved; no speculative clearing of concrete owners.
Normal House::placeStructure ownership for manually built foundations/roads
remains. Roads still provide foundations; enemy roads/concrete do not expand a
house's construction range. Roads are not converted to concrete or removed.

Police/turret upkeep remains. Separate power charges remain and are not part of
the city budget panel: `powerRequirement/32` every 15 seconds when enabled, or
nominally `powerRequirement/8` per city year. Construction, units and repairs
are also separate from gross tax income.

## Complete harvester cycle

Capacity 700; harvesting 0.1344/cycle = 8.4/second, so filling takes 83.33 seconds.
A healthy refinery unloads 0.625/cycle = 39.0625/second, taking 17.92 seconds.
Travel, fields, queues, damage and carryalls alter actual delivered income.

| Extra travel/queue seconds per trip | Gross credits/minute | High R or C equivalent | High I equivalent | High Palace equivalent |
| --- | ---: | ---: | ---: | ---: |
| 0 (upper bound) | 414.80 | 7.94 | 9.92 | 3.97 |
| 30 | 319.99 | 6.12 | 7.65 | 3.06 |
| 60 | 260.46 | 4.98 | 6.23 | 2.49 |

Formula: `700*60/(83.333+17.92+extraSeconds)`. Source-derived estimates, not
measured match income. A refinery receives 700 per full delivery, but has no
independent passive income: its income is its fleet's delivered spice. Do not
add refinery income to harvester income again. The Tornie-only Worfinery also
processes deliveries; it is not a standard DuneCity tax-producing building.

## Zone construction and AI

R/C/I use normal BuilderBase configured timing, respecting house/mod data.
Default 40*15 ticks*16ms = 9.6 simulated seconds at full speed with sufficient
funds. Roads/instant-build options unchanged; zone prices and population growth
unchanged. QuantBot forecasts include construction plus 60s growth allowance.

First refinery remains an income/technology prerequisite, then a demanded R
hedge. No C/I is forced against nonpositive demand. Zone choice normalizes demand
maxima; among needs within 20% of strongest, balances built+queued plots with the
existing 3:1:1 R/C/I weights. This avoids C-before-I 500 and forced-R-infill starvation.
Housing infill remains a placement preference, not a zone-type override.

Tax versus refinery comparison evaluates four simulated minutes of marginal
proceeds per credit, with actual setup/power, demand, suitability, growth and
unfinished lots. No road upkeep estimate. C/I gets limited indirect credit for
supporting housing short of jobs, using the new R/8 tax weighting. Refinery
investment requires additional sustainable near-term bay capacity, considering
current/queued workers plus 3 and sustainable spice target. Includes fill/unload,
construction, bounded local travel sampling and danger; no new pathfinding.

Payout, budget, QuantBot services/production and both active Mentat build paths
use the same weighted taxable census. Telemetry `tax_base_eighths` explicitly
labels its units; policy `micropolis-tax-palace-v57`. Existing demand/population
census remains unweighted and separate from taxation.

## Reference

Micropolis `simulate.cpp` setValves uses R/8+C+I; collectTax uses landValue/120,
tax percentage and FLevels 1.4/1.2/0.8. DuneCity adopts the easy 1.4 factor for all
AI difficulties; it is an economic balance constant, not an AI handicap.
Verified local read-only reference:
`../simcity/micropolis/MicropolisCore/src/MicropolisEngine/src/simulate.cpp`.
Upstream: https://github.com/SimHacker/micropolis/blob/master/MicropolisCore/src/MicropolisEngine/src/simulate.cpp
