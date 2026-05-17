# Sprite Import Scripts

Scripts to extract building sprites from a local copy of the original Dune II
game files for use as DuneCity zone art.

**These scripts do NOT bundle copyrighted assets into the repo.** Output goes to
a local directory that is gitignored.

## Prerequisites

- Python 3.8+
- Pillow: `pip install Pillow`
- A local copy of the Dune II DATA directory (containing ICON.ICN, ICON.MAP,
  IBM.PAL, and .WSA files)

## import-sprites.py — Structure tile sprites (ICN/MAP)

Extracts composed structure graphics from the ICN tileset:

```bash
# Export all known structures as 2× PNGs:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA

# Export specific MAP entries at 1× scale:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA --entries 17,19,21 --scale 1

# Custom output directory:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA --out ./my_sprites
```

Output: `./imported_sprites/<structure_name>.png`

## extract-wsa.py — Animation frames (WSA)

Extracts individual frames from WSA animation files (build-menu portraits):

```bash
# Extract all WSA files:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA

# Extract specific files:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA --files WINDTRAP.WSA,CONSTRUC.WSA

# 2× upscale:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA --scale 2
```

Output: `./imported_sprites/wsa/<name>_frameNN.png`

## Importing into DuneCity

Once extracted, the PNGs can be placed in `data/` subdirectories and loaded by
GFXManager. The current zone placeholders in `GFXManager.cpp` are colored
rectangles — replace the `makeZonePlaceholder()` calls with proper PNG loading
once you have art ready.

The `imported_sprites/` directory is gitignored to prevent accidental commits.

## import-micropolis.py — Micropolis (open-source city sim) tiles

Extracts all 960 16×16 tiles from the Micropolis tile sheet (`tiles.xpm`),
plus categorised subsets and assembled multi-tile composites for buildings
and zones.

### Prerequisites

- Python 3.8+
- Pillow: `pip install Pillow`
- (Optional) a local Micropolis source checkout

### Usage

```bash
# Auto-download Micropolis source (cloned into a temp cache):
python3 scripts/import-micropolis.py

# Use a local Micropolis checkout:
python3 scripts/import-micropolis.py --micropolis-dir /path/to/micropolis

# Custom output directory:
python3 scripts/import-micropolis.py --out ./my_output
```

### Output structure

```
imported_sprites/micropolis/
├── raw_tiles/           # tile_000.png .. tile_959.png (all 960 tiles)
├── categories/          # Tiles grouped by function
│   ├── roads/           # Road/bridge/intersection tiles
│   ├── power_lines/     # Power line tiles
│   ├── rail/            # Railroad tiles
│   ├── residential_zones/  # Residential building tiles
│   ├── commercial_zones/   # Commercial building tiles
│   ├── industrial_zones/   # Industrial building tiles
│   ├── airport/         # Airport tiles
│   ├── seaport/         # Seaport tiles
│   ├── coal_power/      # Coal power plant tiles
│   ├── nuclear_power/   # Nuclear power plant tiles
│   └── ...              # 36 categories total
├── composites/          # Assembled 3×3 building PNGs (48×48)
│   ├── res_v0_d0_3x3.png   # Residential value 0, density 0
│   ├── com_v1_d2_3x3.png   # Commercial value 1, density 2
│   └── ...              # 49 zone/building composites
├── composites_2x2/      # 2×2 adapted versions (32×32) for DuneCity
├── composites_special/  # Large buildings: 4×4 and 6×6
│   ├── coal_power_plant_4x4.png
│   ├── nuclear_power_plant_4x4.png
│   ├── airport_6x6.png
│   └── ...
├── composites_special_2x2/  # Large buildings downscaled to 2×2
├── manifest.json        # Full index of all exported files
└── NOTICE.txt           # Licensing and attribution
```

### Naming conventions

- **Residential**: `res_v{value}_d{density}` — value 0–3 (land value), density 0–3
- **Commercial**: `com_v{value}_d{density}` — value 0–3, density 0–4
- **Industrial**: `ind_v{value}_d{density}` — value 0–1, density 0–3
- **Special**: `hospital`, `church`, `fire_station`, `police_station`, `seaport`
- **Large**: `coal_power_plant`, `nuclear_power_plant`, `stadium`, `airport`

### License / trademark caveats

The Micropolis tile art is Copyright (C) 1989–2007 Electronic Arts Inc.,
released under GPL-3.0+.  The additional terms prohibit use of the
"SimCity" trademark.  See `NOTICE.txt` in the output directory.  Do **not**
use "SimCity" in filenames, docs, or any distributed materials.
