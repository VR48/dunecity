# City tax and spice economics (1.0.633, 2026-09-11)

These are source-derived estimates at normal simulation speed, not measured
match income. Tax/budget constants are unchanged by 1.0.633.

## Runtime tax and budget

`CityEffects.h::computeAnnualTaxRevenue` taxes the sum of raw R population and
C/I jobs: approximately `population * (200/3) * taxPercent/100 * averageLandValue/128`.
The runtime aggregates by house, rounds the annual total, and pays fractional
credits each cycle. Average land value is house-wide, not the individual lot's.
The displayed population multiplier of 20 is not used in tax calculation.
At average land value zero the legacy formula skips the land-value multiplier.

One year is 3,750 cycles, 60 simulated seconds. `CityBudgetWindow` dividing the
annual forecast by 60 matches actual payouts. Faster/slower simulation changes
both harvesters and taxes; wall-clock FPS should not be used to balance them.

At 7% tax, average land value 128, per fully occupied high-density zone:

| Zone | Internal population/jobs | Approximate gross credits/year (= simulated minute) |
| --- | ---: | ---: |
| R | 40 | 186.67 |
| C | 5 | 23.33 |
| I | 4 | 18.67 |

Four R plots cost 400 before foundations and power, the same sticker price
as a refinery, and eventually earn about 746.67/minute. They require demand,
clean/suitable land, jobs, power and time to mature. C/I's direct tax understates
their economic value: each job supports eight residential population.

The supplied budget screenshot shows 42,506 tax, 1,300 police, 720 rocket-turret
service costs and 1,064 roads: 3,084 listed costs (7.26%) and 39,422 net/year,
657.03/second. This is the city budget, not total treasury cash flow. It excludes
construction, units, repairs and the separate power bill. Power costs
`powerRequirement/32` every 15 seconds when power rules are enabled, i.e. nominal
`powerRequirement/8` per city year. Pure power upkeep at high density is 1.5/year
for R, 2.25 for C and 3 for I, before shared services/roads.

## Complete harvester cycle

Capacity is 700; harvesting is 0.1344/cycle (8.4/second). Filling alone takes
83.33 seconds. A healthy refinery unloads 0.625/cycle (39.0625/second), requiring
17.92 seconds. Travel, field changes, queues and damage add delay; carryalls and
roads can reduce transport delay. Refinery construction/delivery adds initial
startup delay, separate from recurring throughput.

| Extra travel/queue seconds per trip | Gross credits/minute | Equivalent high-density R | C | I |
| --- | ---: | ---: | ---: | ---: |
| 0 (upper bound) | 414.80 | 2.22 | 17.78 | 22.22 |
| 30 | 319.99 | 1.71 | 13.71 | 17.14 |
| 60 | 260.46 | 1.40 | 11.16 | 13.95 |

Formula: `700 * 60 / (83.333 + 17.92 + extraSeconds)`. Equivalents use gross
income at 7% tax and land value 128, and exclude startup/operating costs. At land
value 64 tax halves and required zone counts double; at 192 counts are two-thirds.

## Micropolis comparison

Verified against local `simcity/micropolis/MicropolisCore/src/MicropolisEngine/src/simulate.cpp`
(setValves and collectTax) and
https://github.com/SimHacker/micropolis/blob/master/MicropolisCore/src/MicropolisEngine/src/simulate.cpp .
Micropolis tax population is `R/8 + C + I`; tax is approximately
`taxPopulation * averageLandValue/120 * taxRate * difficultyFactor`, with easy
factor 1.4. Ignoring aggregate integer rounding, high-density R/C/I return about
52.27/52.27/41.81 annually at the same tax/land value. DuneCity R is 3.57x that
annual amount; C/I are 0.446x. Overall difference depends on zone mix.

Police costs 100/year in both engines. DuneCity copies easy road upkeep 0.7/year
per ordinary tile (heavy twice), with free automatic perimeter-road construction and an additional exemption below displayed
population 2,000. Smaller 2x2 zones also need fewer frontage tiles than 3x3 zones.
Thus current city margins are not an exact Micropolis balance. Keep the correct
60-second payout conversion; separately review tax population weighting and show
power costs before choosing a broad tax reduction/upkeep increase. No such
balance change has been made in this version.

## QuantBot investment policy

The first refinery remains essential income/technology. A first demanded R plot
then hedges spice income; no missing C/I is forced against zero/negative demand.
Further investments compare forecast proceeds per credit over four simulated
minutes. A refinery must also add capacity for sustainable, near-term workers
(current/queued fleet plus three, capped by sustainable target, three workers per
bay). Already-funded bays therefore favour useful demanded zoning.

Forecasts use actual prices, unprepared footprint cost, free automatic frontage
construction (future road maintenance only), amortized power,
tax/average land value, low/medium expected growth with a one-minute maturation
allowance, demand, pollution/crime and unfinished same-type plots. Demanded C/I
gets partial credit for jobs supporting existing/pending housing short of work.
Refinery estimates include a free worker if under cap, marginal fleet throughput,
fill/unload/construction time, a bounded sampled local distance, half theoretical
bay capacity for manoeuvring, remaining spice share and a danger discount. These
are intentionally approximate: no new pathfinding or measured-income history.

Log `city_economy_comparison` records cost, income, upkeep, delay, confidence,
proceeds, capacity/hedge/funding decisions and the selected item. It is sampled
per yard at 30-second intervals. Compare subsequent match cash flow to these
forecasts before refining their heuristic parameters.
