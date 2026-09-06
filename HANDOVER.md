# Handover — DuneCity session, 2026-09-06

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
