#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys
if sys.stdout.encoding and sys.stdout.encoding.lower() not in ('utf-8', 'utf_8'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
"""
Extract tile sprites from the Micropolis (open-source SimCity) tileset.

Parses images/tiles.xpm from a Micropolis source tree, slices all 960
16×16 tiles, and exports them as individual PNGs plus categorised sets
and assembled composites for multi-tile buildings/zones.

Micropolis source code is Copyright (C) 1989-2007 Electronic Arts Inc.,
released under GPL-3.0-or-later with additional terms.  See NOTICE.txt
in the output directory for full details.

Usage:
    # Auto-download Micropolis source (into a temp cache):
    python3 scripts/import-micropolis.py

    # Use a local Micropolis checkout:
    python3 scripts/import-micropolis.py --micropolis-dir /path/to/micropolis

    # Custom output directory:
    python3 scripts/import-micropolis.py --out ./my_output
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image, ImageFilter
except ImportError:
    sys.exit("Error: Pillow is required.  Install with:  pip install Pillow")

# ---------------------------------------------------------------------------
# Micropolis tile constants (from src/sim/headers/sim.h)
# ---------------------------------------------------------------------------
TILE_COUNT = 960
TILE_SIZE = 16

# Category ranges  (start, end_exclusive, name)
CATEGORY_RANGES = [
    (0, 2, "terrain_dirt"),
    (2, 21, "terrain_water"),
    (21, 44, "terrain_trees"),
    (44, 48, "rubble"),
    (48, 52, "flood"),
    (52, 56, "radiation"),
    (56, 64, "fire"),
    (64, 207, "roads"),
    (208, 223, "power_lines"),
    (224, 239, "rail"),
    (240, 249, "residential_empty"),
    (249, 261, "houses"),
    (265, 409, "residential_zones"),
    (409, 418, "hospital"),
    (418, 423, "church"),
    (423, 436, "commercial_empty"),
    (436, 612, "commercial_zones"),
    (612, 625, "industrial_empty"),
    (625, 693, "industrial_zones"),
    (693, 709, "seaport"),
    (709, 745, "airport"),
    (745, 761, "coal_power"),
    (761, 770, "fire_station"),
    (770, 779, "police_station"),
    (779, 800, "stadium"),
    (800, 811, "stadium_full"),
    (811, 827, "nuclear_power"),
    (827, 828, "lightning"),
    (828, 832, "h_bridge_special"),
    (832, 840, "radar_anim"),
    (840, 844, "fountain"),
    (844, 852, "telecom"),
    (852, 868, "explosion"),
    (916, 932, "coal_smoke"),
    (932, 948, "football"),
    (948, 952, "v_bridge_special"),
]

# Named multi-tile buildings: (name, center_tile_id, grid_width, grid_height)
# In Micropolis the center tile of a 3×3 building is base+4 (ZonePlop lays
# tiles left-to-right, top-to-bottom starting at base).
# fmt: off
BUILDINGS_3x3 = [
    # Residential zone variants: value 0-3, density 0-3
    # base = ((value*4 + den)*9) + RZB - 4  → center = base+4 = ((v*4+d)*9) + RZB
    *[
        (f"res_v{v}_d{d}", ((v * 4 + d) * 9) + 265 - 4, 3, 3)
        for v in range(4) for d in range(4)
    ],
    # Commercial zone variants: value 0-3, density 0-4
    # base = ((value*5 + den)*9) + CZB - 4
    *[
        (f"com_v{v}_d{d}", ((v * 5 + d) * 9) + 436 - 4, 3, 3)
        for v in range(4) for d in range(5)
    ],
    # Industrial zone variants: value 0-1, density 0-3
    # base = ((value*4 + den)*9) + IZB - 4
    *[
        (f"ind_v{v}_d{d}", ((v * 4 + d) * 9) + 625 - 4, 3, 3)
        for v in range(2) for d in range(4)
    ],
    ("hospital",        409 - 4, 3, 3),
    ("church",          418 - 4, 3, 3),
    ("fire_station",    761, 3, 3),
    ("police_station",  770, 3, 3),
    ("seaport",         693, 3, 3),  # tiles 693-701 but center at 698
    # Special: these buildings use different base→center relationships
]

# Larger special buildings (laid out as contiguous tile runs, row by row)
BUILDINGS_SPECIAL = [
    ("coal_power_plant", 745, 4, 4),
    ("nuclear_power_plant", 811, 4, 4),
    ("stadium",          779, 4, 4),
    ("stadium_full",     800, 4, 4),  # during football game
    ("airport",          709, 6, 6),
]
# fmt: on

# ---------------------------------------------------------------------------
# XPM parser
# ---------------------------------------------------------------------------
def parse_xpm(xpm_path: str) -> Image.Image:
    """Parse an XPM file and return a PIL Image."""
    with open(xpm_path, "r") as f:
        content = f.read()

    # Extract strings between quotes
    strings = re.findall(r'"([^"]*)"', content)
    if not strings:
        sys.exit(f"Error: no XPM data found in {xpm_path}")

    # First string is the header: width height ncolors chars_per_pixel
    header = strings[0].split()
    width, height, ncolors, cpp = int(header[0]), int(header[1]), int(header[2]), int(header[3])

    # Parse colour table
    colour_map = {}
    for i in range(1, ncolors + 1):
        line = strings[i]
        key = line[:cpp]
        # Find the colour value after "c "
        rest = line[cpp:]
        m = re.search(r'\bc\s+(#[0-9A-Fa-f]+|None)\b', rest, re.IGNORECASE)
        if m:
            cval = m.group(1)
            if cval.lower() == 'none':
                colour_map[key] = (0, 0, 0, 0)
            else:
                # Handle both 6-digit and 12-digit hex colours
                hexval = cval[1:]
                if len(hexval) == 12:
                    r = int(hexval[0:2], 16)
                    g = int(hexval[4:6], 16)
                    b = int(hexval[8:10], 16)
                    colour_map[key] = (r, g, b, 255)
                elif len(hexval) == 6:
                    r = int(hexval[0:2], 16)
                    g = int(hexval[2:4], 16)
                    b = int(hexval[4:6], 16)
                    colour_map[key] = (r, g, b, 255)
                else:
                    colour_map[key] = (0, 0, 0, 255)
        else:
            colour_map[key] = (0, 0, 0, 255)

    # Parse pixel data
    img = Image.new("RGBA", (width, height))
    pixels = img.load()
    for y in range(height):
        row = strings[ncolors + 1 + y]
        for x in range(width):
            key = row[x * cpp:(x + 1) * cpp]
            pixels[x, y] = colour_map.get(key, (255, 0, 255, 255))

    return img


def slice_tiles(sheet: Image.Image) -> list:
    """Slice a tile sheet into individual 16×16 tile images."""
    tiles = []
    for i in range(TILE_COUNT):
        y = i * TILE_SIZE
        tile = sheet.crop((0, y, TILE_SIZE, y + TILE_SIZE))
        tiles.append(tile)
    return tiles


def assemble_composite(tiles: list, base_id: int, cols: int, rows: int) -> Image.Image:
    """Assemble a multi-tile composite from sequential tile IDs."""
    composite = Image.new("RGBA", (cols * TILE_SIZE, rows * TILE_SIZE))
    for r in range(rows):
        for c in range(cols):
            tid = base_id + r * cols + c
            if 0 <= tid < len(tiles):
                composite.paste(tiles[tid], (c * TILE_SIZE, r * TILE_SIZE))
    return composite


def downscale_pixel_art(source: Image.Image, target_size: int) -> Image.Image:
    """Downscale pixel art to target_size×target_size with edge-preserving quality.

    Strategy: upscale with NEAREST to a clean integer multiple of the target,
    then downscale with LANCZOS so the filter samples fall on pixel boundaries.
    A light unsharp mask restores crispness lost in the downscale.
    """
    src_w, src_h = source.size
    # Find a NEAREST upscale factor so the intermediate is an integer multiple
    # of target_size and at least 3× target for good LANCZOS sampling.
    up_factor = 1
    while (src_w * up_factor) < target_size * 3:
        up_factor += 1
    # Also ensure the intermediate is an exact multiple of target_size
    while (src_w * up_factor) % target_size != 0:
        up_factor += 1

    intermediate_size = src_w * up_factor
    upscaled = source.resize((intermediate_size, intermediate_size), Image.NEAREST)
    downscaled = upscaled.resize((target_size, target_size), Image.LANCZOS)

    # Light unsharp mask: small radius to sharpen pixel edges without halos.
    # radius=0.8, percent=60, threshold=1 — gentle enough to avoid crunchy artifacts.
    sharpened = downscaled.filter(ImageFilter.UnsharpMask(radius=0.8, percent=60, threshold=1))
    return sharpened


def assemble_composite_2x2(tiles: list, base_id: int, cols: int, rows: int) -> Image.Image:
    """Assemble a 2×2 adapted composite by downscaling the full building art
    using pixel-art-aware resampling."""
    full = assemble_composite(tiles, base_id, cols, rows)
    target = 2 * TILE_SIZE  # 32
    return downscale_pixel_art(full, target)


def ensure_micropolis_source(args) -> str:
    """Return path to Micropolis source tree, downloading if needed."""
    if args.micropolis_dir:
        p = Path(args.micropolis_dir)
        # Try both root layouts
        for sub in ["micropolis-activity", "."]:
            candidate = p / sub / "images" / "tiles.xpm"
            if candidate.exists():
                return str(p / sub)
        sys.exit(f"Error: cannot find images/tiles.xpm under {p}")

    # Auto-download into a cache directory
    cache = Path(args.cache_dir)
    cached_src = cache / "micropolis" / "micropolis-activity"
    if (cached_src / "images" / "tiles.xpm").exists():
        print(f"Using cached Micropolis source at {cached_src}")
        return str(cached_src)

    print("Downloading Micropolis source from GitHub...")
    cache.mkdir(parents=True, exist_ok=True)
    clone_dir = cache / "micropolis"
    try:
        subprocess.run(
            ["git", "clone", "--depth", "1",
             "https://github.com/SimHacker/micropolis",
             str(clone_dir)],
            check=True, capture_output=True, text=True
        )
    except subprocess.CalledProcessError as e:
        sys.exit(f"Error cloning Micropolis: {e.stderr}")

    if not (cached_src / "images" / "tiles.xpm").exists():
        sys.exit(f"Error: cloned repo does not contain expected file structure")

    return str(cached_src)


NOTICE_TEXT = """\
NOTICE — Micropolis Tile Art
=============================

The tile artwork extracted by this script originates from Micropolis,
the open-source release of the original city-building game.

Original code and artwork:
  Copyright (C) 1989 - 2007 Electronic Arts Inc.
  Licensed under the GNU General Public License, version 3 or later.
  https://github.com/SimHacker/micropolis

Additional terms (from sim.h):
  No trademark or publicity rights are granted.  This license does NOT
  give you any right, title or interest in the trademark SimCity or any
  other Electronic Arts trademark.  You may not distribute any
  modification of this program using the trademark SimCity or claim any
  affiliation or association with Electronic Arts Inc. or its employees.

These sprite exports are generated for local development use in the
DuneCity project.  If redistributing, comply with GPL-3.0+ and the
additional terms above.
"""


def main():
    parser = argparse.ArgumentParser(
        description="Extract Micropolis tile sprites for DuneCity")
    parser.add_argument("--micropolis-dir", type=str, default=None,
                        help="Path to local Micropolis source tree")
    parser.add_argument("--out", type=str, default="imported_sprites/micropolis",
                        help="Output directory (default: imported_sprites/micropolis)")
    parser.add_argument("--cache-dir", type=str,
                        default=os.path.join(tempfile.gettempdir(), "dunecity_cache"),
                        help="Cache directory for auto-downloaded sources")
    args = parser.parse_args()

    src_dir = ensure_micropolis_source(args)
    xpm_path = os.path.join(src_dir, "images", "tiles.xpm")
    print(f"Parsing {xpm_path} ...")

    sheet = parse_xpm(xpm_path)
    print(f"Tile sheet: {sheet.size[0]}×{sheet.size[1]} → {TILE_COUNT} tiles")

    tiles = slice_tiles(sheet)

    out = Path(args.out)
    manifest = {
        "source": "Micropolis (GPL-3.0+)",
        "tile_size": TILE_SIZE,
        "tile_count": TILE_COUNT,
        "raw_tiles": [],
        "categories": {},
        "composites_3x3": [],
        "composites_2x2": [],
        "composites_special": [],
        "composites_special_2x2": [],
    }

    # --- 1. Raw tiles ---
    raw_dir = out / "raw_tiles"
    raw_dir.mkdir(parents=True, exist_ok=True)
    for i, tile in enumerate(tiles):
        fname = f"tile_{i:03d}.png"
        tile.save(raw_dir / fname)
        manifest["raw_tiles"].append(fname)
    print(f"Exported {len(tiles)} raw tiles to {raw_dir}/")

    # --- 2. Categorised tiles ---
    cat_dir = out / "categories"
    for start, end, name in CATEGORY_RANGES:
        cdir = cat_dir / name
        cdir.mkdir(parents=True, exist_ok=True)
        fnames = []
        for i in range(start, min(end, TILE_COUNT)):
            fname = f"tile_{i:03d}.png"
            tiles[i].save(cdir / fname)
            fnames.append(fname)
        manifest["categories"][name] = {
            "tile_range": [start, end],
            "count": len(fnames),
            "files": fnames
        }
    print(f"Exported {len(CATEGORY_RANGES)} categories to {cat_dir}/")

    # --- 3. 3×3 building composites ---
    comp_dir = out / "composites"
    comp_dir.mkdir(parents=True, exist_ok=True)
    for name, base, cols, rows in BUILDINGS_3x3:
        if base < 0 or base + cols * rows > TILE_COUNT:
            continue
        img = assemble_composite(tiles, base, cols, rows)
        fname = f"{name}_{cols}x{rows}.png"
        img.save(comp_dir / fname)
        manifest["composites_3x3"].append({
            "name": name, "file": fname,
            "base_tile": base, "grid": f"{cols}x{rows}"
        })

    # --- 4. 2×2 adapted composites for zone buildings ---
    comp2_dir = out / "composites_2x2"
    comp2_dir.mkdir(parents=True, exist_ok=True)
    for name, base, cols, rows in BUILDINGS_3x3:
        if base < 0 or base + cols * rows > TILE_COUNT:
            continue
        img = assemble_composite_2x2(tiles, base, cols, rows)
        fname = f"{name}_2x2.png"
        img.save(comp2_dir / fname)
        manifest["composites_2x2"].append({
            "name": name, "file": fname,
            "source_base": base, "source_grid": f"{cols}x{rows}"
        })

    # --- 5. Special/large building composites ---
    spec_dir = out / "composites_special"
    spec_dir.mkdir(parents=True, exist_ok=True)
    spec2_dir = out / "composites_special_2x2"
    spec2_dir.mkdir(parents=True, exist_ok=True)
    for name, base, cols, rows in BUILDINGS_SPECIAL:
        if base < 0 or base + cols * rows > TILE_COUNT:
            print(f"  Skipping {name}: tiles {base}-{base+cols*rows-1} out of range")
            continue
        img = assemble_composite(tiles, base, cols, rows)
        fname = f"{name}_{cols}x{rows}.png"
        img.save(spec_dir / fname)
        manifest["composites_special"].append({
            "name": name, "file": fname,
            "base_tile": base, "grid": f"{cols}x{rows}"
        })
        # 2×2 adapted
        img2 = assemble_composite_2x2(tiles, base, cols, rows)
        fname2 = f"{name}_2x2.png"
        img2.save(spec2_dir / fname2)
        manifest["composites_special_2x2"].append({
            "name": name, "file": fname2,
            "source_base": base, "source_grid": f"{cols}x{rows}"
        })
    print(f"Exported {len(manifest['composites_3x3'])} zone composites (3×3)")
    print(f"Exported {len(manifest['composites_2x2'])} zone composites (2×2 adapted)")
    print(f"Exported {len(manifest['composites_special'])} special building composites")
    print(f"Exported {len(manifest['composites_special_2x2'])} special building composites (2×2)")

    # --- 6. NOTICE.txt ---
    notice_path = out / "NOTICE.txt"
    notice_path.write_text(NOTICE_TEXT)

    # --- 7. Manifest ---
    manifest_path = out / "manifest.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"\nManifest: {manifest_path}")
    print(f"License notice: {notice_path}")

    # Summary
    total_files = (
        len(manifest["raw_tiles"])
        + sum(c["count"] for c in manifest["categories"].values())
        + len(manifest["composites_3x3"])
        + len(manifest["composites_2x2"])
        + len(manifest["composites_special"])
        + len(manifest["composites_special_2x2"])
        + 2  # manifest + NOTICE
    )
    print(f"\nTotal files generated: {total_files}")
    print("Done.")


if __name__ == "__main__":
    main()
