# DuneCity: SimCity + Dune Legacy Design Analysis

## Executive Summary

This document analyzes the feasibility of merging SimCity-style city building mechanics with Dune Legacy (the open-source Dune 2 remake) using C++. The existing Dune Legacy codebase already contains a partial DuneCity simulation module with zone management, power grids, traffic simulation, and disaster systems—providing a strong foundation for the proposed hybrid game.

The core insight: **You're not just building a military base—you're building a city on Arrakis.** Civilians inhabit your base, generate tax revenue through RCI zones, and create strategic vulnerabilities. The existing codebase has ~70% of the simulation infrastructure; the gap is integration and expansion.

---

## 1. What Makes Each Game Fun and Unique

### Dune Legacy / Dune 2

| Aspect | What Makes It Fun |
|--------|-------------------|
| **Faction asymmetry** | Three distinct houses (Atreides, Harkonnen, Ordos) with unique unit rosters, combat abilities, and tactical preferences |
| **Base building** | Construction dependency chains (silo → refinery → heavy factory), power grid management, limited building placement |
| **Resource combat** | Spice harvesting as a contested resource, territory control, map control determining economic advantage |
| **Combat tactics** | Unit composition matters (infantry screen + tanks + air support), terrain advantage, siege warfare |
| **Atmosphere** | Desert aesthetics, sandworms as mobile hazards, spice storms reducing visibility, fremen/mentat narrative voice |
| **Real-time pressure** | Enemy harassment, spice depletion, escalation timelines |

### SimCity (Classic - SC2000)

| Aspect | What Makes It Fun |
|--------|-------------------|
| **Organic growth** | Cities develop based on zone placement, infrastructure connectivity, and service availability |
| **Economic feedback loops** | Land value → tax revenue → better services → population growth → more taxes |
| **Interconnected problems** | Traffic congestion causes pollution, pollution reduces land value, lower land value reduces tax—solutions require holistic thinking |
| **Long-term progression** | Population milestones, city specialization, mayor rating system, technology tree |
| **Tile-level detail** | Individual roads with turn lanes, zones with density (low/medium/high), granular building placement |
| **Mayoral agency** | Policy decisions: tax rates, ordinances, disaster preparedness |

### The Synthesis Opportunity

**Dune Legacy brings:**
- Real-time combat and faction competition
- Military unit production and tactics
- Spice as a contested, limited resource
- Desert map with unique hazards (sandworms, storms)
- Existing rendering engine (SDL2) and asset pipeline

**SimCity brings:**
- Civilian population as distinct from military units
- RCI zoning with density-based organic growth
- City services (police, fire, health) affecting zone health
- Economic simulation with tax revenue and budget allocation
- Long-term progression and mayoral decision-making

**The synthesis**: A city-builder where military infrastructure and civilian population coexist. Your military protects the city; the city funds the military. Destroying enemy military leaves their civilian population available for "liberation" and tax revenue. Neglecting civilian needs causes unrest that weakens military output.

---

## 2. Core Mechanics to Merge

### 2.1 Dual Population System

```
┌─────────────────────────────────────────────────────────┐
│                    HOUSE (Faction)                       │
├─────────────────────────────────────────────────────────┤
│  Military Units ←──── Combat ←──── Player Control      │
│       ↑                                               │
│       │ (consume credits, require power)              │
├───────┼───────────────────────────────────────────────┤
│       │              DuneCity Simulation               │
│  Civilian Pop ←──── RCI Zones ←──── Growth Logic      │
│       ↓                                               │
│  Tax Revenue ←──── City Budget ←──── Player Control   │
└─────────────────────────────────────────────────────────┘
```

**Implementation approach**: Extend existing `Tile` class city zone fields (already partially implemented in `CitySimulation`). Military units and civilian population coexist on the map:
- Military structures (barracks, factories, turrets) don't contribute to tax revenue but consume power
- Civilian zones (residential, commercial, industrial) generate tax but create traffic demand and vulnerability

The existing `ZoneType` enum in `CityConstants.h` already defines `Residential`, `Commercial`, `Industrial`.

### 2.2 Hybrid Resource System

| Resource | Dune Source | SimCity Source | Role |
|----------|-------------|----------------|------|
| Spice | Harvested by harvester units | — | Primary military/economic resource |
| Credits | Market trading, combat rewards | Tax revenue from civilian zones | General purpose currency |
| Power | Windtraps, Solar (existing) | Same structures | Required for all structures |
| Population | — | RCI zone growth | Determines tax ceiling, military recruitment pool |
| Materials | Refinery output | Trade routes | Advanced construction |

**Key design insight**: Spice becomes both a military resource AND an economic driver. High civilian population requires spice imports via trade routes or creates economic unrest. Players must balance military consumption of spice with civilian economic needs.

### 2.3 Combat-City Interaction

| Mechanic | Implementation |
|----------|----------------|
| **Defensive vulnerability** | Dense civilian areas present more target options for enemy raiders; civilian casualties cause population flight |
| **Strategic infrastructure** | Power grid attacks cause city-wide brownouts—civilian zones depopulate, military units lose recharge |
| **Sandworm attraction** | Active civilian zones in desert tiles have higher sandworm spawn probability (leverage existing `DisasterType::SandwormAttack`) |
| **Liberation mechanics** | Capturing enemy civilian structures intact yields immediate population + tax; destroying them removes enemy economic base |
| **Base defense integration** | Civilian zones can be walled off; guard towers serve dual purpose (military defense + police protection) |

### 2.4 City Services (New Structures)

| Service | Dune Structure | SimCity Effect |
|---------|---------------|----------------|
| Police | Security Office | Reduces crime rate, improves commercial land value |
| Fire | Fire Station | Prevents building decay from fires, required for high-density zones |
| Health | Med Center | Increases population growth rate |
| Education | Research Academy | Unlocks advanced military units (tech progression) |
| Entertainment | Spice Circus | Reduces unrest, attracts high-value commercial |
| Transport | Spice Silo Hub | Enables trade routes (new resource export) |

**Integration path**: Extend existing `StructureBase` hierarchy. Add new structure types that don't exist in vanilla Dune 2, reusing the existing construction yard system.

### 2.5 Map Overlays

Existing overlays in Dune Legacy: terrain, fog of war, spice concentration, building selection
City-builder overlays to add:
- Traffic density (leverage existing `TrafficSimulation`)
- Pollution (partially implemented in `CitySimulation`)
- Land value (partially implemented)
- Crime rate (partially implemented)
- Service coverage radius

---

## 3. Technical Approach

### 3.1 Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Game Loop                             │
│   (existing: Game.cpp ~176KB handles main game logic)      │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐  ┌──────────────────┐                 │
│  │ Dune Legacy RTS  │  │ DuneCity Sim     │                 │
│  │ - Units          │  │ - Zones          │                 │
│  │ - Structures     │  │ - Population     │                 │
│  │ - Combat         │  │ - Traffic        │                 │
│  │ - Spice          │  │ - Power Grid     │                 │
│  │ - Fog of War     │  │ - Disasters      │                 │
│  └────────┬─────────┘  └────────┬─────────┘                 │
│           │                      │                            │
│           └──────────┬───────────┘                            │
│                      ▼                                        │
│              Shared Tile Grid                                 │
│              (existing: Tile.h/cpp)                          │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Existing Codebase Strengths

**Dune Legacy provides:**
- SDL2-based rendering (no new dependencies needed)
- CMake build system with vcpkg integration
- Object-oriented C++ with clear hierarchy (`ObjectBase` → `StructureBase` / `UnitBase`)
- Tile-based map system with terrain types
- Network multiplayer infrastructure
- Existing save/load serialization

**DuneCity module already contains:**
- `CitySimulation.cpp/h` — main simulation coordinator
- `ZoneSimulation.cpp` — RCI zone growth logic
- `TrafficSimulation.cpp` — vehicle pathfinding
- `PowerGrid.cpp` — electrical network
- `CityBudget.cpp` — tax/economic model
- `CityEvaluation.cpp` — city ratings
- `CityScanner.cpp` — map analysis
- `CityConstants.h` — configuration constants

### 3.3 Build System (CMake)

Existing CMake is sound. Recommended structure:

```cmake
# src/dunecity/CMakeLists.txt
add_library(dunecity STATIC
    CitySimulation.cpp
    ZoneSimulation.cpp
    TrafficSimulation.cpp
    PowerGrid.cpp
    CityBudget.cpp
    CityEvaluation.cpp
    CityScanner.cpp
)

target_link_libraries(dunecity
    PUBLIC SDL2::SDL2
    PRIVATE fixmath
)

target_include_directories(dunecity
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
```

### 3.4 Rendering

**Keep SDL2** — it's proven, handles the 2D sprite-based aesthetic well, and adding OpenGL/GLFW introduces unnecessary complexity for this project. The existing `blitToScreen()` pattern in `ObjectBase` derivatives is sufficient.

If advanced effects are needed later (particle systems for explosions, shaders for heat haze), consider SDL2_gfx or a separate rendering layer—but not for initial scope.

### 3.5 Data Flow

```cpp
// In Game.cpp - main game loop
void Game::update() {
    // Existing: Update units, structures, combat
    updateUnits();
    updateStructures();
    updateBullets();
    updateMap();

    // New: Advance city simulation
    if (citySimulation_) {
        citySimulation_->advancePhase(currentGameCycle);
    }
}
```

The city simulation runs on a different cycle (configurable in `CityConstants.h`):
- Dune Legacy: ~30 ticks/second (game cycles)
- City simulation: configurable phase interval (currently `CITY_PHASE_INTERVAL = 1`)

---

## 4. Compelling Features

### 4.1 Faction-Specific City Building

Each house gets unique city bonuses:

| House | Military Bonus | City Bonus |
|-------|---------------|------------|
| **Atreides** | Infantry cost -20% | Windtrap efficiency +25% |
| **Harkonnen** | Tank damage +15% | Population tolerance to high density |
| **Ordos** | Unit speed +10% | Commercial zone tax +20% |

### 4.2 Dynamic Spice Economy

- **Spice fields** under civilian zones generate bonus tax (dangerous but profitable)
- **Harvester routes** use the road network—protect your harvesters or lose economic output
- **Spice silos** enable trade exports to other players

### 4.3 Environmental Hazards

Existing Dune disasters to integrate:
- **Sandstorms** — reduce visibility, damage unpowered structures
- **Sandworms** — consume units on desert tiles (zones in desert at risk)
- **Spice blooms** — temporary massive spice yield in contested areas

### 4.4 Progressive Unlocks

Population milestones unlock:
- 500: Basic military (soldiers, trikes)
- 2,000: Heavy military (tanks, artillery)
- 5,000: Air units (ornithopters)
- 10,000: Superweapons (death hand, missile tower)
- 25,000: Victory condition (city rating + military dominance)

### 4.5 Multiplayer City Wars

- Coalition building: ally with AI or human players
- Economic warfare: disrupt enemy trade routes
- Liberation mechanics: capture cities intact for population bonus
- Combined arms: military protects economic infrastructure

---

## 5. Potential Challenges

### 5.1 Pacing Tension

**Problem**: City builders are slow, RTS games are fast. City building rewards patience; combat rewards aggression.

**Solutions**:
- Separate "city mode" and "battle mode" or allow both simultaneously at different scales
- Make civilian population regenerate slowly but military units respond instantly
- Use the existing "pause" and "speed" controls to let players manage both
- Design missions where city growth enables military options (progression unlocks)

### 5.2 Complexity Explosion

**Problem**: Both games have significant depth. Merging could create analysis paralysis.

**Solutions**:
- Start with minimal viable city: just RCI zones + tax + power
- Add services incrementally (police → fire → health → education)
- Tutorial missions focused on each system in isolation
- AI assistants (Mentats) that provide city management hints

### 5.3 Map Scale

**Problem**: SimCity maps are large (tile counts in hundreds); Dune maps are smaller for combat visibility.

**Solutions**:
- Use Dune Legacy's existing variable map sizes
- Implement a "city view" zoom that shows more tiles
- Hybrid approach: small tactical map, city simulation abstracted at region level

### 5.4 Save/Load Compatibility

**Problem**: Adding city simulation state to save files.

**Solution**: Leverage existing `OutputStream`/`InputStream` serialization pattern. The `CitySimulation` class already has initialization from game state.

### 5.5 AI Opponents

**Problem**: Existing Dune AI focuses on combat and economy (spice harvesting). City-builder AI is different.

**Solutions**:
- Extend existing AI with city-building goals (prioritize zones near power)
- Create separate city-focused AI advisor that suggests RCI balance
- Use SimCity-style city templates for AI start conditions

---

## 6. Concrete Next Steps

### Phase 1: Foundation (Weeks 1-4)

1. **Audit existing DuneCity code**
   - Review `CitySimulation.cpp` (~400 lines) integration with `Game.cpp`
   - Verify zone placement UI exists
   - Test existing power grid and traffic simulation

2. **Add civilian population to existing structures**
   - Each residential structure gets population capacity
   - Population grows based on zone connectivity (existing `ZoneSimulation`)
   - Display population in UI (existing top bar)

3. **Implement tax collection**
   - Population generates credits per tick (existing `CityBudget`)
   - Credits go to faction treasury (existing `House::credits`)
   - UI shows income/expense breakdown

### Phase 2: City Services (Weeks 5-8)

4. **Add new structure types**
   - Security Office (police)
   - Fire Station
   - Med Center
   - Research Academy

5. **Implement service effects**
   - Police: reduce crime, increase commercial demand
   - Fire: prevent building decay
   - Health: population growth multiplier
   - Education: unlock advanced unit tiers

6. **City evaluation system**
   - Connect existing `CityEvaluation` to UI
   - Show mayor rating based on services

### Phase 3: Combat Integration (Weeks 9-12)

7. **Civilian vulnerability**
   - Combat damage to zones causes population flight
   - Destroyed civilian structures reduce tax income
   - Captured structures add population

8. **Power grid warfare**
   - Disabling power grid affects civilian zones first
   - Recovery priority system (military → services → zones)

9. **Sandworm integration**
   - Zones in desert have disaster risk
   - Warning system before attack

### Phase 4: Polish (Weeks 13-16)

10. **UI improvements**
    - City view overlay (traffic, pollution, land value)
    - Budget allocation panel
    - Population breakdown

11. **AI integration**
    - AI builds RCI zones
    - AI prioritizes services appropriately
    - AI responds to city crises

12. **Balancing**
    - Tax rates vs population growth
    - Military spending vs city development
    - Faction-specific bonuses

---

## 7. Recommended Codebase Changes

### 7.1 New Files

```
include/dunecity/
├── CityStructures.h      # New structure definitions
├── FactionCityBonus.h   # House-specific city bonuses
└── CitySaveData.h       # Serialization helpers

src/dunecity/
├── CityStructures.cpp
├── FactionCityBonus.cpp
└── CitySaveData.cpp
```

### 7.2 Modifications to Existing Files

| File | Change |
|------|--------|
| `Game.h/cpp` | Add `CitySimulation*` member, advance in update loop |
| `House.h/cpp` | Add `civilianPopulation`, `taxRate`, faction bonuses |
| `StructureBase.h/cpp` | Add `isCivilian` flag, `populationCapacity` |
| `Tile.h/cpp` | Add zone overlay rendering |
| `GUI/` | Add city management panels (budget, services) |

### 7.3 Configuration

Add to `dune_constants.h` or create new `city_difficulty.json`:

```json
{
  "difficulty": {
    "easy": {
      "disaster_chance": 480,
      "tax_rate_max": 20,
      "population_growth_rate": 1.0
    },
    "hard": {
      "disaster_chance": 60,
      "tax_rate_max": 25,
      "population_growth_rate": 0.5
    }
  }
}
```

---

## 8. Conclusion

The merger of SimCity and Dune Legacy is technically feasible and strategically compelling. The existing DuneCity module provides a strong foundation—most of the simulation framework already exists in source. The primary challenges are design (balancing RTS and city-builder pacing) rather than technical.

**Why this works**:
1. The Dune universe already has civilian elements (Harkonnen slaves, Fremen sietches) that map naturally to SimCity mechanics
2. Spice and credits provide dual resource loops that both games understand
3. The existing SDL2 rendering, CMake build, and object hierarchy are production-quality
4. The "city under siege" narrative is compelling—build fast, defend faster

**Why it might fail**:
1. Pacing mismatch between slow city growth and fast combat
2. Scope creep from two complex games
3. AI difficulty in managing both systems

**Recommendation**: Start with Phase 1 (foundation) and validate the core loop before adding city services. If civilian population + tax + power grid feels fun, proceed to Phase 2. If not, the existing Dune Legacy gameplay remains unchanged.

---

## Appendix: Key Files Reference

| File | Purpose | Status |
|------|---------|--------|
| `src/dunecity/CitySimulation.cpp` | Main simulation coordinator | Implemented |
| `src/dunecity/ZoneSimulation.cpp` | RCI zone growth | Implemented |
| `src/dunecity/TrafficSimulation.cpp` | Vehicle movement | Implemented |
| `src/dunecity/PowerGrid.cpp` | Electrical network | Implemented |
| `src/dunecity/CityBudget.cpp` | Tax/economy | Implemented |
| `include/dunecity/CityConstants.h` | Configuration | Implemented |
| `src/Game.cpp` | Main loop | Modified for city integration |
| `include/Tile.h` | Map tiles | Has city zone fields |
