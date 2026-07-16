from __future__ import annotations

import argparse
from pathlib import Path
from shutil import copyfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "mods" / "Tornie" / "data"
TEAM_RAMP = tuple(range(144, 151))
HOUSE_RAMPS = (52, 128, 144, 160, 176, 192, 208, 224)


def validate_indexed_sheet(path: Path, expected_size: tuple[int, int]) -> Image.Image:
    image = Image.open(path)
    if image.mode != "P" or image.getpalette() is None:
        raise RuntimeError(f"{path} must be an indexed PNG")
    if image.size != expected_size:
        raise RuntimeError(f"{path} has size {image.size}, expected {expected_size}")
    if image.info.get("transparency", 0) != 0:
        raise RuntimeError(f"{path} must use palette index 0 for transparency")
    return image


def palette_colors(image: Image.Image) -> list[tuple[int, int, int]]:
    data = image.getpalette()
    if data is None:
        raise RuntimeError("The reference sheet has no palette")
    data = data[: 256 * 3] + [0] * max(0, 256 * 3 - len(data))
    return [tuple(data[index : index + 3]) for index in range(0, 256 * 3, 3)]


def nearest_palette_index(
    rgb: tuple[int, int, int],
    palette: list[tuple[int, int, int]],
    excluded: set[int],
) -> int:
    r, g, b = rgb
    choices = (
        ((r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2, index)
        for index, (pr, pg, pb) in enumerate(palette)
        if index not in excluded
    )
    return min(choices)[1]


def harkonnen_ramp_index(rgb: tuple[int, int, int]) -> int | None:
    r, g, b = rgb
    if r < 70 or r < g * 1.15 or r < b * 1.15:
        return None
    brightness = max(r, g, b)
    position = round((235 - brightness) * (len(TEAM_RAMP) - 1) / 205)
    return TEAM_RAMP[max(0, min(len(TEAM_RAMP) - 1, position))]


def convert_elite_launcher(source: Path, reference: Image.Image) -> Image.Image:
    rgba = Image.open(source).convert("RGBA")
    if rgba.size != (128, 16):
        raise RuntimeError(f"{source} has size {rgba.size}, expected (128, 16)")

    palette = palette_colors(reference)
    excluded = {0}
    for ramp_start in HOUSE_RAMPS:
        excluded.update(range(ramp_start, ramp_start + 8))

    indexed = Image.new("P", rgba.size, 0)
    indexed.putpalette([component for color in palette for component in color])
    cache: dict[tuple[int, int, int], int] = {}
    output: list[int] = []
    for r, g, b, a in rgba.getdata():
        if a < 96 or (r < 4 and g < 4 and b < 4):
            output.append(0)
            continue
        team_index = harkonnen_ramp_index((r, g, b))
        if team_index is not None:
            output.append(team_index)
            continue
        color = (r, g, b)
        output.append(cache.setdefault(color, nearest_palette_index(color, palette, excluded)))

    indexed.putdata(output)
    indexed.info["transparency"] = 0
    return indexed


def parse_args() -> argparse.Namespace:
    default_root = Path(r"C:\Users\canti\OneDrive\Images\Dune 2")
    parser = argparse.ArgumentParser(description="Import Tornie indexed unit sheets")
    parser.add_argument("--units", type=Path, default=default_root / "Pixelart" / "units_sp.png")
    parser.add_argument("--units2", type=Path, default=default_root / "Pixelart" / "units2_sp.png")
    parser.add_argument("--units2-flame", type=Path, default=default_root / "Pixelart" / "units2_sp2.png")
    parser.add_argument("--elite-launcher", type=Path, required=True)
    parser.add_argument("--harvestank-icon", type=Path, default=default_root / "harvestank_icon.png")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    DESTINATION.mkdir(parents=True, exist_ok=True)

    sheets = (
        (args.units, "TornieUnits.png", (160, 192)),
        (args.units2, "TornieUnits2.png", (160, 64)),
        (args.units2_flame, "TornieUnits2Flame.png", (160, 64)),
    )
    reference = None
    for source, name, expected_size in sheets:
        image = validate_indexed_sheet(source, expected_size)
        copyfile(source, DESTINATION / name)
        print(f"copied {source} -> {DESTINATION / name}")
        if name == "TornieUnits2.png":
            reference = image.copy()

    if reference is None:
        raise RuntimeError("TornieUnits2.png was not imported")

    elite_launcher = convert_elite_launcher(args.elite_launcher, reference)
    elite_launcher.save(DESTINATION / "EliteLauncherGun.png", optimize=False)
    print(f"converted {args.elite_launcher} -> {DESTINATION / 'EliteLauncherGun.png'}")

    icon = Image.open(args.harvestank_icon)
    if icon.size != (91, 55):
        raise RuntimeError(f"{args.harvestank_icon} has size {icon.size}, expected (91, 55)")
    copyfile(args.harvestank_icon, DESTINATION / "HarvestankIcon.png")
    print(f"copied {args.harvestank_icon} -> {DESTINATION / 'HarvestankIcon.png'}")


if __name__ == "__main__":
    main()
