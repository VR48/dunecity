## Direct P2P branch checkpoint — 13 September 2026

Branch `feat/p2pkit-direct-crossplay` is under test; **not deployed**. Live remains 1.0.661.
The branch vendors P2PKit commit `94ae7eb8818a629478e0a6ba0aa3232c5fc0b1ab` RTCTransport and
framing, with direct-only bounds. Browser gameplay uses those actual modules; native crossplay
uses pinned libdatachannel `443f6934d9007eb7076ab7825ba330f355fcbead` with compatible framing.
Apache/PHP only serves admission, public lobby/chat and SDP/ICE introductions. No TURN or
in-game forwarding/relay. Existing native ENet remains available.

Opus supplied the initial implementation and part of the review corrections. Stefan explicitly
asked Codex to **stop using Opus** on 13 September; do not resume its sessions for this task.
Codex completed the remaining fixes and owns coding/testing. Hermes has supplied independent
security findings; a fixed snapshot review is still running. Do not describe that as approval.

Current checks: six native CTest suites pass; 160 PHP HTTP/concurrency tests pass; vendored RTC,
bridge, bounded Fetch, framing and package tests pass. Real browser↔browser and browser↔native
RTC transport tests passed previously; three native full sessions passed a 75-second outage of
all PHP workers. The final-source repeat and full browser game acceptance remain in progress.
A full browser game join uncovered unsigned timeout underflow when a new link was created after
the frame clock sample; the signed deadline fix and advancing-clock regression now pass natively.
The browser rebuild containing that fix is pending. A failed pair now requires fresh admission
instead of repeatedly generating new certificates under an existing pair identity.

Security corrections include atomic grant redemption+seat commit in one authoritative room file,
authoritative directory snapshots despite stale cache, conditional expiry deletion under lock,
strict duplicate-fingerprint rejection, native ordered/reliable channels and bounded SCTP message
size, shared bounded HTTP admission/signaling, and roster freeze before STARTGAME fanout with
failure propagated to the host menu. Guest roster freezes at receipt of authorized host START.
See `docs/direct-play.md`, `tools/p2p-signaling/README.md` and `tools/p2p-session-smoke/README.md`.

## Public HTTPS polling acceptance — 13 September 2026

**1.0.661 is published on the normal website and desktop release channels.** Source
and stable tag are 43ec1dc. Candidate CI 34707717946 and stable run 34708449665 passed.
All six published GitHub packages match their API checksums. SourceForge run
34708945092 passed all uploaded checksums and verified Windows/macOS/Linux defaults;
an independent HTTPS git read confirms its source branch and tag point to 43ec1dc.
Browser publication 34708945094 and website deployment 34709214712 succeeded at
website commit 8f348f4. The live `/play/build.json` identifies 1.0.661 / 43ec1dc;
all six browser asset hashes, relay origins, CSP and WASM MIME type were checked.
Fresh production UI startup displayed 1.0.661 and opened the online lobby without
the previous crash. Relay health remained OK with zero rooms/connections after
deployment. Evidence: `../outputs/network-hardening/public-661-deployed-hashes.json`
and `../outputs/network-hardening/release-661-github-verification.json`.

Public 1.0.659 matches overflowed the polling queue (close 4431). 1.0.660 at 52ac12e
paces relay command history every 100 ms with a retention guard and removes two
redundant browser waits. Public browser/native and browser/browser matches then ran
approximately 22 and 19 minutes and ended intentionally, but simulation advanced
only about 37–42 cycles/s against 62.5 configured despite 60Hz browser RAF callbacks
(these callbacks are not direct SDL render-frame instrumentation).

Actual Claude Opus implemented a bounded startup allowance; Codex narrowed it to
HTTP polling, preserving ENet/WSS sizing and CommandValidation's existing bounds.
1.0.661 budgets 700–1120 ms, capped at 70 cycles, fixed for the match. It requests a
heartbeat at join and has a fallback before an answer arrives. More allowance adds
input delay; arbitrary jitter/asymmetry and faster game settings can still stall
lockstep. This is a bounded improvement, not elimination of network latency.

Actual Hermes and Opus found no code/security blocker in the final narrowed source.
Native CTest 6/6, dependency audits, generated-fetch regression and wasm32 network
wire 80 checks pass. Public crossplay ran over ten minutes and browser/browser over
seven, with movement, MCV deployment and Windtrap construction. Final retained digest
samples matched (148 crossplay and 124 per browser client), with no captured premature
close. Hosts exited normally and health returned to zero rooms/connections. Twenty-
second RAF captures averaged 16.66 ms; simulation was roughly 56–57 cycles/s. These
samples do not prove complete determinism or performance on every connection.
Evidence: `../outputs/network-hardening/public-661-evidence.json` and
`../outputs/network-hardening/poll-latency-review/`.

The website's scheduled download-count deployment removed the previously manual
relay gateway during test startup. Website main now includes the companion gateway
and analytics source at 9a85d48; deployment 34707911135, web security 34707911134 and
analytics compatibility 34707911193 passed. Relay health recovered and both actual
matches ran through this tracked gateway. Keep these files in website main: routine
rsync --delete removes anything merely uploaded to the webroot. Preview directories
also disappear on website deploy, so restore a preview only after that deploy ends.

The existing server now runs the restricted-account Apache/PHP HTTPS gateway at
`https://dunelegacy.com/relay`, with Node bound only to loopback. Deployment revision
10cd89f has v2 artifact manifests, release pinning and a single supervised child with
cron recovery and kernel parent-death protection. Actual Hermes rechecked and cleared
the two deployment findings (release-switch execution race and missing root-directory
permission checks). 38 manifest checks, 42 supervisor checks and the release-pinning
fixture pass on the server. Actual hung-child recovery took 80 seconds; killing the
supervisor recovered through cron in 70 seconds. No administrator access was used.

Actual public transport harnesses exchanged 17 matching digests each with no mismatch;
this proves transport only, and the later actual-game failure above supersedes any
readiness inference. SQLite schema-2 migration preserved the backed-up legacy rows;
signed relay lifecycle records arrived over local HTTPS. External event submissions
return 403. Bounded concurrent gateway requests and a short-body timeout test passed.
The limited test was not a capacity or DDoS certification.

## Crossplay candidate — 12 September 2026

Final candidate CI `34694065319` is successful on Windows, macOS and Linux, with
all six packages downloaded under `../outputs/network-hardening/release-657-artifacts-b4c6af3/`.
Actual Hermes's final installer recheck (`20260912_124943_2a850b`) clears the six
previous findings for bundle source `c1bd25e`. The archive is staged outside the live
website under `/home/dunelegacy-deploy/relay-deployment-c1bd25e/`; its checksum was
read back and verified. Operator instructions: `../outputs/network-hardening/relay-administrator-handoff.md`.
Administrator access and actual public WSS/proxy/SQLite verification are still required.

Branch `fix/network-hardening` is published for candidate packaging as
`release-1.0.657`; current main `8879732` is incorporated. Production main and the
stable tag are deliberately not advanced: the restricted metaserver SSH account
cannot install services/Apache configuration and no administrator access is known.
The configured production crossplay endpoint is `https://dunelegacy.com/relay`.

Native 5/5 CTest, 185 relay tests, 173 wasm32 wire checks, secure-WebSocket feature
checks and browser package policy/hash checks pass. Forced libcurl partial-write
fixture passes. Actual Claude and Hermes findings and scope are recorded in
`docs/crossplay-final-review.md`. Opus implemented the administrator bootstrap
hardening; Codex reviewed it and ran its 53 isolated helper checks. Public TLS/WSS,
proxy behavior and signed SQLite delivery still need on-host verification.

Stefan joined a browser-hosted match from the native app through the public list.
Actual browser/native digest samples agree through cycle 40,600, including a browser
menu interval; these are samples, not proof of complete determinism. Current running
clients predate the latest lobby presentation changes. The public join button is now
full-width immediately below the game list; private joining says “Join with invite
code”. These changes are committed and built for preview separately from the match.

The initial test-app launch failure was macOS CODESIGNING/Invalid Signature after
copying a binary; its bundle was re-signed. A pre-existing native test process survived
SIGTERM and remained on its earlier executable. Do not mistake copying a binary or a
new launch command for replacement of that process. Avoid interrupting Stefan's match.

# Browser match logging — 1.0.655

Extends the Play Online display hotfix below. Both start/end summaries carry an
optional schema-3 client_runtime (browser/native). Emscripten bypasses the native
MetaServerClient SDL thread, which is unavailable without pthreads, and queues
same-origin POSTs through the shell with two bounded attempts. Form payloads
avoid GET length limits; small requests use keepalive. Game code never waits on
analytics. Tab close/crash can still leave a start-only match.

Metaserver commit e02d40d adds client_runtime to analytics_matches in both PHP/PDO
and Python backends. Legacy/missing/invalid values default to unknown; missing
later values preserve known runtimes. No historical classification is invented.
Deployment 34673787973 and compatibility CI 34673787975 passed. Live health
migrated 1220 existing matches as unknown. SQLite backup before migration:
/tmp/dunecity-runtime-655/live-before.sqlite on metaserver. Query details and
compatibility tests are in the website repo's metaserver/ANALYTICS.md.

Local Emscripten and native Release builds passed; dependency audits and CTest
passed (588 passed, 3 expected skips). Actual Chrome skirmish start and quit
produced matching start/end IDs, client_runtime=browser, version 1.0.655, and
abandoned outcome (649/2602-byte summaries), captured at
/tmp/dunecity-analytics-events.jsonl. Shell tests cover ordered POST/retry/offline
behaviour. The packager now handles Emscripten's unquoted minified HTML attributes;
a regression test verifies all three asset references receive the build token.
Published website commit d2aebcd; deployment 34673987445 and browser security
check 34673987519 succeeded. Live manifest and all six SHA256s match game source
7124425. Production Chrome loaded 1280x720 with all five asset URLs sharing
?v=1.0.655-2c0245d2a43c. An actual live skirmish generated HTTP 200 OK for start/end
of m1-65b41f2ce0dc0-73d992ca-65b41f2ce0dc0; production SQLite confirms browser,
1.0.655, skirmish, abandoned, timestamps and both JSON records. All 1220 historic
match rows match the pre-migration snapshot byte-for-value on original columns.
The brief abandoned smoke-test row remains identifiable by that match ID.
Installed desktop remains 1.0.653; no new desktop release tag was published.
The desktop CI jobs triggered by the main push are separate from the verified
local native build and published browser package.

# Play Online display fixes — 1.0.654

Browser hotfix based on the latest released 1.0.653 gameplay. Removed the web
640x480 reset and SDL_WINDOW_RESIZABLE (SDL otherwise substitutes the CSS size
for the requested backing buffer). The shell fits the actual canvas ratio into
the stage. Display exposes working 4:3/16:9 controls; Options offers 19 backing
resolutions through 3840x2160, plus the saved custom size. Desktop default is
1280x720, rising to 1600x900/1920x1080 when the stage fits; small touch screens
start at 854x480. A one-time browser config marker migrates old forced VGA while
preserving subsequent deliberate VGA selections and other saved resolutions.

Emscripten 4.0.14 package built locally in /tmp/dunecity-web-build. Chrome tests
verified actual backing sizes, both aspect buttons with Automatic selected,
1920x1080 through the Options dropdown, reload persistence, browser resize and
fullscreen, old-VGA migration, explicit-VGA preservation and fresh touch default.
Native Release build/dependency audits passed; CTest: 588 passed, 3 expected skips.
Three initial Node shell tests cover defaults, aspect fitting and common asset versioning.
Local app rebuilt in build/bin; installed /Applications app left as 1.0.653.

Shared Python packager (also called from PowerShell) records source commit and
SHA256s, versions shell/WASM/data URLs together, and includes web/.htaccess.
Publish browser build follows successful stable desktop releases and refuses a
downgrade; see docs/release-operations.md. This workflow is configured for future
releases; local browser hotfix publication is verified separately below. Browser
multiplayer remains unavailable. The existing reports/ directory is unrelated.

# Public co-op release published — 1.0.653

Release version bump only over the user-tested 1.0.652 gameplay. Includes all
unreleased fixes since 1.0.642 plus campaign/skirmish shared-house co-op.
Release notes are releases/desktop/1.0.653.md. Local Release build and before/after
Ninja dependency audits passed; CTest reports 588 passed and 3 expected skips.
Installed /Applications/dunecity.app, SHA256
9647fa6549b42527dfb7dc42251d5c94683cbb4134d484d4903adb14cd2344e9.
Backup: /Applications/.dunecity-653-fvc2i619/dunecity-previous.app.
Published and verified on 2026-09-12. GitHub tag v1.0.653 points to eaaff4a753d00fe2916c1d8f9c951ae2193cd54d.
Build Dune Legacy run 34672540279 passed Windows, macOS (Mac mini), Linux,
Linux tests and release publication. All six packages are present in the stable release:
https://github.com/VR48/dunecity/releases/tag/v1.0.653.
SourceForge run 34673037345 succeeded: six packages plus notes/checksums were
read back and hash-verified; all three OS defaults now select 1.0.653.
SourceForge dunecity branch and peeled dunecity-v1.0.653 tag match eaaff4a.
Website commit dcfec4a / Deploy to Droplet run 34673168756 succeeded; live home,
Dune City, co-op guide, modding and sitemap match the published source. Co-op is
announced as available and all desktop download links point to 1.0.653.
Legacy SourceForge master commit 591b9e1 adds co-op hosting guidance. The changed
downloads.html was backed up, uploaded atomically, read back and byte-verified
against both the source and public page. Original user profile remains untouched.
Concurrent uncommitted 1.0.654 browser work and reports/ are outside this release.

# Internet-listed co-op smoke test — 2026-09-12

Tested installed 1.0.652 using two independent local app bundles, profiles and
ports (29851/29853). Host Campaign Co-op registered successfully with the live
metaserver; its list2 response and the guest's Internet Games UI both contained
the lobby. Guest joined from that listing, passed mod/config verification, and
both human controllers entered Ordos mission SCENO001.INI. Before save/reload,
337 valve-debug and 42 daily CitySim rows had identical shared prefixes, with
no desync logged. During Stefan's interaction, the guest rehosted a campaign
save and the original host joined through Internet Games; both completed all
save-load stages and resumed the shared game with reversed network roles.

Same-NAT detection selected the local address for game packets. This verifies
real Internet discovery plus local co-op joining/play/save hosting, not a
connection across separate routers. UPnP discovery found no usable IGD; STUN
and metaserver registration succeeded. The second local instance could not
bind the shared LAN discovery port and retried every five seconds, but Internet
listing/joining worked independently. Toggle buttons require Space rather than
Return for keyboard activation (Button::handleKeyPress).

Evidence snapshots: /tmp/internet-test-dunecity-internet-host.log and
/tmp/internet-test-dunecity-internet-guest-local.log. Live profiles are
/tmp/dunecity-internet-host and /tmp/dunecity-internet-guest-local; test apps
DuneInternetHost.app and DuneInternetGuestLocal.app remain open for Stefan.
Stefan subsequently confirmed testing two campaign missions and save/reload.
Final logs show both clients load SCENO002 after the shared save, and shut down
normally. After save/reload, 230 comparable CitySim valve/day records match;
after mission transition, 101 comparable records match. The first mission's
379 pre-load records also match. DESYNC DEBUG entries are routine diagnostics,
not reported desynchronizations. Final evidence snapshots use the prefix
/tmp/internet-test-final-dunecity-internet-*.log. Both test apps are now closed.

Published website guide https://dunelegacy.com/coop.html with a Multiplayer
Co-op navigation tab and home/DuneCity announcements (website commit 964ac35,
Deploy to Droplet run 34671938595 successful; six live files byte-verified).
Home link reads "Play campaign co-op with your friends online!" per Stefan.
Guide covers hosting, joining, QuantBot partners, skirmish and solo/shared saves.
At that checkpoint public desktop downloads were still 1.0.642. The 1.0.653
release above supersedes that pending state and updates the guide/download links.

The Mac mini guest was stopped after switching to the requested local test.
Its portable bundle needed SDL3 explicitly included beside SDL2: Homebrew's
sdl2-compat loads SDL3 dynamically, so otool dependency traversal alone misses
it. No game source, installed app, normal profile, or remote release changed.

# Campaign and mission shared-house co-op — 1.0.652 (local)

Campaign house selection and single-mission/skirmish setup have Host Co-op.
Multiplayer has Create LAN Game, Host Internet Custom Game, Host Campaign Co-op.
The campaign setup chooses house and mission (1–22), Internet or LAN only,
or loads a solo campaign save (save/) or shared campaign save (mpsave/).
The fixed-house lobby exposes exactly two controllers: primary human plus
another human or QuantBot (Easy/Medium/Hard/Brutal/Defend; support variants also
available). Existing server listing, connection, mod/config synchronization
and countdown machinery is used. Scenario enemy identities/teams are preserved.

Campaign continuation preserves both controllers and campaign progress, with
host-selected next scenario/seed sent reliably to the client. Old simulation
packets are rejected using their mission seed; callbacks are cleared before
replacing Game. Initial shared missions skip the blocking solo briefing.
Save loading distinguishes the original solo/network binary layout before
converting to co-op, including the outer mod header and saved house colors.
Existing matching partner state is retained; new partners initialize only
after all saved objects exist. New controller setup also preserves planned
future campaign enemy slots not present in the current save.

Stefan observed an inactive QuantBot partner after advancing an early mission.
The partner was present, but Campaign mode only rebuilt initial scenario
buildings: a human starting with only a construction yard had no windtrap
baseline. QuantBot now detects an actual HumanPlayer sharing its house and
uses Custom/normal economic and military planning at the selected difficulty,
while still obeying mission tech/build availability. Campaign enemies retain
campaign behavior. This also migrates saved partners on their first update.
Fresh QuantBot initialMilitaryValue=-1 is a serialized pending-init sentinel:
new midgame partners no longer skip initialization because cycle!=0 or read
uninitialized initialItemCount. Existing initialized bots keep saved baselines.

Save format is 9837 and network protocol 5. Both network clients need 1.0.652;
older saves remain readable, older executables reject new saves. POSIX
DUNECITY_USERDIR optionally selects an absolute isolated profile; ordinary
user paths are unchanged. Tests used separate app IDs, profiles and ports.

Validation: dependency audits before/after Release build, ctest (588 passed,
3 expected skips), version/whitespace checks, app signature and binary hash.
New tests exercise shared controller/settings/scenario round trips, human and
bot next-mission retention, old save headers/colors, malformed headers, and
preservation of absent future enemy slots when hosting a save.
Two separate localhost clients joined Atreides, ran in lockstep (462 CitySim
valve rows and 57 day rows had identical shared prefixes), and saved a shared
campaign. A separate test loaded that save with a new QuantBot Easy partner
at tech1/cycle15747; correct initial counts and Custom mode were logged.
Accepted construction began Windtrap, Residential, Industrial, Refinery,
then further economy/power construction. Stefan also confirmed it worked.
Evidence: /tmp/coop-two-client-host.log, /tmp/coop-two-client-guest.log;
/tmp/dunecity-coop-economy-verify/ai-decisions/1789179844509552-0/events.jsonl.
No live two-machine Internet test or complete two-human campaign transition;
next-mission settings are covered by tests and the bot transition was observed
in Stefan's test. Original user profile/logs were not overwritten.

Installed /Applications/dunecity.app, SHA256 4265200d009b95f97957b823b513282f4dcafc540a4a62a3d982cc99b3f885b6.
Previous app: /Applications/.dunecity-652-fyt_4fmo/dunecity-previous.app.
No remote deployment. Existing reports/ remains unstaged.

# Original single-mission house identities — 1.0.651 (local)

Current session1789170044652756-0 loads SCENA022.INI as GameType::Skirmish.
Its loose data/scena022.ini is a Tornie import (5a172ce) that changes original
Harkonnen -> Ordos, Sardaukar -> Harkonnen, Ordos -> Mercenary. House IDs were
instantiated correctly from that wrong source. Campaign already used
openCampaignFile; the single-mission picker bypassed it outside Tornie.

INIMap now routes both Campaign and Skirmish through openCampaignFile. Original
A/H/O scenarios resolve from SCENARIO.PAK; Tornie retains its campaign resolver.
Custom games/multiplayer still use their supplied map data. No hardcoded house
swaps or save-format changes. Original SCENA022 has Harkonnen left, Sardaukar
middle, Ordos right (the user's remembered left/right order was reversed).
Verified original units, structures, teams and reinforcements consistently
reference those houses; Mercenary absent. Existing saves retain old identities;
a newly started mission in the new executable gets the fix.

Release build, full ctest suite, before/after Ninja dependency checks, version
and whitespace checks passed. No fresh interactive mission run; current game
and logs preserved. Installed /Applications/dunecity.app SHA256 c5cb7207a0fecf98d1e41d1d8cb0a020437ef32c2a747d5b0664f4a673d847e1.
Previous650: /Applications/.dunecity-651-691z6krw/dunecity-previous.app.
No remote deployment; existing reports/ remains unstaged.

# Main-base proximity for MCV expansion — 1.0.650 (local)

Stefan reported MCVs passing nearby rock and explicitly requires distance to
main base to rank first. Current648 session1789167702882520-0, Ergsun-Fwiffo
seed740015832: Harkonnen MCV243 at cycle50750 selected47,72 via105 route tiles;
MCV257 at52350 selected124,107 via211 tiles. Previous policy maximized enemy
clearance, then room, and only finally MCV travel distance, causing long detours.

Eligible rock now ranks by Manhattan map distance from the oldest surviving
active construction yard first. Enemy clearance, local room and MCV route
length only break ties. No weighted safety detour overrides base distance.
Actual MCV-ground BFS remains a separate reachability filter; own occupied and
reserved formations, unsafe tiles, <48 free/local tiles and <12 enemy clearance
remain excluded. The original yard anchors expansion until destroyed, then the
oldest surviving yard replaces it; no averaged multi-base centre. First-yard
legacy placement unchanged. Telemetry v67 includes main-base anchor, distance,
route length and nearest-main-base selection reason. Save format unchanged.

Validation: 583 tests passed, 3 skipped; dependency audits, version/whitespace
checks and app signature/hash verification passed. Tests cover near small vs
far large islands, unnecessary enemy-clearance detours, actual threats,
unreachable/reserved rock and an MCV already beside the far island. No fresh
full-match verification. Current game/orders were not modified in memory.
Installed /Applications/dunecity.app SHA256 e4f33bd7442e18cb5d9de544398a263957acfb15f5aee99c0d4e6a72debd9723.
Previous649: /Applications/.dunecity-650-ne1awl99/dunecity-previous.app.
No remote deployment. Existing reports/ remains unstaged.

# Wider lobby AI selectors — 1.0.649 (local)

Both custom lobby player dropdowns widened from100 to180 logical pixels,
including their expanded lists, to fit QuantBot and support AI names.
Release build and before/after Ninja dependency audits passed; whitespace and
strict signature/hash verification passed. No new tests for this layout-only
change; no interactive visual run. Current app was not interrupted.
Installed /Applications/dunecity.app SHA256 aff77514e5f64c43ce28fdafa93b5d7ea3dcea0e04ab05826f84d597baa68d4c.
Previous648: /Applications/.dunecity-649-qxc4o3tw/dunecity-previous.app.
No remote changes.

# Ornithopter capacity, safe defence and AI defaults — 1.0.648 (local)

Current 647 session 1789142209744263-0 (Ergsun-Prometh, seed1050788573),
Sardaukar house4: High Tech completed cycle56346 at89,37 and IX cycle63946
at80,1. Both remained at400/400 health at cycle140646. At cycle161546,
Sardaukar had ~51.8k credits but army79950/80000 and air deficit569 at a
600-credit ornithopter price. This observation does not justify forced extra
factories at the cap. Source did reveal extra air capacity was behind all
heavy expansion (unless24 heavy factories), and required every air factory
currently producing an ornithopter, hiding mixed carryall workloads.

- Additional High Tech: funded unmet ornithopter demand of at least one plane,
  >=75% busy capacity, an operational unlocked producer, aircraft/military
  headroom, funds for factory + aircraft + working buffer. Queued factories
  prevent duplicate capacity. Evaluate before optional light/heavy expansion;
  essential economy, power and civic priorities remain. No one-factory cap.
- Ornithopters raid exposed buildings first; ground units are candidates only
  when in weapon range +3 tiles of a live own building or harvester. No roaming
  unit hunts. Defence uses the same visible launcher/rocket-turret coverage
  checks as raids, including footprint and direct approach. Safety margin is
  five tiles beyond weapon range (previously two), including turning room.
  Temporarily unpowered enemy rocket turrets still prohibit attacks nearby.
- General damage-response scramble previously bypassed safe-air planning and
  could assign ornithopters directly into covered combat. Exclude them and
  discard legacy air defence assignments. Safe planner owns their orders and
  guard points. On withdrawal use a reachable safe actual owned building,
  avoiding a dangerous base centroid; inside new AA coverage find a nearest
  safe straight exit. No guarantee against hidden/moving AA or missiles
  already in flight. Ground contact checks use LocalPointIndex.
- Dropdowns lead QuantBot Easy, Medium, Hard, Brutal, Defend, then other AIs.
  Registry labels now spell QuantBot; class identifiers remain qBot* for saves.
  Campaign and skirmish launch mappings match displayed order. New campaign
  default and fallback/config-generation default are qBotEasy. Existing explicit
  saved settings remain user choices. The campaign selector visibly starts Easy.
- Telemetry policy v66 adds usable air producers / military headroom and raid
  versus defensive-intercept reasons. Derived logic only; save version9836.

Validation: 582 tests passed, 3 skipped, dependency audits, version consistency,
menu-to-launch mapping checks, whitespace checks. Coverage includes mixed air
workloads, pending capacity, tech/funds/unit-cap blockers, raid ranking and
withdrawal through/around AA coverage. No fresh full-match or interactive menu
run; the live game was not interrupted. Local app signature/hash verified.
Installed /Applications/dunecity.app SHA256
de20ef211518ab0c038928bcecc1b724e29627076ca5612c32009d342d113216.
Previous647: /Applications/.dunecity-648-6z69dkyn/dunecity-previous.app.
No remote release or website changes. Existing reports/ remains unstaged.

# Unloading queues and safe rock expansion — 1.0.647 (local)

Stefan reported harvesters queueing at refineries and three adjacent MCV
expansions instead of colonising new rock. Current 646 session
1789140497380770-0, Ergsun-Prometh seed1217567228, Rebels house7:
cycle60293 economy forecast recorded 26 workers / 3 refineries, but zero
worker income/bay capacity and an empty refinery candidate. Forecasting was
inside the new-site guard, conflating an unavailable candidate/site with no
capacity pressure. New diagnostics distinguish availability and site failures.
MCVs also auto-deployed at a legal factory exit before selecting a site;
fallback search was only +/-25 tiles with a strong distance penalty.

- Scan owned dropoff occupancy and active loaded harvesters returning within
  six tiles of a busy bay. A net queue of >=2 beyond free bays sustained for
  ten simulated seconds triggers capacity investment. A pending refinery
  suppresses duplicate queue-based orders; normal fleet-capacity forecasts
  still account for committed bays. No harvester production cap added.
- Forecast worker/bay throughput using an existing refinery if a new site is
  unavailable. Queued cargo earns conservative relief credit (at most two
  loads, capped to actual waiting cargo) without assuming additional spice.
  Profitable queue relief reserves yard funds before optional civics/defence/
  zoning. No valid site still cannot authorize an illegal placement.
- Every fifteen simulated seconds survey free rock in current build range.
  Under 48 tiles, or sustained unloading queues with no refinery site, a
  feasible new rock site raises the yard target by one. Reserve MCV money
  from other producers; the selected heavy factory saves for the MCV/unlock
  instead of spending the same funds on harvesters or optional units.
- Expansion MCVs search distinct cardinal rock formations across the map.
  Require >=48 free rock tiles locally and in the formation, a 2x2 footprint,
  and a ground route avoiding known fire/recent-loss tiles. Rank distance
  from visible enemies first, usable space second, travel distance third.
  Own occupied formations and other MCVs' reserved formations are excluded.
  Hidden enemies are not consulted as tactical observations. The actual unit
  movement engine still chooses its route; this does not guarantee its route
  follows the survey's safe route or remains safe as enemies move.
- Expansion MCVs cannot immediately deploy at the factory exit. They deploy
  at assigned sites with fresh local danger/access checks; invalid coordinates
  are never ordered. First-yard deployment retains existing behaviour. If no
  new formation exists but current base has ample free rock, ordinary local
  capacity expansion remains possible; with insufficient space, wait/retry.
  Failed MCV surveys are throttled to five seconds. All new planning state is
  derived, not serialized; save version remains 9836.
- Telemetry policy v65 adds refinery busy/bookings snapshots, waiting cargo,
  queue pressure, placement rejection details, expansion site/room/enemy
  clearance/route length and free-base-rock fields.

Validation: 580 tests passed, 3 skipped; dependency audits, version consistency,
whitespace checks and strict deep signature verification passed. Added queue
burst/free-bay/pending-bay cases and safe/new/reserved/unreachable/threatened
formation tests. No fresh full gameplay run: deployment behaviour and queue
relief must still be observed in the next game. Current 646 game untouched.
Installed /Applications/dunecity.app 1.0.647, matching build SHA256:
01352468280c24aadb6020717f765b4878188671eda7cac95b95ccd5174b4a13.
Previous 646: /Applications/.dunecity-647-1n8_vg2n/dunecity-previous.app.
Intermediate 647 backup: /Applications/.dunecity-647-final-o6q66g18/dunecity-previous.app.
No remote release or website changes. Existing reports/ remains unstaged.

# Brutal opening economy, demanded civics and police anchoring — 1.0.646 (local)

Stefan's running 1.0.645 Harkonnen Brutal game, Moshpit seed 406506788,
session 1789138202535608-0, was slow to compound. Refineries completed at
cycles 2200 and 7100; the first factory harvester at 23505 (~6.3 simulated
minutes), with two bays still at cycle 81700. At cycle 73700 the refinery
forecast was cost 461 / four-minute proceeds 689 versus R cost 109 / proceeds
53, but a worker-capable factory vetoed the refinery. The broad tax hedge
also overrode early refinery ROI, and factory priority ended at four workers,
then required military capital twice worker capital plus a tank cash reserve.

- Brutal custom city games prioritize up to eight committed workers, bounded
  by the existing remaining-spice/map target. Lower difficulty/vanilla policy
  stays unchanged. This is a priority floor, never a worker cap. Afterward,
  Brutal compares equal army/worker capital rather than 2:1; the map target
  remains authoritative. Preferred workers can use their own purchase money
  without the optional IX reserve or an additional tank reserve. Optional
  custom orders cannot preempt them. Emergency reconstruction still applies.
- Preserve the first demanded R hedge. While the opening workforce is short,
  defer the broad one-third tax hedge and permit an economically worthwhile
  third refinery despite a worker-capable factory. The pre-factory opening
  no longer buys six lots before tech; it compares profitable refineries and
  then bootstraps vehicle production. Short/unsafe spice trips still lose on
  the existing cost/power/delay/risk forecast. Beyond the opening, more bays
  require fleet throughput pressure. Included workers count as committed.
- First carryall eligibility still begins at four workers, before a second
  heavy factory, while Brutal continues toward eight. Repair/optional tech
  waits for the larger workforce floor.
- Actual NeedStadium/NeedAirport demand selects a feasible civic investment
  before optional services, production, tech and further zoning. Reserve its
  purchase price from other factories (initial worker recovery remains an
  exception), and wait for funds instead of repeatedly buying cheap plots.
  Essential power/initial economy precede it. Committed civics suppress
  duplicates; unavailable/unplaceable civics do not lock money. No premature
  airport based on total population; palace-satisfied R has no stadium demand.
- Crime at industrial zone (30,4), cycle 83600: pre-police 275, coverage 42,
  final 233, with PD (24,5), rocket (29,3), full funding and sufficient power.
  policeSource incorrectly anchored service at its first adjoining road,
  shifting the source across six-tile district boundaries. Road access now
  affects strength only; the central occupied building tile anchors service
  (lower centre for even footprints). Runtime and placement estimates share
  this helper. Existing Micropolis diffusion, strength, funding/power/road
  penalties and turret 15% contribution remain. Regression reproduces this
  boundary layout and drops below Dangerous without a strength buff.
- Policy telemetry v64 adds opening refinery/Brutal opening fields and civic
  investment/funding decisions. Save format remains 9836.

Validation: 578 test cases passed, 3 skipped via ctest; dependency audits,
version consistency, diff whitespace and strict deep app signature passed.
Four added regression cases cover difficulty/spice limits, the observed
refinery ROI veto, civic feasibility/commitments, and road-side-independent
police coverage. No fresh full match was run; next-game growth speed remains
an empirical check, not a claimed measured improvement.

Installed /Applications/dunecity.app 1.0.646 without interrupting the running
645 game. Built/installed executable SHA256:
98af57a74efb6adc8020c9dd85d7fdac7934ccfbeb3181d246c2f1ddb8198dc8.
Previous app: /Applications/.dunecity-646-ep1iz24v/dunecity-previous.app.
No remote push, release or website deployment in this task. Existing untracked
reports/ belongs to earlier game analysis and was not staged.

# Outlying placement and city reinforcements — 1.0.645 (local)

Stefan reported finished R zones stuck despite open sites in widely separated
edge districts (Harkonnen/Neutral, live 1.0.642 Moshpit game). Verified that
findPlaceLocation searched only +/-50 tiles from the arithmetic base centre.
The screenshot supports this limitation; do not claim each shown tile was
individually proved legal. Normal build-range, terrain, occupancy, pollution,
road/exit, reactor and threat restrictions remain in force.

- Placement first evaluates its existing central region, then, only if no
  suitable site exists, the remaining map origins. Passes are disjoint; both
  results use the existing per-build cache and waiting-yard scheduling.
  Successful central searches incur no broad second search. Diagnostics add
  search centre/pass and reservation/road/neighbour rejection counters;
  completed-yard deferrals include placement quality.
- Harkonnen city-sim light factories can build trikes. The existing Harkonnen
  high-tech ornithopter exception is preserved in the shared CityFactionPolicy
  helper. Tech/upgrades/enabled flags still apply. Vanilla faction restrictions
  remain unchanged. Actual build lists feed QuantBot's available unit mix.
- Police always use Palace's HOUSE_FREMEN cooldown (5 simulated minutes),
  replacing twice the owning faction's palace cooldown (20 min for Harkonnen,
  10 for most houses). Patrol remains 3 troopers + 1 trike with existing caps.
  Harkonnen's earlier JSONL already records successful one-trike patrols; it
  was factory availability, not a universal police spawn ban.
- Airports automatically deploy a pair of free ornithopters every Palace
  Harkonnen missile cooldown (10 simulated minutes). Starts with a full timer;
  requires power to deploy, observes air-unit caps and enabled units, retries
  local blocked/capped deployment every five simulated seconds. Only on-map
  unoccupied air tiles qualify (AirUnit::canPass always returns true). Partial pairs
  retain only their missing aircraft; cooldown resets after the full pair.
  AI aircraft start STOP for QuantBot's explicit safe-target controller; human
  aircraft start GUARD, not Hunt. Airport sidebar shows countdown/power/cap
  status. New airport_unit_spawned/airport_reinforcements telemetry.
- Save format 9836 adds airport countdown and pending pair count. Older saves
  give existing airports a fresh timer. New saves restore partial batches.
  Policy version v63. Existing detailed telemetry stops routine capture near
  240 MiB of its 256 MiB limit: the still-running game's events.jsonl ends at
  cycle ~541k while Dune City.log continues past 1.1m. This limits attribution
  of the latest screenshot; do not present the old JSONL snapshot as current.

Installed /Applications/dunecity.app 1.0.645; strict deep signature verified.
Built/installed executable SHA256:
a3c883454b9e01707037c02a0cd2601f56e9c262b21c5b2154d21db057744099.
Previous app: /Applications/.dunecity-645-b4u98vvi/dunecity-previous.app.
The running 642 game was not interrupted.

Validation: dependency audits passed; 574 test cases passed, 3 skipped via
ctest. Added exhaustive disjoint search-region coverage for remote outposts,
Harkonnen factory/mode restrictions, and partial airport patrol persistence.
No tactical learner/kiting changes from the preceding analysis were requested
or implemented here. No remote release or website update in this task.

# Safe ornithopter raids and base air coverage — 1.0.644 (local only)

Stefan requested that ornithopters exploit buildings/units outside launcher and
rocket-turret protection instead of entering unrestricted Hunt when numerous,
and that QuantBot build enough distributed air defence for its whole base.

The old strike selector enabled HUNT at a map-scaled aircraft threshold and
reused active targets without rechecking air defence. Only the special nearby
reactor shortcut checked anti-air. Turret defence weights included Nuclear,
Heavy Factory and Repair Yard only, so R/C/I outskirts were amenity targets,
not assets requiring protection; centre-based square distance also exaggerated
coverage near diagonal edges.

- QuantBot aircraft now receive explicit forced attacks in STOP mode. STOP
  suppresses automatic acquisition/retaliatory Hunt; UnitBase::engageTarget
  still travels/fires at explicit forced targets. There is no size threshold
  or last-stand exception. Human-controlled units retain their orders.
- Each tactical pass builds one visible enemy anti-air map (Rocket Turret,
  Launcher, Elite Launcher, Deviator), using each weapon's actual range plus
  two tiles of manoeuvre margin and the game's octile distance. Unpowered
  rockets are ignored only when the match requires turret power. Unknown
  fogged defenders cannot be inferred. Entire target footprints and direct
  approaches must be clear. No path through defended space is invented.
- All enemy ground units and real structures, including zones omitted from
  configured priority tables, are candidates; existing priorities rank safe
  choices. Aircraft can independently choose reachable safe opportunities.
  Existing targets are revalidated as launchers move. Unsafe orders are
  cancelled and aircraft return to base. A kill no longer authorizes another
  autonomous target. Badly damaged aircraft are withdrawn.
- All real base buildings now count as defence assets; surfaces, walls and
  turrets do not recursively demand protection. Reactors retain two-cover
  priority. Coverage checks all footprint corners with octile range rather
  than a square around the centre. Existing/reserved turrets prevent duplicate
  coverage purchases by parallel construction yards.
- After opening workers, peaceful city coverage receives a slot after three
  non-service construction orders; known enemy aircraft make gap filling
  urgent. This proactive coverage slot is not capped at two turrets or gated
  on crime/land-value benefits. It still requires an available rocket turret,
  legal road-preserving site, power and affordable credits plus a zone reserve.
- New telemetry: ornithopter_safe_strike, ornithopter_hold, base_air_coverage;
  performance scope ai.ornithopter_safe_strikes. Policy version v62. No new
  saved fields, random draws or per-candidate full-map searches.

Validation: 571 runnable tests pass, 3 skipped. New tests cover protected
footprints, exposed districts, clear/blocked approaches, changing launcher
coverage, diagonal range, building-edge coverage and the expanded asset set.
Ninja dependency audits passed. Installed /Applications/dunecity.app 644 with
verified deep strict signature and matching rebuilt executable SHA256
d45406a6da1a70573eaed622339e599178337d1fae0463bddf6df7f01337c72c.
Previous bundle: /Applications/.dunecity-644-bw39wsr8/dunecity-previous.app.
No running match was interrupted. This is local only,
not a pushed cross-platform release. Restart to use 644.

# Opening economy, base defence and city traffic — 1.0.643 (local only)

Current source fixes Stefan's live 1.0.642 game reports. All 568 runnable tests
pass (3 skipped), including new opening-worker and traffic regressions; pre/post
Ninja dependency audits pass. No remote release or website update in this task.

Evidence: ai-decisions/1789131423179778-0/events.jsonl, map Moshpit with
Garbages, seed 845131971. House 0 had two harvesters after eleven simulated
minutes despite ample map spice and a 120-worker target. It ordered High Tech
at 379s, then a 700-credit Repair Yard at 475s with only 118 spendable credits.
No house ordered a factory harvester in the first eleven minutes. At 1136s,
crime spawned 14 hostile troopers; repeated defence responses dispatched five
units but counted zero already committed on subsequent passes.

- Opening city economies prioritise the first four existing/queued harvesters
  before optional technology, MCVs and repair yards. This is a priority floor,
  never a cap: the map/lobby target still bounds recruitment, and mature armies
  still balance military versus workers. First carryall remains ahead of a
  second Heavy Factory after the opening worker floor.
- Count outstanding production and upgrade costs once, reserve the next worker
  purchase, stop unaffordable optional construction, and run the existing
  demand/suitability/tax hedge before optional infrastructure. Actual power
  shortages can still queue recovery power. New policy telemetry includes
  worker priority, protected cash, queued costs and yard-upgrade spending.
- Distant Area Guard attacks were released as out of range by UnitBase. Defence
  now forces travel to the contact and releases forcing on arrival. Transit
  assignments survive intervening AI passes; human control, retreat, death and
  arrival release them. The old unused escort-assignment save slot is reused
  with unchanged binary layout; old friendly assignments expire safely.
- Airport construction uses the same positive-commercial-demand-blocked bit
  as the human civic notice (commercial population >100 internal), replacing
  the AI's premature >20 check. Derived bit is recomputed, not serialized.
  Starport build-menu availability is now 10,000 displayed population for
  humans and AI (other normal build prerequisites still apply).
- Traffic changes and comparison limits are documented in
  docs/city-traffic-balance.md. A logged mid-game snapshot (cycle 172225) had
  206 heavy cells out of 249 nonzero traffic cells. These are density cells,
  not percentages of all roads. Tests validate quiet, light and heavy flows,
  decay, repeatable alternate routes and no duplicate cell stamps at turns.

Installed /Applications/dunecity.app 643; deep strict signature and executable
SHA256 match the rebuilt bundle (f95545bd5826565b11973050dc9e972a656046a6fd089162bebd72399e5a04a7).
Previous app preserved at /Applications/.dunecity-643-865wluqn/dunecity-previous.app.
The running 642 match was not interrupted or relaunched.
New code takes effect on the next launch; existing traffic values decay through
normal simulation updates rather than being silently reset on load.

# Remote release 1.0.642 — verified 2026-09-11

Published the accumulated 631–642 changes at tag `v1.0.642`, commit `29b1f5f`.
Latest local 642 commit was amended before its first push to include release notes;
it replaces the earlier local-only `df40853` ID without changing game code.
Main and fix/dunecity-ui-quantbot were fast-forwarded. Release build
[34595302140](https://github.com/VR48/dunecity/actions/runs/34595302140) passed
version verification, Linux tests and all three desktop platforms. Six assets
published at https://github.com/VR48/dunecity/releases/tag/v1.0.642.
Duplicate main build 34595302154 was cancelled; ordinary cancellation did not
stop its always-conditioned jobs, so the Actions force-cancel endpoint was used.

SourceForge mirror [34596266575](https://github.com/VR48/dunecity/actions/runs/34596266575)
verified all eight uploaded files by SHA256, advanced the dedicated `dunecity`
source branch, published `dunecity-v1.0.642`, and confirmed Windows/macOS/Linux
platform defaults at 642. No source archive uploaded; Legacy master preserved.

Website automation advanced links, then website commit `c7fb363` updated release
prose. Deploy to Droplet `34596363046` passed. Live index and dune-city pages
returned HTTP 200 and all six 642 desktop links with the corrected summary;
Android remains independently versioned at 0.2.25.
Legacy SourceForge website copy committed on its master as `c06f68f`, deployed
by SFTP temporary upload/rename, compared byte-for-byte after readback and
verified in-browser at downloads.html?updated=642. Corrected obsolete road
upkeep/shared-construction-range claims and described economy, transport and
750 HP reactors. Backup: /tmp/dunecity-sf-web-642/downloads-before.html.
Plain HTTP tooling encountered SourceForge bot filtering (403); browser
verification worked. This game repo contains the same updated legacy HTML.

Local /Applications/dunecity.app was already 1.0.642 from the implementation
turn; version and deep strict signature verified again without launching.
The earlier local-only entries below are historical checkpoints, now released.

# Nuclear plant 750 HP — 1.0.642 (local only)

Stefan revised reactor health from Palace-equivalent 1,000 to 750 HP. Default and
Tornie data now use 750; new city matches force 750 instead of copying Palace HP,
so older user ObjectData files cannot silently retain the previous balance.
Palace HP, reactor power/cost and explosion damage/radius are unchanged. Existing
saves retain their saved stats. A 900-damage reactor blast or centered palace
strike can again destroy a full-health reactor. Updated the existing balance test
and Tornie's ObjectData checksum (the earlier edit left that checksum stale).

Validation: release build, dependency audits and CTest passed (564 passed,
3 optional skips); bundled data/checksum and signature verified. Installed locally
as /Applications/dunecity.app without launching; no remote push or release.
SHA256 6499b00a8624225b37ee4902652ecc0f2608844864664cf269d1c6d17a366848.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-642.b1x3jou6/dunecity.app.

# Palace-strength nuclear plants — 1.0.641 (local only)

Stefan requested nuclear HP equal to Palace HP. Default and Tornie reactor stats
increase from 500 to 1,000 HP. New city games now copy each house's Palace HP
(including custom overrides), replacing the previous Starport comparison.
Existing saves retain their saved object-data stats; start a new match for this
balance. Blast radius/damage, price and power output are unchanged. A full-health
standard reactor now survives one centered 900-damage strike/neighbor blast with
100 HP; damaged reactors can still chain-react. Updated the existing balance test.

Validation: dependency audits, release build, CTest (564 passed, 3 optional skips)
and bundled config checks passed. Signed/hash-verified /Applications/dunecity.app
1.0.641 installed without launching; no remote push or release.
SHA256 11d24800fb5797373ef2685130ec3c37d9dd192d4841af81d83c6460e24aa066.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-641.ogqah3tq/dunecity.app.

# Earlier nuclear, recovery budgets and road reuse — 1.0.640 (local only)

Stefan approved the remaining 638 match findings with one correction: redirect
finished redundant roads to another useful gap; do not cancel/refund them.

- City QuantBot plans nuclear once demand reaches three windtraps' output and
  available power approaches the growth reserve plus one windtrap. A legal,
  unlocked reactor and committed Heavy Factory are required; first transport
  stays ahead. Save the actual reactor price before optional yard/factory orders;
  release reservations during blackouts so affordable wind can restore power.
  Count queued generation and retain reactor clearance/placement checks. Existing
  zone maturity headroom anticipates regrowth after blackouts. No tax/power stats
  changed; this is investment timing, not a blanket opening nuclear order.
- Custom city AI reviews police funding every 30 simulated seconds. Major losses
  mean >=max(3, remaining structures/10) destroyed in the last three minutes.
  With cash <2,000 and police bill >75% of tax after power, cut up to 25 points,
  targeting half that income with a 25% funding floor. Restore 25 points when
  cash >=5,000 or post-power tax covers twice nominal police expense. Support
  mode does not change the shared house budget. Added city_police_budget events.
- Fixed CMD_CITY_SET_BUDGET applying to the local viewer: resolve the command's
  issuing player's house instead. Human UI keeps its local-house accessors.
- Finished redundant/blocked roads reuse the existing connected frontage/through
  gap candidate search. Exclude other queued sites; preserve the following plan.
  Hold the finished road if no useful gap exists, retry after five seconds; only
  one maintenance/redirect attempt per build pass. Restore its queue position if
  placement fails. No cancellation or additional pathfinding; a held road can
  keep that yard occupied until a useful site becomes available.
- Telemetry policy nuclear-budget-road-reuse-v60; nuclear_investment_due state,
  save_nuclear_growth/nuclear_growth_investment decisions and road replan reasons.
  Runtime review/retry timers reset on load; save layout is unchanged.

Validation: release build, dependency audits and CTest passed (564 passed,
3 optional skips, 9,726,326 assertions). Signed/hash-verified local installation
at /Applications/dunecity.app, without launching the game.
SHA256 047992f9666163a802ff7b59fad98f0218c1849f520b6749e17e96175c67ff5c.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-640.j206fhup/dunecity.app.
Live match behavior/balance still needs a new match. No remote push/release.

# Early carryalls, refinery capacity and continuing tax hedge — 1.0.639 (local only)

Current branch fix/dunecity-ui-quantbot. Stefan requested first transport before a
second heavy and earlier R/C/I alongside spice. Completed 638 session
1789115118630650-0 confirmed 22–32 refineries and only one R per AI at ~20min;
all 106 sampled refinery choices had no processing-capacity need. High Tech
followed 3–5 heavies. It was buying refineries for workers while military
factories stayed busy; the residential hedge stopped after one plot.

- Custom QuantBot in city/vanilla prioritizes first High Tech and carryall before
  additional Heavy Factories where tech/site/air capacity allow. Save actual HT
  price; protect first carryall funds from other builders, skip optional upgrades
  until affordable, and set minimum carryall target 1 for an active workforce.
  Counts pending orders, releases the cash reserve during power loss, and avoids
  indefinite ground expansion gates when no feasible transport build exists.
- City refinery investment now follows marginal processing capacity. A busy
  worker-capable Heavy Factory does not justify another otherwise unused bay.
  No worker factory, or workforce below two, can still justify included-worker
  recovery. Needed bays which repay their full cost get priority over tax hedge.
  Existing and queued bays/workers count; added capacity cannot duplicate itself
  across yards. Workers remain governed by spice/map limit and military priority,
  never a workers-per-refinery cap. Payouts/harvesting mechanics are unchanged.
- Continuing tax hedge: forecast tax >= one third of fleet income (25% combined),
  crediting half low-density income of demanded developing/queued lots. Suitable
  R/C/I get alternating ten-second early-priority windows so other infrastructure
  still has opportunities. Normal demand/site checks remain. No saved AI state or
  added world/path scan. Investment still uses ground-trip estimates rather than
  observed queues/carryall improvements; validate balance with a new full match.
- Telemetry policy transport-tax-hedge-v59. Added income/factory-supply fields and
  first_transport_factory, save_first_transport_factory, first_carryall,
  save_first_carryall, city_income_hedge and city_refinery_capacity decision rules.

Further observations (recommendations only): save toward nuclear earlier (only
Neutral bought it, at ~67min); reduce service-budget burden after city losses
(Ordos ~50min: 695 gross tax vs 575 police/min). Road cancellations mostly cancel
redundant single road steps: 115/125 already had roads, not whole building plans.
AI frame max 18.9ms/build max 12.1ms; frame max 170ms with three >100ms samples,
including one unit-update spike 159.2ms. Full evidence in docs/city-economy-balance.md.

Validation: dependency audits, release build and CTest pass: 560 passed, 3 optional
skips, 9,726,298 assertions. Signed/hash-verified /Applications/dunecity.app installed
as 1.0.639; no game launch, remote push or release.
SHA256 d13775877db6911712b370ca70b3639993f535cfe858e3d82cf30f233d8e1d2c.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-639.q4lh_9qp/dunecity.app.

# Double private-zone tax — 1.0.638 (local only)

Stefan chose 2x R/C/I income rather than 3x fleet parity: tax is easier and less
exposed than harvesting. Added kZoneTaxMultiplier=2 in the shared weighted tax
base. All private zone densities and partial houses receive it; Palace remains
at unboosted R+C and other government infrastructure remains exempt. Census,
actual payouts, UI and AI share the multiplier; updated QuantBot indirect
residential tax forecast to use the helper too. Policy parallel-city-economy-v58.

At 7% tax/LV128, high R/C/I yield ~104.53/104.53/83.63 credits/min; Palace
~104.53 unchanged. Power, demand, jobs, growth, road upkeep and harvesting
unchanged. No extra scans or save-format changes. Detailed fleet comparison
and rates are in docs/city-economy-balance.md.

Yard/factory review: Stefan explicitly rejected 3/4 workers-per-refinery caps.
City factories target remaining-map-spice capacity and the map harvester limit;
refinery count never caps production. Removed direct 3-per-refinery gate for
vanilla QuantBot too; its existing target/refinery policy remains. Removed city
hypothetical-refinery reserve.
Yards choose zones alongside a funded idle factory that prefers a harvester unless processing
needs another bay; busy factories leave included-worker refinery option eligible.
Capacity uses 75% ideal unloading; marginal forecast credits actual first loads
and bay relief, not existing fleet or unqueued future workers. Queued refinery
workers counted across passes. Power capital uses owned generators plus foundation.
Added decision telemetry and regressions for spare/busy factory, shared bays,
queued bays, first delivery and unsafe fields. Vanilla refinery-build ratios unchanged; factory worker gate removed.

Factory allocation clarification: a spice target is not unconditional worker
priority. Recover fewer than two committed workers first; otherwise while an
affordable military order is needed, require military value of about 2x current
worker purchase value before spending another slot on a harvester. Once army
target is met, grow to spice/map target. Includes committed queues and falls
through to military selection when workers defer. Yard forecasts use the same
choice; telemetry factory_economy_priority. No new persistent AI state.

Validation: final dependency audit/build/CTest pass, 556 passed and 3 optional
skips. Signed/hash-verified local /Applications/dunecity.app 1.0.638 installed.
SHA256 2266a41ea980f68eeba7b777935c30cbab666b18708d6c1aa3e82162bc1aadf1.
Backup dunecity-before-638.ba21axvi. No game launch, remote push or release.
Live balance still needs the next match; capacity is a throughput forecast,
not a measurement of local queue wait times. No pathfinding was added.

# Micropolis tax, Palace income and road upkeep revert — 1.0.637 (local only)

Stefan authorized implementation of the Micropolis easy tax comparison, made
Palace an R+C tax exception, and requested road costs/ownership reverted.

- Tax now uses (taxableR/8+C+I)*landValue/120*taxPercent*1.4. Only R/C/I zones
  and Palace pay. Eighth-unit census preserves partial houses until aggregate
  annual rounding; smooth per-cycle payouts and 60s year retained. This is the
  easy formula, with less intermediate truncation than Micropolis tiny cities.
  Zero land value now yields zero actual tax; unknown future-land forecasts may
  assume 128. Government jobs/demand are separate from taxable population.
- Palace contributes BOTH R and C at its occupancy tier. At 7%/LV128, high R/C/I
  are ~52.27/52.27/41.81 credits per simulated minute; Palace ~104.53. Full tier
  table and harvester comparisons are in docs/city-economy-balance.md.
- Budget, QuantBot economy/service/production forecasts and Mentat use the same
  tax base. Also fixed two extracted MentatBuildOrder variants still using gross
  population (the Mentat variant is compiled), missed by earlier exemption.
  Telemetry replaces taxable_pop with tax_base_eighths; policy micropolis-tax-palace-v57.
- Removed road upkeep at every population and the maintenance ownership feature:
  no census, deductions, road-only owners, AI road expense, or old-save inference.
  Auto frontage and city road-overlay command preserve underlying tile owners.
  Normal manually placed foundations/roads retain their tile ownership behavior;
  enemy concrete/roads still do not extend construction range. Existing saved
  tile ownership stays; cannot distinguish historical inferred owners safely.
  Road overlay/foundation/traffic behavior, build prices and police upkeep remain.
- No save-format changes or added scans/pathfinding. Latest 636 factory roles and
  normal configured zone construction timing retained. Live balance validation
  remains pending; tests stub the full world runtime scans.

- Final dependency audit, build and CTest passed: 554 tests passed, 3 optional skips.
  Installed signed/hash-verified /Applications/dunecity.app 1.0.637.
  SHA256 3898ee9a75e6a21e4b05e7147b5be467b3072d9a767d9ba72a8eee6c50615644.
  Backup: dunecity-before-637.7w3j44an. No game launch or remote push/release.

# Revised factory roles and normal zone construction — 1.0.636 (local only)

Stefan revised the mapping again after 1.0.635: Light Factory is now low-density
I; Heavy Factory, High Tech Factory and Repair Yard medium-density I; House IX
high-density C. This supersedes 1.0.634's High Tech high-C interpretation.

- Roles, caps, supply/population/emissions helpers and sidebar labels agree.
  Existing load reconciliation clamps old high occupancy automatically. Factory
  emissions follow the tiers: Light 10; Heavy/HighTech/Repair 25. IX stays clean.
- Government infrastructure stays non-taxable; private R/C/I tax formula still
  unchanged. Refinery medium I, Silo low I, WindTrap power only and Starport
  seaport remain as previously requested. Includes 1.0.635 balanced zone choice.

- Stefan's final timing instruction is normal construction timing. R/C/I use
  their configured build time through the standard BuilderBase production path,
  removing the instant-zone override. Default 40 is ~9.6 simulated seconds at
  full builder speed; house/mod overrides respected. This supersedes the earlier
  silo-time request. Roads, instant-build mode and zone prices remain unchanged.
- QuantBot tax-investment delay includes normal configured construction time
  plus its existing 60s growth/foundation allowance. Population growth itself
  is not accelerated. Regression tests cover all zones, overrides and zero-time
  safeguards. No new save fields, scans or pathfinding.

- Final build, before/after dependency checks and CTest pass: 557 passed, 3 optional
  skips. Installed signed/hash-verified /Applications/dunecity.app 1.0.636,
  SHA256 7194a3cd6eece9e4a0cb8224e9ac1056d262e535ae992743a02a07eb1b24bc59. Backup: dunecity-before-636.66dh8ro3.
  No launch/interruption of ongoing game, no remote push or release. Runtime
  balance still needs a subsequent live game; tests stub the full world scans.

# Balanced R/C/I selection — 1.0.635 (local only)

Stefan's current game screenshot showed R -1110 / C +1360 / I +1500 and little
industry. Session 1789107959898835-0 is actually 1.0.634, DuneCity 192x192, seed 460150850.
Snapshot at ~cycle 82000: 49 of 52 evaluations with R<500 and both job demands
positive selected C; 3 selected I. House2 had built 32 R / 15 C / 5 I, so industry was
suppressed intermittently, not universally unavailable. Example cycle 21248:
5 R / 1 C / 1 I, demand -530/1500/1419, I marked lower_rank_not_evaluated. Another at
cycle 71648 chose R via infill despite demand 1685/224/1500 and counts 18/10/5.

- Removed the hard C-before-I 500 gate. Normalize demand maxima (R 2000, C/I 1500).
  Among positive candidates within 20% of strongest demand, choose underprovided
  built+queued plots with the existing 3:1:1 R/C/I tie-balance. Stronger demand
  wins outside that band. A fixed strongest reference preserves sort transitivity.
- Removed unconditional residential infill promotion across zone types. Existing
  placement scoring still favours gaps for housing when R is the selected need.
  First demanded R hedge preserved; no forced missing C/I at nonpositive demand.
- No new scans/state/pathfinding. Existing suitability/site fallback and marginal
  tax/refinery investment comparison remain. Telemetry rule now
  normalized_demand_band_then_committed_balance, policy balanced-zone-demand-v56.
- Regression cases reproduce screenshot and log states, queued commitments,
  sustained slightly unequal positive C/I demands, and near-zero I exclusion.
  Includes 1.0.634 government tax and employment changes; Micropolis formula
  remains a comparison only. Live post-fix balance validation pending.

- Build and before/after dependency audits passed; CTest 556 passed, 3 optional
  skips. Installed signed/hash-verified /Applications/dunecity.app 1.0.635,
  SHA256 e04112fdbd9b739e98bb6a85633e2506f69c608b0ca394f83655e0d751825230. Backup:  dunecity-before-635.4xsde6uo.
  Did not launch/interrupt the live 1.0.634 process. No remote push/release.

# Government tax exemption and infrastructure roles — 1.0.634 (local only)

Stefan requested Micropolis-equivalent R/C/I numbers (comparison only) plus
non-taxable government infrastructure and revised employment tiers.

- Only actual R/C/I zones pay tax. All government/non-zone infrastructure is
  excluded, retaining its other jobs/population roles. Partial R lots pay for
  actual houses. Gross city population still governs unemployment and the
  under-2000 road-upkeep exemption. Existing tax rate/time conversion unchanged.
- WindTrap no longer supplies industry. Light Factory/Refinery cap at medium I;
  Silo low I; Heavy Factory/Repair Yard high I; High Tech high C (final user item
  overrides its earlier contradictory high-I listing). Starport stays seaport.
  Sidebar role labels corrected, including old Refinery/Starport mislabelling.
- Tiers clamp jobs/population/emissions and loaded occupancy; existing aircraft
  manufacturing emissions retained for High Tech's commercial employment.
  Derived taxable census added to existing scans, payout, budget and QuantBot /
  Mentat income forecasts, including service-investment tax gains. No new scan
  or serialized field; telemetry adds taxable_pop for comparisons with R/C/I.
- Micropolis easy at tax7%, LV128 would yield high R/C/I approximately
  52.27/52.27/41.81 annually, also per simulated minute with the existing 60s year.
  Current zoned high R/C/I remain 186.67/23.33/18.67. R is 3.57x Micropolis;
  C/I are 0.446x. Formula restructuring has NOT been applied. Full tier table and
  government scope are in docs/city-economy-balance.md.
- Local build and dependency audits passed. CTest: 555 passed, 3 optional skips.
  Role/tier regression
  tests cover old occupancy, jobs versus tax, government structures and partial
  R lots. Runtime scans are stubbed in the test target; a live match remains
  necessary to validate long-term economy balance. No push or remote release.

- Installed /Applications/dunecity.app 1.0.634 without launching the game.
  Signature verified; executable SHA256 matches the built bundle:
  b043ad88c521adb18123d12acb18fa957b74ff5bc7a3091ffe914e5ce5309fe1.
  Previous app backed up in temporary dunecity-before-634.x8wfd95b directory.

# Demand-led tax/spice investment — 1.0.633 (local only)

Stefan wants an opening R tax hedge, no forced one-each R/C/I seed, and an
explicit refinery-versus-tax comparison that recognizes existing bay capacity.
The 1.0.631 Habbanya-Penny log 1789102780668848-0 includes opening C orders at
zero demand. I was positive at its recorded initial orders; the later screenshot
alone does not prove I was ordered at negative demand. Source rankZones did
explicitly allow missing types with zero/negative demand; that override is gone.

- First refinery retained for income/technology. Then a demanded R hedge;
  missing C/I no longer prerequisites for first vehicle factory. Opening and
  ongoing custom-city investments use one four-minute proceeds-per-credit model.
- No additional refinery investment if current/queued bays already cover the
  sustainable near-term fleet (workers plus three, capped by sustainable target).
  Compare marginal delivered spice, not a whole fleet credited to a new bay.
- Tax candidate uses live positive demand and existing valid-site selection,
  price/foundation/generation cost and future road upkeep, low/medium growth with a 60-second delay,
  tax/house land value, pollution/crime, pending/undeveloped same-type lots and
  limited job-enabled residential tax for C/I. Refinery includes fill/unload and
  construction delays, local sampled trip distance, spice share and danger.
- Vanilla economy retains its prior refinery policy. No extra pathfinding or
  save fields; bounded local spice sampling once per build pass. Telemetry-only
  per-yard throttle added, policy demand-tax-spice-investment-v55. Sampled
  city_economy_comparison exposes every forecast component and selected item.
- Tax amounts/budget balance unchanged. See docs/city-economy-balance.md for
  Micropolis comparison and exact cycle-based harvester equivalences. Budget
  /60 matches payouts, but excludes separate power/unit/construction costs.
  Raw R taxation makes mature R ~3.57x Micropolis easy-mode annual revenue;
  C/I ~0.446x, with upkeep largely retaining original scale.
- Local build, before/after dependency checks and CTest passed (553 cases,
  three optional skips). Includes local 631 power and 632 road fixes. No live
  match validation yet; forecast heuristics need follow-up logged matches.
- Installed /Applications/dunecity.app 1.0.633, signature checked and executable
  hash matched to build bundle. Previous app backed up under temporary
  dunecity-before-633 directory. No launch/interruption, remote push or release.

# Enemy roads are foundations, not construction anchors — 1.0.632 (local only)

Stefan clarified that enemy roads must behave like enemy concrete: a house can
build over them within its own normal build range, but cannot expand from an
unrelated enemy road network. This supersedes 1.0.630's shared-access rule below.

- Map::isWithinBuildRange again recognizes only tiles owned by the constructing
  house, with the existing two-tile search. Road flags and city mode do not grant
  additional reach. Applies equally to humans and every AI.
- Road foundations remain prepared ground, independently of owner; footprint
  placement retains terrain/occupation checks. Building on a road inside normal
  range clears the road under the footprint and stamps the new building's owner.
- MCV deployment remains unchanged: its new construction yard creates the owned
  foothold, allowing normal building around it. Enemy roads beyond that range
  do not extend the foothold. Existing road upkeep ownership is unchanged.
- Replaced the regression that endorsed map-wide shared road access with
  owner-only reach cases, retaining foundation and placement integration checks.
  No new scans, pathfinding or save fields. Policy owned-construction-reach-v54.
- Local build/dependency audits passed; CTest550 passed, three optional skips.
  Includes 1.0.631 power planning. Installed /Applications/dunecity.app version
  1.0.632, signed and executable verified against build bundle. Prior app backed
  up under a temporary dunecity-before-632 directory. No game launch/interruption.
  No remote push/release this turn; published 1.0.630 still has the old road rule.

# Growth-aware nuclear investment — 1.0.631 (local only)

Reviewed completed city 1.0.630 session 1789087168596776-0, SCENA021.INI,
62x62, ending cycle180523 (~48.14 simulated minutes). The logged QuantBot is
house1. Of 41 generator-choice records, 40 chose wind and one nuclear. At40.75min,
it chose wind with234,226 spendable credits, need489 and a valid reactor site:
five100-output windtraps cost1,500, below the2,000 reactor, so the old incremental
cost rule ignored the wealthy city's need for reserve capacity. 34 records had
nuclear_site=false; old telemetry combined unavailable tech and rejected sites,
so it cannot prove which placement restriction caused each failure. A completed
reactor was placed through power-recovery fallback at29.81min, risk500.

The log contains1,446 power_shortage decline events (all reduce population;
1,236 also reduce density). Only2/95 periodic snapshots showed deficits, so
snapshot averages hide short blackouts and subsequent zone shrinkage. The old
30-second trend discarded forecast growth when density/power demand fell.

Changes:
- Account for every owned zone's mature density3 load minus its exact registered
  power draw (including individual residential houses), plus mature queued zones
  and other queued consumers. Use the larger of latent zone load and observed
  two-minute growth, avoiding double counting. This headroom drives the early
  power trigger as well as generator selection, retaining normal reserve.
- When additional power is needed, prefer an affordable reactor during a deficit
  or when spendable cash after queued orders covers five reactor prices plus
  working capital (normally ~10k). Small funded starts still compare wind cost
  and space; exact cost parity now favours nuclear. Pending-generator guards stay.
- Reactor search ranks safe separated sites first; known threat/loss halos and
  four-tile blast clearance are preferences rather than blanket reactor vetoes.
  If none is safe/separated, use the best remaining legal footprint. Terrain,
  occupied buildings, ground exits, roads and neighbouring access remain checked.
  New critical buildings retain the separation veto beside existing reactors.
  Completed generator placement uses the same separation preference. Existing
  redevelopment paths remain conservative. Nuclear placement risks are logged.
- Add zone current/mature draw, committed load, growth headroom, shortage,
  nuclear availability and detailed candidate rejection/risk telemetry. Skip the
  extra wind-site counting scan when wealth/recovery already decides nuclear;
  zone loads share the existing structure loop. Policy zone-growth-power-v53.
- Regression tests cover the recorded rich-city decision, protected cash,
  blackout-recovery load invariance and reactor safety/separation ranking.
  CTest550 passed/3optional skips; dependency audits and build passed. Save layout
  unchanged. No final full-match replay validation or remote release this turn.
- Signed 1.0.631 installed at /Applications/dunecity.app; executable matches
  build/bin/dunecity.app SHA256
  b55294a1d3f74cda0696f46292b7f98a0ca5a55ba7c192948c238341c7ab1082.
  Prior app preserved in temporary dunecity-before-631.28kyr3x6 directory.
  No running match interrupted and no launch used for version verification.

# Shared city road access and desktop release — 1.0.630

Stefan requested enemy roads be reusable, then authorized local/remote builds
and both website updates, including accumulated 1.0.627–629 fixes.
- Roads already supplied prepared foundations independent of owner, but
  Map::isWithinBuildRange only recognized owned tiles. In city mode any road
  now supplies construction reach within the existing two-tile BUILDRANGE.
  This is shared access, including enemy/abandoned road networks; roads do not
  need to be connected to an owned network. Human/AI validation uses the same rule.
- Ownership/upkeep remains unchanged, enemy bare ground/concrete grants no reach,
  Vanilla retains owned-tile reach, and footprint occupation/terrain/AI lane and
  threat checks still apply. Strict foundation checks now accept roads as slabs.
  No path searches or new map-wide scans. Regression cases cover owners, modes,
  foundation/occupancy wiring. Policy shared-road-access-v52, save layout unchanged.
- Before/after dependency audits passed; CTest 547 passed, three optional skips.
- Local 1.0.630 installed at /Applications/dunecity.app, signature verified;
  executable matches build/bin/dunecity.app with SHA256
  fa704a707098914af1e82bd6159249123b3c548dda8484d0d249ed036c0bee82.
- SourceForge legacy downloads copy committed to old Git master e5fe423 and
  pushed. SFTP atomic upload backed up previous HTML under
  /tmp/dunecity-sf-web-630/, verified readback SHA256
  ecf6869ee156ac79918ba4ca93705173464a8ceee6f767796f01372a3992ee8d.
  Public browser page verified with ?updated=1.0.630.
- Game release tag v1.0.630 is 1339839, pushed to GitHub main and working branch.
- Remote build 34485222429 succeeded: Linux tests and all three desktop builds,
  six GitHub release assets verified (ZIP, DMG, AppImage, DEB, RPM, tar.gz).
  Redundant main build 34485222489 cancelled; tag build performed required checks.
- Release notes published from releases/desktop/1.0.630.md (added in follow-up
  docs commit f45314b; immutable release tag remains 1339839).
- SourceForge auto mirror 34486418547 succeeded. After release-note polishing,
  idempotent mirror retry 34486748145 verified eight uploads and all three OS
  defaults. Source branch dunecity and tag dunecity-v1.0.630 point to 1339839.
  No source archive uploaded.
- Main website automation commit 3a2b5a1 updates versions/links; prose commit
  e05c679 rebased onto it and pushed. Deploy 34486697186 succeeded. Both live
  pages returned HTTP 200 with new prose, desktop 1.0.630, Android 0.2.25 and
  all six package URLs matching the published assets. SourceForge web deployment
  and browser verification noted above. Completed 2026-09-11 Australia/Sydney.
- Restart the local game to load 1.0.630; no running match was interrupted or
  launched for verification. Automated validation passed; no full-match replay
  of the final release was performed.

# Repair-yard crash and earlier QuantBot repair support — 1.0.629

Crash evidence: `~/Library/Logs/DiagnosticReports/dunecity-2026-09-10-232316.ips`
(copied to `/tmp/dunecity-repair-yard-crash-20260910.ips`). Main thread crashed
with SIGSEGV at address 0xa0 in RepairYard::updateStructureSpecificStuff()+88.
The executable UUID 826D9D79-4767-3BA0-AB13-FF448179719F matches the saved 1.0.627
binary in `/tmp/dunecity-before-628.8MFLeK/`, despite the report's bundle metadata
saying 1.0.628: the running 627 binary remained mapped when its on-disk build
bundle was updated. Screenshot and crashed telemetry session agree on 1.0.627.

Crashed city session `1789045974757020-0`, Sardaukar Base, ends at cycle149250
with reactor9573 detonating and destroying launcher8051, the object ID held in
crash register x8. Nuclear damage walked inactive ground units, including repair
occupants still holding old map positions. RepairYard dereferenced the expired
ObjectPointer without checking it. The current Dune City.log was already replaced
by a later launch; use the macOS report and immutable session above for evidence.

- Nuclear blasts now target active ground units only. Cargo in repair/refinery/
  carryall storage is not independently hit at stale positions; a destroyed host
  retains its existing occupant-destruction behavior.
- Repair yards resolve a valid GroundUnit before update, deployment or destruction.
  Missing occupants clear repair state/animation and release the booking once.
  Late carryall pickup is safe; booking decrements cannot underflow. Clear state
  before handing off/destroying a unit. Save layout unchanged.
- Regression tests exercise disappearing occupants, repeated cleanup, other booked
  arrivals, subsequent reuse, empty booking counts and stored-unit blast exclusion.

Latest ongoing Vanilla session `1789046664718549-0` showed Atreides at3.07min
with one heavy factory/6,100 army value and no repair yard; at6.11min nine heavy
factories/71,468 credits and still no yard. First repair order6.16min.
- Custom QuantBot now selects feasible affordable baseline repair capacity before
  repeated factory/tech expansion after an operational heavy factory and refinery.
  Keep 1,000 credits beyond yard cost. First yard does not wait for saturation.
- Baseline is bounded by the existing one-per-two-heavy-factories, max-four cap,
  and increases with army value (one additional slot for each >8,000 value).
  Built and queued yards count. Existing saturation rule can still add capacity.
  Latest-game examples: one heavy/6,100 -> one yard; four heavy/8,050 -> two;
  fifteen heavy/29,050 -> four. Added regression cases and repair_baseline telemetry.
  Policy `early-repair-capacity-v51`. Includes prior ornithopter/harvester fixes.
- Validation: before/after dependency audits and CTest passed (545 passed, three
  optional skips). Signed local build and Applications install both 1.0.629,
  executable hashes match. Current game left running; fixes apply next launch.
  No full-match replay validation and no remote push/release performed.

# Less conservative adaptive spice fleet — 1.0.628

Stefan explicitly requested a modest adjustment to calculated harvester targets,
not special lobby-override behavior or a fixed 120-worker target.

Measured completed Vanilla session `1789043602798877-0` using Atreides snapshots,
map spice deltas and all-house worker counts. At 14.21 minutes: 711,263 spice,
189 harvesters across the map, 75 Atreides workers. Trailing ~2-minute depletion
was 75,903 spice/min, implying 9.4 minutes remaining at that rate; old Atreides
spice target 71. At 15.23 minutes: 638,690 spice, depletion 70,704/min, runway
9.0 minutes, old target 63. These are measured aggregate depletion rates and
constant-rate estimates, not guarantees of accessible spice or future duration.
Over 10.16–14.72 minutes, the map averaged 182.7 harvesters and 415 spice removed
per worker per minute; Atreides refined 301 credits per worker per minute.
Removal and refinery income differ because of cargo in transit/losses and other
map effects; do not equate them.

- Reduced desired spice-per-worker by 25%: Vanilla 2,000 -> 1,500; city 3,000 ->
  2,250. Targets increase about one-third where not capped. Five-house Vanilla
  examples: 711,263 spice -> 94 workers; 638,690 -> 85; 215,370 -> 28.
- Equal-share calculation, lobby/engine caps, existing global low-spice limit,
  refinery throughput, budget, queue and factory ordering rules remain intact.
  No override bypass and no production batching changes. Includes 1.0.627's air fix.
- Added actual-match and depletion/cap regression cases. Policy telemetry tag
  `spice-worker-runway-v50`. No save-format change.
- Before/after dependency audits passed; CTest 541 passed, 3 optional skips.
  Built and installed signed local 1.0.628; Applications executable SHA-256 matches
  the tested build. Existing running game left alone; new policy applies next
  launch. No remote push/release performed.

# Fund ornithopter production before ground overflow — 1.0.627

Reviewed last completed session `1789043602798877-0`: Vanilla 1.0.626,
`5P - 128x128 - All against Atreides`, 99,898 cycles (26m38s), ended manually.
Atreides built 0 ornithopters, despite 14.4% target when first available (7.60min)
and final 11.42%. IX completed at 7.15min. Of 42 sampled high-tech decisions:
22 spendable-below-air-threshold, 13 unavailable, 6 factory-busy, 1 carryall-priority.
All 18 accepted high-tech orders were carryalls. Carryalls took priority while
rich; queued/ground spending plus the 2,000 reserve then starved the >1,200 air
cash gate despite aircraft costing 600. Two destroyed high-tech factories also
caused temporary unavailability. Enemy house 3 sonic tanks were identified by
object-ID/type records as attackers in 62/76 Atreides harvester lethal-hit events.

- Priority: city ready/idle yards retain 3/2; high-tech factories 1, light 0,
  remaining structures -1. City yard rotation remains unchanged. Aircraft now
  access their share before ground factories' overflow spends it.
- Pure AirProductionState/chooseAirProduction policy: bootstrap first carryall;
  otherwise unmet affordable combat air precedes additional carryalls. Retain
  prerequisites, upgrades, queue, air and military caps, economy/strategic reserves.
  Use actual unit price after reserves instead of fixed >1,200 threshold. Once
  air target is filled (including queued aircraft), continue carryall production.
- Only accepted orders update planned counts/cash/military. Carryall orders now
  deduct planned cost too. Diagnostic reasons match policy, include availability
  and military cap, and report the same vehicle-plan air target used for selection.
  Policy version is `fund-air-allocation-v49`; no save format change.
- Regression cases cover ordering, carryall starvation, exact-price affordability,
  queued air saturation, both caps, reserves, busy/upgrading and tech prerequisites.
  Local 1.0.627 build, before/after dependency audits and CTest passed: 543 cases,
  540 passed, 3 optional skips. No game launched; live-match behavior still needs
  observation. Installed `/Applications/dunecity.app` 1.0.627; signature verified
  and executable SHA-256 matches the tested build. No new remote release performed.

Final Atreides production / reward-to-lost-value / vehicle-value target:
Launcher 255 / 3.16 / 51.52%; Sonic 122 / 1.76 / 21.61%; Siege 38 / 0.79 / 6.59%;
Tank 56 / 0.70 / 5.47%; Quad 48 / 0.34 / 2.16%; Trike 37 / 0.19 / 1.23%; Orni
0 / no evidence / 11.42%. Also 79 harvesters, 18 carryalls and 7 MCVs produced.
Soldiers/troopers had zero production events but 255/39 losses from starting/free
units; those losses must not be presented as paid infantry production. Counts
were cross-checked against unit_produced and final game_summary.

# Verified desktop deployment — 1.0.626 (2026-09-10)

- Released tag `v1.0.626` at `d1b30c3` to GitHub main and the working branch.
  Build run `34473274658` succeeded: Linux tests and Windows/Linux/macOS builds;
  all six desktop packages are uploaded. Redundant main build was cancelled.
- Local build and `/Applications/dunecity.app` both report 1.0.626. The old
  Applications copy was stale (plist 0.01); replaced after preserving a backup
  under `/tmp/dunecity-before-626.*`. Installed binary SHA256 matches the tested
  build; ad-hoc signature verification passed. No game launch/log truncation.
- Main website release links and new feature copy are deployed, website commit
  `e466be2`, successful deploy `34474219132`. Both public pages returned HTTP200,
  version 1.0.626 and new road/police text. Android remains independently 0.2.25.
- SourceForge mirror run `34474087516` verified all eight files (six packages,
  README, SHA256SUMS), advanced `dunecity` and `dunecity-v1.0.626`, and confirmed
  Windows ZIP, Mac DMG and Linux AppImage defaults. No source archive uploaded.
- SourceForge website copy committed to Legacy master as `91b6b9a`, pushed,
  backed up, deployed by SFTP temporary upload/rename and compared byte-for-byte
  after readback. Public browser confirmed new copy using
  `downloads.html?updated=20260910-626`; curl was blocked by Cloudflare and the
  plain web-tool URL returned stale cached HTML. Both tracked HTML copies match.

# Starter-city road exemption — 1.0.626

Road upkeep is waived while an individual house's displayed total population is
below 2,000. It starts at exactly 2,000 and stops again if population drops below
that threshold. Billing, budget forecast and AI telemetry use the same rule;
the budget explicitly says roads are free below 2,000. Existing road census and
ownership rules from 1.0.625 remain. Police patrols remain 3 troopers + 1 trike.
Local 1.0.626 build and before/after dependency audits passed. Full CTest:
539 cases, 536 passed, 3 optional skips, including population threshold/reversal
and per-house checks. Game not launched. User subsequently authorized remote
release, both websites and SourceForge; publication is verified above.

# Road upkeep and smaller police patrols — 1.0.625

- Added road maintenance using Micropolis `simulate.cpp::collectTax/doRoad`:
  annual cost is `floor((road tiles + heavy road tiles) * 0.7)`. This uses
  Micropolis's default EASY city rate (DuneCity has no city difficulty selector).
  Heavy traffic begins at 192, matching the road animation. Roads remain 1x1,
  so the 2x2 zone conversion does not scale their cost. Funding is fixed at 100%;
  this change does not add underfunding controls or deterioration.
- Census shares the existing budget map walk; no additional per-cycle map scan
  or UI map scan. Derived per-house counts are not serialized. The budget shows
  physical/heavy road counts and annual cost, included in Services and Cash Flow.
  Road charges use `House::takeCredits` after the existing tax/police settlement,
  so upkeep can consume city, spice and starting funds. No debt is introduced
  when funds run out. Telemetry records `roads_due` and actual `roads_charged`
  (also included in `spent_total`), plus road counts/expense in AI city health.
- Automatic perimeter roads and the legacy road tool now set existing Tile owner
  metadata; existing owned roads keep their owner. On load, old unowned roads
  infer ownership only from adjacent structures (nearest, house-ID tie break).
  Isolated unowned map roads remain public/unbilled. Saved owner metadata needs
  no save format change. Removing/building over a road removes it from the bill.
- Police deploy 3 individual Unit_Troopers and 1 Unit_Trike (four units total),
  replacing 9 troopers, 2 trikes and 1 quad. Tooltip and sidebar match. Existing
  cooldown, per-unit 250 military unit checks, QuantBot military value cap and
  blocked-spawn behavior are unchanged. Policy tag: road-upkeep-police-patrol-v47.
- Local app built as 1.0.625. Before/after Ninja dependency audits passed;
  full CTest: 538 cases, 535 passed, 3 optional skips. New regressions cover
  rates, traffic threshold, aggregate rounding, ownership/migration and fractional
  charges. Game not launched, so live visual/gameplay verification remains.
  No push or release requested.

# Windtrap sidebar label — 1.0.624

Corrected the remaining hard-coded `Role: I-medium` text in CityStatsBox to
`Role: I-light`. Simulation was already light in 1.0.623 (population 1,
maximum level 1, industrial supply 10); power and emissions are unchanged.
Local app rebuilt as 1.0.624. Full CTest and before/after dependency audits
passed. No game launch, push or release.

# Occupancy-based traffic, individual houses and city AI priorities — 1.0.623

User screenshots of tiny 1.0.622 settlements showed widespread heavy traffic.
Every occupied city-role structure emitted a successful road journey every city
day. Micropolis `zone.cpp` instead tests R population > random(35), and C/I
population > random(5); its random upper bound is inclusive. Traffic generation
now uses those probabilities (R pop/36, C/I pop/6) with a deterministic site/day
hash. Existing connectivity checks, route sampling (+50 at moves 2/4/6...),
240 cap, 24/34 decay and display thresholds (64 light, 192 heavy) remain.
This corrects trip frequency; it does not guarantee small shared bottlenecks
can never be heavy. Old inflated traffic clears naturally when no longer fed.

Residential zones now store real occupancy: 0–8 houses, then 16/24/32/40
apartment population, and the reverse on decline. The eighth house upgrades
only above local population density 64, as in Micropolis. Existing DuneCity
score, demand, pollution, supply and power gates still govern growth.
- Each successful free-lot growth adds one visible house; decline removes one.
- Counts feed population, demand, density, taxes, traffic, local supply, power,
  crime-service investment and telemetry. House lots are not vacant when
  scoring redevelopment. Sidebar shows Houses n/8.
- 29 residential models per land-value tier are packed in two 15-column rows;
  all zooms remain within the 2048px texture ceiling. Original 2x2 zone/1x1 road
  footprints remain. Civic overlays retain apartment-only eligibility.
- Save format 9835 adds one occupancy byte per ZoneStructure. Older saves
  consume no byte and migrate tile tiers to their existing 0/16/24/40 count;
  new saves preserve individual houses and the 32-pop apartment stage.

Additional requests during this change:
- Windtraps are light industry: maximum occupancy 1, industrial population 1
  and local job supply 10 instead of medium 3/25. Power/emissions unchanged.
  Loaded windtrap occupancy is reconciled; population/supply helpers also
  clamp older medium values.
- Zoning favours C when R <500; if R and C <500, favours I with positive demand.
  Residential infill only overrides other choices at R >=500. Missing bootstrap
  roles still take priority; unavailable/unsuitable land falls through normally.
- Police placement penalises every built/planned station within 12 tiles,
  with a stronger quadratic near-neighbour cost. Crime utility favours
  underserved properties. Actual coverage remains additive and unmodified.
- Service planning rebuilds the small 6-tile police map once per shared bounded
  search from current buildings plus reservations. This includes recently
  completed stations before the next city scan, with original sum-then-smooth
  rounding. Removed the old alternate unbounded police-site search; all police
  construction paths now use the same bounded scorer and overlap penalties.
  Exceptional crime may still justify overlapping stations; there is no spacing ban.

Live log evidence: partial session `1789037053114457-0` had 143 station investment
choices at inspection, 18 with overlap cost >=300 (roughly within five tiles of
an existing/planned station). The old nearest-only cost was easily outweighed
by thousands of utility points. Counts are a read of an ongoing log, not final
match totals. Policy telemetry is `occupancy-traffic-houses-v46`.

Validation: full CTest 534 cases, 531 passed, 3 optional skips; occupancy
progression/save stream alignment, sprite reachability and single-house changes,
occupancy trip probabilities, sparse/light vs busy/heavy roads, cluster costs,
500-demand boundaries and windtrap migration. Atlas reproduction, source/app
atlas hashes, bundle version, git whitespace and before/after Ninja dependency
audits pass. Logs: `/tmp/dunecity-houses-{build,tests}.log`.
Local `build/bin/dunecity.app` is 1.0.623. No game launch, Applications copy,
remote push or release. Live visual/game balance verification remains for the
next run; automated checks are not a claim of measured live FPS improvement.

# Bound repeated city AI placement work — 1.0.622

Completed game `1789031518687887-0` ran 1.0.620 (192x192 SimCity,
seed 1929923577, ended cycle 208163). Imported 181834 events into
`/tmp/dunecity-620-performance.sqlite`: 245 performance windows, zero dropped
samples. Worst frame was 340 ms, including 316 ms in AI. In the final active
minute, 108 frames exceeded 100 ms and 96 included AI over 100 ms. Construction
planning consumed 319 seconds across the session; turret placement (169 s) and
service investment (130 s, includes service site search) explain about 94%.
At cycle 196248, eight yards emitted 24 ineligible service candidates. This is
the measured cause of the recurring hitches. Paths remain a separate background
cost (8.7 ms/frame in the last minute). Rendering averaged 3.8 ms, city 1.9 ms.

Changes in 622:
- Check turret caps, enemy presence, power and affordability before expensive
  location searches where those guards previously came afterwards.
- One service search per house build pass scores police/rocket sites for all
  three selection modes together: normal, emergency and tax-value investment.
  Yards reuse positive and negative results. Each caller still filters its own
  build availability and spending reserve; the first yard's upgrade level must
  not suppress a later yard's rocket option.
- One city defensive turret search per build pass, shared between yard rules
  and placement. Reserved coverage for crime targets is computed once per
  target rather than again for every proposed tile.
- Each search examines at most 4096 origin tiles per item in a deterministic
  rotating batch. Candidate-mask generation is restricted to the same rows.
  All map tiles, including partial edge batches, remain reachable over a sweep.
  These are best-in-batch choices, not a full-city optimum every pass. A failed
  batch means try a different batch next pass, not that the city has no sites.
- New reservations, redevelopment and actual placements invalidate cached
  results without replenishing the pass budget. Later yards defer additional
  expensive searches; ordinary building choices/production keep running.
- Completed CY items take priority over new plans, with deterministic rotation
  within each CY priority group. For blocked reserved turrets, advance the map
  batch only after all ready yards have had a turn; this avoids scan/yard-count
  resonance stranding a yard on the same map strip forever.

Scheduling derives from simulation cycles, never elapsed wall time. Caches and
ready-yard count reset/derive within each build call; save format is unchanged.
City effects, service strengths, overlap rules, road access and unit pathfinding
are unchanged. Vanilla retains its existing placement search; cheap guard
reordering also applies there. Policy telemetry is now `bounded-city-planning-v45`.
New performance counters: `service.cache_hit`, `service.search_deferred`,
`service.scanned_tiles`, `turret.cache_hit`, `turret.search_deferred`,
`turret.scanned_tiles`. Compare these plus existing timed scopes in the next game.

Tests cover complete bounded map sweeps, edge cells, negative cache sharing,
geometry invalidation without renewed work, distinct reservation keys, ready
placement priority, fair yard rotation and every blocked yard visiting every
batch even when the yard count equals the batch count. Build/test logs:
`/tmp/dunecity-planning-{build,tests}.log`. Full CTest: 527 cases, 524 passed,
3 optional skips; before/after dependency audits and version/app metadata passed.
Local app 1.0.622; no live-game launch,
Applications copy, push or release. Actual FPS improvement needs a new game.

# Route-based traffic density and Micropolis decay — 1.0.621

Fixed the traffic animation's inflated input rather than raising sprite
thresholds. In the620 game1789031518687887-0 snapshot59905,5150of6491road
tiles were heavy (79.3%). Root causes: BFS discovered branches were all stamped,
successful destination duplicated, every visited tile added50into2x2cells,
and city-role buildings emitted a radial level*25 traffic halo.

TrafficSimulation now uses CityTrafficPolicy::RouteFinder, which preserves
existing deterministic N/E/S/W BFS connectivity/distance limit but reconstructs
only the successful route with parent links. Failure/NoRoad clears previous
path. Reusable generation stamps and vector queue avoid full-map visited clears
and per-call queue allocations across zone searches. No RNG or unit A* changes.

CityTraffic::addJourney samples moves2,4,6,... from perimeter start (route[0]),
matching original Micropolis tryDrive's dist&1 sampling for2x2traffic cells.
Actual road samples add50 capped240; turret connectors remain traversable but
only road tiles receive density. No extra global cell deduplication: sampling
matches original, including turns which can revisit a density cell.
runEffectsScans retains accumulated traffic and calls decay once per city day,
once per cell: <=24→0, >200→minus34, otherwise minus24. Removed full layer reset,
building halos and per-road minus15. Growth phase adds journeys after decay.
Original refs: MicropolisEngine/src/traffic.cpp and simulate.cpp::decTrafficMap.

This ports density sampling/decay, not original random-walk route selection,
probabilistic journey frequency or absolute calendar cadence. Existing BFS,
2x2zones,1x1roads, zone connectivity/growth checks, animation thresholds64/192
and animation speed remain. Traffic pollution/status use corrected density.
Traffic layer remains derived/unserialized: loading rebuilds it from journeys;
it warms up over subsequent days. No save format change.

New shared-policy tests exercise branched/looped road networks, deterministic
ties, distance bounds, failure/reset/map resize, exact sample positions, true
congestion/cap, nonroad connectors, decay thresholds and partial edge cells.
Full CTest523cases520passed/3optional skips; dependency audits and621version/app
metadata passed. Logs /tmp/dunecity-traffic-{build,tests}.log. Local app built,
no live game launch/Applications copy/remote push/release. Restart to load621.

# Performance investigation and session telemetry — 1.0.620

Last completed game1789026476214205-0 ran1.0.618,192x192 SimCity map,
Harkonnen0 vs Sardaukar4,109737cycles. It predates the619 animation port.
Last full120s performance window:14.9FPS/67.09ms frames, path32.86ms,
AI15.21ms,render7.83ms. Worst frame559.37ms; house update494.8ms.
Path budget5000 vs actual16106nodes/tick: budget enforced between complete
searches, no remaining-budget argument to resolver. Five ticks per frame
multiply that cost. Service evaluations repeat across yards at severe AI
spikes (7/16/18 evaluations in examples), but precise attribution was missing.
City phases also spike10–30ms. No gameplay optimization claimed this turn.

Added bounded five-second in-memory performance aggregation to existing JSONL
session logger, offline SQLite performance_windows/performance_metrics views.
Every frame counted, worst-frame full context, per-house AI build/search/combat
scopes, service site counts, city subphases, paths by unit type/owner, actual
budget/overshoot, queues and logging costs. Inclusive scopes must not be summed
across nesting. Normal stop flushes partial window; telemetry version11,
policy unchanged. Optional existing256MB cap and disabling env var retained.
No SQLite writes in the game loop, no wall timing changes simulation decisions.
Fixed worst-house overwrite across frame ticks, unreset per-frame path-node
and failure counters, stale empty-queue cycle metrics. See
[performance telemetry](docs/performance-telemetry.md) for evidence and queries.

Validated CTest519cases (516passed/3optional skips),12 Python importer tests,
real C++ capture→SQLite import, dependency audits and local620app metadata.
Original legacy text preserved alongside game session as performance-legacy.log.
Analysis database /tmp/dunecity-last-game-performance.sqlite contains80456
structured events plus7836 legacy slow-frame samples and1221 house spikes.
Logs /tmp/dunecity-performance-{build,tests,python-tests}.log.
Local app build only; not launched, no /Applications copy, remote push or release.
Live overhead/FPS improvements need the next game; no such measurement claimed.

# Micropolis building models and animations — 1.0.619

Restored all16 apartment models +12 house styles,20 commercial models,8
industrial models. Prior GFX runtime skipped inhabited d0 variants, highest
commercial d4, and all houses. Stable coordinate hash selects among appropriate
visual models within existing3 inhabited density levels. R/C/I gameplay2x2,
roads1x1 and specials3x3 unchanged; no population/effects/AI/save/RNG changes.

New scripts/build-city-atlases.py assembles tracked raw tiles into seven compact
runtime atlases (Pillow authoring only). Full importer invokes it and fixes
category bounds/omitted full stadium:800 is centre, base795. Factory smoke uses
corrected documented chimney IDs/positions, never the upstream vacant620 bug
or overwritten second chimney. Powered radar, periodic full stadium/football,
nuclear swirl and light/heavy traffic sequences restored. Traffic thresholds
64/192 match original doRoad. Preserve black vehicle pixels; old road recolour
andcentre dot erased cars. No live traffic through fog. Simulation-cycle visual
clock freezes on pause; stadium8of32seconds is an adapted presentation cadence.
See scripts/SPRITE-IMPORT.md for IDs/layouts/source links and regeneration.

GFXManager loads prepared atlases, validates dimensions, scales once and shares
surfaces/textures across houses; eliminates per-house duplication for these
seven larger sheets. Max dimension at3xzoom1728px. Constant-time source-frame
selection, no map scans/per-frame images. Missing/stale atlas data produces a
clear startup error; all assets are tracked and existing platform packaging
copies imported_sprites. Build-menu/editor icons use inhabited static models.

Full CTest passed (518cases,515passed/3optional skipped), before/after Ninja
dependency checks passed. PNG tests verify dimensions, real animation frames,
vacancy/power/traffic gating, all models reachable and clamped frame bounds.
Fresh full Micropolis import into/tmp reproduced all seven atlases pixel-for-
pixel; generated contact sheet inspected. All eight atlas directory files in
local app bundle match source. Version1.0.619 metadata verified. Live-game FPS
not measured; no app launch, /Applications install, push or remote release.
Build/test logs:/tmp/dunecity-animation-{build,tests}.log.

# Micropolis park terrain for walls/turrets — 1.0.618

Replaced walls/gun turrets/rocket turrets radial +15 land-value stamps with one
+15 raw park source per structure origin. Reference: local Micropolis scan.cpp
pollutionTerrainLandValueScan (one +15 per qualifying terrain tile) and
smoothTerrain non-dither branch: (center + cardinalSum/4)/2, single pass with
integer rounding. Original literal FOUNTAIN tile does not receive the tree
terrain increment; this implements Stefan's intended one-park equivalence,
not that original fountain quirk. Both turrets have identical park effects.

Scaling: original4x4 terrain cell corresponds to3-tile zone +1-tile road; use
3x3 terrain cells for2-tile zone + unchanged1-tile road. Gameplay zones remain
2x2, roads1x1 and land-value storage2x2. Average tile samples into the2x2 land
layer to avoid aliasing odd-coordinate sources across nonaligned3/2 grids.
One isolated source gives7 in its terrain cell,1 in cardinal neighbours,0
in diagonal/distant cells before resampling. Multiple sources accumulate raw
before smoothing, avoiding duplicated world-tile additions/rounded emitters.
Park contribution enters base land value before pollution subtraction and
clamping. Existing sand terrain/direct bonuses and Palace/Stadium stamps stay
unchanged; this is park-source parity, not a complete terrain-simulation port.

ParkTerrainPolicy is deterministic/local and derived, rebuilt each effects
scan, not serialized. CitySimulation initializes it for new/load state. AI
service investment and turret amenity estimates share exact marginal smoothed
contributions including planned sources and2x2 resampling. The old doubled
rocket radius is gone; getParkLandValueRadius for park sources is now only a
conservative search bound. Combat and police coverage unchanged.

Tests cover source counting, original kernel goldens, raw overlap aggregation,
map edges/partial cells, no negative-coordinate alias, rebuilding destroyed
sources, pollution-before-clamp, and exhaustive AI/runtime gain agreement on
an11x10 map with overlapping sources and nonaligned grids. Full CTest passed;
Ninja dependency audits passed before/after build. Local1.0.618 app metadata
verified. No app launch, Applications copy, remote push or release. Logs:
/tmp/dunecity-park-terrain-{build,tests}.log. Existing cities recalculate their
lower turret-driven values on the next effects scan; tax/crime may respond.

# Pollution sidebar overlay button — 1.0.617

Added Pollution below Land Value and Crime, using existing CityOverlayMode::Pollution.
Click toggles off/on, selected state tracks Shift+4, and visibility matches other
city-only buttons when selection is empty. Existing green-to-purple renderer and
legend reused; no simulation change. Local617 built and full CTest/dependency
checks passed. No remote release, app launch or /Applications copy.
Also verified user query: Micropolis scan.cpp subtracts pollution from land value;
our computeBaseLandValue does too. DuneCity adds park/turret/sand bonuses afterward,
so positive bonuses can offset pollution. No change to this formula requested.

# Zone suitability, labour demand and civic notices — 1.0.616

Reviewed completed 1.0.614 4 Corners session1789019169404635-0. In the
last building snapshots, all212 vacant R/C zones had pollution>=160. C/I
were capped in all280 state samples after5simminutes. Verified against local
Micropolis simulate.cpp: resHist stores resPop/8, but our saved prevResPop is
raw. computeDemandValves now divides previous residential population by8 at
the labour boundary; save fields and startup safeguards remain unchanged.
This removes the artificial1.3 labour saturation, not all legitimate positive
demand. Existing accumulated valves adjust through subsequent simulation ticks.

AI R/C sites are rejected when their origin pollution blocks growth, using the
same role-specific gate as runZoneGrowth. Industry still tolerates pollution.
Environmental/commute tier now precedes residential infill; safety stays first.
Positive residential demand still prioritizes usable infill, and no-site results
fall back to other demanded zone types. Rejections are logged as
pollution_rejections. No new path searches or pollution formula changes.

The demand calculation reports which positive valves were capped by missing
Stadium (or Palace substitute), Airport, or Starport. Local-house UI notices
name the required building. UI-only state deduplicates notices until resolved,
keeps simultaneous requests pending, spaces them10simseconds apart, and drops
pending requests if the requirement is satisfied. No save-format changes.

Regression tests cover oversupplied jobs draining capped C/I demand into negative
values, civic thresholds/Palace substitution, no false notices for negative
demand, notification spacing/resolution, and suitability before infill. Full
CTest and before/after Ninja dependency audits passed. Version1.0.616 built at
build/bin/dunecity.app, metadata verified. No game launch, /Applications copy,
remote push or release. Logs: /tmp/dunecity-demand-{build,tests}.log.
Pollution spreading still needs a separate audit against Micropolis; not changed
in this task. Live-game balance after the normalization fix is not yet measured.

# Roads are prepared foundations; reuse spare lanes — 1.0.615

Stefan's double-road screenshots showed factories wasting space and concrete.
Tile::hasPreparedFoundation now treats concrete OR a road as prepared ground,
without merging their tile states. StructureBase captures this before clearing
the road flag: previously it cleared the flag and checked only concrete, charging
placement damage on roads. Prepared roads also follow the existing concrete
foundation degradation rule. Covered road flags still clear normally.

QBot foundation scoring counts roads, slab-site search avoids paved roads, and
pre-concreting skips road/concrete cells. Slab4 is used only when all four cells
are bare; mixed footprints get individual missing Slab1s. Shared foundation
helpers are exercised with a3x2 footprint containing3road cells: exactly3Slab1
orders. Placement no longer penalises every covered road; it rewards reusing
redundant parallel lanes while retaining local access and road connectivity
checks. Telemetry reports redundant_roads_reused. No map-wide path tracking.
Tests cover spare-lane reuse, a single road with no alternate connection, mixed
foundations, and foundation capture before road flag clearing. Full CTest and
Ninja dependency audits passed; local615 built, no remote release or launch.
Logs: /tmp/dunecity-road-foundation-{build,tests}.log.

# Local road access and residential infill — 1.0.614

Supersedes 613's global connectivity search at Stefan's request. GroundAccessPolicy
now checks only a candidate footprint and its one-tile border, preserving local
passage between surviving border tiles. No full-map reset/flood, anchor, protected
unit paths or scan of all producers. A 3x2 candidate reads20 occupancy tiles.
QuantBot separately checks deployment openings only for touching producers and
nearby other-yard reservations; roads/slabs remain passable, transient units do
not reserve space. Rocket turrets allow diagonal local passage at road junctions.
Existing city road-perimeter planning and road-connection safeguards remain.
This incremental local rule does not diagnose/repair pre-existing distant traps.

Residential sites with R/C/I on at least two nearby sides (touching or across one
road tile) count as infill. Safe infill ranks above outward expansion; existing
safety checks stay in force. If positive R demand and a buildable infill site exist,
choose R before normal demand/count balancing. Bootstrap and C/I fallback remain
when no residential infill is available or R has no demand. Telemetry identifies
residential_infill and residential_infill_sides. Four-zone block scoring now
permits mixed R/C/I rather than only identical zone types.

Full CTest and dependency audits passed. Tests cover touching blocks, closing a
local lane, reservations/terrain, map edges, junctions, bounded occupancy reads,
and residential demand/infill priority. Synthetic placement-only comparison was
13.1ms for613 versus10.4ms for614 over11907 checks; not an FPS claim. Recent runtime
logs had AI updates up to310.7ms and separate unit path costs around20ms/frame;
live-game performance after this change remains unverified. Local614 built;
no remote release or game launch performed. Build/test logs are under
/tmp/dunecity-local-placement-{build,tests}.log.

# Preserve connectivity rather than fixed lanes — 1.0.613

Current 612 game 1789013651529445-0 (4 corners, city mode) showed the ground
access guard rejecting 55,961 of 70,669 candidate checks for Mercenary in a
five-simulation-minute sample (~79%; repeated candidates, not distinct tiles).
Fixed paths from every unit and every factory perimeter tile over-reserved land.
GroundAccessPolicy now retains unit endpoints and groups of producer exits,
not path tiles. A candidate may reroute access; each previously connected producer
needs at least one remaining outside-connected perimeter tile. Existing connected
units must remain connected and cannot be covered. New factories also need one
outside-connected exit. Existing isolated units/producers do not freeze unrelated
construction. Other-yard reservations remain static barriers in QuantBot's caller.
A connected local perimeter is a cheap proof that detours remain; otherwise an
early-exit flood checks longer detours, then validates protected endpoints when
components split. If the old outside anchor is covered, use the largest remaining
component rather than reserve the anchor forever. No random state or save fields.
Regression cases cover long alternate routes, sealing the final opening, corner
exit replacement, trapped units, and disconnected courtyards. Full CTest passed;
Ninja dependency audits passed before/after build. Local build is 1.0.613; live
612 game was not restarted, and no remote release was requested for this change.

## Current release/hosting entry point — 2026-09-10

Read `docs/release-operations.md` and `docs/sourceforge-releases.md` for current
operations. SourceForge sync is configured and verified; no credential setup is
pending. User policy: do not upload source archives to SourceForge Files. Publish
six binaries, README with tagged Git source link, and SHA256SUMS. Git branch/tag
mirroring remains enabled. The original nine-file publication below is historical;
the current release folder now contains eight files. Verification run
https://github.com/VR48/dunecity/actions/runs/34431698171 passed all eight release
safeguard tests, read back matching upload hashes, and confirmed the existing
Windows/macOS/Linux defaults at 1.0.612. The source archive was removed explicitly.
Retries now check current defaults before issuing an API update: a redundant PUT
returned HTTP 400 after an otherwise successful upload. Source remains available
via the README's tagged GitHub link and SourceForge's dedicated Git refs.

## SourceForge release automation — 2026-09-09

Added `.github/workflows/sourceforge.yml` and `scripts/sourceforge-release.py`.
They mirror successful stable GitHub releases to SourceForge `dunecity/<version>`,
verify uploaded checksums by readback, then publish namespaced source tags and
advance the dedicated `dunecity` branch/platform defaults for the latest release.
Legacy master and old files are preserved. Manual dispatch supports backfills.

SourceForge setup completed 2026-09-10: account svan058, dedicated SSH key and
pinned host keys, Releases API key and GitHub variable/secrets configured.
Workflow run https://github.com/VR48/dunecity/actions/runs/34416472472 succeeded
in 2m15s. All nine files read back with matching SHA256; public Files page lists
six packages, source archive, notes and checksums. API confirmed Windows ZIP,
macOS DMG and Linux AppImage defaults at 1.0.612. Source branch `dunecity` and
peeled `dunecity-v1.0.612` resolve to b949be92a1f44233c1964e3b1b3995088ac3a76e;
Legacy master was unchanged by that release sync; subsequent website-only commit
`dc69c5a` updated its downloads page. Future stable-tag
builds trigger the mirror automatically. Setup/operations documentation is in
`docs/sourceforge-releases.md`. Do not print or commit credential contents.
This is infrastructure-only, pushed with `[skip ci]`; game version remains 1.0.612.

# Preserve qBot factory exits and ground routes — 1.0.612

Stefan's Vanilla611 screenshots show rear factories surrounded by structures,
with their free deployment tiles inside sealed courtyards. Placement previously
checked adjacent open tiles without checking a route out. GroundAccessPolicy
now builds a deterministic static vehicle graph (mountains and structures block;
roads, slabs and transient units do not). It selects the largest component and
an anchor in its widest open area, then preserves routes from own production/
refinery/repair/police deployment rings (including corners) and active ground
units. Proposed ground producers require every free deployment-ring component
to connect outside without passing through their own footprint. This allows a
small detour around a new factory. Other buildings cannot cover protected lanes.
Other yards' reserved footprints are included; cached state clears per planning
pass/reservation/placement and simulation cycle. MCV deployment also checks lanes
and reservations; MCV positions themselves are exempt because deployment consumes
that unit. Generic, service/turret, redevelopment, generator fallback and final
placement paths use the guard. Rear safety scoring is retained. No save fields,
random draws or non-qBot controller behavior changed. Already enclosed bases are
not automatically demolished/repaired by this prevention change.

504 test cases passed, 3 optional skipped through CTest. Pre/post Ninja dependency
audits, deep strict signing and version612 checks passed. Synthetic access-only
benchmark for eight passes/80000 candidates: 34ms at128x128, 7ms at192x192, 10ms
at256x256 (not a live FPS measurement). Local build612 ready; not launched/pushed.
Build/test logs: /tmp/dunecity-612-build.log, /tmp/dunecity-612-tests.log.

Harvester investigation (no economy policy changes in612): Vanilla611 session
1788876460778739-0, Atreides qBotBrutal, override100. Both AI/engine caps were100;
actual fleet passed40 and peaked75 at14.21simulation minutes. 738249 spice still
remained but equal division by5 houses followed by2000 spice/harvester reduced
target to73. Consecutive samples had46-53 actively harvesting and roughly17-30k
credits/minute income: the cap worked, but this total-inventory/equal-share
heuristic is too weak to justify the expansion cutoff. Recommended follow-up is
usable local field/refinery-throughput demand, subject to explicit cap and cash,
instead of treating each house as entitled to exactly one-fifth of all spice.
User asked to evaluate that assumption; replacement policy is not implemented.

# Restore legacy AI harvester restart — 1.0.611

Stefan reported Original AI harvesters stranded in Vanilla610. Logs show empty,
respondable harvesters with mode6(STOP), no target/path, stationary30seconds.
610 removed native auto-resume for every AI; Original/Smart controllers rely on
that restart and only issue early-return orders themselves. The scope was wrong.
Harvester::checkPos now restores STOP->HARVEST for AI houses without a QuantBot
controller. A qBot co-controller retains responsibility for safety holds/resumption;
human STOP remains preserved. Runtime checks the house player list, not display
names or mod flags. The empty-refinery-loop fix remains. No save layout changes.
Regression covers legacy AI, qBot, human and human+qBot restart decisions.
Local611 built on MacBook Air: pre/post dependency audits passed, CTest495passed/
3optional skipped, deep strict signature and bundle version611 verified. Not
launched or pushed. Logs /tmp/dunecity-611-build.log and /tmp/dunecity-611-tests.log.

# Harvester empty-refinery oscillation — 1.0.610

Current Vanilla609 session1788874105288417-0 showed repeated retreat_refinery
orders with zero cargo (harvester619 had120 logged orders in the read snapshot).
Two conflicts: safety requested refinery trips for an empty vehicle whose old
spice destination was dangerous despite its current location being safe; native
Harvester::checkPos forcibly changed AI STOP back to HARVEST on every check.
Removed forced auto-resumption. Safety now requests a new refinery refuge for
immediate local danger, or cargo plus unsafe-job/return state. Empty safe vehicles
search safe spice or disperse/hold. Existing valid safe return trips still finish;
loaded unloading and threatened empty evacuation remain. No new timer/save fields.

Local610 build, pre/post dependency audits, signature and CTest494pass/3skip passed.
No game restart or remote push. Regression covers retreat then unloaded state,
unsafe empty job and loaded returns. Live behaviour requires the new process.

Current609 match at24.32simminutes: Harkonnen qBotBrutal71,810/80karmy,credits2888;
Atreides60, Fremen60, Mercenary2370army. Hark target mix tank12.43%siege14.67%
launcher54.99%special13.34%quad4.57%; reward/lostcost1.46/1.87/5.01/1.78/.91.
Merc orni reward/loss.15,target1.55%; launcher3.11,target41.13%.
These are live snapshots, not final outcomes; buildable-type availability changes
as factories are lost. Script /tmp/review-live609.py; no balance change beyond bugfix.

# Performance weighting, fair attack timing and local placement lookup — 1.0.609

Stefan approved score^1.5 plus the prior review recommendations and a local build.
UnitMixPolicy now sharpens normalised performance scores with an integer square
root, after the fading trial prior. Full-match reward/loss evidence, tech openings,
Vanilla evidence blending, the universal 80% type cap and Vanilla air cap remain.
Integer normalisation bounds arithmetic and avoids floating-point pow differences.
Telemetry records exponent1500 and per-type allocation_weight alongside the
original score; SQLite unit_allocation exposes both new fields, with old logs NULL.

Attack delays now derive 75–125% of the configured interval from seed, simulation
cycle and house through a fixed uint32 hash. Both initialization and resets use it.
This replaces the permanent (houseID-3)*15-second bias. No wall clock or extra RNG
consumption; existing saved countdowns still load, next reset uses the new policy.
An attack_schedule event records base and selected cycles. This is deterministic
by construction; a live multi-computer test has not been run.

Service planning snapshots own police and rocket positions once per decision,
instead of rescanning the global structure list per property/candidate. An 8-tile
spatial index visits only properties within the existing 23-tile Chebyshev radius.
Scoring, coverage stacking, reservations, candidate iteration/tie order and building
priorities are unchanged. The local snapshot is rebuilt per decision. Regression
checks compare indexed results against exhaustive scans, including map/bucket edges.
This addresses a visible repeated-scan cost; it does not claim to identify every
source of the logged 405ms AI spike or prove a live FPS improvement.

Local Release1.0.609 built on MacBook Air, dependency audits before/after passed,
CTest493passed/3optional skipped, SQLite importer11tests passed, version metadata
and deep strict ad-hoc signature checked. No game launch or remote push requested.
Policy performance-weighted-fair-attacks-v44; save layout unchanged at9834.
Logs /tmp/dunecity-609-build.log, /tmp/dunecity-609-tests.log,
/tmp/dunecity-609-sql-tests.log. Source is committed for the next release.

Previous release1.0.608 is public (a094d10), all GitHub platform/tests passed and
website links verified. The next reviewed match was actually1.0.606:
session1788869520113577-0,81.84simminutes,328467events imported/audited clean in
build/review-latest-606.sqlite. All4qBotHard70kcap,roughly947k–999999credits,24heavy
factories and8yards each. Latest mix snapshots76–77min: launcher shares55/47/49/42%,
reward/loss4.38/3.24/5.44/3.33; orni4.5/7.9/3.5/6.6%,reward/loss.35/.53/.37/.51.
Huntorders73/46/19/22 (Harkonnen/Ordos/Neutral/Rebels); readiness also affects counts.
Last performance window18.2FPS,AIaverage12.43ms,max405.02ms,~1000units,5cycles/frame.
No additional economy, factory, crime or military-limit changes made here.

# Road completion, launcher safety and grouped outbreaks — 1.0.607

Stefan approved all three findings from the completed 606 review. ConstructionYard
now recognises successful tile placement when House::placeStructure consumes the
completed queue entry, even though it returns nullptr for roads/concrete. This
fixes false failures and stale road reservations/retries in qBot. Snapshot the
queue length before placement so several identical queued roads are handled
correctly. Failed placement with unchanged queue still reports failure. Tile
mutation remains in House; no road-routing responsibility moves into the yard.

qBot's shared harvester danger grid adds 3 tiles beyond visible launcher weapon
range. Other weapons retain existing reach; crushable foot troops remain excluded.
Before missiles hit, threatened harvesters request a clearing team against the
nearest relevant visible launcher using the cached threat list. Proactive responses
use troops within 18 tiles and require enough total local/committed health-adjusted
value for the existing 125% threat budget; nearby defending turrets also count as
threats. Insufficient local strength sends no sacrificial partial team. Already
committed responders count, human orders/retreats/other live fights remain protected.
Proactive incident checks debounce at 5 seconds; actual damage responses retain
2 seconds and existing emergency behaviour. Both Vanilla and DuneCity qBot benefit.
Returning harvesters now validate the refinery corridor as well as its endpoint,
so they cannot keep an unloading trip through launcher fire merely because the
refinery itself is safe. Existing safe-refinery preference and field redirection
remain; paths use the existing corridor estimate, not a full pathfinding proof.
Telemetry includes clear_spice_launcher response reason and harvester threat radius.

City gangs retain each district's 4–6 minute crime buildup and occupied density
1/2/3 strengths, threshold192 and population gate5000. Once mature, an outbreak
waits up to approximately30 extra simulation seconds (city-scan quantisation) to
combine neighbouring ready districts. Eight-neighbour connected components are
restricted to the same house; only fully mature districts join. Any member's
expired gathering window releases the component together at its highest-crime
origin. Policing/vacancy/small population cancels pending readiness. Contributing
districts all reset once, even if engine capacity/space limits actual deployment.
This is bounded district-grid work, not per-trooper graph searches. Telemetry adds
contributing_districts, district_strengths and gathering_ms.

Save version9834 reuses the old64bit exposure slot for readyCycles. Loading9833
retains buildup progress but discards old exposure; older timers retain their
existing reset migration. Current saves preserve grouping delay; simulation cycles
and stable ordering only. Policy grouped-unrest-harvester-safety-v43.

Verified local Release1.0.607: dependency audits, version consistency, signature
and CTest490passed/3optional skipped. Regression checks cover tile queue success
versus rejection, launcher margin/crossing/escape, sufficient nearby clearing
forces, gathering delay/immature exclusion/house+row boundaries/policing, and saved
readiness/legacy exposure. Not launched, pushed or released. Live balance needs a
new game; no claim of measured FPS improvement.

Evidence prompting these fixes: completed606 session1788866702844001-0 lasted
34.023simminutes, both qBotHard cap70000 and both alive. SQLitebuild/review-606.sqlite
104213events, auditclean. Fremen harvester losses3 versus Harkonnen39;20of38 logged
Harkonnen harvester lethal events identify known-built launchers. All1753heavy
no_affordable_capacity decisions had<300remaining cap. Road placement189events
allfalse;353road cancellations allshow existingroad,175reservedoverlap. Gang141
outbreaks1402troopers,max54,39waves<=3;1969/3083defenceresponses targeted exactgang
IDs. Final32police each versus177/194rocket turrets,meancrime12/8. Reviewnotes at
/tmp/dunecity-606-review.md. No further army-limit/service-ratio change requested.

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
consistent. CTest482passed, 3optional skipped. Tests cover4/6minute boundaries,
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

Session 1788846458415706-0 ended cleanly at760548cycles/202.81simulation minutes,
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
passed (471 cases passed, 3optional skipped), app signature and version checked.
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

Session 1788799572693304-0 v597: both houses stayed at two yards with
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
Session 1788691499646985-0 endedcycle95 and1788691615471597-0 cycle99 in554; normal log
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


## 2026-09-13 — HTTPS polling candidate 1.0.658

Current work on `fix/network-hardening` adds browser/native HTTP polling over the
existing Apache/PHP server, preserving the relay/game protocol. No main/public
release or cron activation yet. See `docs/https-relay-deployment.md` for verified
constraints, tests and remaining release gates; `docs/room-relay-http-polling.md`
for the transport. Node analytics accepts a private key file and emits schema2
with observed `https-poll`; the website receiver changes are in the separate
`dunelegacy-relay-analytics` worktree. Never bundle the deployed private keys.

User testing found two failures: generated EM_JS escaped a regex incorrectly,
rejecting successful open responses; duplicate default names were refused but
presented as an ended game with controls disabled. Both are fixed, with compiled
JS and relay-session regression coverage. The lobby has a separate Join Game
button and Change name action. Local two-browser gameplay has started successfully;
this does not attest public Apache multiplayer. Test service 18790 and web8768
serve the local 1.0.658 candidate, separate from old8787/8766 clients.

Follow-up verification on the same candidate: metaserver Node22.23.2 passed all
200 relay tests and its PHP8.3 gateway contract checks. A bounded launch of the
exact candidate under Landlock + Node permissions reported analytics enabled,
answered authenticated health and refused unauthenticated health. It was stopped
and the prior candidate symlink restored; no public gateway or cron installed.
Hermes reviewed frozen source402caf4 (artifact SHA256 recorded in the review),
reported no new source-verified blocker, and passed200 tests. Its bounded run
interrupted one parallel review worker; this is limited review, not full security
certification. Existing production/watchdog gates remain. Browser match logs
advanced beyond31500 cycles with a tested movement order and no reported state
digest mismatch; host and guest continued exchanging performance reports.
