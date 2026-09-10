# Session performance capture

From 1.0.620, performance data shares the existing per-session `events.jsonl`
stream in the user's `ai-decisions/<session>/` folder. Import it into SQLite
with the existing script. The game does not open SQLite on its simulation
thread. `DUNECITY_AI_TELEMETRY=0` disables the structured capture.

Every rendered frame contributes to an in-memory window. Approximately every
five wall-clock seconds (at a frame boundary), one `performance_window` event
contains sums, counts, maxima with cycles, and counts exceeding 33.333/100/250ms.
The slowest frame also includes its full phase breakdown, worst house, tick
setting, simulation cycles, unit/structure counts, search nodes and queue size.
Normal shutdown flushes the partial final window. Session byte limits still
apply; an interrupted process can lose its unfinished window. No per-frame SQL
or JSON output is added, and elapsed times never change simulation decisions.

`performance_windows` exposes window duration and worst frame context;
`performance_metrics` expands the measurements by scope, house, and item type.
`unit='us'` means microseconds; `unit='count'` means work counts or gauges.
The generic columns `total` and `maximum` deliberately retain those units.

Measurements include:

- `frame` and `frame.ai/city/path/units/structures/render/network`, including
  every frame, not just slow samples. Rendering includes presentation/VSync.
- `frame.cycles`, `frame.tick_ms`, `frame.paused`, and population of objects.
  The first window can include startup time; paused windows need separate
  interpretation. Frames/window wall seconds measures actual throughput;
  frame duration excludes the subsequent diagnostic serialization/browser yield.
- `house.update`, AI build, service investment/site search, placement,
  troop checks/attacks, tactical danger, defence and telemetry snapshots.
- `service.properties` and `service.scored_sites` quantify repeated placement
  work, with the latter separated by proposed building type.
- `city.budget/growth/effects`; effects split into pollution, terrain/value,
  density, crime/rebels and traffic/status. Tile updates/target queues also timed.
- `path.search` by owner and unit type, found/failed/invalid node samples, actual
  cycle budget (including carry), overshoot and pending queue. Search counters
  cover cycles with pending work, not idle cycles.
- `telemetry.write` includes JSON serialization and buffered stream writes/
  occasional flush; `telemetry.text_flush` times the legacy synchronous text
  writer. Performance-window serialization itself is excluded from these scopes.

Scopes are **inclusive**. For example, service search is inside AI build,
which is inside house update and frame AI. Never add all scope durations
together. At most 512 distinct scope/house/item/unit keys are accumulated per
window; instrumentation names are fixed, never object IDs or coordinates.
`dropped_samples` reports any overflow of that guard.

```sh
python3 scripts/ai-decisions.py --db /tmp/game.sqlite import '/path/to/session/events.jsonl'
python3 scripts/ai-decisions.py --db /tmp/game.sqlite query "
 SELECT session,scope,house,item,SUM(samples) AS calls,
        ROUND(SUM(total)*1.0/SUM(samples)/1000,2) AS mean_ms,
        ROUND(MAX(maximum)/1000.0,2) AS worst_ms
 FROM performance_metrics WHERE unit='us'
 GROUP BY session,scope,house,item ORDER BY SUM(total) DESC"
```

Find a scope's `max_cycle`, then inspect nearby events for the same session and
house to identify the decision/placement or combat context. Use weighted means
(`SUM(total)/SUM(samples)`), not a mean of window means. Legacy sessions have
no structured performance windows; reimporting cannot invent missing timings.

# Reviewed game: 10 September 2026

Session `1789026476214205-0`, version **1.0.618**, map
`2P - 192x192 - SimCity`, seed1427080984. Harkonnen(0) versus Sardaukar(4),
ended without a result at cycle109737. Local wall time17:47:56–18:03:13;
about29.3 simulation minutes. This predates the1.0.619 animation port.

Last complete legacy interval (1788 frames, about120 seconds, around cycle103142):

| Measurement | Mean per rendered frame | Maximum in interval |
|---|---:|---:|
| Whole frame |67.09ms (14.9FPS)|559.37ms|
| Pathfinding |32.86ms|67.66ms|
| House/AI updates |15.21ms|494.85ms|
| Rendering/presentation |7.83ms|36.62ms|
| Units |2.06ms|7.00ms|
| Structures |1.05ms|29.83ms|
| Network wait |0|0|

Nearly five simulation ticks per frame, with6.57ms pathfinding per tick.
Search budget5000 nodes/tick, actual16106 average; path completion97.9%,
peak pending queue261. Source confirms budget is checked *between complete
searches* (`Game::processPathRequests` calls `resolvePendingPathRequest`
without a remaining-budget argument). A large search overshoots the nominal
budget. This is sustained CPU load, not primarily network or rendering lag.

Across the game,1221 house updates exceeded10ms; maxima H0=494.8ms,
H4=473.3ms. At cycle108446/H4,16 service candidate evaluations accompany the
473ms update. At109700/H0,18 accompany390.6ms. At101200/H0,7 accompany494.8ms.
Source scans candidate sites/properties repeatedly for construction yards;
these are strong suspects, **not yet measured attribution**. Other slow
updates have few decision events, so don't assume placement explains all AI.
City effects/growth also produce periodic10–30ms spikes; adjacent simulation
ticks often execute within the same rendered frame.

Legacy slow-frame samples are thresholded/rate-limited and cannot establish
whole-game frame percentiles. They also sometimes misattribute the worst house:
that field was overwritten each tick instead of retaining the frame maximum.
Per-frame node and failure counters were never reset, producing absurd token
peaks (over1billion). These diagnostic bugs are fixed in1.0.620.

Next optimization targets, after scope capture: cache/bound repeated AI service
searches; make large path searches resumable with deterministic node budgets;
then distribute measured expensive city phases. Do not simply hard-abort a
route at5000 nodes (that changes reachability), change simulation work by local
wall-clock timing, or blame the new animations using this older game.

Local evidence: the original performance text was copied alongside this session
as `performance-legacy.log`. `/tmp/dunecity-last-game-performance.sqlite` holds
80456 structured events plus7836 legacy slow-frame samples and1221 house
spikes in separate `legacy_frame_spikes`/`legacy_ai_spikes` tables. The legacy
tables are a one-off analysis import, not part of the forward capture schema.
