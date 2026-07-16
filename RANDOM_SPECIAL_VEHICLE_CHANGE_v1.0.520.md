# DuneCity Torneko v1.0.520 — Random Special Vehicle Selection

## Modified behavior

`Unit_Special` entries loaded from an INI map now select a random enabled vehicle when the House pool contains more than one valid candidate.

- The House pools remain defined in `include/SpecialVehicle.h`.
- Disabled or invalid candidates are filtered before the draw.
- Zero valid candidates skip the special unit.
- One valid candidate is selected directly without consuming RNG state.
- Two or more candidates use `Game::randomGen`, preserving deterministic multiplayer and save behavior.

## Modified source files

- `src/INIMap/INIMapLoader.cpp`
- `CMakeLists.txt` — version updated to 1.0.520
- `include/config.h` — fallback version updated to 1.0.520
- `release_notes/RELEASE_NOTES.md`

## Existing behavior already verified

Runtime `Unit_Special` creation in `src/ObjectBase.cpp` and Tech Center spawning in `src/structures/TechCenter.cpp` already use random selection. This patch makes INI scenario loading consistent with those paths.
