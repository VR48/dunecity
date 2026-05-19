# Task: Ambient city aircraft

## Context

DuneCity now has city airports and civic/economy structures. Add SimCity-style ambient airplane and helicopter traffic using the existing Dune air-unit framework, but keep this as ambience/service signalling rather than a full RTS combat/buildable unit feature.

## Goal

Add ambient city aircraft:
- Ambient airplane
- Ambient helicopter

These should reuse the Dune framework for units/air units as much as practical, while staying non-combat and non-invasive.

## Desired behaviour

- Spawn from, orbit near, or periodically pass over city airport/civic areas.
- Use existing air rendering/tile assignment/update plumbing.
- Non-selectable or effectively ignored by normal combat selection.
- Non-combat: should not attack, be targeted as a military threat, or disrupt RTS balance.
- Should visually read as city ambience rather than Ornithopter combat spam.
- Aircraft should be deterministic enough not to desync multiplayer.
- Ambient aircraft should not count against normal combat air-unit caps unless the existing framework makes that unavoidable and the tradeoff is documented.
- If dedicated art is not available, use placeholder existing air-unit art with clear names and comments.

## Scope

Implement the cheapest good version first:
- Framework-backed ambient units/classes if needed.
- Spawn/update integration tied to Airport or city simulation cadence.
- Names/object data/sand resolver support if required.
- Minimal UI exposure only if the object data system requires it.

Do not build the hard version yet:
- No full player-controlled airplane/helicopter build queue.
- No runway simulation.
- No combat behaviours.
- No AI production logic.
- No new art pipeline.

## Suggested code areas

Inspect:
- `include/data.h`
- `src/units/AirUnit.cpp`
- existing `Ornithopter`, `Carryall`, `Frigate` classes
- `src/Tile.cpp` air-unit assignment/rendering
- `src/ObjectBase.cpp` object factory/save-load switch
- `src/structures/Airport.cpp`
- `src/dunecity/CityEffectsRuntime.cpp`
- `src/Game.cpp` object update/render loop and selection filters
- `src/sand.cpp` name/size/detail mappings
- `config/ObjectData.ini.default`

## Constraints

- Work in `/Users/stefanclaw/development/dunecity`.
- Preserve all current uncommitted DuneCity changes, including phase10 civic-expansion work and budget-window fixes.
- Keep normal R/C/I zones at 2x2.
- Preserve save/load compatibility unless a version bump is strictly required and documented.
- Do not restart OpenClaw or the gateway.
- Keep changes surgical; no broad air-system rewrite.

## Verification

Minimum:
- Compile the `dunecity` target or, if blocked, compile changed objects and explain.
- Run focused tests if any helper logic is covered.
- Manually inspect that object factory/name mappings are complete enough for save/load and debug output.

## Completion report

Report:
- Built
- Not yet built
- Blocked on
- Verification commands and outcomes
- Changed file paths

