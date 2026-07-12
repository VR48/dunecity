from pathlib import Path
from shutil import copyfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
PYTHON_ASSETS = Path(r"C:\Users\canti\OneDrive\Images\Dune 2")
PALETTE_IMAGE = ROOT / "data" / "RocketTrikeMask.png"

UNIT_ASSETS = {
    "Deviator.png": PYTHON_ASSETS / "deviator_d2_compatible - Copie.png",
    "EliteSiegeTank.png": PYTHON_ASSETS / "Elite_Siege_Tank_32x32 - Copie.png",
}
DIRECT_ASSETS = {
    "HeraldRebels.png": PYTHON_ASSETS / "HeraldRebels.png",
}
DEST_DIRS = [
    ROOT / "data",
    ROOT / "mods" / "Tornie" / "data",
    ROOT / "build-msys" / "bin",
]

TEAM_RAMP = list(range(144, 151))
HOUSE_RANGES = (52, 128, 144, 160, 176, 192, 208, 224)


def read_palette(path: Path) -> list[tuple[int, int, int]]:
    palette_image = Image.open(path)
    palette_data = palette_image.getpalette()
    if not palette_data or len(palette_data) < 768:
        raise RuntimeError(f"{path} has no 256-color PNG palette")
    return [tuple(palette_data[i : i + 3]) for i in range(0, 768, 3)]


def nearest_palette_index(rgb: tuple[int, int, int], palette: list[tuple[int, int, int]], excluded: set[int]) -> int:
    r, g, b = rgb
    best_index = 1
    best_distance = 1 << 60
    for idx, (pr, pg, pb) in enumerate(palette):
        if idx in excluded:
            continue
        distance = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
        if distance < best_distance:
            best_distance = distance
            best_index = idx
    return best_index


def team_ramp_index(rgb: tuple[int, int, int]) -> int | None:
    r, g, b = rgb
    red_team = r >= 70 and r >= g * 1.15 and r >= b * 1.15
    green_team = g >= 70 and g >= r * 1.15 and g >= b * 1.05
    yellow_team = r >= 80 and g >= 80 and abs(r - g) <= 70 and max(r, g) >= b * 1.35
    if not (red_team or green_team or yellow_team):
        return None

    brightness = max(r, g, b)
    pos = round((235 - brightness) * (len(TEAM_RAMP) - 1) / 205)
    return TEAM_RAMP[max(0, min(len(TEAM_RAMP) - 1, pos))]


def convert_unit_sheet(source: Path, palette: list[tuple[int, int, int]]) -> Image.Image:
    rgba = Image.open(source).convert("RGBA")
    if rgba.size != (128, 16):
        raise RuntimeError(f"{source} has size {rgba.size}, expected 128x16")

    out = Image.new("P", rgba.size, 0)
    flat_palette: list[int] = []
    for r, g, b in palette:
        flat_palette.extend((r, g, b))
    out.putpalette(flat_palette)

    excluded = {0}
    for start in HOUSE_RANGES:
        excluded.update(range(start, start + 8))

    cache: dict[tuple[int, int, int], int] = {}
    pixels: list[int] = []
    for r, g, b, a in rgba.getdata():
        if a < 96 or (r < 4 and g < 4 and b < 4):
            pixels.append(0)
            continue

        team_index = team_ramp_index((r, g, b))
        if team_index is not None:
            pixels.append(team_index)
            continue

        key = (r, g, b)
        idx = cache.get(key)
        if idx is None:
            idx = nearest_palette_index(key, palette, excluded)
            cache[key] = idx
        pixels.append(idx)

    out.putdata(pixels)
    out.info["transparency"] = 0
    return out


def main() -> None:
    palette = read_palette(PALETTE_IMAGE)
    for name, source in UNIT_ASSETS.items():
        converted = convert_unit_sheet(source, palette)
        for dest_dir in DEST_DIRS:
            dest_dir.mkdir(parents=True, exist_ok=True)
            dest = dest_dir / name
            converted.save(dest, optimize=False)
            print(f"indexed {dest} {converted.size[0]}x{converted.size[1]}")

    for name, source in DIRECT_ASSETS.items():
        image = Image.open(source)
        if image.mode != "P" or image.getpalette() is None:
            raise RuntimeError(f"{source} must remain indexed for DuneCity")
        for dest_dir in DEST_DIRS:
            dest_dir.mkdir(parents=True, exist_ok=True)
            dest = dest_dir / name
            copyfile(source, dest)
            print(f"copied {dest}")


if __name__ == "__main__":
    main()
