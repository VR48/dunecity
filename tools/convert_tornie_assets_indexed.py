from pathlib import Path
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "data" / "Tornie_AdvancedWindtrap_gfx.png"
TARGETS = [
    "Worfinery.png",
    "TechCenter.png",
    "Scoutpost.png",
    "Advanced_Power_Plant.png",
    "advanced_power_2x3.png",
    "BuildSite_2x3.png",
]
DEST_DIRS = [
    ROOT / "data",
    ROOT / "mods" / "Tornie" / "data",
    ROOT / "build-msys" / "bin",
]


def load_reference_palette():
    ref = Image.open(REFERENCE)
    if ref.mode != "P" or ref.getpalette() is None:
        raise RuntimeError(f"{REFERENCE} is not an indexed PNG with a palette")
    palette = ref.getpalette()[: 256 * 3]
    if len(palette) < 256 * 3:
        palette += [0] * (256 * 3 - len(palette))
    return palette


def nearest_palette_index(rgb, palette):
    r, g, b = rgb
    best_index = 1
    best_distance = 1 << 62
    for idx in range(1, 256):
        pr = palette[idx * 3 + 0]
        pg = palette[idx * 3 + 1]
        pb = palette[idx * 3 + 2]
        dr = int(r) - int(pr)
        dg = int(g) - int(pg)
        db = int(b) - int(pb)
        distance = dr * dr + dg * dg + db * db
        if distance < best_distance:
            best_distance = distance
            best_index = idx
    return best_index


def convert_to_indexed(src, palette):
    rgba = Image.open(src).convert("RGBA")
    out = Image.new("P", rgba.size, 0)
    out.putpalette(palette)

    cache = {}
    src_pixels = rgba.load()
    out_pixels = out.load()
    width, height = rgba.size
    for y in range(height):
        for x in range(width):
            r, g, b, a = src_pixels[x, y]
            if a < 128:
                out_pixels[x, y] = 0
                continue
            key = (r, g, b)
            idx = cache.get(key)
            if idx is None:
                idx = nearest_palette_index(key, palette)
                cache[key] = idx
            out_pixels[x, y] = idx

    out.info["transparency"] = 0
    return out


def main():
    palette = load_reference_palette()
    for name in TARGETS:
        source = ROOT / "data" / name
        if not source.exists():
            print(f"missing {source}")
            continue
        converted = convert_to_indexed(source, palette)
        for dest_dir in DEST_DIRS:
            dest_dir.mkdir(parents=True, exist_ok=True)
            dest = dest_dir / name
            converted.save(dest, optimize=False)
            print(f"indexed {dest}")


if __name__ == "__main__":
    main()
