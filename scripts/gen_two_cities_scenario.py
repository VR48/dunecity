#!/usr/bin/env python3
"""
Generate a 256x256 two-player scenario: two large pre-built city-bases
sit in opposite corners with spice and sand between them. Either slot
can be the human — house and AI/human assignment happen in the lobby
(no [house] sections, no Brain= lines; we follow the singleplayer
2-player convention from `Twin Fists.ini` and `North vs. South.ini`).

City layouts are SimCity-Classic-inspired: a Construction Yard + power
spine on the north edge, a road grid in the centre with residential/
commercial/industrial zones, Police Stations for crime control, and a
defensive turret line on the perimeter. Both cities get identical
layouts so the matchup is symmetric.

Output: data/maps/singleplayer/2P - 256x256 - Twin Cities.ini
"""

import os
from pathlib import Path

# ---------- Map dimensions ----------
SIZE = 256

# ---------- Terrain characters (must match INIMapLoader.cpp:260-331) ----------
SAND = '-'
DUNES = '^'
ROCK = '%'
MOUNTAIN = '@'
SPICE = '~'
THICK_SPICE = '+'
SPICE_BLOOM = 'O'
SPECIAL_BLOOM = 'Q'

# ---------- City anchor positions ----------
# Top-left city (Player1): rock plateau spanning roughly (5..72, 5..72).
TL_X, TL_Y = 5, 5
TL_W, TL_H = 68, 68

# Bottom-right city (Player2): symmetric mirror in opposite corner.
BR_X, BR_Y = SIZE - TL_W - TL_X, SIZE - TL_H - TL_Y  # (183, 183) span 68x68
BR_W, BR_H = TL_W, TL_H


def pos(x, y):
    """Linear position used by [STRUCTURES] / [UNITS]: pos = y * SIZE + x."""
    return y * SIZE + x


# ============================================================================
# Terrain layout
# ============================================================================

def build_terrain():
    """Return a 256x256 grid of terrain characters."""
    grid = [[SAND] * SIZE for _ in range(SIZE)]

    # Big rock plateaus for the two cities.
    for y in range(TL_Y, TL_Y + TL_H):
        for x in range(TL_X, TL_X + TL_W):
            grid[y][x] = ROCK
    for y in range(BR_Y, BR_Y + BR_H):
        for x in range(BR_X, BR_X + BR_W):
            grid[y][x] = ROCK

    # Scatter mid-map rock patches so each side has somewhere to expand
    # without having to fight directly to the other city wall. The
    # symmetric pair of patches around the diagonal keeps the matchup
    # fair regardless of which side the player picks.
    rock_patches = [
        (90,  60, 8, 6), (160, 70, 7, 5), (60, 160, 6, 7),
        (170, 165, 8, 7), (95, 110, 5, 5), (155, 110, 5, 5),
        (180, 100, 6, 4), (75, 175, 6, 4),
        (120, 120, 8, 8),  # centre rock for mid-map skirmishes
    ]
    for px, py, pw, ph in rock_patches:
        for y in range(py, py + ph):
            for x in range(px, px + pw):
                if 0 <= x < SIZE and 0 <= y < SIZE:
                    grid[y][x] = ROCK

    # Mountain barriers near each city — gives some natural choke
    # without sealing the cities off.
    mountains = [
        (74, 30, 2, 12), (74, 50, 2, 8),     # right edge of top-left city
        (30, 74, 12, 2), (50, 74, 8, 2),     # bottom edge of top-left city
        (180, 198, 2, 12), (180, 215, 2, 8), # left edge of bottom-right
        (198, 180, 12, 2), (215, 180, 8, 2), # top edge of bottom-right
    ]
    for mx, my, mw, mh in mountains:
        for y in range(my, my + mh):
            for x in range(mx, mx + mw):
                if 0 <= x < SIZE and 0 <= y < SIZE:
                    grid[y][x] = MOUNTAIN

    # Spice fields — heavy in the central no-man's-land, plus a strip
    # along each city's outer flank so harvesters have something to chew.
    def spice_blob(cx, cy, radius, thick=False):
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                if 0 <= x < SIZE and 0 <= y < SIZE:
                    d = abs(x - cx) + abs(y - cy)  # Manhattan disc
                    if d <= radius and grid[y][x] == SAND:
                        if thick and d <= radius // 2:
                            grid[y][x] = THICK_SPICE
                        else:
                            grid[y][x] = SPICE

    spice_blobs = [
        (128, 128, 10, True),    # central megafield (contested)
        (100, 100, 6, True),     # NW-of-centre
        (156, 156, 6, True),     # SE-of-centre
        (90, 200, 8, False),     # SW flank
        (200, 90, 8, False),     # NE flank
        (160, 40, 5, False),     # near top-right
        (40, 160, 5, False),     # near bottom-left
        (220, 130, 6, True),     # bottom-right city's outer harvest
        (40, 130, 6, True),      # top-left city's outer harvest
    ]
    for cx, cy, r, thick in spice_blobs:
        spice_blob(cx, cy, r, thick)

    # Two spice blooms in unclaimed sand for early-game windfall.
    grid[60][195] = SPICE_BLOOM
    grid[195][60] = SPICE_BLOOM

    # Sand dunes in the deep desert — variety, no gameplay effect.
    dune_patches = [(40, 110, 8, 4), (210, 145, 8, 4),
                    (115, 200, 6, 5), (140, 55, 6, 5)]
    for dx, dy, dw, dh in dune_patches:
        for y in range(dy, dy + dh):
            for x in range(dx, dx + dw):
                if 0 <= x < SIZE and 0 <= y < SIZE and grid[y][x] == SAND:
                    grid[y][x] = DUNES

    return grid


# ============================================================================
# City layout — generate structures for one corner, then mirror for the other.
# ============================================================================

# A "big SC-style city" template, expressed as (relative_x, relative_y, name,
# footprint_w, footprint_h). Positions are offsets from the corner anchor.
# The template fits inside a 60x60 region with room for roads in between.
#
# Layout zones (within the 60x60 plateau):
#   - Power & industry along the north (y < 15)
#   - Construction yard & defensive backbone NW (x < 15)
#   - Residential grid in the middle (15 < x,y < 45)
#   - Commercial cluster along the south (y > 45, x < 30)
#   - Industrial cluster along the east (x > 45, y < 30)
#   - Police stations scattered through the residential blocks
#   - Turret line on the outer rim

CITY_TEMPLATE = [
    # --- Construction & service spine (NW corner) ---
    ("Const Yard",          2,  2, 2, 2),   # CY footprint 2x2
    ("Windtrap",            5,  2, 2, 2),
    ("Windtrap",            8,  2, 2, 2),
    ("Windtrap",           11,  2, 2, 2),
    ("Windtrap",           14,  2, 2, 2),
    ("Windtrap",            2,  5, 2, 2),
    ("Windtrap",            5,  5, 2, 2),
    ("Refinery",            8,  5, 3, 2),
    ("Spice Silo",         12,  5, 2, 2),
    ("Spice Silo",         15,  5, 2, 2),
    ("Spice Silo",         18,  5, 2, 2),
    ("Repair Yard",        21,  5, 3, 2),

    # --- Industrial belt along north edge (heavy industry, polluting) ---
    ("Heavy Factory",      30,  2, 3, 2),
    ("Light Factory",      35,  2, 2, 2),
    ("Hightech Factory",   39,  2, 3, 2),
    ("House IX",           45,  2, 2, 2),

    # --- City-mode power: nuclear plant in the corner ---
    ("Nuclear Plant",      52,  2, 3, 3),

    # --- Radar / Outpost ---
    ("Outpost",            25,  9, 2, 2),

    # --- Residential grid (zones) — 4 rows x 5 cols, with road grid ---
    # Each zone is 2x2; we leave a 1-tile gap between rows for roads.
    ("Residential Zone",   15, 15, 2, 2),
    ("Residential Zone",   18, 15, 2, 2),
    ("Residential Zone",   21, 15, 2, 2),
    ("Residential Zone",   24, 15, 2, 2),
    ("Residential Zone",   27, 15, 2, 2),
    ("Residential Zone",   15, 18, 2, 2),
    ("Residential Zone",   18, 18, 2, 2),
    ("Residential Zone",   21, 18, 2, 2),
    ("Residential Zone",   24, 18, 2, 2),
    ("Residential Zone",   27, 18, 2, 2),
    ("Residential Zone",   15, 21, 2, 2),
    ("Residential Zone",   18, 21, 2, 2),
    ("Residential Zone",   21, 21, 2, 2),
    ("Residential Zone",   24, 21, 2, 2),
    ("Residential Zone",   27, 21, 2, 2),
    ("Residential Zone",   15, 24, 2, 2),
    ("Residential Zone",   18, 24, 2, 2),
    ("Residential Zone",   21, 24, 2, 2),
    ("Residential Zone",   24, 24, 2, 2),
    ("Residential Zone",   27, 24, 2, 2),

    # --- Police stations interleaved with residential (crime control) ---
    ("Police Station",     31, 18, 2, 2),
    ("Police Station",     31, 24, 2, 2),
    ("Police Station",     12, 18, 2, 2),
    ("Police Station",     12, 24, 2, 2),

    # --- Commercial cluster south of residential ---
    ("Commercial Zone",    15, 30, 2, 2),
    ("Commercial Zone",    18, 30, 2, 2),
    ("Commercial Zone",    21, 30, 2, 2),
    ("Commercial Zone",    24, 30, 2, 2),
    ("Commercial Zone",    27, 30, 2, 2),
    ("Commercial Zone",    15, 33, 2, 2),
    ("Commercial Zone",    18, 33, 2, 2),
    ("Commercial Zone",    21, 33, 2, 2),
    ("Commercial Zone",    24, 33, 2, 2),
    ("Commercial Zone",    27, 33, 2, 2),

    # --- Industrial cluster east (downwind of res, with pollution buffer) ---
    ("Industrial Zone",    40, 12, 2, 2),
    ("Industrial Zone",    43, 12, 2, 2),
    ("Industrial Zone",    46, 12, 2, 2),
    ("Industrial Zone",    40, 15, 2, 2),
    ("Industrial Zone",    43, 15, 2, 2),
    ("Industrial Zone",    46, 15, 2, 2),

    # --- More Residential southern fringe ---
    ("Residential Zone",   15, 37, 2, 2),
    ("Residential Zone",   18, 37, 2, 2),
    ("Residential Zone",   21, 37, 2, 2),
    ("Residential Zone",   24, 37, 2, 2),
    ("Residential Zone",   27, 37, 2, 2),
    ("Residential Zone",   15, 40, 2, 2),
    ("Residential Zone",   18, 40, 2, 2),
    ("Residential Zone",   21, 40, 2, 2),

    # --- Defensive perimeter: rocket turrets at the outer corners ---
    ("Rocket-Turret",       1, 50, 1, 1),
    ("Rocket-Turret",      30, 50, 1, 1),
    ("Rocket-Turret",      50,  1, 1, 1),
    ("Rocket-Turret",      50, 30, 1, 1),
    ("Rocket-Turret",      50, 50, 1, 1),

    # --- Gun turret line ---
    ("Gun-Turret",          1, 30, 1, 1),
    ("Gun-Turret",          1, 40, 1, 1),
    ("Gun-Turret",         15, 50, 1, 1),
    ("Gun-Turret",         40, 50, 1, 1),
    ("Gun-Turret",         50, 15, 1, 1),
    ("Gun-Turret",         50, 40, 1, 1),

    # --- Walls between turrets ---
    ("Wall",                5, 50, 1, 1),
    ("Wall",               10, 50, 1, 1),
    ("Wall",               20, 50, 1, 1),
    ("Wall",               25, 50, 1, 1),
    ("Wall",               35, 50, 1, 1),
    ("Wall",               45, 50, 1, 1),
    ("Wall",               50,  5, 1, 1),
    ("Wall",               50, 10, 1, 1),
    ("Wall",               50, 20, 1, 1),
    ("Wall",               50, 25, 1, 1),
    ("Wall",               50, 35, 1, 1),
    ("Wall",               50, 45, 1, 1),
]

# Roads (GEN entries) — connect the residential grid to industrial /
# commercial / construction yard. Each entry is a relative (rx, ry).
def gen_city_roads():
    roads = set()
    # Horizontal road across y=17 (between residential rows)
    for x in range(5, 35):
        roads.add((x, 17))
    # Horizontal road across y=23
    for x in range(5, 35):
        roads.add((x, 23))
    # Horizontal road across y=29 (separating res and commercial)
    for x in range(5, 35):
        roads.add((x, 29))
    # Vertical road at x=14 (west boundary of residential)
    for y in range(8, 45):
        roads.add((14, y))
    # Vertical road at x=30 (east boundary of residential, into industrial)
    for y in range(8, 45):
        roads.add((30, y))
    # Vertical road at x=38 (into industrial)
    for y in range(8, 20):
        roads.add((38, y))
    # Spine from construction yard down to residential
    for y in range(4, 14):
        roads.add((4, y))
    # Connect to power plant
    for x in range(4, 52):
        if 2 <= x <= 4 or 31 <= x <= 37 or 48 <= x <= 51:
            continue  # don't overwrite structure footprints
        roads.add((x, 7))
    return roads


def emit_city(structures, gens, owner, ox, oy, next_id, next_gen_id,
              wall_size_w, wall_size_h):
    """Append a full city's structures and roads. Returns (next_id, next_gen_id)."""
    for name, rx, ry, _w, _h in CITY_TEMPLATE:
        wx = ox + rx
        wy = oy + ry
        # Sanity check: keep within the plateau (with 2-tile margin).
        if wx < ox or wy < oy or wx >= ox + wall_size_w or wy >= oy + wall_size_h:
            continue
        line = f"ID{next_id:03d}={owner},{name},256,{pos(wx, wy)}"
        structures.append(line)
        next_id += 1

    for rx, ry in sorted(gen_city_roads()):
        wx = ox + rx
        wy = oy + ry
        if wx < ox or wy < oy or wx >= ox + wall_size_w or wy >= oy + wall_size_h:
            continue
        gens.append(f"GEN{next_gen_id:03d}={owner},Road")
        # GEN actually needs a position-based key — see how Four Cities does it.
        next_gen_id += 1

    return next_id, next_gen_id


# ============================================================================
# Build the INI string
# ============================================================================

def emit_player_structures(structures, owner, ox, oy, plateau_w, plateau_h,
                           start_id):
    """Place a full city template. Returns next available ID."""
    nid = start_id
    for name, rx, ry, _w, _h in CITY_TEMPLATE:
        wx = ox + rx
        wy = oy + ry
        if wx >= ox + plateau_w - 1 or wy >= oy + plateau_h - 1:
            continue
        structures.append(f"ID{nid:03d}={owner},{name},256,{pos(wx, wy)}")
        nid += 1
    return nid


def emit_city_roads(gens, owner, ox, oy, plateau_w, plateau_h):
    """A simple grid of roads connecting the city's quadrants.

    Roads are emitted as `GEN<position>=<owner>,Road` (the suffix after
    'GEN' is the world-tile linear position, not a sequential counter —
    see how Wall entries are written in Four Cities.ini).
    """
    # Horizontal arteries through the residential / commercial belt.
    h_rows = [13, 17, 23, 29, 36]
    for ry in h_rows:
        for rx in range(5, 50):
            wx, wy = ox + rx, oy + ry
            if wx >= ox + plateau_w - 1 or wy >= oy + plateau_h - 1:
                continue
            gens.append(f"GEN{pos(wx, wy)}={owner},Road")
    # Vertical spines.
    v_cols = [14, 17, 21, 25, 30, 38]
    for rx in v_cols:
        for ry in range(8, 45):
            wx, wy = ox + rx, oy + ry
            if wx >= ox + plateau_w - 1 or wy >= oy + plateau_h - 1:
                continue
            gens.append(f"GEN{pos(wx, wy)}={owner},Road")


def emit_starting_army(units, owner, anchor_x, anchor_y, start_id):
    """Harvesters + a small starting army for one city. Same loadout
    for both sides so neither player gets a unit-comp advantage based
    on which slot they picked."""
    uid = start_id
    # 3 harvesters near the refinery (refinery is at relative (8, 5)).
    for dx in (0, 2, 4):
        units.append(f"ID{uid:03d}={owner},Harvester,256,{pos(anchor_x + 9 + dx, anchor_y + 9)},64,Harvest")
        uid += 1
    # A defensive force inside the perimeter.
    for dx, dy, unit in [
        (35, 35, "Tank"), (37, 35, "Tank"), (39, 35, "Siege Tank"),
        (35, 37, "Quad"), (37, 37, "Quad"), (39, 37, "Trike"),
        (35, 40, "Launcher"), (37, 40, "Launcher"),
        (42, 35, "Devastator"),
    ]:
        units.append(f"ID{uid:03d}={owner},{unit},256,{pos(anchor_x + dx, anchor_y + dy)},64,Area Guard")
        uid += 1
    # A carryall and a thopter for early air pressure.
    units.append(f"ID{uid:03d}={owner},Carryall,256,{pos(anchor_x + 20, anchor_y + 35)},64,Area Guard")
    uid += 1
    units.append(f"ID{uid:03d}={owner},'Thopter,256,{pos(anchor_x + 22, anchor_y + 35)},64,Area Guard")
    uid += 1
    return uid


def main():
    grid = build_terrain()

    structures = []
    units = []
    gens = []

    # --- Player1: top-left corner ---
    next_id = emit_player_structures(structures, "Player1",
                                     TL_X, TL_Y, TL_W, TL_H,
                                     start_id=100)
    emit_city_roads(gens, "Player1", TL_X, TL_Y, TL_W, TL_H)

    # --- Player2: bottom-right corner (mirror) ---
    next_id = emit_player_structures(structures, "Player2",
                                     BR_X, BR_Y, BR_W, BR_H,
                                     start_id=next_id)
    emit_city_roads(gens, "Player2", BR_X, BR_Y, BR_W, BR_H)

    # --- Starting armies (identical for both sides) ---
    next_unit_id = emit_starting_army(units, "Player1", TL_X, TL_Y, start_id=10)
    next_unit_id = emit_starting_army(units, "Player2", BR_X, BR_Y,
                                      start_id=next_unit_id)

    # --- Compose INI ---
    lines = []
    lines.append("; Twin Cities — 256x256 two-player scenario.")
    lines.append(";")
    lines.append("; Two pre-built SC-style city-bases face off across an open")
    lines.append("; spice desert. Both sides ship the same loadout: Construction")
    lines.append("; Yard, refinery, nuclear power, residential/commercial/")
    lines.append("; industrial zones, police stations, defensive turret line,")
    lines.append("; harvesters and a starting army. The matchup is fully")
    lines.append("; symmetric — pick either slot in the lobby. House and human/")
    lines.append("; AI assignment are decided at lobby time (no [house] sections,")
    lines.append("; no Brain= lines, following Twin Fists / North vs South).")
    lines.append("; Generated by scripts/gen_two_cities_scenario.py.")
    lines.append("")
    lines.append("[BASIC]")
    lines.append("Version=2")
    lines.append("License=CC-BY-SA")
    lines.append('Author="DuneCity"')
    lines.append("TechLevel=8")
    lines.append("WinFlags=3")
    lines.append("LoseFlags=1")
    lines.append("LosePicture=LOSTVEHC.WSA")
    lines.append("WinPicture=WIN2.WSA")
    lines.append("BriefPicture=SARDUKAR.WSA")
    lines.append("TimeOut=0")
    lines.append("")
    lines.append("[MAP]")
    lines.append(f"SizeX={SIZE}")
    lines.append(f"SizeY={SIZE}")
    for y in range(SIZE):
        lines.append(f"{y:03d}={''.join(grid[y])}")
    lines.append("")
    lines.append("[CHOAM]")
    lines.append("Carryall=2")
    lines.append("Harvester=4")
    lines.append("Launcher=5")
    lines.append("MCV=2")
    lines.append("'Thopter=5")
    lines.append("Quad=5")
    lines.append("Siege Tank=6")
    lines.append("Tank=6")
    lines.append("Trike=5")
    lines.append("")
    lines.append("[STRUCTURES]")
    lines.extend(structures)
    # GEN entries (roads/walls/concrete) live in [STRUCTURES] alongside
    # ID entries — they're not a separate section.
    lines.extend(gens)
    lines.append("")
    lines.append("[UNITS]")
    lines.extend(units)
    lines.append("")
    # Two neutral player slots. The lobby picks which one is human and
    # which house each plays — leaving Brain unset lets the lobby
    # default to AI for the unpicked slot.
    lines.append("[Player1]")
    lines.append("Quota=0")
    lines.append("Credits=20000")
    lines.append("")
    lines.append("[Player2]")
    lines.append("Quota=0")
    lines.append("Credits=20000")
    lines.append("")

    out_dir = Path(__file__).resolve().parent.parent / "data" / "maps" / "singleplayer"
    out_path = out_dir / "2P - 256x256 - Twin Cities.ini"
    out_path.write_text("\n".join(lines))
    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")
    print(f"Structures: {len(structures)}, Units: {len(units)}")


if __name__ == "__main__":
    main()
