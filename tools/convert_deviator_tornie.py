from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"C:\Users\canti\OneDrive\Images\Dune 2\deviator_d2_compatible.png")
PALETTE_IMAGE = ROOT / "data" / "RocketTrikeMask.png"
OUTPUTS = [
    ROOT / "data" / "Deviator.png",
    ROOT / "mods" / "Tornie" / "data" / "Deviator.png",
    ROOT / "build-msys" / "bin" / "Deviator.png",
]

TEAM_RAMP = list(range(144, 151))
HOUSE_RANGES = (52, 128, 144, 160, 176, 192, 208, 224)


def read_palette(path: Path) -> list[tuple[int, int, int]]:
    palette_image = Image.open(path)
    palette_data = palette_image.getpalette()
    if not palette_data or len(palette_data) < 768:
        raise RuntimeError(f"{path} has no 256-color PNG palette")

    return [
        tuple(palette_data[i : i + 3])
        for i in range(0, 768, 3)
    ]


def nearest_palette_index(rgb: tuple[int, int, int], palette: list[tuple[int, int, int]], excluded: set[int]) -> int:
    r, g, b = rgb
    best_index = 0
    best_distance = 1 << 60
    for i, (pr, pg, pb) in enumerate(palette):
        if i in excluded:
            continue
        d = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
        if d < best_distance:
            best_distance = d
            best_index = i
    return best_index


def team_ramp_index(rgb: tuple[int, int, int]) -> int | None:
    r, g, b = rgb
    if r < 80 or r < g * 1.25 or r < b * 1.25:
        return None

    brightness = (r + max(g, b)) / 2
    pos = round((brightness - 80) * (len(TEAM_RAMP) - 1) / 175)
    return TEAM_RAMP[max(0, min(len(TEAM_RAMP) - 1, pos))]


def convert() -> None:
    palette = read_palette(PALETTE_IMAGE)

    source = Image.open(SOURCE).convert("RGBA")
    if source.size != (256, 32):
        raise RuntimeError(f"Unexpected Deviator source size {source.size}, expected 256x32")

    resized = Image.new("RGBA", (128, 16), (0, 0, 0, 0))
    for frame in range(8):
        frame_image = source.crop((frame * 32, 0, (frame + 1) * 32, 32))
        frame_image = frame_image.resize((16, 16), Image.Resampling.NEAREST)
        resized.paste(frame_image, (frame * 16, 0))

    indexed = Image.new("P", resized.size)
    excluded = {0}
    for start in HOUSE_RANGES:
        excluded.update(range(start, start + 8))
    pixels = []
    for r, g, b, a in resized.getdata():
        if a < 96 or (r < 4 and g < 4 and b < 4):
            pixels.append(0)
            continue

        team_index = team_ramp_index((r, g, b))
        if team_index is not None:
            pixels.append(team_index)
            continue

        pixels.append(nearest_palette_index((r, g, b), palette, excluded))

    indexed.putdata(pixels)
    flat_palette = []
    for r, g, b in palette:
        flat_palette.extend((r, g, b))
    indexed.putpalette(flat_palette)
    indexed.info["transparency"] = 0

    for output in OUTPUTS:
        output.parent.mkdir(parents=True, exist_ok=True)
        indexed.save(output)
        print(f"wrote {output} {indexed.size[0]}x{indexed.size[1]}")


if __name__ == "__main__":
    convert()
