# Density-scaled hotspot outbreaks and idle road repairs — 1.0.606

Stefan corrected 605 outbreaks: one compact group at the worst crime hotspot,
with 1 trooper per low-density dangerous building, 2 per medium, 3 per high.
This supersedes the historical-average size, 12 minimum/60 maximum and spread
across several buildings in 605 below. Current occupied city level supplies the
weight; vacant zones and non-city-role structures contribute zero. Existing
per-house 16x16 district scope, dangerous threshold 192, population gate 5000,
and 4–6 simulation-minute buildup remain. The Micropolis crime formula is unchanged.

The entire district wave uses one highest-crime occupied building as its origin
and target. Legal infantry slots are filled nearest-first within 16 tiles of that
hotspot, with stable integer ordering. One advancing cursor avoids rescanning
blocked/full tiles for every trooper. Engine unit limits and available space can
still reduce actual deployment; telemetry retains requested versus spawned and
adds occupied_density_1_2_3 plus hotspot coordinates. The old buildingExposure
field remains reserved in save format 9833, preserving saved progress and byte
layout; it no longer affects strength. No wall-clock/random decisions added.

Idle qBot city yards now queue paid road repairs after strategic/city construction
selects no work. The older independent road helper scanned only 20 tiles from the
base centre; new maintenance considers edges/corners around all surviving owned
buildings, including outer-city road gaps and intersections. Broken through-roads
rank before junction/edge extensions. Only connected, legal, unoccupied terrain
outside reserved footprints qualifies. Queue at most 8 road segments once per
planning pass, respecting spendable cash above the economic reserve. Normal
construction retains priority. Road placement uses the ordinary yard production
and placement pipeline; no new free road command. Road repairs can reuse recently
destroyed areas like concrete, and cancel safely if their site becomes blocked
or has already been repaired. Telemetry: city_road_repair, idle_yard_road_gaps.
Vanilla is unaffected by these city-only changes.

Built/signed locally as 1.0.606; dependency audits before/after build, version
metadata and signature verification pass. CTest: 484 passed, 3 optional skipped.
Tests cover density-weighted totals, uncapped district sizes, 4/6-minute timing,
reset/population gates, saved legacy fields, stable nearest-hotspot sites and map
edges, and outer-city road gap prioritisation/deduplication/blocking. Not launched,
pushed or released. Live wave balance and road maintenance still need gameplay.

# Larger district outbreaks and flexible production — 1.0.605

Stefan requested larger coordinated rebel outbreaks after longer sustained crime,
then explicitly shortened the proposed wait to 4–6 simulation minutes. The old
three-trooper district spawn is replaced by a 12–60 trooper outbreak. Mean crime
among dangerous buildings controls buildup: 192 takes ~6 minutes, 250 takes ~4,
quantized to the city scan cadence. Dangerous threshold192 and the existing5000
population gate remain; the Micropolis crime formula itself is unchanged.

Each house's existing16x16 district timer accumulates dangerous-building exposure
throughout buildup. Force is3 times the crime-weighted average dangerous-building
count, bounded12–60. More buildings make a bigger wave, not a shorter timer; a
last-second surge cannot inherit a fully mature large force. No dangerous buildings
or population below5000 resets both accumulators. Wave groups of3 emerge together
around several dangerous buildings, distributed across the district when the force
cap permits only a subset. A blocked spawn location skips that trooper rather than
aborting all the other groups. Existing hostile faction selection, engine unit
limits, deployment cancellation and urgent news warning remain. A wave consumes
the buildup even when capacity/space limits it, avoiding rapid retries.

Save version9833 appends per-district64bit building exposure after the old progress
array. Old saves read the old array then reset outbreak timers, since they cannot
supply exposure history. New saves preserve both accumulators exactly. The codec
checks district count. This is deterministic simulation state; no wall clock/RNG.

Both Vanilla and DuneCity qBot can now add light factories when at least75% of
existing lanes are busy and funded light-unit shortage covers the factory cost.
Unfinished/queued factories block duplicate expansion; reserves, placement and
engine limits remain. Light backlog expansion comes before optional heavy-factory
cash expansion. Telemetry adds light busy/backlog/deficit and light_unit_backlog.

When every available heavy type is above its preferred share, heavy factories may
still fill spare funded army capacity. They choose the least overrepresented type
relative to its learned share, considering the next unit's cost. Fielded and queued
value count against the army cap and orders consume money/cap sequentially. No new
fixed troop ratios. Light factories receive troop-order priority so heavy overflow
cannot consume their immediate slots; city yards retain their existing first
priority. Whole-match learning and safer factory placement remain intact.

Policy district-outbreak-production-v42. Heavy telemetry identifies
available_factory_capacity fallback and no_affordable_capacity. Crime telemetry
includes dangerous-building count, requested force, mean crime and buildup rate;
member records identify each spawn-origin building.

Built and signed locally1.0.605. Ninja dependency audits passed, version metadata
consistent. CTest482passed,3optional skipped. Tests cover4/6minute boundaries,
cluster size/history, policing/small-population reset, district spread, old/new
save codecs with trailing sentinels and invalid sizes, heavy overflow balancing,
parallel cap/cash consumption and light backlog expansion gates. Not pushed,
released or launched; live balance still needs a gameplay test.

# Police reinforcement ceiling and completed 603 match — 1.0.604

Stefan requested increasing police deployment's per-house count ceiling to 250.
PoliceStation now permits reinforcements below 250 military units and blocks at
250; the existing per-member batch recheck prevents overshoot. Count still excludes
harvesters, MCVs, carryalls, frigates, sandworms and ambient units, as before.
The qBot army-value ceiling and engine unit limits remain enforced. Automatic and
manual deployment, and the sidebar Unit limit reached indicator, share this gate.
Cooldown and composition unchanged. Built locally as 1.0.604; dependency audits,
version check, signature verification and CTest passed (474 passed, 3 skipped).
Not pushed, released or launched.

Reviewed completed 603 telemetry session1788861469063600-0, 2P - 192x192 - SimCity,
seed132153238. Ended without result at85818cycles/22.8848simulation minutes;
Fremen and Mercenary both alive, with458803 and600912credits. SQLite
build/review-603.sqlite imported52277records, zero invalid/incomplete records,
audit no issues. Full session captured; report/tmp/dunecity-603-report.txt.
This is the new simple-hunt/full-match-memory controller, unlike the old601 review.

Findings (recommendations only; 604 changes the police ceiling alone):
- Heavy production constrained by strict allocation shares, not lack of cash.
  Final snapshots show8/24 and4/22 heavy factories busy. In last~5minutes,
 207/314 and217/296 heavy-allocation decisions had no positive affordable deficit;
  final samples show all available heavy candidates affordable but above quota.
  Military values only~65k/~71k against100k ceiling. Light allocation rose from
  4% opening to39.80%/34.55%, while each house had one light factory. Candidate
  refinement: make production capacity follow actual deficits, and permit useful
  heavy production to fill spare army capacity while light production catches up.
-534crime events spawned1602troopers,1597 subsequently destroyed.4059/4288 defence
  dispatch events (94.7%) targeted those exact spawned IDs. Local crime hotspots
  remain despite final mean crime12/15. Suggested refinement: district gang spawn
  pacing/outstanding-gang controls and persistent incident handling, leaving the
  Micropolis crime calculation intact. Earliest spawn1.41min on a populated map;
  do not call this a tiny-population regression without checking starting population.
- Combined reward/lost-value ratios:launcher4.280,ornithopter0.680,quad1.377,
  trike1.827,raider1.359. Reward includes weighted actual damage plus unitkillbonus.
  Final launcher allocation33.31%/42.93%,air6.45%/5.50%. Light shares reflect actual
  results across three separate types; plentiful gang infantry may influence the
  matchup mix, but victim-specific reward attribution was not established here.
-17ground hunts issued, typical groups~100–135units; defence response median1unit,
  max12/13, total7363dispatches (orders, not distinct troops). Both ended with40
  harvesters; only3/6harvester losses and~229.5k/~227.1k refined spice each. Heavy
  factories lost5/7, significantly fewer absolute losses than the much longer601
  game, but duration/map/player differences prevent a causal comparison.
- No frame-time samples in this game's ordinary log; do not claim an FPS gain.

# Full-match unit learning and safer factories — 1.0.603

Stefan explicitly requested keeping the entire game's unit performance rather
than fading old evidence. This supersedes the longer-confidence/recent-results
proposal in the 601 review below. Both DuneCity and Vanilla qBot now calculate
performance and exploration confidence from cumulative House combat rewards and
losses. Cost-weighted actual damage and the 20% unit kill bonus are unchanged.
Idle time never removes evidence or restores an unsuccessful type's uncertainty.
Opening availability rules and existing allocation constraints are unchanged.

PerformanceHistory retains the former PerformanceWindow binary save layout.
On the next allocation, authoritative saved House totals replace loaded decayed
values, so old compatible saves regain all recorded evidence. Save version stays
9832. Telemetry identifies lifetime inputs and policy
`lifetime-mix-safe-factories-v41`; cumulative raw fields remain available for SQL.

Factory placement no longer rewards proximity to the army rally. Heavy, light,
high-tech factories and infantry production prefer greater clearance from visible
hostile weapon zones, after avoiding recent loss sites and before city frontage
and compactness preferences. The heavy-factory redevelopment fallback uses the
same safety ranking. Existing legal placement, fire-zone, road and reactor checks
still apply. A constrained legal site remains usable; this introduces no new veto.
Clearance saturates twelve tiles beyond the existing firing buffer to avoid
needlessly chasing remote map edges. Existing rear preference also applies to
light and infantry factories.

A deterministic two-pass distance transform is cached with the existing two-second
threat map: O(map area), not enemy scans per candidate. Placement telemetry includes
`enemy_clearance_tiles`. The cache is derived and rebuilt after loading.

Verified local build 1.0.603: dependency audit before/after build, ad-hoc signature,
version consistency and CTest passed (474 passed, 3 optional skipped). Regression
tests cover full-match retention through long idle periods/save-load, migration
from a decayed saved window, all 512 threat arrangements on a 3x3 map, footprint
clearance at edges, safety ranking and constrained-site fallback. Built and committed
locally; not pushed, released, launched or tested in a live match.

# Completed 601 match review after simple-controller build

Session1788846458415706-0 ended cleanly at760548cycles/202.81simulation minutes,
local_result ended_without_result; all four houses alive with999999credits. This
was the old601 controller throughout, not a602 test. FinalSQLite import266721rows,
no invalid/deferred tails; auditclean. DBbuild/review-601-live.sqlite and final
report /tmp/dunecity-601-final-report.txt. Ordinary engine log confirms clean
teardown and reports metaserver analytics end recorded (not remote verification).

Detailed capture stopped at298448cycles/79.59minutes because the15/16 detail
allowance of256MiB was exhausted; final summary/session_end survived at203minutes.
No capture_limit event is emitted at this soft cutoff. Final totals cover the
full match; allocation/placement timeline claims stop at~80minutes.

Across four houses, credited damage+unit kill bonuses divided by lost replacement
value:launchers2.880 (6576built/6347lost),siege1.581(2042/1900),tanks1.080(4160/3999),
ornithopters0.363(4571/4548). Air losses cost2728800credits for991667reward.
Final heavy factories23–24 each; totals246built/151lost (Mercenary85/62).
Many unit types can have free/initial/captured spawns, so built minus lost is not
necessarily final count; do not treat MCV deployment losses as battlefield deaths.

At last detailed snapshots air targets3.60–4.71%; light targets12.06–20.34%.
Ordos recent air raw score~0.151 was lifted to0.408 by exploredScores uncertainty
prior. Recent evidence decays~2.6min half-life, making repeatedly poor performers
look uncertain again. Proposal:retain longer-lived confidence while using recent
performance for effectiveness, allowing exploration after actual changes without
repeatedly subsidising known poor matchups. No unit-specific hard cap proposed.

Civic investment60–80min:639rockets versus82police, so turret selection is working.
Crime means at last snapshots2/2/7/2, existing local hotspots explain some police
orders. Do not infer global police excess from final count alone. Prioritise602
battlefield test before further balance changes. Other proposals:protect costly
factory rebuilding from active fronts; keep compact periodic snapshots throughout
long games after detail sampling is curtailed. No additional gameplay changes.

# Simpler army control — 1.0.602

Stefan requested removing the formation controller after the live 601 game left
nearby troops gathering while cities were destroyed and slowed to ~15 FPS.
Reviewed session `1788846458415706-0`, DuneCity 192x192, seed316409388.
SQLite snapshot `build/review-601-live.sqlite`:99,644 events through cycle182298
(~48.6 simulation minutes), audit clean; one incomplete live JSONL tail deferred.
All four houses repeatedly assembled/regrouped. At cycle181945 house5 had42/150
members ready; earlier snapshots included185 troops waiting with131 ready and a
69-member force with zero engaged pursuing a target74tiles away. Code excluded
squad members from scramble defence and ordinarily limited city defence to a10%
reserve. These are direct causes of idle armies during nearby attacks.

The most recent1,000 FRAME SPIKE samples at review time had median79.35ms frames,
55.75ms unit work,16.4ms pathfinding,2.3ms rendering and442 queued paths (max633).
These are slow-frame samples, not an unbiased FPS average or proof that squad
logic accounts for all cost. AI itself occasionally spiked to291.2ms.

602 removes the assembly/formation/forced economic-target controller and its
unused policy/formation tests. Normal ground attacks issue native HUNT once to
available healthy troops, leaving current fights and human commands alone. No
readiness percentage, cohesion wait, shared base target or retreat-to-regroup gate.
Idle combat troops loosely gather around active harvesting centre of mass, offset
three tiles towards the nearest visible ground threat; base centre is the fallback
without working harvesters. Anchor search is bounded17x17 and cached30seconds,
with5tile position tolerance. No global flood fill or per-member connected slots.
Idle repositioning allows four orders per AI update, eight local candidate slots
per unit, skips stressed queues (>150), existing movement and queued destinations,
and never falls back onto an occupied centre. Human control and kiting remain.

Defence now draws from all usable AI troops, including hunters, when a building or
harvester takes a hit. It estimates the nearby8tile enemy force by health-adjusted
replacement value, requests125% strength, subtracts existing responders, then
recruits nearest compatible troops with deterministic ID ties. Engaged troops in
other fights and human commands are excluded. Local non-forced attacks permit
nearer target selection; AREAGUARD keeps the response local after the attacker dies.
Fixed base/escort pools are removed. Repeated hits are debounced2seconds per8tile
incident district, separately for air/ground. No artificial unit-number ceiling.
Also fixed the old damage callback sending pixel centre coordinates to a tile move.

SAVEGAMEVERSION9832 appends the small defence debounce map. Legacy squad save
fields remain readable; old squad orders are released once on the first AI update.
Rally order budget is local to each check, not unsaved cross-cycle state. Decisions
use simulation cycles, stable integer iteration and the existing deterministic
multiplayer path queue. No new random calls. Telemetry policy simple-hunt-v40 adds
`ground_hunt`, `defence_response`, `harvest_army_rally` in generic SQLite events.

Validation: local Release602 built, dependency audit passed before/after, CTest
passed (471 cases passed,3optional skipped), app signature and version checked.
Tests cover defence force sizing, existing responders, insufficient armies,
deterministic nearest-first selection and bounded blocked rally destinations.
Live FPS/combat and two-peer save/load still require runtime verification.
No game launched/restarted; no release/tag/push requested for this change.

# Windows portability correction — 1.0.601

The 1.0.600 tag was not published as a release: its Windows compiler expands
the legacy Windows-header `near` macro, which collided with a local distance
predicate. Renamed it to `withinRallyRadius`. All other 600 CI jobs passed;
the new all-platform release gate correctly blocked publication. Version601
includes all changes and maps described below; the failed600 tag stays intact.

# Squad crash, obstructed rallies and desktop release — 1.0.600

Session `1788836338773483-0` crashed on 2026-09-08 in the gather lambda
of `QuantBot::updateGroundSquad`. `hasATarget()` checks a stored object ID;
resolving a destroyed target can still return null. The shared engagement
check now resolves once and verifies health/attackability before reading range.
The crash regression exercises the null resolution and dead-object cases.

Ordos made nine assemblies but launched once; seven ended `assembly_obstructed`.
Its 68-member launched force stayed around (159,166) until `wave_complete`.
Old anchors survived city growth, blocked formation slots collapsed onto one
tile, and autonomous target changes fought repeated squad orders. Rally selection
now checks connected terrain and army-sized capacity, ignores temporary friendly
traffic, and searches reachable clearings out to 48 tiles from the base centre.
Members receive unique legal destinations; failed assemblies invalidate the rally.
Rally radius scales with formation size and readiness uses the same square area.
The main core advances with >=70% cohesion while detached units catch up. Local
combat/kiting still takes priority; shared AI attack targets are committed so
individual searches cannot repeatedly replace them and clear paths.

The three-minute timeout now measures lack of movement/combat rather than time
since launch. SAVEGAMEVERSION 9831 saves its progress cycle/location; old saves
start with an invalid sample and establish one on the first update. New decisions
remain simulation-cycle/integer based. Telemetry `connected-army-v39` adds rally
capacity, assembly readiness, compact count, distance and stalled cycles.

Default maps now include the user's unchanged CC-BY-SA DuneCity 192x192 and
4 corners 128x128 maps. Stable release publication requires tests and all three
desktop builds; missing Linux/Mac downloads are no longer ignored. Explicit
nan/inf text rejection fixes the two Mac fast-math configuration test failures.
Validation: Release build, dependency audit, full CTest and app signature pass.
Game effectiveness and live multiplayer remain for gameplay verification; no
claim of a full match simulation is made by the policy regressions.

# Coordinated army and investment fixes — 1.0.599

Implemented Stefan's approval of the six recommendations in AI-598-TACTICAL-REVIEW.md,
plus the police eligibility fix from AI-598-POLICE-REVIEW.md. His correction overrides
the proposed blanket reactor city buffer: R/I/C may remain next to reactors.

- Heavy/light/air production use one funded army target and live+queued military
  accounting. Vehicle shares exclude committed infantry. Infantry accepted orders
  also debit the planning budget and respect the military cap. Air availability at
  the engine air-unit cap removes its share from the plan. Construction backlog
  calculations use the same vehicle plan, avoiding phantom factory demand.
- A saved ground squad gathers 80% of eligible healthy AI-controlled combat units,
  including existing hunters/AI forced orders. Fixed base rally, not harvester
  clusters. Launch at 85% gathered; after 90 seconds permit a >=70% original core,
  otherwise abort. Minimum6 units/3000 initial value. Stragglers stay for the next
  wave. Front-runners stop to wait; fragmented unengaged formations gather again.
  Local combat units override distant economic objectives. Shared objectives have
  20-second persistence, evaluated on deterministic two-second simulation intervals.
  Regroup below half strength or after three unengaged minutes. Base and harvester
  reserves are10% each; escort assignments stick to a surviving harvester.
- Economic targets include harvesters while spice remains, and production/power/city
  buildings thereafter. Endpoint and straight-corridor danger are weighted relative
  to force value; no absolute lightly-defended veto for a full squad. This corridor
  estimate is not a proof of path safety. Removed gameplay retargeting from telemetry.
- Network-replayed human unit commands create saved control leases. Squad gathering,
  ordinary unit handling, scramble defense and air strikes respect these; protection
  lasts at least120seconds and continues while the manual unit is moving/forced/engaged.
- Repeated heavy-factory losses accumulate placement danger for15minutes rather than5,
  with rear-placement preference for heavy/high-tech factories. Other losses retain
  five-minute influence. Existing safe/recovery placement handling remains.
- Reactor clearance applies both ways to reactors, construction/heavy/high-tech/repair
  yards, refineries, IX, palace and starport. R/I/C and low-cost services remain allowed.
- Unit allocation keeps a per-type uncertainty prior and recent combat evidence,
  decaying1/8 every30 simulation seconds (~2.6-minute half-life). No named-unit minimum.
  Recent and lifetime reward/loss inputs plus final shares are logged. Old saves seed
  the new window from available lifetime evidence; subsequent samples decay normally.
- Civic purchases need nonzero actual crime reduction plus sufficient weighted
  economic/growth utility. Raw cumulative crime reduction no longer bypasses cost.
  The emergency exception needs >=32 points of relief above191 crime. Coverage and
  Micropolis crime formulas are unchanged. Existing military turret paths remain.
- Routine city_growth_sample/harvest_rally_move_order observations sampled1/8; actual
  level changes retained. Large captures reserve1/16 for game_summary/session_end/
  simulation_exception. Detailed capture can cease before the end, but accounting
  continues and end summaries remain writable. Policy tag coordinated-army-v38.

SAVEGAMEVERSION9830 stores squad state/membership, manual orders, escort assignments,
placement-loss history and recent performance window; older saves default these fields.
All new decisions use simulation cycles/integer math, stable iteration and the existing
seeded commitment choice. No multiplayer runtime test was performed.

Validation: build and app ad-hoc signature passed; dependency audit passed before/after.
Bundle reports1.0.599. CTest473:468 passed,2 pre-existing parseDouble("nan") failures,
3 optional asset/atlas skips. Eight added tests cover assembly, concentration,
production ledger, exploration, reactor rules, civic purchase value, capture reserve
and recent-performance save/load. Logs: /tmp/dunecity-599-build.log and
/tmp/dunecity-599-tests.log. No game launched or restarted. Real-map squad navigation
and effectiveness need the next match; compile/policy tests do not prove combat wins.

# Player-centred metaserver analytics — 1.0.598

The structured payload now follows the existing multiplayer start model: map,
mod, version, and one row per actual player with display name, house, team and
controller. End events add house-owned results to each participant, including
spice, totals and sparse `[item id, name, kind, produced, killed, lost]` rows for
every unit or structure with activity. QBot rows retain final production weights
and combat score components. The payload schema is v2 and the bound is 64 KiB;
zero-only item rows are omitted. The metaserver normalizes item rows into
`analytics_player_items` and migrates existing SQLite databases in place.

Legacy multiplayer clients below v1.0.598 still create start-only rows from the
existing `House: Player` list. Newer multiplayer announcements no longer create
an additional legacy analytics row because the structured start/end reporter
owns that match. Python fallback storage was verified for start/end upsert,
player identity, item rows, QBot rows, and migration from the old player table.

# Metaserver analytics retry — 1.0.598

Production accepted both the start and end summaries for the completed v597
match. The preceding match had one start request hit the client's three-second
HTTP timeout with zero response bytes, while its end summary succeeded. Twelve
production health requests then completed in 0.814–0.936 seconds, so this was a
transient transport/server delay rather than an ongoing SQLite outage.

Compact match start/end writes now retry once with the same opaque match ID and
the same three-second bound. The metaserver's upsert makes this idempotent even
when the first request completed after the client timed out. Both attempts stay
on the analytics worker and remain independent of simulation and multiplayer
lockstep. The end payload now fills the existing SQLite damage-value and kill-
bonus columns separately as well as their combined reward; these fields were in
the deployed schema but had been omitted by the client serializer. No credentials
or local decision logs are added. Multiplayer display names are retained in the
participant rows, matching the existing game-start announcement.

Version 1.0.598 builds and signs successfully; dependency records are complete.
Ctest reports 460 passed, the two known `parseDouble("nan")` failures and three
skips. The retry path itself awaits a real transient failure in a future game.

# Cash-first city MCV expansion — 1.0.598

Session1788799572693304-0 v597: both houses stayed at two yards with
~100k–140k credits because only one R/C/I valve was positive. A second positive
valve raised the target to six; both reached six within ~40 simulation seconds.
User rejects demand gating. Wealth now sets minimum yard targets5 at20k,
6 at50k,8 at100k after existing production commitments, even with no positive
valves. Low-cash demand targets remain. City MCV production/unlock upgrades
now precede extra harvesters and generic upgrades, preserving working cash
for a tank, needed harvester/refinery and minimum1000. Queued/live MCVs count
towards capacity; engine ground limits still apply. Vanilla unchanged.
Removed old late city MCV branch. Telemetry policyv37 adds city_mcv_cash,
city_mcv_working_reserve and city_cash_construction_capacity order/unlock rule.
No new persistent state or RNG; no game restarted.
Build, dependency checks and signature verification passed. CTest460 passed,
2 existing parseDouble("nan") failures,3 atlas skips; no new failures.

# Power-demand forecast and earlier turrets — 1.0.597

Latest completed session `1788797915444105-0` v596: houses0/6/7/3 ordered
85/79/74/78 windtraps and0/0/1/1 nuclear plants. Incremental reserve top-ups kept
the immediate gap below reactor break-even even with tens of thousands of cash.
Generator comparison now adds a two-minute demand-growth forecast, sampled each
30 simulation seconds, bounded by current demand and zero for flat/falling demand.
Forecast sample cycle, previous demand and projected growth are saved as of
SAVEGAMEVERSION9829 (old saves default to empty forecast). No wall-clock/random
state. Telemetry includes forecast_growth/seconds, nuclear site availability/price.
Offline snapshot approximation found13–22 financially eligible choices for houses
0/6 instead of zero; this is not a live placement test or predicted order count.

User-requested rocket land-value bonus halved30->15, radius unchanged. Shared
simulation/AI helper and expected tests updated. Profitable crime-reducing R/C
turret slot now checks before reserved police/service, every three non-service
orders. Exception: if >half developed zones are dangerous, strongest-service
comparison retains priority. Still requires some actual crime reduction and R/C
tax gain; zero-crime civic turrets remain forbidden. Police stacking unchanged.

Build/dependency/signature checks passed; no game restarted.

# Power choice, turret returns and spice workers — 1.0.596

Implemented user-approved v595 recommendations. City generator choices now use
current shortage + queued consumer demand + existing city reserve. Compare cost
of windtraps needed against reactor price, and count disjoint legal wind sites
to detect land shortage. Nuclear must leave working cash for a tank, needed
harvester and pending refinery expansion; actual brownouts may use this reserve.
All city power orders pass through shared final choice; vanilla unchanged.
Logs `city_generator_choice` demand, cash/reserve, wind count/sites and choice.

City harvester target no longer depends on number of combat vehicles. It is
min(map-share sustainable target, 3 * actual refineries). Orders need price plus
one tank's cost rather than price+1000, still one per build pass with actual
engine capacity checks. Other factories retain combat production; vanilla's
existing 1000 cash threshold is unchanged. Existing city bootstrap/refinery
expansion remains in effect.

Added an early profitable rocket-turret investment slot every four non-service
orders after core economy/factory bootstrap. Requires land-value tax plus
conservative growth tax alone to repay construction/upkeep/power/placement cost
within one year; crime utility cannot qualify this slot. Cash reserve and queued
costs are protected. Uses existing marginal coverage/park calculations and road
junction placement, so existing/planned services diminish additional benefit.
Police still competes for emergency crime and ordinary service orders; no hard
ban. Structure selection rule `turret_land_value_investment` identifies orders.
User correction before release: all civic turret candidates must reduce some
actual crime (zero reduction is rejected). Early land-value slots must additionally
improve R/C value. Ranking adds a 50% preference on the R/C share of forecast tax
gain; this is placement utility only, not extra reported income or simulation tax.
Actual payback checks use unmodified income. Candidate telemetry includes
`res_com_tax_gain`. Military threat-response branches remain distinct.

Build/dependency/signature checks passed. New policy tests cover generator
cost/space/reserve choices, harvester capacity and turret tax-only payback.
No game restarted; runtime effectiveness remains to be evaluated next game.

# Nuclear balance and completed-game review — 1.0.595

User set nuclear price2000 and nominal output2000. Updated source default plus
installed `mods/dunecity/ObjectData.ini`; other installed mods untouched. Existing
health-scaled nuclear output remains, windtraps stay300credits/100power independent
of damage while alive. Version reseeding will carry source defaults into DuneCity.

Completed session `1788795517885598-0` ran v593 (not v594 opening). White slot3 is
Fremen. At minute5 its land value68 vs orange Mercenary41, with identical population
and almost identical spice income. Earlier rocket service orders (minute9.39 vs
12.27) preceded value219/crime33 at minute12 vs orange55/210, producing much higher
tax income. At minute25 white had14ref/40harvesters/7HF vs orange5/13/3. Final white
city net241814 plus spice148545; orange125617+61865. White lost0ref/0HF/7harvesters;
orange11/5/21. All houses alive when user ended game. Purple Rebels also strong,
with18HF and124launchers at end; do not call this a confirmed white win.

Recommendation ONLY (not implemented): choose generators by actual/queued near-term
power shortfall, free legal land, industrial demand and available cash after
refinery/harvester/military reserves. Wind suits small incremental demand and
industrial jobs; nuclear saves land and wins direct capex once >=7 windtraps would
be needed at current prices. Preserve nuclear blast clearance/health risk. User's
claim equal power per credit is incorrect: wind3credits/power, new nuclear1.

# Spice-first city opening — 1.0.594

Live v593 session `1788795517885598-0`: all four houses ordered only one
refinery despite ~193000 spice share each. The first factory saving rule was
too early; radar/light/power spending delayed the factory until cycle~16000
while only one refinery supported income. City opening now reserves for up to
three refineries (bounded by sustainable map-share harvester target), ahead of
R/C/I seeding and factory prerequisites. Refineries provide initial harvesters.
Missing legal sites do not lock planning. Queued refineries count. Optional
power-surplus construction waits until this opening is complete; actual power
shortage recovery still precedes it. Added `city_spice_opening` telemetry with
target/count/spice share/price/funding. Vanilla unchanged. Policy tests cover
rich/scarce/no spice and a one-harvester limit. No game restarted.

# Starter survival and completed power placement — 1.0.593

Session `1788794508386046-0` confirmed tiny-settlement gang outbreaks: first
house4 outbreak cycle8346, residential population40 (800 displayed residents),
local crime250. Custom unrest now requires 5000 displayed total population per
owner, resetting progress below it. Micropolis crime calculations remain intact.
Outbreak events include population/minimum_population; boundary tests added.

House4's completed reactor was rejected seven times at (12,183): legal footprint
and blast clearance but threat300–500. Completed generators now fall back to
the least exposed legal site, maintaining blast spacing, roads and zone access.
Logs `placement_power_recovery`. Ordinary planned construction keeps its threat
veto. Primary city deficit/reserve power rules require funds for nuclear orders,
otherwise using windtraps instead of tying up a poor starter yard.

House5 ordered 75 R/C/I before its first heavy-factory order at cycle62495;
radar only cycle58795. Cheap zones consumed cash below infrastructure thresholds.
After one R/C/I seed and refinery, city AI saves actual price for an available,
placeable heavy factory or radar/light prerequisite, ahead of further zones or
civic services. Logs `city_bootstrap_reserve`. Extra city refineries now need
their price plus300 instead of4000, still requiring fleet demand and a factory.

Build, dependency checks and bundle signature passed. Ctest:455 passed,2 known
parseDouble("nan") failures,3 skipped (460 cases). No game launched/restarted;
priority changes still need live gameplay validation in the next test.

# Early crime and refinery retreat — 1.0.592

Session 1788793767206934-0 (v590, 4P192 DuneCity) showed average crime250
at cycle3800 with only 40 residential population. Density was incorrectly stamped
as overlapping radius2 halos. Now uses Micropolis populationDensityScan exactly:
point-set source min254, three non-dithered centre+cardinals /4 passes (clamp255),
then byte-map doubling. Map block2 is retained. No invented population crime cap.
Reference scan.cpp populationDensityScan/smoothDitherMap; default donDither=0.

Harvester safety logged 21 redirects; several vehicles carried 350+ spice with
current danger0 and old destination danger300. Visible threats were triplets of
troopers spawned by crime. Foot infantry no longer adds to harvester danger (still
counts for tactical defence). Threatened/unsafe-destination harvesters prefer a
safe owned refinery/dropoff using existing network movement commands. An active
safe unload trip retains control instead of being overwritten by spice searches.
If no safe refinery corridor exists, prior safe-field/dispersal fallback remains.
No game launched or restarted.

# Shared civic investment selection — 1.0.591

Replaced the city service picker with a shared police/rocket search, evaluating
legal police footprints and turret junctions independently. The prior fallback
amenity picker no longer bypasses this comparison. Essential military/power
priorities are unchanged. Each candidate compares credit-equivalent benefit
(occupied-property crime removed + one-year tax gain + conservative growth tax
+ threat-based defence value) against construction + funded annual upkeep +
power cost + a separate police overlap placement penalty. Emergency allocation
requires >=100 aggregate crime reduction; low-crime amenities can qualify with
positive net return without any immediate crime reduction.

Tax gain uses actual total city population, tax rate and property-average land
value sensitivity. Park prediction matches the existing coarse-cell accumulation
in stampFalloff, saturates at 250 and includes reserved amenity projects. Police
can earn tax credit by removing the existing >190 crime land-value penalty.
Growth is explicitly an estimate: up to 25% of one demanded additional level,
scaled by value improvement, only while powered and pollution <128. Defence is
a weighted estimate from visible armed enemies within 12 tiles of heavy factories,
repair yards and reactors (reactors doubled); existing/queued turrets discount it.
No enemy threat means no defence credit; unpowered turrets receive none.

Both best eligible candidates and the winner are logged as city_service_candidate
and city_service_investment, including every score/cost component. Policy tag is
civic-investment-v36. Integer ordering and deterministic tile traversal preserve
multiplayer behaviour. No game launched or restarted.

# Crime construction allocation and police spawn limits — 1.0.590

For populated, living owned R/C/I zones, dangerous means crime >=192. At >=25%
dangerous, reserve one in four construction orders for crime services; above 50%,
one in two. Essential power/bootstrap recovery still runs first. A saturating
non-service order counter advances only on accepted non-road/non-slab orders,
resets on a selected crime service, and is persisted in save version 9828 (older
saves default to immediate response). Existing fallback service selection remains.
Chosen crime-service location is retained instead of reselecting a different
defence site later. Telemetry includes the zone counts, interval and counter.

Police cooldown is twice the palace cooldown. Police batches stop at 100 military
units owned by that player (transport/harvesting/MCV/ambient excluded) and retain
house limits. Sidebar overlay reads "Unit limit reached" while capped and clears
automatically. The suggested 2000 map-wide cutoff was rejected and removed.
QuantBot Brutal-controlled houses also check
each unit against the same military valuation as the production allocator,
including earlier spawned batch members. At/above the limit nothing spawns;
fully blocked batches retain readiness. No game restarted.

Current Twin Cities session 1788790735257321-0 is still 1.0.589. Snapshot during
analysis: Harkonnen 49 police/5 rocket crime selections; Ordos 60/10. Old comparison
rejects zero-crime-benefit turret sites even with amenities, and uses raw amenity
points rather than projected tax or upkeep. User requested analysis, not a new
turret-versus-police balance change. Keep that comparison intact for now.

# Crash repair — 1.0.589

The September 7 23:55 crash was a stale-object ABI mismatch, not police diffusion.
macOS report `dunecity-2026-09-07-235532.ips` identifies the fault at
CityStatsBox::update +3032 (the in-game signal stack misleadingly reports the
previous call return address +1852). Disassembly reads pollution vector data at
CitySimulation+0x4a8 while the rebuilt simulation stores it at +0x5a8, following
the new environment summary fields. StructureBase.cpp.o was two hours old and
contributed the obsolete inline CityStatsBox implementation. Ninja recorded zero
dependencies for it and five other objects, so header changes did not rebuild it.

Performed a complete clean build. Verified sidebar now uses pollution+0x5a8 and
land value+0x5c0, matching simulation initialization. All existing object dependency
records are populated; bundle signature passes. Added scripts/check-build-deps.py
and required pre/post-build checks in AGENTS.md. Guard tested against a deliberately
empty Ninja dependency record. Crash log/binary preserved in /tmp/dunecity-588-*.
No game launched. Gameplay confirmation remains for the next user test.

# Police diffusion — 1.0.588

Replaced linear police halos with Micropolis source accumulation followed by
three `(center + neighbours / 4) / 2` integer smoothing passes. Full source
strength is 1000, turrets 150; funding, missing power, and missing perimeter
road scale the source, emitted at the first road. Grid uses six tiles instead
of eight to preserve two zone-plus-road pitches (2+1 here, 3+1 in Micropolis).
Actual overlapping sources are summed before smoothing, never penalised.
AI spacing penalty remains placement-only. Its isolated-source estimate uses
the same diffusion/boundaries, with small rounding differences versus combined
sources. No running game relaunched. Prior 586 build failure was corrected:
missing TextManager include. Budget summaries/sidebar categories are in bundle.

# Full-capacity heavy-factory allocation — 1.0.579

Live vanilla session `1788769143717332-0`, 5P128 All against Atreides, showed
Atreides with 6k–74k spendable credits and 8k–55k military against an 80k cap.
Of 7,109 heavy allocation decisions, 6,805 were
`no_affordable_positive_deficit`, leaving factories idle because the normal
one-unit mix horizon considered a proportionally balanced small army complete.
When that happens below the military cap, the allocator now uses a deterministic
doubling expansion horizon, selects the largest affordable configured-mix
shortfall, and fills the lane. It repeats in stages until the cap or resources
become the constraint. `expansion_horizon`, `expansion_fallback` and candidate
`expansion_deficit_scaled` make the reason directly queryable in telemetry
version 6 / policy `full-capacity-allocation-v31`. No game launch, commit or push.

# Main-force harvester strikes — 1.0.578

Removed the below-threshold 2–6 unit recovery raid. At a qualifying attack
window, a stateless multiplayer-safe roll selects a safe exposed enemy harvester
about one third of the time; every available main-force unit receives a forced
order against it. The base and harvester-escort reserves remain assigned. A
turret-covered field, returning/non-harvesting harvester, or local defender value
above 2000 falls back to the ordinary HUNT wave. The force budget is the entire
available force for a strike and the existing deterministic commitment percentage
for a hunt. `harvester_strike` events, five-second progress/outcome samples and
SQLite operation labels make full-force outcomes queryable; old captures remain
`legacy_small_raid`. No game launched, commit or push.

# Neutral radar visibility and light-raider tactics — 1.0.577

Neutral now uses a dedicated bright cyan radar marker in every mod, instead of
the vanilla grey palette entry that merges into rock. The override is minimap
only; Neutral sprites, UI and lobby colour mapping stay unchanged.

QuantBot trikes, raider trikes and quads now evade an armoured tank that is
actively targeting them within its weapon range, retreating two tiles beyond
that range. A tank hit uses the same immediate retreat even between AI updates.
While hunting and not on a forced command, light raiders choose local visible
launchers, harvesters, light raiders and infantry/troopers within 12 tiles over
their normal target. Decisions are deterministic and logged as
`light_raider_evade` and `light_raider_target`. No game launched, commit or
push.

# Approved final573 follow-up — 1.0.576

User approved recommendations 1,2,4,5 and explicitly declined 3. Implemented:
custom-match main-wave minimum actual dispatch of 6 units / 3000 value with
15-second retry; stable harvesting anchor (30-second dwell, 25% larger cluster,
immediate danger/depletion override); largest affordable positive HF allocation
deficit with queued units and one-funded-unit horizon, no unconditional tank
fallback; raid members/rewards/losses/outcomes and sampled duration logging.
Campaign dispatch thresholds are preserved. No ornithopter gate change: still
planning money >1200. No other air/production strategy changes.

Save format 9827 adds QuantBot rally selection cycle after supportMode. Older
saves expire the initial dwell. Pure fixed-order integer policies use no RNG.
Raid observations are runtime-only and do not affect decisions. Game teardown
flushes active raid outcomes before object cleanup and logger shutdown.
New SQLite views and details in AI-DECISION-TELEMETRY.md. No gameplay launched.
Built 1.0.576. C++: 444 passed, 2 existing parseDouble("nan") failures, 3 skipped.
Python analytics: 11 passed. Version metadata, bundle and codesign verified.
No launch/restart, no commit/push.

# Final573 match analysed

AI-573-FINAL-ANALYSIS.md;49,427events,auditclean,finalsummary/session_end.
LocalOrdosdefeat;Atreidesalsoeliminated,Sardaukar58,650army/25harvestersdominant,
Neutralalive1390army/2harvesters. Economyrefined218789Sardaukarversus113325/160099/
173800;harvesterloss27vs63/55/48. Gunselection0;defeatedcarryalls0;kitingcommandsactive.
19recoveryraidorders,outcomesnotlogged. Sardaukarrally1581evaluations543distinct
suggestions49>10tilejumps;do notclaimallareexecutedrelocations. Mainwaveeligibility
bug:25,350armybut900eligiblecaused1unitwave,41forcedground. Recommendations:
minimumeligiblemainwave;stableharvestanchor;perclusterescortmetrics;costbasedairgate;
deficit-basedHForders;raidmember/outcomelogging. No newgameplayeditsinanalysis.

# Ornithopter live review and decision diagnostics — 1.0.575

User askedwhyfewornithopters. Currentmatch573session1788753759241895-0,vanilla
4corners seed1137063083. AI-573-ORNITHOPTER-REVIEW.md capturesstable20minsample.
Air reward/lossAtreides.36,Ordos.57,Sardaukar.45,Neutral.70; targets~9.6/14.8/7.2/6.8%.
25/39/19/28acceptedairordersby20min. Actualaircountslowbecauseoflosses,notzeroorders.
Code subtractsqueuedcommitments/priorordersand>=2kreserve, thenrequires>1200for600
orni; carryallsandupgradebranchhavepriority. No gameplayretuningin575.
Addedperiodicair_production_decisionwithprecisereason,planningbudget/threshold,
shares/committedvalue,carryallcount/target,andproducerstate. Availableonlynew575runs.
Refactoredbranchbooleansmatchpriorconditions. Built575,440C++pass,2existingnanfail,
3skip;10Pythonpass,version/bundle/signatureverified. No gamelaunch/restartorcommit.
SQLitebuild/review-573-live.sqlite importedlivecapture; onepartialtaildeferrednormal.

# Required-power display restored — 1.0.574

WindTrapInterface always shows numeric Required alongside Output and Produced,
including vanilla. RemovedPower:Notrequiredreplacementwhichhidactualdemandwhen
rocket-turretpowerwasenabled. Displayonly; simulationpowerpolicyunchanged.
Build574, version/bundlesignaturecheck; no additional testsforlabel-onlychange.
No game launch/restart, no commit/push.

# Constant windtrap output — 1.0.573

User clarified damage must not reduce windtrap generation; only DuneCity nuclear
plants scale with health. generatorOutput helper returns full nominal whilealive
for WindTrap, AdvancedWindTrap andScoutpost; nuclear scalesonlyisCitySimEnabled.
Zerohealth removesoutput, preservingdeltaaccounting/destructorcleanup. Removed
QBot repairDamagedWindtraps power-recovery specialrule; ordinaryrepairsremain.
Game::load rebuilds producedPower afterallobjectsload fromallfourgeneratorclasses,
so olderhealth-scaledtotals do notremainstale. Powerdemand/saveformatunchanged.
Built573;440C++passed,2preexistingnanfailures,3skip; source/bundleversion/signature
verified. Unitcoverageincludesdamaged/full/dead/noncityreactor andzero-double-removal.
No interactive gameplay/save-load smoke test performed. No game launch, commit/push.
Policyconstant-windtrap-output-v29. Prior572rocketturretpowerexceptionretained.

# Vanilla rocket-turret power and defence preference — 1.0.572

User clarified vanilla: when rocketTurretsNeedPower is on, AI must supply power;
otherwise one windtrap suffices. Ordinary vanilla power bypass stays unchanged
(no production/radar penalty or power upkeep). RocketTurret checks actual global
produced>=required when toggleon, independentofHouse::hasPower bypass; historical
campaign/skirmishAI exemption is retained only for power-required nonvanilla modes.
QBot turretbuffer now respects toggleevenvanilla, restoresgeneration for existing/
queuedrocket turrets if actualpowerdeficit. Existing225buffer and onegeneratorpending
checks retained. Telemetryrocket_turrets_need_power distinguishes vanillaexception.
Vanilla gun turrets came from separate ground_defense fallback. It now chooses
rocket turrets if enabledandtechlevelavailable, never substitutes gun turrets while
waitingforCYupgrade/power. Gunsremain only belowrocket tech or rocketdisabled.
No removal of existing guns. No citycrime/economychanges, no newRNG/saveformat.
Policyvanilla-rocket-power-v28. Built572,439C++passed,2preexistingnanfailures,3skip;
source/bundleversionandsignatureverified. No game launch; no commit/push.

# Harvest-area main force and demand-based production — 1.0.571

User corrected569: extra high-tech needs priorities, not arbitrary cap; only
all factories making ornithopters should justify more. RemovedhighTechFactoryTarget.
needsProductionLane checks completed=committed, >=75%busy, funded unit deficit,
credits>=economyreserve+factoryprice+1000. CY computes next-wave heavy/air deficits
from allocation fractions versus actual+queued units. heavy_unit_backlog rule
comes after essential economy/earlyfactory rules and before optional Starport/tech.
Existing24HFceiling remains; missing first-air unlock remains as before.
Extra air requires ALL completed HTactively makingornithopters (notheld/upgrading),
no pendingHT, funded air deficit, and no heavybacklog unlessHFceiling reached.
Carryall queues alone cannot expandair. builder_status logs bothdeficits/backlogs
and high_tech_building_ornithopters. Snapshotbusycounts update each build pass.

findSquadRallyLocation picks safe adjacent tile beside densest radius6active
harvesting cluster, ignoring returning/inactiveharvesters; stabilizesnearoldanchor.
Refresh500cycles, resting combat units spread5x5nearanchor. Active targets/HUNT/
retreat/forced units andbase/escortroles preserved. Existing fallbackwhen no safe
workingfield. No path guarantee: sampled tile and normalpathfinder governroute.
At a normal attack window, `shouldUseMainHarvesterStrike` deterministically selects
an exposed, actively harvesting enemy harvester about one third of the time. It sends
the entire currently available main force (base and harvester-escort reserves remain)
with forced target orders; otherwise it launches the ordinary HUNT wave. It refuses
turret-covered fields and escorts worth more than 2000. The policy is stateless and
does not consume the multiplayer RNG stream. Telemetry records `harvester_strike`
and five-second progress/outcome samples; SQLite labels older small raids
`legacy_small_raid` and new operations `main_force_strike`.

Built571;438C++pass,2existingnanfailures,3skip;10Pythontestspass. Version/bundle/
signatureverified. No gameplay launch/test, no commit/push. Priorheartbeat paused.
Actualmatch behaviour stillneeds nextmatch validation. Saveversion9826unchanged.

# Final568 analysis and defeated carryall cleanup — 1.0.570

User exited568 with Sardaukar winning. Actual end result ended_without_result;
Atreides defeated, three houses alive. Final46,302 events audited; rewards valid.
AI-568-FOUR-CORNERS-ANALYSIS.md includes all-house performance/value/kill bonuses,
allocation histories at5minute intervals, economy and recommendations.
Engine log copied build/review-568-final-engine.log; heartbeat paused after final.
Carryall::update now destroys carrier when owner !isAlive(), returning immediately.
Uses normal destruction/bookings/cargo cleanup, also inherited ChemicalCarryall.
No recursive iteration over global units in House::lose. Team0 remains alive under
existing rules; an owned combat unit/MCV still prevents defeat as before.
Built570, version/bundle/signature verified;436 tests pass,2pre-existing nan failures,
3skip. No gameplay launch or runtime defeat test performed. No commit/push.
Recommendations are not implemented automatically;569 factory/kiting fixes included.

# Air capacity and defender kiting — 1.0.569

Live568 session1788747908951557-0 is vanilla4corners seed78385311, four Qbots
Atreides/Sardaukar/Mercenary/Neutral. By~12minutes each had12 heavy factories,
3–4 high-tech,15–17 refineries; cash fell to2–3k. No evidence to raise HF cap
again from this snapshot. Air expansion had no ceiling, only all-busy check.
Now high-tech target clamp(1+completedHF/8,1,3); queued high-tech counts against
it. First-air unlock remains unchanged. builder_status adds target/busy.
Defender/escort role early return bypassed existing launcher/deviator kiting.
Close ground-target check now runs before role exclusion. Existing range-2
trigger and Easy exemption retained. Stationary units no longer suppress kite
because of stale destinations; only genuinely moving-away destinations do.
Existing path-queue stress guard and retreat geometry retained. combat_kite
records issued moves for subsequent analysis;568 cannot show these new events.
Policyair-cap-kiting-v26; no save or random-stream changes. Monitor heartbeat
review-next-dunecity-match active every5minutes for exact568 session, quiet unless
material new findings; DBbuild/review-568-live.sqlite, statebuild/next-match-monitor.json.
No game launch/restart/control, no commit/push. Built569 for next user launch.
Validation:436 C++cases pass,2 pre-existing parseDouble nan failures,3skip;
10 Python tests pass. Version/bundle/signature verified. LiveSQLite31,337events
auditclean;6,192 reward rows,zero component-total mismatches;1,152 allocationrows.

# Value damaged plus20% killing-blow score and SQLite — 1.0.568

User approved credit-weighted actualdamage plus20%unitcostkillerbonus, thenaskedall
statsinSQLite. CombatReward.h calculatesclippedHPvalue, unit-onlykillbonus,noallied/
healing/deadobjectreward. ObjectBasecaptureshealthbefore/after, creditsattackingtype
once. Structuresgetdamagevaluebutno20%unitbonus. Deviatorconversionpreservesold10/100%
creditproxyseparatelywithoutkillbonus. House rewardstatsintegercreditmilli+HPmilli,
hits/killingblows. QBotusesreward/lostvalue,3kcreditrewardlearningthreshold; rawdamage
stilllogged. Existingvanillablend/capsandopeningavailabilityretained.
Save9826 persists rewards + rawdamage + pertype losses (previouslynotpersisted).
Olderloadsfreshrewardhistory; streams gatedonloadedversion. House summaryserializer
acceptsObjectDataparameter so Game destructor doesnotdereferencepossiblynullglobal.
Policyvalue-kill-reward-v25. All-houseperiodicsnapshots+game_summaryincludecombat_rewards.
SQLcombat_reward_samples/final andunit_allocation exposeallcomponentsandshares.
Noextraeveryhitevents. UpdatedAI-DECISION-TELEMETRY.md hascolumnsandexamplequery.
Do not claim old564captureincludesnewrewards. Pythonanalytics10testspass; old559DB
upgradedwithviews,auditcleanandnonewrewardrows(noinventedhistory).
Built568; CTest434pass/2existingnanfailures/3skip; Pythonanalytics10pass.
Source/bundleversionandsignatureverified; logsbuild/combat-reward-568-{build,tests}.log.
No game launch/restart; no commit/push.

# Tech-aware opening mix — 1.0.567

User wants small high-tech trike/quad opening ratios and tech/availability-dependent
defaults, plus advice on improving learning. UnitMixPolicy::openingMix allocates
light15%attech4,8%at5–6,4%at7+,30%below4whenheaviesalsoavailable. Onlylightavailable
means100%ofavailablevehiclemix; nofactoriesmeansallzero. Quadsweight2,trike/raider1
withinlightshare. Heavy/airhouseconfiguredratiosrenormalizeoveravailabletypes.
ActualownedLF/HF/HighTechbuildlistsdetermineavailability(includesupgradelocks);
CHOAMignored. Baselinesrefreshasproductionunlocks/disappears. Learningretains566
scoringandvanillablending; scoresmaskedforunavailabletypes. No hard4%learnedcap.
Infantrydifficultyquotaunchanged. Policytech-aware-opening-v24; unit_mixtech_level,
opening_light_bps; mix_inputsavailable/opening_bps. Deterministicintegerhelpers,
noRNG/savechanges. Testscovertechbands,unavailabletypes,missingproducers,upgrades,
zeroconfigfallbackandexactsharetotal. Built567; CTest432pass/2existingnanfailures/
3skip. Source/bundleversionandsignatureverified; logsbuild/tech-opening-567-*.log.
No game launched/restarted.
Algorithmrecommendationsareproposalsonly: AI-ALLOCATION-IMPROVEMENTS.md.

# Adaptive trikes and quads — 1.0.566

User explicitly requested trikes/quads participate in damage-versus-loss allocation.
QuantBot now allocates one normalized8-type vehicle/air mix: tank,siege,launcher,
specialgroup,ornithopter,trike,raidertrike,quad. New UnitMixPolicy.h usesint64scores
(damage*1e6/(lostreplacementvalue+oneunitprice)); specialgroupkeeps700prior.
Negative damage clamps0, disabled/tech-ineligible types get0weight. Learningdamage
nowincludesSonic/Deviatorandlighttypes, previouslyomitted. Zero-scorefallbackavoids
olddividebyzero. Openingdefaultlightshare12/16/20/24%difficulty dividedacrossenabled
lighttypes, deductedfromheavy/airdefaults; infantryquotaremainsseparate.
After3000damagelearnsall8together; vanilla50/50baselineblendand25%aircapretained,
80%singletypecapwherealternativesexist. Othermodesunblendedlearningstillapplies.
Lightfactoryselectshighestpositivevalue-deficitamongavailabletypes(countsqueues),
notfewestowned; onlyprelearningminimum2bootstrap. Citylightproductioncancontinue
withHFpresentwhenadaptiveallocationcallsforit. Acceptedordersdeductplanningcash.
ExistingHFopportunistictankfallbackremains; these aretargets,notexactarmycomposition.
Policyadaptive-light-vehicles-v23. unit_mix adds allocation_types=8, trike/raider/quad
bps, light_vehicle_bps,total_damage,mix_inputs(damage,lost_value,score). All8bpssum10000;
older5fieldscoveredheavy/airalone. rawOrni nowrawuncappedshareacross8beforeblend.
Testscovercostefficiency/losses,zero/negativedamage,openingdefaults,disabledraider,
normalization/caps,queuedvalue-deficitsandreproduciblepeerresults. Built566;
CTest430pass/2existingnanfailures/3skip. Version/bundlesignatureverified. Logs
build/adaptive-light-566-{build,tests}.log. No game launch/restart, no new save/RNGstate.

# First High Tech Factory priority — 1.0.565

User reports slow High Tech Factory. Live564 session1788709485434043-0, vanilla
All against Atreides seed1806040624: firstHeavyordered1.81min, MCVs2.59–3.81min,
16heavyordersbeforefirstHighTechat5.44min. Our earlyfactorypriority delayedunlock.
565 customvanilla now selects firstHighTech afteranactualHFexists, beforeexpanding
refineries/repeatedHFpriority. Checksaffordability, actualtech/placementavailability;
queuedHighTechcountpreventsduplicatesacrossyards. ExistinglaterfirstHTfallbackand
extraHTbusycapacityrulesremain. Earlieremergency/power/firstrefineryrulesretained.
Policyvanilla-early-hightech-v22; rulefirst_air_productionidentifiesthenewselection.
No city/Tornie/campaign changes. No game launch/restart. Built565; CTest426pass/
2existingnanfailures/3skip. Source/bundleversionsandsignatureverified. Logs
build/early-hightech-565-{build,tests}.log.

# Vehicle-focused custom vanilla — 1.0.564

User says the infantry barracks is unnecessary. Custom vanilla QBot no longer
selects Barracks or WOR in its generic construction priority, freeing yard time
for vehicle infrastructure. Existing infantry buildings may still produce units;
campaign rebuilding and other mods remain unchanged. Includes563parallelMCVs.
Telemetry policy vanilla-vehicle-opening-v21 identifies this build; no schema change.
Built564, CTest426passed/2existingnanfailures/3skipped. Source/bundle versions and
signature checked. Logs build/vehicle-opening-564-{build,tests}.log. No launch.

# Parallel MCV expansion — 1.0.563

User explicitly requested multiple MCVs. Removes the one-pending-MCV restriction
for custom vanilla priority. Each eligible idle factory can order an affordable
MCV while actual yards + existing/queued MCVs is below the cash/economy yard target
(max8). Counts and spending update after each accepted order, preventing same-pass
overshoot. At~100k with1yard, up to7MCVs can be pending across available factories.
MCV unlock upgrades can also run in parallel, bounded by the remaining shortfall;
upgrade counts are reconstructed each build pass, with no new saved state or RNG.
City/Tornie behavior unchanged. Telemetry policy vanilla-parallel-mcv-v20 adds
mcv_shortfall and mcv_upgrades_in_progress; old boolean mcv_upgrade_in_progress kept.
Built563, CTest426pass/2existingnanfailures/3skip; version and signature verified.
Logs build/parallel-mcv-563-{build,tests}.log. No game launched/restarted.

# Wealth-funded vanilla factory expansion — 1.0.562 (2026-09-07)

User wants the100k custom vanilla opening to expand aggressively viaMCVs/HFs.
Latest two sessions are559 campaigns (SCENH019/022), not a new custom test; no561
capture. Requeried build/review-559-vanilla.sqlite: factory target1 at97k, then
tech-policy blocks at93–95k. See appended AI-559-VANILLA-ANALYSIS.md follow-up.
562 changes vanillaFactoryTarget to allow cash-funded capacity above harvester cap:
min(existingpolicy,max(harvesters/3,1+max(0,spendable-10000)/4000)), bounded1..24.
Early custom vanilla factory selection at>=20k targets2HFs/CY, countsqueued, runs
after refinery needs andbeforestarport/optionalinfra. Legalavailability/placement,
army/unitlimits remain. Vanilla usesactualtechavailability, removingextraRepair/IX
policygate onHF expansion. City/Tornieunchanged. Includesall560/561MCV,cap,mixfixes.
Policyvanilla-cash-expansion-v19; rulecash_factory_expansion, builder_status adds
heavy_cash_target/heavy_economy_target/heavy_opening_target. No RNG/savechanges.
Build562 completed; CTest425pass/2existingnanfailures/3skip; bundle/version/signature
checked. Logs build/cash-expansion-562-{build,tests}.log. DO NOT launch/restart game.

# Current vanilla review and combined arms — 1.0.561

DO NOT launch/restart the game; no commits/pushes. Report AI-559-VANILLA-ANALYSIS.md.
559 session1788699472069518-0 finished at17.97min, ended_without_result/allhousesalive.
7990 events imported into build/review-559-vanilla.sqlite, audit clean; engine log
preserved. Army at10.16min only12590 versus41390 in557 (differentseed). Early wealth
failed to accelerate yards/MCV upgrades. Pending560 fixes below address this and
named-housecap40→60 fornewmatches. No560testmatchhasoccurred.
561 blends learned vanilla unit mix50/50 withconfiguredhouse mix andcapsair25%;
78%airtarget hadsqueezedlaunchers/Sonics, thenHFtankfallbackdominated(136built125lost).
Openingmix andcity/Tornieadaptationunchanged. Policyvanilla-combined-arms-v18.
Telemetry raw_ornithopter_bps, blended_damage_per_loss basis, forced_with_target/
forced_without_target. Do notcancel forcedorders blindly: mayalreadybefighting.
IMPORTANT: pre561 house_comparison.military wascumulative, notcurrent. Corrected
usingunitcounts×priceexcludingMCV/harvester/carryall/worm; military_basis marks it.
Build561 successful; CTest424pass/2existingnanfailures/3skip. No game restarted.

# Wealthy vanilla MCV priority — 1.0.560 (same pending build)

User observed559 with~95kcredits,18harvesters/6refineries,1HF/1CY,noMCV at6.61min.
They explicitly want MCVs prioritised with plentiful cash. Supersedes558 strict
harvester-gated yard target: target=max(economy target,1+spendable/10000), max8.
Vanilla custom QBot prioritises affordable MCV before more harvesters, one existing/
queuedMCV at a time; also prioritises the required HFupgrade before harvester orders
can starve the unlock. Only one factory unlock upgrade is in progress at a time.
Keep2kreserve andprice+1kspendableguard. Other factories can keep producing harvesters
while theMCV isqueued/deploying. City/Tornie order unchanged. Source560 also includes
named-house60capfix below. Current559game is unchanged; no restart/launch.
Policy vanilla-mcv-v17 adds cash_construction_capacity order rule, mcv_unlock event,
vanilla_yard_target computed fromcurrentloggedspendable, mcv_upgrade_in_progress.
Tests cover wealth override, affordability, oneMCVpending andmax8.
Built560 successfully; CTest423passed/2existing nan failures/3skipped. Version/plist
560 and bundle signature verified; logs build/mcv-priority-560-{build,tests}.log.

# Named-house vanilla cap correction — 1.0.560

User is playing559 session1788699472069518-0, vanilla All against Atreides,
seed2045383069, QBotBrutal. DO NOT restart it. Current559 correctly logs modeflags,
queue liabilities and house_comparison, but engine/AIcap40 exposed an omission:
558 applied +50% only in INIMapLoader::getOrCreateHouse, not the ordinary named-house
loading path. Both paths now apply the existing tested vanilla capacity helper;
explicit overrides and city/other-mod limits remain unchanged. New matches in560
will use60 here. Existing559 match/old saves keep40. This test can assess other
changes but must not be reported as evidence forcap60.
Monitoring automation reactivated for this exact session, comparisons against557,
then pause after result report. State in build/next-match-monitor.json. No gameplay
changes beyond fixing the omitted default-cap application. Build560 for next launch.

# Visible active mod — 1.0.559

Main menu replaces misleading generic Dune City logo with a centred `MOD: VANILLA`
(or active mod) banner above buttons, uppercase 24px white, thickened lettering on
opaque black. Works in classic/enlarged menu layouts and reflows when mods switch.
Version footer remains separate. Gameplay badge uses20px uppercase lettering onblack,
reads the match's mod from GameInitSettings and sizes to text. Existing watermark
visibility preference is retained. Changes are presentation-only. Build559 succeeded;
version metadata and signature checked. No game launched/restarted; visual runtime
verification remains for user's next launch. No new tests for this small UI change.

# Vanilla loss review and next build — 1.0.558 (2026-09-06)

DO NOT launch/restart the game. No commits/pushes. Built bundle is for user's next test.
Full report: AI-557-VANILLA-ANALYSIS.md. Completed vanilla session
1788694972180753-0 (23.62 simulation minutes), 12,578 records in
build/review-557-vanilla.sqlite, audit clean. Formally ended_without_result, nearly
wiped out. Four allied opponents start with 51 refineries/28 HF/588 rockets; not
an equal-start comparison. QBot had 8CY/6HF/1ref at6min, 40-harvester cap, bankrupt
later; only4 waves,17 army-threshold deferrals. No earlier-binary win-rate comparison.

558: max speed4ms (was8), accumulator allowance supports render pacing. Explicit
user request: vanilla ignores shortages/deterioration/upkeep, radar/production/
rockets use common House power rule. Keep windtrap prerequisites, actual outputs;
city and other named mods retain power rules using session mod settings.

Vanilla QBot prioritizes spice harvesters/refinery capacity; 2k planning reserve;
default harvester caps+50% (huge40→60), explicit lobby limits unchanged; engine old
save caps honored. Queue liabilities deducted from new-order budget. CY target
1+harvesters/8 (cash-bound,max8), HF target bounded byharvesters/3. Optional gun/wall
quotas await fleet/cash; emergency anti-air retained. Brutal vanilla custom threshold
cap24k/Hard28k; respect lower config. Brutal threshold recheck15s. Deterministic
commitment20–100 usesbest3samplesBrutal/best2Hard; city behavior unchanged.

Policy vanilla-economy-v16: queue liabilities, economy reserve, mode flags, both
harvester caps, all-house comparison at QBot snapshots, attack eligibility diagnostics.
See telemetry doc. Build success; CTest422pass/2known nan failures/3skipped; Python9pass.
Logs build/vanilla-558-{build,tests}.log. No runtime win/performance claim. Next
recommendation: general air anti-air corridor screening (59/63 ornithopters lost).

# Final555 capture reviewed after quit

User manually quit cleanly at74.428game minutes. Full197,457records in build/review-555.sqlite;
game_summary ended_without_result + session_end, no capture_limit or simulation_exception.
Allhousesalive. Final ordinarylog build/review-555-game-final.log. See final section of
AI-555-VANILLA-REVIEW.md. Newrecommendation: densityhysteresis/minimumleveldwell; Harkonnen
670declines+658growths inlast10min, individualzones26changes. NOT implemented ahead ofvanilla.
RichAIs24HF/~80karmy cap, so cashstockpile alone doesnotjustify morefactories. Source557
unchanged andready. Do notlaunchgameforuser.

# Live 555 review, city investment and vanilla audit — 1.0.557 (2026-09-06)

**No game launch/restart.** User next match will be vanilla. Built557 in build/bin/dunecity.app.
Review: AI-555-VANILLA-REVIEW.md. Evidence SQLite build/review-555.sqlite through41.87min,
98,194 records; ordinary log preserved build/review-555-game.log. Audit old data clean for
sequence/references (does not prove semantic correctness or completed match).

Critical live bug: Fremen674,200 reported power despite7reactors+2windtraps (max7,200).
Rejected placement constructs a generator and credits power, then directly deletes it;
default destructors leaked the contribution. Repeated failed reactor attempts explain
phantom surplus. WindTrap/NuclearPlant/AdvancedWindTrap/Scoutpost destructors now setHealth(0),
removing remaining power without detonating on cancellation/teardown. Failed House placement
marks cancelPlacement before delete, suppressing fake combat-loss callbacks. Full-health
windtrap demolition is covered too, relevant to vanilla. Existing running555/old inflated
save totals are not retroactively repaired. Next new match is the validation target.

City improvements: stable construction-yard-first planning (store IDs, resolve each time so
redevelopment cannot retain dangling zone pointers); demanded feasible zones ahead of
optional land-value turrets, defensive turrets still first. City MCV expansion keeps existing
income target/max8, one in flight, MCV cost+1000 working cash rather than strict>3000; may use
optional Palace reserve so construction investment does not starve. Accepted MCV subtracts
planning budget. Vanilla keeps money/4000 CY policy and original ordering. Small armies keep
one base defender; empty reserves fall back to configured emergency structure response.

Vanilla: all city-only object entries disabled on new-game init, upgrade-level calculation
also filters them. Generic ObjectData no longer silently rewrites reactor HP; city match init
applies Starport-equivalent HP. CityStatsBox attaches/updates only when city sim enabled;
windtrap output and requested auto-repair/demolish UI remain in both modes. City effects,
Harkonnen ornithopter exception, zoning/overlays/palette remain city-only. General QBot
balancing/escorts/deterministic attacks remain shared deliberately.

Telemetry policy city-investment-v15: post-plan yard_planning_result (pre-plan queue=0 not
lasting idleness), construction yard/MCV counts, planning order flag, crime above250 bin,
power_accounting reported/generator sum/difference in snapshots. Include all4 generator
classes; expose AdvancedWindTrap output read-only for telemetry. SQLite audit aggregates
power mismatches; old captures with missing fields accepted. Generator lifecycle test is
source-integration, not a full renderer/game test; Python test covers accounting alert.

Build successful. CTest418passed/2baseline parseDouble(nan) failures/3skipped; Python9pass.
Version source/config/plist557 agree; no tag atHEAD, no commits/pushes. git diff --check clean.
Detailed recommendations in review: smaller raids below32000 Brutal gate, placement stalls,
coalesced harvester telemetry; assess growth/outage timing after real power totals restored.

# Readable DuneCity house colours — 1.0.556 (2026-09-06)

User's current555 match remains running; DO NOT restart/launch apps. Built556 for nextlaunch.
Neutral is bright cyan, Fremen ivory; standard slots H/A/O/F/S/M/N/R now use distinct
red/blue/green/ivory/magenta/orange/cyan/violet. Definitions include/dunecity/HouseColors.h.
getHouseColorSDL returns these ramps only for active dunecity mod, slots0..7. GFXManager
uses existing private indexed/truecolour remapping path for these slots; avoids editing
shared IBM.PAL terrain/neutral metal colours. Explicit player colour-slot overrides remain.
getHouseRadarColor uses brightest shade; terrain radar colours dim to55% in dunecity
so spice and sand do not dominate ownership dots. Classic/Tornie palettes unaffected.
No simulation/save/network changes beyond matching game-version metadata.

Build success; CTest415passed,2known parseDouble(nan) failures,3skipped. New colour tests
check pair separation, shade order/alpha and Neutral cyan. Logs build/house-colors-{build,tests}.log.
Metadata/plist556. Runtime visual verification remains for user's next launch; no app opened.

# Startup boundary fixes — 1.0.555 (2026-09-06)

User reported match-start exit in553, then again554. STOP launching/reloading the game:
user explicitly requested this after UI verification attempts. No launch of555 performed.

Preserved initial failure: build/startup-553-crash.log contains Map.h:98 Tile(92,-1)
does not exist during initial heavy-factory search. First fix554 bounded road/paving
callbacks via CityPlacementPolicy::assessRoadsOnMap. Regression covers four edges/corners.

Further static audit found fourZoneBlockBonus independently reading off-map neighbours
and its road perimeter. 555 skips block layouts whose full4x4+road perimeter cannot fit;
individual edge lots remain legal, they merely receive no block bonus. Regression checks
all candidate origins/offsets on128x128. Placement failure telemetry also bounds tile reads.
Session1788691499646985-0 endedcycle95 and1788691615471597-0 cycle99 in554; normal log
was overwritten by later menu launches, so exact second exception was not retained.
Do not claim full runtime verification. New simulation_exception event wraps updateGameState
before destructor closes telemetry; subsequent launches cannot erase that session evidence.

Built555 successfully; CTest414passed,2known parseDouble(nan) failures,3skipped. Logs
build/startup-boundary-{build,tests}.log. Source/plist555; no commit/push. GUI automation
resolved an old /Applications copy and had bundle-cache ambiguity; do not repeat it.

# Final review, multiplayer, unrest and escorts — 1.0.553 (2026-09-06)

Latest old-game capture: 88.17 min,493358 events, audit clean; still live/no session_end.
AI-FINAL-LIVE-547-REVIEW.md contains evidence and difficulty proposal. MULTIPLAYER-553-REVIEW.md
records lockstep review and remaining integration-test limits. Do not mistake old547
telemetry for results from these changes. No game restarted, commit or push.

Build553: attack commitment20–100% of eligible AVAILABLE ground force, deterministic
Uint32 mix of match seed/cycle/house/player. Excludes hunters, forced, damaged, retreat,
base-defender and escort units. Existing attack threshold unchanged; fixed force ratio
INI setting no longer determines main attack size. Logs percent/availablevalue/seed.
QBot aircraft favour visible reactor with half ready wing within12tiles and no visible
AA covering sampled straight approaches. Early distance filter bounds extra work.

Base defenders10% of active ground combat count (floor), prefer launchers; harvester
escorts20%, max2 per active harvester. Derived each check, excludes ongoing hunters/
forced/retreat/damaged, moves beside harvesters, bypasses old rally and attack allocation.
Base damage response restricted to base pool; harvester reactive scramble increased50%.
No claim of full tactical integration testing. defence_allocation logs targets/assigned.

Crime: removed250 and intermediate300 clamps; uint16 crime layer, overlay colour
saturates255 while query/SQL retain real values. Three Unit_Trooper individuals per
outbreak, per-owner16x16district. Timers~176sec at201,90sec250,60sec300+; reset if<=200.
Uses existing living opposing faction, rotating deterministic selection; no newhouse.
Respects unit capacity, enabled flag and local free space. No enemy => no spawn.
crime_unrest logs origin/owner/district/crime/spawned/hostilehouse/members/failure.
Save9825 appends district progress; older saves initialize zero.

Hostile armed visible units within4tiles of a property's footprint reduce landvalue:
max80 atcontact,64/48/32/16 at1/2/3/4tiles; strongest only, no cumulative army blob
penalty, floor1. Friendly/unarmed units excluded. Recomputed, no lingering loss.
Diagnostic hostile_value_penalty map and growthfield/SQLite city_growth column.

Network handshake now hard-rejects different game versions; mod sync cannot fix
executable differences. Tests cover acceptance/rejection and reproducible attack rolls.
Full two-peer play/save/load test remains. Existing foreign-player command validation
and whole-state checksums are separately documented follow-up concerns.

Validation logs build/unrest-{build,tests,analytics-tests}.log. Latest expected baseline:
CTest412passed,2preexisting parseDouble(nan) failures,3skipped; Python8passed.
Source/plist1.0.553. Previous growth/low-power timing recommendation remains UNIMPLEMENTED.

# Stalemate, crime, redevelopment and UI — 1.0.551 (2026-09-06)

Built successfully. CTest407passed,2known parseDouble(nan) failures,3skipped; analytics
Python8passed. Logs build/crime-coverage-{build,tests}.log. Metadata/plist1.0.551,
no HEAD tag, no commit/push/live restart. Visual and match behaviour need next-launch test.

AI-STALEMATE-547-REVIEW.md records live snapshot through40.74minutes,207,915SQLiteevents,
auditclean. 3,998power-associated declines; successive decline median1.248sec versus
growth19.968sec. Recommendations:30-45sec outage grace then~60sec perlevel; growth
45-60sec L1->2 /90-120sec L2->3, decoupled from taxation. NOT IMPLEMENTED timing changes.

Micropolis stacking verified in simulate.cpp1545 and scan.cpp415-432. Fixed duplicate
per-worldtile police stamping into2x2cells; distinct sources still add, existing16tile
falloff retained (not Micropolis diffusion). Wide basecrime300 then coverage then final250.
New derived, unsaved crime_before_police and police_coverage layers/snapshot/growthfields;
SQLite views upgraded with those and police_cost_milli. Policy crime-coverage-v12.

Human Destroy button in DefaultStructureInterface applies to owned buildings in allmodes;
new commands appended. Zones clear without explosions/refund, retain roads/concrete;
other buildings use their ordinary destruction effects, including nuclear blasts. Deliberate
removal excludes combat loss counters/callbacks; zone_demolished/building_demolished logs.
Zone density shows /3, turret lines Park:1fountain and Police:15%.

AI may redevelop up to4low-value(<=64) own R/C/I lots when no normal site exists for
needed heavy factories/windtraps/reactors. Normalized owner demand, density/value and
rear position rank displacement. No hospital/church removal. Demolition ONLY after
successful building order; reserved sites, threat, reactor-spacing and road checks remain.
Concrete is skipped for these redevelopment orders (building can start damaged, normal
repair applies). redevelopment_committed logs removed IDs/item/demand/value/density.

Soft 2x2 zone-block preference keeps each lot2x2; 5tile repeating block+roadgap and
completion bonus. CityRoadImpact models the actual automatic perimeter road additions
so adjacent lots can replace internal road segments without severing connectivity.
Important next-match watch: avoid immediate rezoning of demolished footprints and verify
actual factory placement completes, harvester unloading, and crime balance with true15%.

# Overlay buttons — 1.0.550 (2026-09-06)

Added Land Value and Crime buttons directly below Auto Repair in the empty-selection
DuneCity sidebar. Click an active button to clear the overlay; choosing the other switches
layers. Pressed states follow keyboard shortcuts too. Hidden in normal Dune mode and
while the object panel is showing, like Auto Repair. Local presentation only, no simulation
or save changes. Build log build/overlay-buttons-build.log; CTest402passed,2baseline
parseDouble(nan) failures,3skipped. Source metadata/plist1.0.550, no tag/commit/push/restart.
Live visual check remains for next launch.

# Current follow-up — 1.0.549 (2026-09-06)

Uncommitted; live match remains 1.0.547. Do not restart it. 1.0.548 added DuneCity-only
Harkonnen Ornithopters through both HighTech upgrade discovery and build-list gates;
normal Dune unchanged, standard IX/tech/upgrade requirements retained.

1.0.549: police sidebar reinforcement labels split into short rows, portrait region
fixed-height, taller stats rows, correct Police role. Budget now has station/rocket/gun
counts and separately funded annual costs. Both turrets give ONE fountain bonus (15),
15% police strength. Station100, rocket15, gun7.5 upkeep; FixPoint billing retains
fractions at every funding level. Saved legacy integer expense caches remain compatible
and round the aggregate; telemetry police_cost_milli is exact, police_cost rounded.

Harvester policy harvester-redistribution-v11: actual circular weapon reach instead of
construction's square range+2 buffer; no 30sec shelter veto, no120sec field veto.
Prefer reachable-by-corridor safe spice, soft recent-loss penalty, per-harvester destination
reservations and crowd penalties; if no safe field, disperse nearby without base attraction.
Escape corridor permits leaving danger but rejects rising danger/re-entry. This is a
straight-corridor approximation, not a pathfinder guarantee; checks recur every2sec.
New harvester_safety actions redirect_spice/disperse/no_safe_route log current and old
destination danger, candidate/rejected-route counts, memory/crowding penalties, cargo.

Live session1788686750413469-0 sampled:82,514 retreat commands,58,832 with zero current
position danger,900 already at commanded destination. Destination danger was not logged
in the old decision, so don't infer all58,832 were entirely safe.
User police house4 object1221 at(118,10),cycle38146: roads on all four footprint sides;
21 R/C zones within16 tiles before placement allcrime0, ten nearby rocket turrets.
Good geometric access, poor incremental crime payoff. Don't relocate user's station.

AI police auto-deployment no longer excludes local/spectated AI house (5sec retry).
Human houses retain manual deployment. Command-number combinations no longer trigger
city overlays/groups; Shift+5 land value, Shift+1 off remain.

Validation: build successful; CTest402passed,2known parseDouble(nan) failures,3skipped;
Python analytics8passed. git diff --check clean, three metadata files and app plist1.0.549,
no HEAD tag. Logs build/civic-harvester-{build,tests}.log. Panel layout/behaviour awaits
next-launch visual check; no live restart, commit or push.

# Handover — DuneCity session, 2026-09-06

## City analytics before next match: 1.0.547

User authorized complete city stats logging before starting the next match. Gameplay
unchanged (policy tactical-safety-v10), telemetry4. Added30sec crime bands/threshold
counts, initial/120sec full QBot building snapshots, city level-change causal records,
~120sec unchanged growth evaluations, and global terrain/roads/effect-layer snapshots.
See AI-DECISION-TELEMETRY.md for fields, cadence, raw population scale and phase caveats.
JSONL limit256MiB. scripts/ai-decisions.py has city_buildings/city_growth SQLite views.
Build1.0.547 successful; CTest400passed, same2nan failures,3skipped. Python8passed.
Logs build/city-analytics-{build,tests}.log. No gameplay tweaks, restart, commit or push.
Next match had not started at last check; prior completed session1788680806413568-0.
Heartbeat review-next-dunecity-match active every5min, waits quietly for first new
match, audits/analyzes at completion then pauses. Progress build/next-match-monitor.json.


## Police eligibility correction (analysis only; executable still1.0.546)

User explicitly rejects building police at low crime or for troop payoff. Removed
previous automatic-first-station proposal from AI-TACTICAL-STRATEGY.md.
AI-POLICE-VS-TURRETS.md compares actual costs and proposes persistent harmful crime
+ marginal benefit/payback against legal turret alternatives. One/two extra turrets
normally win; police niche is wide severe residual crime requiring several extra
turrets without significant additional turret amenity/defense value. Coverage must
model coarse stamps, not flat100/15. No police construction code added this turn.


## Strategy clarification and police assessment (no executable change)

User wants the proposed base response force to favour rocket launchers for air.
AI-TACTICAL-STRATEGY.md updated: launcher-heavy anti-air reserve with ground screen.
Army role allocation remains a proposal, not implemented. Source confirms QBot has
no PoliceStation construction rule, though AI-owned stations auto-spawn units.
Documented default economics:500build,20power,100upkeep per60game seconds;1400full
batch purchase value every5/10min. Proposed one station after essential power/initial
heavy production, extras for uncovered harmful crime or actual reinforcement need.
No police-building rule added in this analysis turn. Executable remains1.0.546.


## Tactical safety, factory pressure and reactor defense: 1.0.546

Implemented user-approved items from old-match analysis. See AI-TACTICAL-STRATEGY.md
for exact rules, limitations and the proposed70/20/10 army-role split (proposal only).
QBot caches visible weapon danger every2seconds; checks build and final placement.
Five-minute decaying overlapping structure-loss memory; previous60sec exclusion kept.
Reactors favour rear relative to visible enemy bases, four clear tiles from reactors/
HF/RY/CY (including queued reservations), and seek2rocket-turret coverage, weight2.
Factory target adds2..4 lanes under75% utilisation/recent2min HF losses with>=8000cash;
queued factories count, ceiling24, existing unit/army caps remain.
Harvesters proactively retreat, shelter30sec, blacklist fields120sec, assign safe fields
or wait; immediate damage reaction covers empty harvesters. Straight corridor danger
is a heuristic, actual pathfinding unchanged. Escorted formations are not implemented.
Runtime caches/memories are not serialized (save9824 unchanged).

Telemetry tactical-safety-v10: threat snapshots, placement risk/rejection counts,
heavy_losses_2min, harvester_safety actions. TacticalSafetyPolicy helpers tested.
Build1.0.546 successful, metadata/plist agree. Ctest400passed, same2nan failures,
3skipped; Python importer7passed. build/tactical-{build,tests}.log. No game restart,
commit or push. Needs same-map live test to assess survival and possible over-caution.


## Completed old-match analysis and police batch: 1.0.545

See AI-FINISHED-539-ANALYSIS.md for session1788680806413568-0 (75.94min).
54908 events audit clean; Harkonnen lost with170069credits; 344/627 completed R
zones died within60sec. Engine log preserved build/finished-539-engine.log.
Remaining proposals are analysis only. Source audit finds pollution growth/day parity
coupling, pre-police clamp mismatch, coarse stamp accumulation to investigate.

Police batch now9 individual troopers,1quad,2trikes, within3tiles of station using
complete nearest-first rings. No distant fallback. Palace and police share
Palace::getSpecialWeaponCooldownForHouse:5/10min normally, Tornie Rebels7.5min,
Wildspade10min. Existing save9824 timer layout preserved. UI shows actual seconds.
New police_unit_spawned logs IDs/positions; batch logs quads and skipped reasons.
Build1.0.545 successful, versions/plist agree. Ctest396passed, same2nan failures,
3skipped. build/police-batch-{build,tests}.log. Not restarted or committed.


## Factory cap, police budget breakdown and Palace roles: 1.0.544

User approved raising QBot's heavy-factory ceiling from 8 to24 (both city and
classic paths). City target remains max(1+estimatedTaxPerSec/50,
1+max(0,credits-2000)/2500), now clamped1..24. Classic keeps /4000 cash formula.
Queued counts, military80000 limit, prerequisites and other gates unchanged.
At23000citycredits target9; at59500target24; observed149408treasury nowtarget24.
Policy factory-cap24-v9. Boundary/current-match regression assertions updated.

City Budget now has two full-width rows beneath Police Services total: Police
stations count + annual paid cost, Rocket turrets count + annual paid cost.
Counts are live completed local-house items. Cost scales with pending funding,
uses actual CityEffects cost constants (100/15) and components sum to displayed
total. Forecast nominal uses the same live counts to avoid stale census mismatch.
Window height380->424 to fit44 extra pixels; width420 unchanged.

Palace changed from2R+2C population to one residential and one commercial zone:
raw R16/24/40 and C1/3/5 at occupancy1/2/3. Both share existing Palace occupancy,
capped3. Added commercial supply for Palace, previously missing despite its
commercial population; now both supply/population match one ordinary R/C zone.
No save layout change from9824. Existing Palace sidebar displays both portions.

Built app1.0.544, metadata/plist agree. Ctest396passed, same2nan baseline failures,
3skipped. Logs build/factory-cap24-{build,tests}.log. No restart/commit/push;
UI presentation and live AI effects await user's next launch.


## Auto repair, police reinforcements and city siting: 1.0.543

This supersedes the zero-police rocket behavior in 1.0.542: user now wants
rocket coverage AND annual budget upkeep at 15% of a police station. Values are
15 coverage / 15 yearly cost vs station 100/100. Rocket land-value strength 30,
intersection road connectivity and weighted asset defense siting are retained.

New Auto repair on/off sidebar button below Ornithopter (below Chemical Carryall
in Tornie), visible when nothing is selected, for normal Dune and all mods.
House-wide setting defaults off. Enabling starts normal paid repairs for living
damaged structures with >=5 credits; insufficient funds pause and funded future
updates restart. Off prevents new automatic starts; already-started/manual repairs
finish normally (tooltip says this). Command is attributed to the issuing player's
house, not a caller-supplied house ID, and runs through the command manager.
QBot starts reactor repairs for any damage whenever >=5 planning credits; existing
health-proportional power is unchanged. Fixed rich/turret repair branches starting
repairs on already-full structures. reactor_repair telemetry records health/cash.

Police stations gain the Palace/TechCenter READY picture-button and cooldown UI.
Default batch 3 trikes +6 individual troopers, interleaved, free, deployed around
the station in GUARD mode. Five-minute initial and repeat recharge (Fremen Palace
cadence). Respect unit limits, enabled unit types and deployment space. A partial
batch starts full cooldown; total failure keeps ability ready, AI retries every
five seconds. AI houses auto-deploy like Palace. Human commands check station
ownership. police_reinforcements logs actual counts, zero charge and cooldown.

Save format 9824: House bool after team ID; PoliceStation timer after base fields.
Both reads are version-gated; older saves default auto repair off and fresh police
cooldown. Existing command IDs are unchanged; two new commands appended before
CMD_MAX. Tests updated for appended IDs and save version.

City placement now accounts for whole footprints, polluting factories as well as
I zones, and other construction yards' queued sites. Candidate tiers outrank old
clustering scores: outside pollution radius (>5 footprint tiles) and within local
supply reach is preferred; nearby crowded sites are fallback, disconnected sites
last. Supply uses conservative origin distance <=16, with missing-role allowances
for bootstrap. R requires jobs; C requires available R/I; I requires R. Existing
road continuity/frontage checks remain. Local Micropolis source traffic.cpp and
micropolis.h use MAX_TRAFFIC_DISTANCE=30 road steps; DuneCity TrafficSimulation
uses 20 road steps and city growth kSupplyRadius=16 with coarse grid aggregation.
No simulation distances changed, and origin reach is not proof of a road route.
R/C scoring averages pollution/land value over the footprint and favors adjacent
open sand/dunes. Severe pollution outweighs sand/value. Clean industrial buildings
like windtraps do not get a pollution separation requirement. Placement details
in construction_selection.site.placement_quality include tier, score, supply flag,
nearest role origins, pollution buffer, mean value/pollution and adjacent sand.

Build 1.0.543 passes 395 C++ cases, same two nan baseline failures, three skipped.
Build/test logs build/repair-police-{build,tests}.log. Earlier Python importer tests
pass (7). Source and app plist checked. No user-game restart or GUI playtest; no
commit/push. New UI, saves and deployment behavior need the user's next launch.

## Live heavy-factory cap diagnosis (after 1.0.543 work)

User asks to explain cap before tweaking. Running match remains 1.0.539 session
1788680806413568-0. At cycle 239400 Harkonnen:149408 credits, eight actual HF,
zero queued, six busy, military9630/80000, no ground unit limit, power7200/5180.
Builders say heavy_target8, heavy_reason target-met. Last five game-minutes had
four lost HFs and four accepted replacement orders; two newly completed HFs were
lost almost immediately. Other survivor Rebels has530104credits, eight HF and
military81310/80000, blocked by military-limit instead.
Current shipped running formula: min(8, max(1+taxPerSecond/50,
1+max(0,credits-2000)/5000)). Updated source uses /2500 but still caps at8.
Neither adapts the cap to threat/losses. No further factory-cap change made yet;
user requested explanation and discussion of tuning.


## Rocket defense and land value: 1.0.542

User replaced rocket-turret crime suppression with twice-strength park amenity
and critical-asset defense, then R/C intersections. Read local MicropolisCore:
`../simcity/MicropolisCore/MicropolisEngine/src/tool.cpp` putDownPark picks either
WOODS2..5 or FOUNTAIN. `scan.cpp` pollutionTerrainLandValueScan adds 15 for terrain
IDs below RUBBLE, smooths terrain memory, then adds it to land value. FOUNTAIN=840
is not below RUBBLE=44: the core has no distinct positive fountain coefficient.
Use the agreed park/terrain reference 15 -> rocket strength 30 in DuneCity's
existing park stamp/falloff (radius 3). This is an adaptation, not a literal
port of fountain behavior. Existing block aggregation, land-value caps and tax
formula are unchanged. Rocket police coverage is now zero; gun turret remains
25. Higher value still has normal indirect city effects; rockets do not apply
a direct crime-reduction stamp. Sidebar says Land value +30.

Replaced crime-hotspot search and crime-triggered construction with weighted
uncovered defense and useful R/C amenity siting. Nuclear weight 2, HeavyFactory
and RepairYard weight 1. Coverage uses weapon range minus one tile from asset
center. Existing/queued turrets suppress duplicate coverage; relocation excludes
its own pending turret. Queued target buildings also count. Defense scores rank
before junction preference, R/C benefit and closeness. R/C-only sites require
cross/T/corner junction bonus and an uncovered zone below max land value within
park range. Once coverage is established, city zoning can continue instead of
building turrets endlessly. Rocket city siting has no generic crime/perimeter
fallback; gun turret placement keeps its ordinary defense search.

Existing road connection/render/traffic code retained; continuity and neighboring
zone-access checks remain. `RocketTurretPolicy.h` holds testable priorities and
bounded estimated benefit. `turret_site_evaluation` logs reason, position, weighted
uncovered defense, estimated R/C value benefit, reactor weight and state.
Policy rocket-amenity-v7 (schema 1, telemetry 3). Ctest: 392 passed, two existing
nan failures, three skipped; seven Python tests pass. Build/plist version 1.0.542,
logs build/rocket-amenity-{build,tests}.log. Ready for next launch; no in-game
placement/tax outcome claim yet. No restart, commit or push.


## Funded idle construction yards: 1.0.541

Confirmed in live session 1788680806413568-0 (running 1.0.539, seed 1424269878).
Harkonnen builder 78 idle with 26,298 credits (seq 6777), power 4,200/1,803,
maximum R/C/I valves, 15/5/9 zones; heavy target five already met. Zone decisions
explicitly reject all candidates as spice_economy_priority. The old hedge gate
requires spiceShare <30,000 or zones <max(6,harvesters), irrespective of cash.
Preserved 9,761 records through cycle 71,646 in build/city-growth-before.jsonl;
summary build/city-growth-before-summary.json. SQLite build/current-growth.sqlite
audit: zero issues. Last five game-minutes: 16/20 CY status samples idle (sampled
observations, not exact idle duration). Full capture: 129 candidate vetoes.

Removed hedge veto; ongoing city growth follows demand even on spice-rich maps.
User clarified that needed Dune buildings should retain priority, then idle yards
should zone whenever demand and a valid site exist. No new priority timer or
city-before-factories override. Existing affordability and power headroom guards
remain. Spice/refinery/harvester investment continues independently.

Factory cash step reduced from 5,000 to 2,500 above 2,000 working capital;
23,000 credits now targets eight factories, previously five. Income target,
actual-plus-queued counts, military/unit caps and classic AI ratios preserved.
Telemetry policy city-growth-v6 records independent zoning policy and zone result.
See AI-DECISION-TELEMETRY.md. 390 C++ tests pass, same two nan baseline failures,
three skipped; seven importer tests pass. build/city-growth-{build,tests}.log.
Version source and built plist agree on 1.0.541. Running game was not restarted;
behavioral playtest remains for next launch. No commit/push.


## Windtrap output and clean industry: 1.0.540

WindTrapInterface now shows the selected windtrap's actual health-scaled output,
using the same getter that updates house power, alongside existing house totals.
CityStatsBox replaces Coal Power with I-medium and shows Emissions: 0 separately
from Local pollution (ambient pollution from surrounding industry). The extra
emissions row is attached only for windtraps, preserving other panels' layout.

Windtraps now have Industrial city role and maximum occupancy level 2, providing
industrial supply/jobs through existing census, demand and growth code. Explicit
pollution exemption keeps windtraps clean at all levels despite the new role.
Power output remains independent of city occupancy. Existing windtraps acquire
the role on the next city scan after loading with this build.

Rebuilt build/bin/dunecity.app version 1.0.540; metadata and plist agree. Ctest:
389 passed, 2 known parseDouble nan failures, 3 skipped. Updated city-effects
regressions cover medium-tier supply/jobs and zero emissions. Build/test logs:
build/windtrap-{build,tests}.log. No game restart, commit or push; sidebar visual
confirmation remains for the user's next test.


## Nuclear chain reactions: 1.0.539

User requested reactor death explosions with twice palace-missile destruction
area, reactor HP equal to a Starport, and palace AI targeting reactors.
New `NuclearBlastPolicy` uses a circular 42-tile equivalent area (2x the existing
missile's 21 impact tiles), radius ~3.66 tiles /117 pixels. Radial tests are
integer-only. Structures intersecting the circle and ground units inside receive
900 damage once (the centered missile's nine 100-damage impacts); terrain/roads
and visible blasts use the disk's tile centers. Map edges are clipped. Air units
retain the normal ground-nuclear immunity. Adjacent plants die and detonate on
their own update, not recursively inside damage iteration.

NuclearPlant::destroy removes remaining power, records trigger/credit owner,
applies blast, then normal structure teardown. Destructor itself never explodes
on quit/load. Chain-reaction credit follows the initiating attacker when known;
direct destruction falls back to reactor owner. Pending credit is runtime-only.
ObjectBase ignores non-healing hits on already-dead objects to prevent duplicate
kill awards from a palace missile's multiple impacts.

Default/Tornie reactor HP now 500, same as Starport. INI loading copies each
house's Starport HP into reactor HP, including overrides. Existing saves retain
their saved object-data balance table; use a fresh match for the new HP table.
Centered palace strike already delivers up to 900; missile scatter is unchanged.
Shared Player targeting selects visible live enemy reactors, prefers clusters,
aims at their center, then falls back to existing target logic. Used by QuantBot,
AIPlayer, CampaignAIPlayer and Mentat, without overriding manual player aim.

New telemetry: palace_target (cluster score), palace_missile_launched (aim tiles,
scattered destination pixels), nuclear_detonation (center pixels, squared radius,
damage, trigger and credit house). Policy nuclear-chain-v5, telemetry remains 3.
Built app 1.0.539; 388 C++ cases pass, same two baseline nan failures, three
skipped. Logs build/nuclear-blast-{build,tests}.log. Full in-game chain/visual
verification remains for user's fresh match. No commit, push or launch.

## Power reserve follow-up: 1.0.538

User requested more surplus power, especially for large cities. City AI target
is now ceil(25% of current demand), increased to one owned generator's nominal
output where useful; this allowance is capped at 50% of demand for small bases.
Examples: demand 6,000 with a 1,000-output plant -> 1,500 surplus; demand 14,000
-> 3,500 surplus (formerly 1,400). Existing cross-yard pending-generator guard
remains, so expansion is reassessed after each generator completes. Zero demand
adds no reserve. No change to actual power consumption/output or classic AI.

Telemetry policy power-reserve-v4 (schema 1/telemetry 3) logs
city_power_reserve_target and largest_generator_nominal in decision state.
Rebuilt app 1.0.538. Validation recorded in build/power-reserve-tests.log.

## Codex follow-up: completed 199-minute match, 1.0.537

User finished the game and requested full analysis, fixes and better capture.
Read `AI-COMPLETED-MATCH-ANALYSIS.md`. Completed demand-first-v1 session has
104,867 consecutive valid records; no corrupt tails. Duration 198.98 minutes,
not the old zero-cycle session_end. Evidence/index/report retained under build/.

Confirmed concurrent overlapping yard plans (Atreides HF and C zone at 98,19
in cycle 99); total 177 HF orders, 56 completions, 107 placement cancellations.
No residential selections over stronger normalized jobs demand (8,688 evals).
Much late support construction replaced losses; Fremen silo lifetime ~7.4 sec
by location matching. Old capture lacks official result and lethal causes.

New source/runtime policy reserved-sites-v3: shared footprint reservations,
60-second avoidance of recent economic/production building losses, funded
factory recovery ahead of city seeding, pending storage/crime-defense guards.
Retains prior spice/road/concrete/civic/power/UI fixes. Runtime-only planner
state does not alter save layout. Records actual placement success.

Telemetry v3: final roster/result/cycle; fractional cumulative economy ledger;
producer progress/gates, harvesters, unit mix; producer/object completions,
object destruction and lethal attacker; attack new vs existing membership.
Engine lifecycle events use player -1 and supplement (do not add to) old
callbacks. Disabled TechCenter text spam suppressed. SQLite match-report,
economy_samples view, and conflicting-reimport rejection added.

Built source version 1.0.537 with script; all three metadata files agree.
385 C++ cases pass, same two baseline nan failures, three skipped; seven Python
tests pass. Logs: build/completed-match-build.log and completed-match-tests.log.
No full match run on v3 yet; user will test on return. No commit/push/launch.

## Codex follow-up: live audit, placement, spice economy and queue guards

Read `AI-LIVE-ANALYSIS.md` for the running 4-corners match (seed 1034718315,
session 1788669627998013-0, cutoff ~38:33). 23,375 events audit cleanly. Confirmed
11 factory and 248 turret placement cancellations, duplicate stadium/nuclear
orders across yards, and no residential choices over stronger normalized jobs
demand. Source/build now uses policy spice-road-v2; running match is still v1.

New changes: retain/replan finished buildings without full-concrete gating;
road-continuity-aware placement, rocket traffic junctions, restoration of road
surfaces after damage; spice-based harvesters/refineries with city hedge and
combat/cash constraints; queued civic/power guards; nuclear plant power panel.
Telemetry v2 adds detailed placement observations, all-producer statuses, crime
defense reasons, queued civic/power inputs, road scores, spice fleet targets and
credit provenance. SQLite tool adds audit/report. See telemetry doc for semantics.

Built `build/bin/dunecity.app`, metadata consistently 1.0.536. Validation:
`build/ai-placement-tests.log`: 382 passed, same two baseline parseDouble("nan")
failures, three skipped. Five Python importer/audit tests pass. New UI and policy
still need observation after user restarts; do not interrupt the running match.
No commit/push. Existing queues/buildings are not rewritten on save load.

## Codex follow-up: demand-first zoning and structured AI telemetry, 2026-09-06

Preserved current game in `build/zoning-before.log`; imported 2,328 logged zone
selections into `build/ai-decisions.sqlite`. Of 1,576 residential selections,
1,240 occurred with stronger normalized C/I demand. Root cause: `rankZones`
used demand only as a positive gate and ranked raw gaps from a fixed 3R:1I:1C
ratio. It now ranks normalized demand first (R*3, C/I*4), breaking ties by
weighted counts; bootstrap still seeds missing types. Campaign's duplicated
zoning branch now calls the same chooser.

Added per-session JSONL telemetry, SQLite importer/reports and tests. Read
`AI-DECISION-TELEMETRY.md` for event schema, paths, SQL, coverage and limits.
Captures are local under application support `ai-decisions/<session>/events.jsonl`.
No external DB service, save-format/RNG changes, commit or push. Snapshot/decision
inputs, candidate reasons, queue acceptance, placement requests, actual built/loss
callbacks, and main attack gates are separate records. Capture is bounded at
128 MiB per session; completed sessions are retained without automatic deletion.
Set DUNECITY_AI_TELEMETRY=0 to disable. Other AI classes and tactical/pathfinding
choices are not instrumented by this change.

Local build is now source version 1.0.536 (version files advanced elsewhere during
this work; this task did not bump them). `build/ai-telemetry-tests.log`: 375 passed,
the same two pre-existing parseDouble("nan") failures, three skipped. Three Python
importer tests pass; C++-written fixture imports as valid JSONL into SQLite with
zero invalid records. The existing open game has not been restarted; save/reload
in rebuilt `build/bin/dunecity.app` is required for live verification and capture.

## Codex follow-up: repair/factory balance, 2026-09-06

Current-game evidence saved in `build/ai-balance-before.log`: Neutral ordered
its fourth repair yard with two busy heavy factories and later held ~18k credits;
Mercenary ordered its sixth repair yard with two factories. Some heavy factories
were being built, but the city cash target added only one per 10k credits while
repair yards grew unconditionally with army value (one per 6k).

`QuantBotBuildPolicy` now targets an extra city heavy factory per 5k credits above
2k working capital, still taking the larger income target and capping at eight.
Extra repair yards require all existing yards busy, count queued yards as spare
capacity, and cap at ceil(completed heavy factories / 2), minimum one, maximum four.
The first-yard tech rule remains. City factory expansion uses actual build-list
availability without the additional policy-only repair-yard/IX prerequisite;
classic-mode factory prerequisites remain. Power recovery and economy seeding
still precede expansion, and military/unit limits still stop factory expansion.

Added 30-game-second per-CY `BUILD-BALANCE` logs (HF/RY completed/queued/busy,
factory target/reason, repair cap, estimated tax income, power) and `BUILD-CHOICE`
logs alongside the existing no-site/rejected-order diagnostics.

Local app rebuilt at `build/bin/dunecity.app`, source version 1.0.535 checked
consistent; no version bump, commit, or push. `build/ai-balance-tests.log` reports
372 passed, the same two pre-existing parseDouble("nan") failures, three skipped.
The running game must be saved, quit, and reloaded in this rebuilt app before
these changes and new logs take effect. Live post-change validation is pending.

## Codex follow-up: QuantBot production and civic graphics, 2026-09-06

User reported Brutal QuantBot's heavy factory idle, one construction yard
repeatedly building residential lots, the other idle, and building graphics
appearing in other places. Preserved live evidence in `build/quantbot-before.log`.
Bots held roughly 50–60k credits with military values far below their configured
80k limit. Logs repeatedly selected Heavy Factory, then deduplicated Palace,
then entered `PROACTIVE: Building Residential Zone`. The running game's user
override enables `Only One Palace`; the screenshot also showed `ALREADY BUILT`.

Verified production causes and fixes:

- QuantBot's city palace target ignored `onlyOnePalace` and mixed internal and
  displayed population. BuilderBase rejected the extra palace, while QuantBot
  counted the rejected order and reserved the entire production loop for it.
  The planner now respects the option and the documented 30k displayed-population
  scaling; counts update only on accepted normal construction orders. Subsequent
  yards re-evaluate palace/IX counts including queued orders.
- Strategic saving now reserves the item's price and allows factories to spend
  the remaining cash. An unplaceable strategic structure does not reserve funds.
- Removed unconditional residential fallback. City bootstrap, ongoing zoning,
  and idle-yard fallback rank R/I/C demand and count balance, trying another
  demanded type if the first cannot be placed. The fallback respects power.
- Heavy factory expansion considers both income and surplus cash, bounded at
  eight factories, and stops expansion when the military-value or ground-unit
  limit is reached. Corrected siege-tank budget accounting.
- Concrete placement plans are now per construction yard. The old shared FIFO
  could send one yard to the other's planned location. The old serialized list
  remains for save-layout compatibility; runtime per-yard plans rebuild after
  load. Placement cache clears after placing a structure.
- Construction-yard status logging is throttled per bot rather than using
  shared static state across all yards/houses. HF/CY diagnostics identify the
  builder, queue, hold state, budget and relevant limits/reserves.

Graphics cause: a zone's civic overlay selected a 1x1 hospital/church texture,
but StructureBase's per-draw refresh replaced it with the 4x4 residential atlas
using the unchanged graphicID. The full atlas then rendered over neighbouring
lots. ZoneStructure now updates graphicID with the civic image and restores the
zone ID and atlas dimensions together when the overlay clears or density is zero.

Build: `build/bin/dunecity.app` rebuilt successfully; source version remains
1.0.534, uncommitted. `build/quantbot-fix-tests.log`: 370 passed, the same two
pre-existing parseDouble("nan") failures, three skipped. New tests cover the
production policies and the civic texture-refresh contract. Live confirmation
after saving/restarting/reloading the user's current game is still pending.

## Codex follow-up: credits root cause confirmed, 2026-09-06

The live diagnostic fired with `dst=2515,135`, `output=2560x1600`,
`logical=0x0`, `target=screenTexture`, and `copy=0`. The active texture is
960x600. This supersedes the stale-texture hypothesis in §3.1: texture creation
already follows the final logical-size adjustment.

On this Mac's sdl2-compat backend, binding the texture clears the logical size,
but `SDL_GetRendererOutputSize` still returns the window's native pixel size.
`getRendererSize()` consequently positioned dynamically right-aligned elements
off the target, while the sidebar retained its correct construction-time position.
The fix in `include/misc/DrawingRectHelper.h` queries the active texture when
there is no logical size, falling back to output size only for the backbuffer.

`tests/RendererSizeTestCase.cpp` covers target switching, credits positioning,
and backbuffer restoration. It uses software rendering by default; run through
`DUNECITY_RENDERER_TEST_GPU=1 ctest --test-dir build --output-on-failure` to
exercise the native HiDPI backend. Before the fix the native test reproduced
`getRendererWidth() == 2560` where 960 was expected. The rebuilt app is at
`build/bin/dunecity.app`. Both software and native-backend suite runs now report
363 passed, the same 2 pre-existing `parseDouble("nan")` failures, and 3 skipped.
Logs are `build/credits-tests-before.log`, `build/credits-tests-after-native.log`,
and `build/credits-tests-after-software.log`. The temporary constructor/blit
diagnostics were removed; the signed digit arithmetic is retained. The currently
open game is still the previous executable and needs a restart for visual
confirmation. No commit or release/version change has been made.

The original handover below is retained as historical context.

Written by Claude Code for the next agent (Codex). Everything below is **uncommitted**
in the working tree on `main` at `24b57ff`, version `1.0.534` (all three version files agree).

23 files changed, ~554 insertions. Nothing has been committed, tagged or pushed.

---

## 1. Build environment on this Mac (this was not documented before)

Host is Stefan's MacBook Air (`Stefans-MacBook-Air.local`, Apple M5, 10 cores, macOS 26.5.2).
The repo docs describe vcpkg (CI) and a Windows laptop; neither applies here. **Homebrew, no
vcpkg, no Xcode** — Command Line Tools clang 21 is enough.

```bash
brew install cmake ninja sdl2_mixer sdl2_ttf miniupnpc catch2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew -DDUNECITY_BUILD_TESTS=ON
cmake --build build --parallel 10        # ~4 min cold -> build/bin/dunecity.app
cmake --build build --target dmg         # -> build/DuneCity-1.0.534-macOS.dmg
ctest --test-dir build --output-on-failure
```

Gotchas:

- `-DCMAKE_PREFIX_PATH=/opt/homebrew` is **required**, otherwise `find_library(MINIUPNPC_LIBRARY …)`
  in `src/CMakeLists.txt` fails and configure dies on a NOTFOUND link item.
- Homebrew `sdl2` is an alias of `sdl2-compat` (SDL2 API over SDL3). It works.
- `discord-rpc` is not in Homebrew and is optional in CMake, so Discord presence is compiled out.
- libcurl comes from the macOS SDK.
- Use `build/`. The tracked `build2/`, `build_phase4/`, `build.bad/`, `buildtests/` are stale
  CMake trees from claw.local (`CMAKE_HOME_DIRECTORY=/Users/stefanclaw/development/dunecity`).
  `CLAUDE.md`'s `~/development/dunecity` paths are claw.local's, not this machine's.
- This build links `/opt/homebrew` dylibs, so the app and its DMG are **not portable**.
  The distributable DMG still comes from CI's vcpkg static build.
- The bundle `Info.plist` shows version `0.01` because `IDE/xCode/Info.plist` uses Xcode
  `$(…)` variables CMake does not substitute. Real version: `build/include/config.h` and the
  log line `Starting DuneCity <version>`.

**Always run the test suite through `ctest`, never the binary directly.** `tests/CMakeLists.txt`
passes `DUNE_CITY_SOURCE_DIR` and `DUNECITY_DATADIR` via `set_tests_properties(... ENVIRONMENT ...)`;
running `./build/bin/dunelegacy_tests` by hand silently skips ~50 source-reading tests and
produces bogus failures.

### Test status

`ctest` → **362 passed / 2 failed / 3 skipped** (367 cases, 2422 assertions).

Both failures are **pre-existing, not caused by this session**:

- `tests/GenericNinthHouseRegressionTestCase.cpp:71` and `:130` — `parseDouble` accepts the
  string `"nan"` on macOS (`ModMentatConfig::parseDouble` / `CustomHouseConfig::parseDouble`
  in `include/mod/`). `std::stod`/`strtod` parse `nan` on this libc; the test expects rejection.
  Fix by rejecting non-finite results (`std::isfinite`) in both headers.

Separately, `tests/Dune2RAssetManagerTestCase.cpp` **segfaults non-deterministically** (line 32,
52 or 72 depending on the run) — 6 of 6 isolated runs crashed. It passes under `ctest` when the
whole suite runs, so it is order- or environment-dependent. This is a real bug and was **not**
investigated. An lldb backtrace needs a one-time macOS debugger authorisation that could not be
granted headlessly.

---

## 2. What was changed (all uncommitted)

### 2.1 Windowed resolution changes were ignored — FIXED, verified

`setVideoMode` in `src/main.cpp` snapped the requested window size to
`SDL_GetClosestDisplayMode` before creating the window. That is exclusive-fullscreen logic and
the game only ever uses `SDL_WINDOW_FULLSCREEN_DESKTOP`. On a Retina Mac SDL only offers
low-density modes as candidates, so 1280x800 → 1920x1200, 1440x900 → 1920x1200,
1024x768 → 2048x1326. The window was also created larger than the desktop (1710x1107 points).

Now: the requested size is used as-is, clamped to `SDL_GetDisplayUsableBounds` in windowed mode,
floor `SCREEN_MIN_WIDTH/HEIGHT`. The logical size derives from what is actually presented (the
desktop in fullscreen, the window otherwise), so the saved windowed size survives a fullscreen
round trip. New log line: `Window: <req> requested, <got> created (<mode>), <w>x<h> pixels`.

`OptionsMenu::determineAvailableScreenResolutions` now drops modes larger than
`SDL_GetDisplayBounds` (the native 2880x1864 panel mode could never be used).

### 2.2 Black bars and mushy text in windowed mode — FIXED, verified

Two causes. The `DISPLAY` screen's "SCREEN SHAPE" forced the logical width to 4:3 or 16:9 while
the height was pinned by the interface preset, so a 4:3 screen sat inside a 16:10 window. And
`SDL_HINT_VIDEO_HIGHDPI_DISABLED` was set to `"1"` (present since the initial commit), so a
1440x900 window had a 1440x900 pixel surface and 600 logical rows were scaled 1.5x.

Now: `SDL_WINDOW_ALLOW_HIGHDPI` is set on desktop, the HiDPI-disable hint is gone, and the
logical width follows the window's shape via a new `interfaceWidthForShape()` in `src/main.cpp`.
Only the height stays a preset. The SCREEN SHAPE row is hidden on desktop in
`src/Menu/DisplayMenu.cpp` (Android keeps the old behaviour) and that menu's layout was compacted.

Mouse mapping under sdl2-compat was verified with a standalone probe before enabling HiDPI:
events arrive in logical coordinates correctly.

**Caveat discovered later and NOT addressed** — see §3.1: `Game::renderFrame()` renders
everything into `screenTexture` at the logical size and then upscales, so the HiDPI surface does
not actually buy sharpness yet.

### 2.3 "Multiple players per house" forgotten — FIXED

New `settings.general.multiplePlayersPerHouse`, persisted as
`[General] Multiple Players Per House` in `Dune City.ini`. The checkbox in
`src/Menu/CustomGameMenu.cpp` seeds from it and writes on toggle. Also written by
`OptionsMenu::saveConfiguration2File` and included in the generated default config in `main.cpp`.

### 2.4 Second player slot per house missing — FIXED (was a regression)

Came in with commit `5a172ce` "Import Tornie 1.0.520 source snapshot" (2026-07-15), not with any
DuneCity fix. `CustomGamePlayers`'s constructor force-disabled
`setMultiplePlayersPerHouse(false)` for every custom game, calling the second slot "unusable",
and `onNext()` rejected two players in one house as `bTwoPlayersInSameHouse`. Both removed. The
newer duplicate-house and duplicate-colour checks were kept. The slot code itself is byte-identical
to v1.0.359.

**Not verified in play.** Stefan has not yet started a co-op game with two players in one house.

### 2.5 Credits SFX on by default — FIXED

Defaulted to off in all three places: the `getBoolValue` fallback in `main.cpp`, the generated
default config (which previously omitted the key entirely and so inherited `true`), and the
shipped template `config/Dune City.ini`. Existing configs keep the player's own value.

### 2.6 Zones placed on top of each other — PARTIALLY FIXED, needs play testing

`House::placeStructure`'s pre-placement check only tested `hasAGroundObject()` and never
`hasCityZone()`. It now refuses both and logs
`placeStructure: refused zone item <id> for house <h> at (x,y): tile (x,y) already belongs to a zone`.

**This log line has already fired once** in Stefan's session (`item 20 for house 0 at (24,13)`),
which proves the guard works and that something is still *requesting* overlapping placements.
Whether visible overlap remains is unconfirmed.

`Game::load` now re-attaches every zone structure to its 2x2 footprint after `objectManager.load`,
because zones from older saves could come back without owning their tiles. It logs
`Loaded game: re-attached N zone tiles, M zone tiles overlap another object`.

### 2.7 Zone road frontage and sand placement — DONE, needs play testing

In `src/players/QuantBot.cpp`, city-mode zone placement (`findPlaceLocation`, guarded by
`cityZonePlacement`):

- The per-adjacent-tile `locationScore += 10` compact-base bonus is suppressed for zones. That
  bonus is what produced solid packed blocks.
- New `alignedWithNeighbouringZone()` gives +50 for continuing an existing row or column, either
  touching or exactly one road tile apart.
- New `wouldLandlockNeighbouringZone()` rejects a lot that would take a neighbour's last open side.
- Small bonus per sand/dunes tile under the lot so rock stays free for Dune structures.

Sand rule, implemented in four places that all had their own copy of the terrain test:

- `DuneCity::isCityZoneTerrain()` (new, `include/dunecity/CityConstants.h`) — rock, slab, sand or dunes.
- `Map::okayToPlaceStructure(..., itemID)` — zones use the new predicate plus an `anchoredTiles > 0`
  requirement; other city-only structures keep the strict rock/slab rule.
- `ZoneStructure::canBePlacedAt` — same.
- `Game.cpp`'s placement preview — same, with `zoneFootprintAnchored` computed once per footprint.

`tests/ZoneStructureTestCase.cpp` had a source-text test asserting the old strict rule; it was
updated to assert the new one.

### 2.8 Game options only saved from the Options screen — FIXED

There were **three** layers, not two: `[Game Options]` in the main config, then the active mod's
`GameOptions.ini` overlaid on top. The Dune City mod's file lists every key, so it always won —
which is why changing a default under Options never stuck either.

New helpers in `src/globals.cpp` / `include/globals.h`: `userGameOptionsSection()`,
`writeGameOptionsToConfig()`, `applyGameOptionsFromConfig()`, `saveGameOptionsAsDefaults()`.
The player's choices are stored per mod in the main config as `[Game Options <modname>]` and
layered over the mod's defaults in `ModManager::loadEffectiveGameOptions`.

**The mod's own `GameOptions.ini` is deliberately left untouched** — `ModManager::updateChecksums`
hashes it for the multiplayer config-sync check, so writing into it would make two players with
different preferences fail to sync.

Every Game Options window now calls the helper on close: Options ("Change…"), the Custom Game
lobby, Skirmish, and the campaign house choice. `Restore Config Defaults` removes the override
section and its message was updated to say so. The `City Effects` key, which the Options screen
never persisted, is included.

### 2.9 Production stall diagnostics — ADDED

`BuilderBase::updateProductionProgress` had a silent branch: if on hold, at the unit limit, or at
zero credits, nothing happened and nothing was reported. It now logs every 10 s with the reason
and a credit breakdown, and posts a ticker message every 30 s for the unit limit and for no money.

This was added for Stefan's "heavy factories aren't building" report, which is **not diagnosed**.
Note `House::getCredits()` sums three pools (`cityCredits + storedCredits + startingCredits`) and
QuantBot in the same house shares the human's wallet.

### 2.10 macOS test link fix

`tests/CMakeLists.txt` did not compile `IDE/xCode/MacFunctions.m` on APPLE, so `dunelegacy_tests`
failed to link with `_getMacApplicationSupportFolder` undefined (from `fnkdat.cpp`). Added the
game target's `if(APPLE)` block plus the Cocoa/Foundation/CoreFoundation frameworks.

---

## 3. Open items for Codex

### 3.1 Credits counter shows nothing — THE MAIN OPEN BUG

Reported repeatedly: the credits box below the radar renders empty in-game.

**What has been ruled out**, from a diagnostic already in the running build
(`Interface: credits digits texture 80x8, sidebar 144x600 at x=816, credits 20000`):

- The digits texture loads fine: 80x8, i.e. 10 glyphs of 8x8 from `SHAPES.SHP` frames 2..11.
- The credits value is correct (20000 at construction).
- The geometry is correct. Logical screen 960x600, sidebar 144 wide at x=816. `PictureFactory`
  blits `creditsBorder` (63x13) at sidebar-relative (46,132) → global 862..925 x 132..145.
  `GameInterface` draws digits at x = 816+49+(6-n+i)*10, y=135, 8x8 → 875..923 x 135..143.
  That is inside the box.
- Palette is not the cause. The glyphs use only indices 31, 81 and 90; `Custom_IBM.PAL` in
  `Tornie.PAK` leaves all three identical to `IBM.PAL`, and `applyCustomPaletteRuntimeHouseRamps`
  only touches 52..59.
- Draw order is fine: credits are drawn after `Window::draw`, and the radar occupies y=0..132.
- `drawCityStatsOverlay()` is the only thing drawn afterwards in the same function and
  `showCityStatsOverlay` defaults to false.
- No `SDL_RenderSetClipRect` exists anywhere in the in-game render path.

**One real bug was found and fixed while looking**: `NumDigits` is `std::string::size_type`
(unsigned), so `6 - NumDigits` wrapped around for credits with more than 6 digits and threw every
glyph far off-screen. Now cast to `int`. This is **not** the reported symptom (5-digit credits
were affected too), but it would have bitten at 1,000,000 credits.

**A one-shot draw-time diagnostic is in the build at `build/bin/dunecity.app` (12:53).** On the
next in-game frame it logs one line to `~/Library/Application Support/Dune City/Dune City.log`:

```
Credits blit: value=… digits=… src=… dst=… copy=… output=…x… logical=…x… clip=… blend=… alpha=… rgbMod=… target=…
```

Start a game and grep for `Credits blit:`. That line settles it: whether `SDL_RenderCopy`
returns non-zero, whether a clip rect is active, whether the texture is fully transparent
(`alpha=0`) or colour-modulated to nothing, and which render target is bound.

**Strongest remaining hypothesis**, worth checking first: `Game::renderFrame()` sets the render
target to `screenTexture` (created at `settings.video.width x settings.video.height`), draws
everything, then copies it to the backbuffer. If `screenTexture` is stale or smaller than the
current logical size after a resolution change, the sidebar's right-hand columns would fall
outside it. `setVideoMode` recreates it, but check that every path that changes
`settings.video.width/height` also recreates `screenTexture` — the interface-preset override in
`setVideoMode` runs *after* the window is created and could leave the two out of step.

### 3.2 Budget window looks blurry

Not reproduced. `CityBudgetWindow` uses the same `Label`/`Window` pipeline as the sidebar text
that renders crisply, and the screenshot supplied was scaled ~1.3x, which would explain it.

If it is real, the likely cause is §3.1's `screenTexture`: the whole frame is composited at
960x600 and then upscaled by ~2.67x to 2560x1600 with nearest-neighbour
(`SDL_HINT_RENDER_SCALE_QUALITY` is `"0"`). Non-integer nearest scaling gives uneven pixel
doubling that reads as mushy. Rendering the UI at native density, or choosing an integer scale,
would fix it — but that is a real change to the render architecture, not a one-liner.

### 3.3 Heavy factories not building

Not diagnosed. The diagnostics from §2.9 are in the build; play a game and grep the log for
`Production stalled:`. Check the unit limit first: `House::isGroundUnitLimitReached()` counts
`numGroundUnit + (numItem[Unit_Soldier]+2)/3 + (numItem[Unit_Trooper]+2)/3 >= maxUnits`, and
Stefan's Game Options screenshot shows "Override max. number of units" **ticked with a value of 0**.
`INIMapLoader` treats an override `>= 0` as authoritative, and `maxUnits == 0` is documented as
"unlimited" in `House.h` — verify that 0 really means unlimited on every path rather than
"no units allowed", because the override is applied before that comment's assumption.

### 3.4 Verification still owed

Nothing in §2.4, §2.6 or §2.7 has been confirmed in actual play. The city-placement changes in
particular are scoring heuristics and need a game watched for a few minutes.

---

## 4. Before committing

`CLAUDE.md` requires the version bump in the same commit as any release work, and CI verifies
that the tag matches. This session did **not** bump anything; the tree is still 1.0.534, which is
already tagged. Decide on 1.0.535 and run `scripts/bump-version.sh 1.0.535` before tagging.

Suggested split, since these are unrelated fixes:

1. Windowed resolution + HiDPI + DISPLAY menu (§2.1, §2.2)
2. Custom game lobby: remembered checkbox + restored second slot (§2.3, §2.4)
3. Credits SFX default off (§2.5)
4. City zone placement: overlap guard, road frontage, sand (§2.6, §2.7)
5. Game option defaults saved from any pre-game screen (§2.8)
6. Diagnostics: production stalls, credits blit, macOS test link (§2.9, §2.10)

The credits-blit block in `src/GameInterface.cpp` is a temporary probe — remove it once §3.1 is
solved, but keep the unsigned-arithmetic fix.
# Bundled user maps — 1.0.599

The user-authored single-player maps `4P - 192x192 - DuneCity.ini` and
`4P - 128x128 - 4 corners.ini` are now part of the default map set. Their
source is the local Dune City user-map directory on this Mac. The files are
kept byte-for-byte unchanged, including their CC-BY-SA metadata, and are
packaged under `Resources/maps/singleplayer` by the existing data copy step.
