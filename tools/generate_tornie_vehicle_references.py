from __future__ import annotations

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "mods" / "Tornie" / "data"
BASE_DIR = ROOT / "mods" / "Tornie" / "source-sprites" / "base"
OUTPUT_DIR = ROOT / "mods" / "Tornie" / "source-sprites" / "vehicles"

FRAME_SIZE = 16
SOURCE_COLUMNS = 10
SOURCE_FRAMES = (2, 1, 0, 1, 2, 3, 4, 3)
MIRROR_FRAMES = (False, False, False, True, True, True, False, False)
TRANSPARENT_INDEX = 0


def load_indexed(name: str, expected_size: tuple[int, int] | None = None) -> Image.Image:
    path = SOURCE_DIR / name
    image = Image.open(path)
    if image.mode != "P" or image.getpalette() is None:
        raise RuntimeError(f"{path} must be an indexed PNG")
    if expected_size is not None and image.size != expected_size:
        raise RuntimeError(f"{path} has size {image.size}, expected {expected_size}")
    return image.copy()


def new_indexed(reference: Image.Image, size: tuple[int, int]) -> Image.Image:
    image = Image.new("P", size, TRANSPARENT_INDEX)
    palette = reference.getpalette()
    if palette is None:
        raise RuntimeError("The reference image has no palette")
    image.putpalette(palette)
    image.info["transparency"] = TRANSPARENT_INDEX
    return image


def save_indexed(image: Image.Image, name: str) -> None:
    path = OUTPUT_DIR / name
    image.info["transparency"] = TRANSPARENT_INDEX
    image.save(path, optimize=False, transparency=TRANSPARENT_INDEX)
    print(f"generated {path.relative_to(ROOT)}")


def validate_atlas(image: Image.Image, name: str, frame_size: int = FRAME_SIZE) -> None:
    expected_size = (8 * frame_size, frame_size)
    if image.size != expected_size:
        raise RuntimeError(f"{name} has size {image.size}, expected {expected_size}")
    validate_strip(image, name)


def validate_strip(image: Image.Image, name: str) -> None:
    if image.mode != "P" or image.width % 8 != 0:
        raise RuntimeError(
            f"{name} has mode/size {image.mode} {image.size}; expected an indexed 8-frame strip"
        )
    if image.getpalette() is None:
        raise RuntimeError(f"{name} has no indexed palette")

    frame_width = image.width // 8
    pixels = image.load()
    for angle in range(8):
        opaque_pixels = 0
        for y in range(image.height):
            for x in range(frame_width):
                if pixels[angle * frame_width + x, y] != TRANSPARENT_INDEX:
                    opaque_pixels += 1
        if opaque_pixels == 0:
            raise RuntimeError(f"{name} orientation {angle} is empty")
        if opaque_pixels == frame_width * image.height:
            raise RuntimeError(f"{name} orientation {angle} is fully opaque")


def extract_atlas(sheet: Image.Image, first_frame: int) -> Image.Image:
    atlas = new_indexed(sheet, (8 * FRAME_SIZE, FRAME_SIZE))
    for angle, (source_offset, mirror) in enumerate(zip(SOURCE_FRAMES, MIRROR_FRAMES)):
        source_index = first_frame + source_offset
        source_x = (source_index % SOURCE_COLUMNS) * FRAME_SIZE
        source_y = (source_index // SOURCE_COLUMNS) * FRAME_SIZE
        frame = sheet.crop(
            (source_x, source_y, source_x + FRAME_SIZE, source_y + FRAME_SIZE)
        )
        if mirror:
            frame = frame.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        atlas.paste(frame, (angle * FRAME_SIZE, 0))
    return atlas


def nearest_palette_index(
    rgb: tuple[int, int, int], palette: list[tuple[int, int, int]]
) -> int:
    red, green, blue = rgb
    return min(
        range(1, len(palette)),
        key=lambda index: (
            (red - palette[index][0]) ** 2
            + (green - palette[index][1]) ** 2
            + (blue - palette[index][2]) ** 2
        ),
    )


def rgba_to_indexed(source: Image.Image, reference: Image.Image) -> Image.Image:
    palette_data = reference.getpalette()
    if palette_data is None:
        raise RuntimeError("The reference image has no palette")
    palette_data = palette_data[: 256 * 3] + [0] * max(0, 256 * 3 - len(palette_data))
    palette = [
        tuple(palette_data[index : index + 3])
        for index in range(0, 256 * 3, 3)
    ]

    rgba = source.convert("RGBA")
    indexed = new_indexed(reference, rgba.size)
    cache: dict[tuple[int, int, int], int] = {}
    pixels: list[int] = []
    source_pixels = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            red, green, blue, alpha = source_pixels[x, y]
            if alpha < 128:
                pixels.append(TRANSPARENT_INDEX)
                continue
            color = (red, green, blue)
            pixels.append(cache.setdefault(color, nearest_palette_index(color, palette)))
    indexed.putdata(pixels)
    return indexed


def expand_five_frame_strip(source: Image.Image, reference: Image.Image) -> Image.Image:
    if source.width < 5 * FRAME_SIZE or source.height != FRAME_SIZE:
        raise RuntimeError(
            f"Five-frame source has size {source.size}, expected at least (80, 16)"
        )
    indexed = rgba_to_indexed(source.crop((0, 0, 5 * FRAME_SIZE, FRAME_SIZE)), reference)
    synthetic_sheet = new_indexed(reference, (SOURCE_COLUMNS * FRAME_SIZE, FRAME_SIZE))
    synthetic_sheet.paste(indexed, (0, 0))
    return extract_atlas(synthetic_sheet, 0)


def compose_atlases(
    base: Image.Image, turret: Image.Image, x_offset: int, y_offset: int
) -> Image.Image:
    if base.size != (8 * FRAME_SIZE, FRAME_SIZE):
        raise RuntimeError(f"Base atlas has unexpected size {base.size}")
    if turret.width % 8 != 0:
        raise RuntimeError(f"Turret atlas width {turret.width} is not divisible by 8")
    if turret.getpalette() != base.getpalette():
        turret = rgba_to_indexed(turret.convert("RGBA"), base)

    result = base.copy()
    result.info["transparency"] = TRANSPARENT_INDEX
    turret_frame_width = turret.width // 8
    for angle in range(8):
        base_x = angle * FRAME_SIZE
        turret_x = angle * turret_frame_width
        for source_y in range(turret.height):
            destination_y = source_y + y_offset
            if not 0 <= destination_y < FRAME_SIZE:
                continue
            for source_x in range(turret_frame_width):
                destination_x = source_x + x_offset
                if not 0 <= destination_x < FRAME_SIZE:
                    continue
                pixel = turret.getpixel((turret_x + source_x, source_y))
                if pixel != TRANSPARENT_INDEX:
                    result.putpixel((base_x + destination_x, destination_y), pixel)
    return result


def normalize_existing_atlas(name: str, reference: Image.Image) -> Image.Image:
    path = SOURCE_DIR / name
    image = Image.open(path)
    if image.size != (8 * FRAME_SIZE, FRAME_SIZE):
        raise RuntimeError(f"{path} has size {image.size}, expected (128, 16)")
    # Converting through RGBA honours PNG transparency tables and normalizes
    # every transparent pixel to palette index 0 in the generated reference.
    return rgba_to_indexed(image, reference)


def load_base_strip(name: str, reference: Image.Image) -> Image.Image:
    path = BASE_DIR / name
    image = Image.open(path)
    if image.width % 8 != 0:
        raise RuntimeError(f"{path} is not an 8-frame strip")
    return rgba_to_indexed(image, reference)


def load_approved_turret_reference(name: str, fallback: Image.Image) -> Image.Image:
    path = OUTPUT_DIR / name
    if not path.exists():
        return fallback.copy()

    image = Image.open(path)
    if image.mode != "P" or image.getpalette() is None:
        raise RuntimeError(f"{path} must remain an indexed PNG")
    approved = image.copy()
    approved.info["transparency"] = TRANSPARENT_INDEX
    validate_strip(approved, name)
    return approved


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    units = load_indexed("TornieUnits.png", (160, 192))
    units2 = load_indexed("TornieUnits2.png", (160, 64))

    # These strips are decoded directly from the user's original UNITS2.SHP
    # by scripts/extract-unit-sprite.py. Native turret dimensions are kept so
    # their engine offsets remain exact.
    tank_base = load_base_strip("TankBase_vanilla.png", units2)
    siege_base = load_base_strip("SiegeBase_vanilla.png", units2)
    launcher_turret = load_base_strip("LauncherTurret_vanilla.png", units2)
    sonic_turret = load_base_strip("SonicTurret_vanilla.png", units2)

    rocket_trike = normalize_existing_atlas("RocketTrike.png", units)
    sonic_trike_source = Image.open(SOURCE_DIR / "SonicTrike.png")
    sonic_trike = expand_five_frame_strip(sonic_trike_source, units)
    # Dune II stores the Harvester in native 24x24 frames. The compact 16x16
    # Tornie draft is useful as a color guide, but using it as an engine-ready
    # reference changes the scale and pivot of every orientation.
    harvestank = load_base_strip("HarvesterBase_vanilla.png", units)
    harvestank_compact_guide = extract_atlas(units, 10)

    # Preserve user-approved native turret strips. Falling back to the vanilla
    # launcher keeps a fresh checkout buildable until custom art is supplied.
    deviator_turret = load_approved_turret_reference(
        "Deviator_turret_reference.png", launcher_turret
    )
    flame_turret = load_approved_turret_reference(
        "FlameTank_turret_reference.png", launcher_turret
    )
    elite_launcher_turret = load_approved_turret_reference(
        "EliteLauncher_turret_reference.png", launcher_turret
    )
    elite_siege_turret = extract_atlas(units2, 15)
    rebel_sonic_turret = sonic_turret.copy()

    outputs = {
        "RocketTrike_reference.png": rocket_trike,
        "SonicTrike_reference.png": sonic_trike,
        "Harvestank_reference.png": harvestank,
        "Harvestank_compact_color_guide.png": harvestank_compact_guide,
        "Deviator_turret_reference.png": deviator_turret,
        "Deviator_reference.png": compose_atlases(tank_base, deviator_turret, 3, 0),
        "FlameTank_turret_reference.png": flame_turret,
        "FlameTank_reference.png": compose_atlases(tank_base, flame_turret, 3, 0),
        "EliteLauncher_turret_reference.png": elite_launcher_turret,
        "EliteLauncher_reference.png": compose_atlases(
            tank_base, elite_launcher_turret, 3, 0
        ),
        "EliteSiegeTank_turret_reference.png": elite_siege_turret,
        "EliteSiegeTank_reference.png": compose_atlases(
            siege_base, elite_siege_turret, 2, -4
        ),
        "RebelSonicTank_turret_reference.png": rebel_sonic_turret,
        "RebelSonicTank_reference.png": compose_atlases(
            tank_base, rebel_sonic_turret, 3, 1
        ),
    }

    for name, image in outputs.items():
        if name == "Harvestank_reference.png":
            validate_atlas(image, name, frame_size=24)
        elif name == "Harvestank_compact_color_guide.png" or "_turret_" in name:
            validate_strip(image, name)
        else:
            validate_atlas(image, name)

    approved_turret_references = {
        "Deviator_turret_reference.png",
        "FlameTank_turret_reference.png",
        "EliteLauncher_turret_reference.png",
    }

    for name, image in outputs.items():
        path = OUTPUT_DIR / name
        if name in approved_turret_references and path.exists():
            print(f"preserved {path.relative_to(ROOT)}")
            continue
        save_indexed(image, name)


if __name__ == "__main__":
    main()
