#!/usr/bin/env python3
"""
Pack two SimCity-style cities onto the user-created
"2P - 192x192 - SimCity.ini" map. Each city sits on one of the two
largest connected rock pieces and aims to cover ~60% of that rock with
structures + roads.

Strategy per rock piece:
- 4-connected flood fill to identify the rock component.
- Reserve a NW "Dune backbone" strip (ConstYard, Windtraps, Refinery,
  Silos, Repair Yard, factories, Nuclear Plant, Outpost) at fixed
  relative offsets within the bbox, snapping to rock-only cells.
- Cover the rest of the rock with a 3x3 lot grid:
    - top-left 2x2 of each lot = R/C/I zone or Police Station
    - rightmost column + bottom row of each lot = roads
  This gives ~5/9 ≈ 55% nominal coverage; we tune to hit ~60% by
  pushing extra road tiles into still-empty rock cells around the
  built area until we cross the threshold.
- Defensive turrets are sprinkled around the city's outer rim.

Edits the file in place. Idempotent: previous [STRUCTURES] blocks and
auto-generated UNITS for Player1/Player2 are removed before re-adding.
"""

from pathlib import Path
import re

PATH = Path(
    "/Users/stefanclaw/Library/Application Support/Dune City/maps/singleplayer/2P - 192x192 - SimCity.ini"
)

MAP_SIZE = 192

ROCK = '%'
SAND = '-'

# Coverage target: built tiles / rock tiles >= 0.60
TARGET_COVERAGE = 0.60

# Per-structure footprints
FOOTPRINT = {
    "Const Yard":         (2, 2),
    "Windtrap":           (2, 2),
    "Refinery":           (3, 2),
    "Spice Silo":         (2, 2),
    "Repair Yard":        (3, 2),
    "Heavy Factory":      (3, 2),
    "Light Factory":      (2, 2),
    "Hightech Factory":   (3, 2),
    "House IX":           (2, 2),
    "Nuclear Plant":      (3, 3),
    "Outpost":            (2, 2),
    "Police Station":     (2, 2),
    "Residential Zone":   (2, 2),
    "Commercial Zone":    (2, 2),
    "Industrial Zone":    (2, 2),
    "Rocket-Turret":      (1, 1),
    "Gun-Turret":         (1, 1),
    "Wall":               (1, 1),
}

# Dune backbone — placed first inside each city. Offsets are within the
# rock component's bounding box, snapped to rock during placement.
#
# Power budget: a fully-grown city of ~500 zones can demand 8,000+ power.
# Starting infrastructure must comfortably cover early growth (~2,500
# power at density 1) so zones never decline before QuantBot's city-mode
# power-buffer rule kicks in. 4 Nuclear Plants (+4,000) and 6 Windtraps
# (+600) totals 4,600 — enough to push the city to mid-density density
# without intervention, after which QuantBot adds more Nuclear Plants as
# the buffer drops.
BACKBONE = [
    ("Const Yard",        2,  2),
    ("Windtrap",          5,  2),
    ("Windtrap",          8,  2),
    ("Windtrap",         11,  2),
    ("Windtrap",         14,  2),
    ("Windtrap",         17,  2),
    ("Windtrap",         20,  2),
    ("Refinery",          2,  5),
    ("Spice Silo",        6,  5),
    ("Spice Silo",        9,  5),
    ("Spice Silo",       12,  5),
    ("Repair Yard",      15,  5),
    ("Heavy Factory",    19,  5),
    ("Light Factory",    23,  5),
    ("Hightech Factory", 26,  5),
    ("House IX",         30,  5),
    ("Outpost",          33,  5),
    # Nuclear Plants are 3x3 footprints — placing four of them gives a
    # ~4,000-power baseline that survives most of the city's growth.
    ("Nuclear Plant",    37,  5),
    ("Nuclear Plant",    41,  5),
    ("Nuclear Plant",    45,  5),
    ("Nuclear Plant",    49,  5),
]


# ----------------------------------------------------------------------
# Parsing / writing the INI
# ----------------------------------------------------------------------

def parse_map(text):
    m = re.search(r'\[MAP\](.*?)(?=\n\[)', text, re.S)
    rows = []
    for line in m.group(1).splitlines():
        if re.match(r'^\d{3}=', line):
            rows.append(list(line.split('=', 1)[1]))
    return rows, m.span()


def find_rock_components(rows):
    H, W = len(rows), len(rows[0])
    visited = [[False] * W for _ in range(H)]
    comps = []
    for y in range(H):
        for x in range(W):
            if rows[y][x] == ROCK and not visited[y][x]:
                stack = [(x, y)]
                cells = []
                visited[y][x] = True
                while stack:
                    cx, cy = stack.pop()
                    cells.append((cx, cy))
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < W and 0 <= ny < H \
                                and not visited[ny][nx] \
                                and rows[ny][nx] == ROCK:
                            visited[ny][nx] = True
                            stack.append((nx, ny))
                comps.append(cells)
    comps.sort(key=len, reverse=True)
    return comps


# ----------------------------------------------------------------------
# City packing
# ----------------------------------------------------------------------

def can_place(rockset, used, wx, wy, w, h):
    for dy in range(h):
        for dx in range(w):
            cell = (wx + dx, wy + dy)
            if cell not in rockset or cell in used:
                return False
    return True


def mark_used(used, wx, wy, w, h):
    for dy in range(h):
        for dx in range(w):
            used.add((wx + dx, wy + dy))


def pack_city(rock_cells, structure_emitter, road_emitter):
    """Pack a city onto `rock_cells` (set of (x,y)). Returns
    (built_count, rock_count) for coverage reporting."""

    rockset = set(rock_cells)
    used = set()  # tiles consumed by structures (not roads)

    xs = [p[0] for p in rock_cells]
    ys = [p[1] for p in rock_cells]
    bx, by = min(xs), min(ys)
    mx, my = max(xs), max(ys)

    # Backbone anchor: find the rock cell nearest the bbox NW corner
    # that has a wide stretch of contiguous rock to its east (room for
    # the 50-tile-wide backbone strip). The bbox NW corner itself is
    # often sand — bbox is defined by the rock cells' min/max but the
    # rock shape can be far from the corner. Without this, the backbone
    # slide-search falls off the rock and structures fail to place.
    def score_anchor(ax, ay):
        # Count how many of the next 50 cells eastward at the anchor
        # row are rock — higher = more backbone fits.
        return sum(1 for dx in range(50)
                   if (ax + dx, ay) in rockset
                   and (ax + dx, ay + 1) in rockset)

    backbone_anchor = None
    best_score = -1
    for ay in range(by, by + 40):
        for ax in range(bx, bx + 80):
            if (ax, ay) not in rockset:
                continue
            s = score_anchor(ax, ay)
            if s > best_score:
                best_score = s
                backbone_anchor = (ax, ay)
        if best_score >= 45:
            break  # good enough — stop on the first row with a full backbone
    if backbone_anchor is None:
        backbone_anchor = (bx, by)
    abx, aby = backbone_anchor

    # --- 1) Dune backbone, anchored at the densest NW rock row --------
    placed_backbone = []
    for name, rx, ry in BACKBONE:
        w, h = FOOTPRINT[name]
        target_x = abx + rx
        target_y = aby + ry
        # Expanding-square BFS: try the target first, then concentric
        # square rings outward. Caps at search_radius 16 so we don't
        # wander all the way across the city.
        placed = False
        for radius in range(0, 17):
            if placed: break
            for dy in range(-radius, radius + 1):
                if placed: break
                for dx in range(-radius, radius + 1):
                    # Only the ring boundary, not the interior (already searched at smaller radius)
                    if radius > 0 and abs(dx) != radius and abs(dy) != radius:
                        continue
                    wx = target_x + dx
                    wy = target_y + dy
                    if can_place(rockset, used, wx, wy, w, h):
                        structure_emitter(name, wx, wy)
                        mark_used(used, wx, wy, w, h)
                        placed_backbone.append((name, wx, wy))
                        placed = True
                        break

    # --- 2) Tile the remainder with 3x3 lots --------------------------
    # Lot origin grid: (bx + 1 + 3i, by + 1 + 3j). Top-left 2x2 = zone,
    # right column + bottom row = roads.
    #
    # Zone type comes from the lot's distance to the bbox edges, not a
    # round-robin cycle, so pollution-emitting industrial winds up in
    # ONE corner (the south-east of each city's bbox) instead of mixed
    # in with residential. SC's land-value formula penalises pollution
    # adjacency: with the old interleaved cycle, every res zone was one
    # tile from an industrial source and capped at tier-1 forever.
    #
    # Residential fills the middle and north (residents stay near the
    # safe Dune backbone). Commercial wraps the residential core to the
    # west/south. Industrial sits in the south-east corner, downwind of
    # the residential heart, with its pollution radiating into mostly
    # empty rock instead of into res zones.
    police_quota = 8
    placed_police = 0
    road_cells = set()

    total = len(rockset)
    target = int(total * TARGET_COVERAGE) + 1

    bbox_w = mx - bx + 1
    bbox_h = my - by + 1

    # Industrial cluster goes on the OUTWARD-facing edge of the city —
    # the side furthest from the map centre. Pollution then radiates
    # into open desert instead of into the residential core. Commercial
    # sits on the inward-facing side (toward the map centre / the trade
    # route to the other player).
    #
    # We do percentile-based assignment along the outward axis so the
    # ratios stay 25% industrial / 20% commercial / 55% residential
    # regardless of the rock's actual shape. This guarantees a real
    # residential majority — the failure mode of fixed-distance
    # thresholds was a 12-tile-wide residential strip lost between two
    # huge C/I bands.
    rcx = sum(p[0] for p in rock_cells) // len(rock_cells)
    rcy = sum(p[1] for p in rock_cells) // len(rock_cells)
    map_cx = MAP_SIZE // 2
    map_cy = MAP_SIZE // 2

    ox = rcx - map_cx
    oy = rcy - map_cy
    use_x_axis = abs(ox) >= abs(oy)
    out_sign = (1 if (ox if use_x_axis else oy) > 0 else -1)

    def projection(wx, wy):
        return ((wx - rcx) if use_x_axis else (wy - rcy)) * out_sign

    # Build the candidate lot grid once, filter to lots that fit in
    # rock, and partition by projection percentile.
    candidate_lots = []
    for j in range((my - by) // 3 + 2):
        for i in range((mx - bx) // 3 + 2):
            wx = bx + 1 + i * 3
            wy = by + 4 + j * 3
            if can_place(rockset, used, wx, wy, 2, 2):
                candidate_lots.append((wx, wy))
    # Sort by outward projection (lowest = innermost commercial side,
    # highest = outermost industrial side).
    candidate_lots.sort(key=lambda p: projection(p[0], p[1]))
    n_lots = len(candidate_lots)
    n_commercial = int(n_lots * 0.20)
    n_industrial = int(n_lots * 0.25)
    n_residential = n_lots - n_commercial - n_industrial
    lot_type_map = {}
    for idx, lot in enumerate(candidate_lots):
        if idx < n_commercial:
            lot_type_map[lot] = "Commercial Zone"
        elif idx < n_commercial + n_residential:
            lot_type_map[lot] = "Residential Zone"
        else:
            lot_type_map[lot] = "Industrial Zone"

    def zone_type_for(wx, wy):
        return lot_type_map.get((wx, wy), "Residential Zone")

    # Police-station targets: 8 fixed positions evenly spread relative to
    # the rock centroid so every residential cluster falls inside a
    # coverage radius (kPoliceRadius = 6 blocks ~= 12 world tiles).
    # Use rock-centroid offsets so the targets land on rock for irregular
    # shapes too.
    police_targets = []
    radius_x = bbox_w // 3
    radius_y = bbox_h // 3
    for dy_frac in (-0.6, -0.2, 0.2, 0.6):
        for dx_frac in (-0.4, 0.4):
            tx = rcx + int(radius_x * dx_frac)
            ty = rcy + int(radius_y * dy_frac)
            police_targets.append((tx, ty))

    # Visit lots in row-major scan order — gives the regular grid look
    # SimCity is known for. We stop once coverage hits the target.
    lot_positions = []
    for j in range((my - by) // 3 + 2):
        for i in range((mx - bx) // 3 + 2):
            wx = bx + 1 + i * 3
            wy = by + 4 + j * 3
            lot_positions.append((wx, wy))

    placed_lots = []
    for wx, wy in lot_positions:
        if len(used) + len(road_cells) >= target:
            break
        if not can_place(rockset, used, wx, wy, 2, 2):
            continue
        # If this lot is the closest yet-unfilled lot to a pending
        # police target, plant a police station here.
        is_police = False
        if placed_police < police_quota and police_targets:
            # Find nearest target to this lot
            best_idx, best_d = -1, 99999
            for idx, (tx, ty) in enumerate(police_targets):
                d = abs(tx - wx) + abs(ty - wy)
                if d < best_d:
                    best_d = d
                    best_idx = idx
            if best_d <= 2:
                police_targets.pop(best_idx)
                is_police = True

        name = "Police Station" if is_police else zone_type_for(wx, wy)
        if is_police:
            placed_police += 1
        structure_emitter(name, wx, wy)
        mark_used(used, wx, wy, 2, 2)
        placed_lots.append((wx, wy))
        # Lay this lot's road border immediately so coverage tally is
        # accurate for the next loop iteration's stop check.
        for ry in (wy, wy + 1):
            cell = (wx + 2, ry)
            if cell in rockset and cell not in used and cell not in road_cells:
                road_cells.add(cell)
        for rx in (wx, wx + 1, wx + 2):
            cell = (rx, wy + 2)
            if cell in rockset and cell not in used and cell not in road_cells:
                road_cells.add(cell)

    # Place any remaining police stations at their target spots even
    # if the lot grid didn't naturally route a placement through them.
    for tx, ty in police_targets:
        if placed_police >= police_quota:
            break
        # Snap to a nearby empty 2x2 rock cell.
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                wx, wy = tx + dx, ty + dy
                if can_place(rockset, used, wx, wy, 2, 2):
                    structure_emitter("Police Station", wx, wy)
                    mark_used(used, wx, wy, 2, 2)
                    placed_police += 1
                    break
            else:
                continue
            break

    for rx, ry in sorted(road_cells):
        road_emitter(rx, ry)

    # --- 5) Defensive turret rim --------------------------------------
    # Walk the rock-component boundary and drop a turret every so often.
    boundary = []
    for (cx, cy) in rock_cells:
        # Boundary if any 4-neighbour is off-rock
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (cx + dx, cy + dy) not in rockset:
                boundary.append((cx, cy))
                break
    boundary.sort(key=lambda p: (p[1], p[0]))
    spacing = max(8, len(boundary) // 24)
    turret_count = 0
    for i, (cx, cy) in enumerate(boundary):
        if i % spacing != 0:
            continue
        if (cx, cy) in used or (cx, cy) in road_cells:
            continue
        # Alternate rocket and gun turrets
        name = "Rocket-Turret" if turret_count % 3 == 0 else "Gun-Turret"
        structure_emitter(name, cx, cy)
        used.add((cx, cy))
        turret_count += 1

    return len(used) + len(road_cells), len(rockset)


# ----------------------------------------------------------------------
# Top-level: edit the INI in place
# ----------------------------------------------------------------------

def main():
    SIZE = 192

    def pos(x, y):
        return y * SIZE + x

    text = PATH.read_text()
    rows, _ = parse_map(text)
    comps = find_rock_components(rows)
    big = [c for c in comps if len(c) > 1000]
    assert len(big) >= 2, f"Expected two large rock components, got sizes {[len(c) for c in comps]}"
    comp_p1, comp_p2 = big[0], big[1]

    structures = []
    next_id = [1]

    def emit_struct(player):
        def go(name, wx, wy):
            structures.append(f"ID{next_id[0]:03d}={player},{name},256,{pos(wx, wy)}")
            next_id[0] += 1
        return go

    def emit_road(player):
        def go(wx, wy):
            structures.append(f"GEN{pos(wx, wy)}={player},Road")
        return go

    built1, total1 = pack_city(comp_p1, emit_struct("Player1"), emit_road("Player1"))
    built2, total2 = pack_city(comp_p2, emit_struct("Player2"), emit_road("Player2"))

    print(f"Player1 city: {built1}/{total1} rock tiles built = {100*built1/total1:.1f}%")
    print(f"Player2 city: {built2}/{total2} rock tiles built = {100*built2/total2:.1f}%")

    # Compose new [STRUCTURES] section and a starting-army [UNITS]
    # block (Player1/Player2) appended to the existing sandworm UNITS.

    units = []
    uid = [100]

    def emit_unit(player, name, wx, wy, action="Area Guard", angle=64):
        units.append(f"ID{uid[0]:03d}={player},{name},256,{pos(wx, wy)},{angle},{action}")
        uid[0] += 1

    def starting_army(player, comp):
        xs = [p[0] for p in comp]
        ys = [p[1] for p in comp]
        cx, cy = sum(xs) // len(comp), sum(ys) // len(comp)
        # Pick rock cells near the centroid for unit placement
        comp_set = set(comp)
        def free_near(ox, oy):
            for r in range(20):
                for dy in range(-r, r + 1):
                    for dx in range(-r, r + 1):
                        cell = (ox + dx, oy + dy)
                        if cell in comp_set:
                            return cell
            return (ox, oy)
        loadout = [
            ("Harvester", "Harvest"),
            ("Harvester", "Harvest"),
            ("Harvester", "Harvest"),
            ("Tank", "Area Guard"),
            ("Tank", "Area Guard"),
            ("Siege Tank", "Area Guard"),
            ("Quad", "Area Guard"),
            ("Quad", "Area Guard"),
            ("Trike", "Area Guard"),
            ("Launcher", "Area Guard"),
            ("Launcher", "Area Guard"),
            ("Devastator", "Area Guard"),
            ("Carryall", "Area Guard"),
            ("'Thopter", "Area Guard"),
        ]
        for i, (name, action) in enumerate(loadout):
            ox = cx + (i % 5) * 2 - 4
            oy = cy + (i // 5) * 2 - 2
            ux, uy = free_near(ox, oy)
            emit_unit(player, name, ux, uy, action)

    starting_army("Player1", comp_p1)
    starting_army("Player2", comp_p2)

    # ----- Build the rewritten INI -----
    # Strip any pre-existing [STRUCTURES] section.
    text = re.sub(r'\n\[STRUCTURES\][\s\S]*?(?=\n\[|\Z)', '', text)

    # Find the [UNITS] block and append our new starting-army units to it
    # without duplicating: remove any prior Player1/Player2 unit entries
    # (those are ours from a previous run; sandworms are Fremen/Player3
    # and stay).
    def strip_owned_units(units_block):
        kept = []
        for line in units_block.splitlines():
            mm = re.match(r'ID\d+=([^,]+),', line)
            if mm and mm.group(1) in ("Player1", "Player2"):
                continue
            kept.append(line)
        return "\n".join(kept)

    um = re.search(r'(\[UNITS\]\n)((?:.*\n?)*?)(?=\n\[|\Z)', text)
    if um:
        existing = um.group(2)
        cleaned = strip_owned_units(existing)
        new_units_block = um.group(1) + cleaned.rstrip() + "\n" + "\n".join(units) + "\n"
        text = text[:um.start()] + new_units_block + text[um.end():]
    else:
        text += "\n[UNITS]\n" + "\n".join(units) + "\n"

    # Re-insert [STRUCTURES] just before [UNITS]
    structures_block = "\n[STRUCTURES]\n" + "\n".join(structures) + "\n"
    text = re.sub(r'\n\[UNITS\]', structures_block + "\n[UNITS]", text, count=1)

    # Patch [Player1]/[Player2]: bump credits + make sure they're playable
    def patch_player(text, name, credits=15000):
        pat = re.compile(rf'(\[{name}\]\n)((?:[^\[]*\n)?)', re.M)
        body = "Credits=15000\nQuota=0\n"
        if re.search(rf'\[{name}\]', text):
            text = re.sub(
                rf'\[{name}\][\s\S]*?(?=\n\[|\Z)',
                f"[{name}]\n{body.rstrip()}",
                text,
                count=1,
            )
        else:
            text += f"\n[{name}]\n{body}\n"
        return text

    text = patch_player(text, "Player1")
    text = patch_player(text, "Player2")

    PATH.write_text(text)
    print(f"Wrote {PATH} ({PATH.stat().st_size} bytes)")
    print(f"Total structures emitted: {len(structures)}")
    print(f"Total starting-army units: {len(units)}")


if __name__ == "__main__":
    main()
