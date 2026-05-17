#!/usr/bin/env python3
"""
import-sprites.py — Extract building sprites from original Dune II game files.

Reads a user-provided local DUNE2 data directory (containing .PAK archives)
and extracts structure/building sprite frames as individual PNGs into an
output directory for use as DuneCity zone art.

This script does NOT copy or bundle copyrighted original-game assets into the
repository.  It operates on files you supply from your own local copy of
Dune II and writes output to a local directory (default: ./imported_sprites/).

Usage:
    python3 scripts/import-sprites.py /path/to/DUNE2/DATA [--out ./imported_sprites]

Requirements:
    - Python 3.8+
    - Pillow (pip install Pillow)
    - A local copy of the original Dune II DATA directory containing:
        ICON.ICN, ICON.MAP, and the Dune II palette file (IBM.PAL)

The script reads the ICN tileset format used by Dune II for structure graphics
and produces 32×32 or 64×64 PNG files suitable for replacing zone placeholders.
"""

import argparse
import os
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Error: Pillow is required.  Install with:  pip install Pillow")


# ---------------------------------------------------------------------------
# Dune II palette loader (6-bit IBM.PAL → 8-bit RGB)
# ---------------------------------------------------------------------------

def load_palette(pal_path: Path) -> list[tuple[int, int, int]]:
    """Load a Dune II 6-bit palette (768 bytes, 256 entries × 3 channels)."""
    data = pal_path.read_bytes()
    if len(data) < 768:
        sys.exit(f"Error: palette file too small ({len(data)} bytes, expected ≥768)")
    palette = []
    for i in range(256):
        r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
        # Dune II palettes are 6-bit (0-63); scale to 8-bit
        palette.append((r << 2 | r >> 4, g << 2 | g >> 4, b << 2 | b >> 4))
    return palette


# ---------------------------------------------------------------------------
# ICN file reader (16×16 tile bitmaps)
# ---------------------------------------------------------------------------

def load_icn_tiles(icn_path: Path) -> list[bytes]:
    """
    Load tiles from an ICN file.  Each tile is 16×16 pixels, stored as raw
    palette-indexed bytes.  Returns a list of 256-byte tile buffers.
    """
    data = icn_path.read_bytes()
    # ICN header: first 2 bytes = tile width (always 16), next 2 = height (16),
    # next 2 = shift (0 for ICON.ICN).  Tile data starts after the 0x20 header.
    header_size = 0x20
    tile_size = 16 * 16  # 256 bytes per tile

    tiles = []
    offset = header_size
    while offset + tile_size <= len(data):
        tiles.append(data[offset:offset + tile_size])
        offset += tile_size
    return tiles


# ---------------------------------------------------------------------------
# MAP file reader (tile-index sets per map entry)
# ---------------------------------------------------------------------------

def load_map_entries(map_path: Path) -> list[list[int]]:
    """
    Load a MAP file.  Each entry is a variable-length list of tile indices
    that compose one structure/terrain graphic.

    MAP format: sequences of uint16 LE tile indices, terminated by 0x0000.
    The file itself is a flat array of uint16; entry boundaries are at the
    offsets stored in a separate index (the first N uint16 values point to
    the start of each entry).
    """
    data = map_path.read_bytes()
    num_words = len(data) // 2
    words = [struct.unpack_from('<H', data, i * 2)[0] for i in range(num_words)]

    if not words:
        return []

    # The first entry starts at word index given by words[0] / 2.
    # All words before that are the offset table.
    first_offset_word = words[0] // 2
    num_entries = first_offset_word

    entries = []
    for e in range(num_entries):
        start = words[e] // 2
        # Collect tile indices until 0 terminator or end of data
        tile_indices = []
        pos = start
        while pos < num_words:
            idx = words[pos]
            if idx == 0 and pos != start:
                break
            tile_indices.append(idx)
            pos += 1
        entries.append(tile_indices)

    return entries


# ---------------------------------------------------------------------------
# Compose a structure sprite from ICN tiles + MAP entry
# ---------------------------------------------------------------------------

def compose_structure(tiles: list[bytes],
                      tile_indices: list[int],
                      palette: list[tuple[int, int, int]],
                      cols: int = 0) -> Image.Image:
    """
    Compose a structure image from a list of tile indices.
    Tiles are arranged left-to-right, top-to-bottom.
    If cols is 0, auto-detect based on tile count (1→1, 2-4→2, 6→3, etc.).
    """
    n = len(tile_indices)
    if cols == 0:
        if n <= 1:
            cols = 1
        elif n <= 4:
            cols = 2
        elif n <= 9:
            cols = 3
        else:
            cols = 4
    rows = (n + cols - 1) // cols

    img = Image.new('RGB', (cols * 16, rows * 16))
    for i, tidx in enumerate(tile_indices):
        if tidx >= len(tiles):
            continue
        tile_data = tiles[tidx]
        tile_img = Image.new('RGB', (16, 16))
        for y in range(16):
            for x in range(16):
                pixel = tile_data[y * 16 + x]
                tile_img.putpixel((x, y), palette[pixel])
        col = i % cols
        row = i // cols
        img.paste(tile_img, (col * 16, row * 16))
    return img


# ---------------------------------------------------------------------------
# Known Dune II structure MAP indices
# ---------------------------------------------------------------------------

# These are the standard MAP entry indices for structures in the original
# Dune II ICON.ICN/ICON.MAP.  Index numbers may vary between game versions;
# these match the commonly distributed versions.
STRUCTURE_MAP_INDICES = {
    'palace':           11,
    'light_factory':    12,
    'heavy_factory':    13,
    'hi_tech':          14,
    'ix':               15,
    'wor':              16,
    'construction_yard': 17,
    'barracks':         18,
    'windtrap':         19,
    'starport':         20,
    'refinery':         21,
    'repair_yard':      22,
    'gun_turret':       23,
    'rocket_turret':    24,
    'silo':             25,
    'radar':            26,
}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Extract Dune II building sprites to PNG for DuneCity zone art.')
    parser.add_argument('datadir', type=Path,
                        help='Path to local Dune II DATA directory '
                             '(containing ICON.ICN, ICON.MAP, IBM.PAL)')
    parser.add_argument('--out', type=Path, default=Path('./imported_sprites'),
                        help='Output directory (default: ./imported_sprites)')
    parser.add_argument('--scale', type=int, default=2, choices=[1, 2, 3],
                        help='Upscale factor (default: 2 → 64×64 for 2×2-tile structures)')
    parser.add_argument('--entries', type=str, default=None,
                        help='Comma-separated MAP entry indices to export (default: all known structures)')
    args = parser.parse_args()

    datadir = args.datadir
    outdir = args.out

    # Locate required files (case-insensitive search)
    def find_file(name: str) -> Path:
        for f in datadir.iterdir():
            if f.name.upper() == name.upper():
                return f
        sys.exit(f"Error: {name} not found in {datadir}")

    icn_path = find_file('ICON.ICN')
    map_path = find_file('ICON.MAP')

    # Try several common palette filenames
    pal_path = None
    for palname in ['IBM.PAL', 'DUNE.PAL', 'bene.pal', 'BENE.PAL']:
        try:
            pal_path = find_file(palname)
            break
        except SystemExit:
            continue
    if pal_path is None:
        sys.exit(f"Error: no palette file (IBM.PAL / DUNE.PAL) found in {datadir}")

    print(f"Loading palette from {pal_path}")
    palette = load_palette(pal_path)

    print(f"Loading ICN tiles from {icn_path}")
    tiles = load_icn_tiles(icn_path)
    print(f"  → {len(tiles)} tiles loaded")

    print(f"Loading MAP entries from {map_path}")
    map_entries = load_map_entries(map_path)
    print(f"  → {len(map_entries)} entries loaded")

    outdir.mkdir(parents=True, exist_ok=True)

    # Determine which entries to export
    if args.entries:
        indices = [(int(x.strip()), f"entry_{x.strip()}") for x in args.entries.split(',')]
    else:
        indices = [(idx, name) for name, idx in sorted(STRUCTURE_MAP_INDICES.items(), key=lambda t: t[1])]
        # Also export all entries if user wants everything
        print(f"\nExporting {len(indices)} known structure sprites...")

    exported = 0
    for map_idx, name in indices:
        if map_idx >= len(map_entries):
            print(f"  SKIP {name} (MAP index {map_idx} out of range)")
            continue
        tile_indices = map_entries[map_idx]
        if not tile_indices:
            print(f"  SKIP {name} (empty MAP entry)")
            continue

        img = compose_structure(tiles, tile_indices, palette)
        if args.scale > 1:
            img = img.resize((img.width * args.scale, img.height * args.scale),
                             Image.NEAREST)

        out_path = outdir / f"{name}.png"
        img.save(out_path)
        print(f"  {out_path}  ({img.width}×{img.height}, {len(tile_indices)} tiles)")
        exported += 1

    print(f"\nDone. {exported} sprites exported to {outdir}/")
    print("These files are for LOCAL USE ONLY — do not commit copyrighted assets to the repo.")


if __name__ == '__main__':
    main()
