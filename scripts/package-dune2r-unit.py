#!/usr/bin/env python3
"""Package Oathkeeper Dune2R frames as an enhanced unit mod payload."""

from __future__ import annotations

import argparse
import configparser
import json
import math
import shutil
from pathlib import Path

from PIL import Image, ImageOps

from dune2r_composite import compose_layers


DIRECTIONS = (
    "east",
    "north_east",
    "north",
    "north_west",
    "west",
    "south_west",
    "south",
    "south_east",
)

SUPPORTED_STATES = {
    "idle": (("full_unit_idle", "idle"), "Idle", True),
    "movement": (("full_unit_movement", "movement"), "Movement", True),
    "combat": (("full_unit_combat", "combat"), "Combat", False),
    "damage_smoking": (("full_unit_damage_smoking", "damage_smoking"), "DamageSmoking", True),
    "damage_damaged": (("full_unit_damage_damaged", "damage_damaged"), "DamageDamaged", True),
    "damage_exploded": (("full_unit_damage_exploded", "damage_exploded"), "DamageExploded", False),
    "damage_aftermath": (("full_unit_damage_aftermath", "damage_aftermath"), "DamageAftermath", True),
    "damage_dissipation": (("full_unit_damage_dissipation", "damage_dissipation"), "DamageDissipation", False),
}

COMPOSITE_STATES = {
    "idle": (("chassis_idle",), ("turret_idle",)),
    "movement": (("chassis_movement", "chassis_idle"), ("turret_idle",)),
    "combat": (("chassis_movement", "chassis_idle"), ("turret_fire",)),
}

TILE_VARIANTS = (
    "island", "up", "right", "up_right", "down", "up_down", "down_right", "not_left",
    "left", "up_left", "left_right", "not_down", "down_left", "not_right", "not_up", "full",
)

BUILDING_STATES = {
    "building_placement": ("Placement", False),
    "building_construction": ("Construction", False),
    "building_idle": ("Idle", True),
    "building_active": ("Working", True),
    "building_damaged": ("Damaged", False),
    "building_repair": ("Repair", False),
    "building_destroyed": ("Destroyed", False),
}

MAX_ATLAS_SIZE = 4096


def load_frames(frames_dir: Path, frame_size: int) -> list[Image.Image]:
    paths = sorted(frames_dir.glob("frame_*.png"))
    if not paths:
        return []

    frames: list[Image.Image] = []
    for path in paths:
        with Image.open(path) as source:
            rgba = source.convert("RGBA")
            if rgba.width <= 32 or rgba.height <= 32:
                return []
            fitted = ImageOps.contain(rgba, (frame_size, frame_size), Image.Resampling.LANCZOS)
            frame = Image.new("RGBA", (frame_size, frame_size))
            frame.alpha_composite(
                fitted,
                ((frame_size - fitted.width) // 2, (frame_size - fitted.height) // 2),
            )
            frames.append(frame)
    return frames


def load_sprite(sprite_path: Path, frame_size: int) -> list[Image.Image]:
    if not sprite_path.is_file():
        return []
    with Image.open(sprite_path) as source:
        rgba = source.convert("RGBA")
        if rgba.width <= 32 or rgba.height <= 32:
            return []
        fitted = ImageOps.contain(rgba, (frame_size, frame_size), Image.Resampling.LANCZOS)
        frame = Image.new("RGBA", (frame_size, frame_size))
        frame.alpha_composite(
            fitted,
            ((frame_size - fitted.width) // 2, (frame_size - fitted.height) // 2),
        )
        return [frame]


def load_source_frames(asset_root: Path, assets: dict[str, object]) -> tuple[list[Image.Image], int]:
    animation = assets.get("animation", {}) if isinstance(assets.get("animation"), dict) else {}
    frames_dir = asset_root / str(animation.get("frames_dir", ""))
    frames: list[Image.Image] = []
    if frames_dir.is_dir():
        for path in sorted(frames_dir.glob("frame_*.png")):
            with Image.open(path) as source:
                frames.append(source.convert("RGBA"))
    if not frames:
        sprite = assets.get("sprite", {}) if isinstance(assets.get("sprite"), dict) else {}
        sprite_path = asset_root / str(sprite.get("file", ""))
        if sprite_path.is_file():
            with Image.open(sprite_path) as source:
                frames = [source.convert("RGBA")]
    return frames, max(1, int(animation.get("frame_duration_ms", 90)))


def composite_layer(metadata: dict[str, object], asset_root: Path,
                    category_name: str, direction: str) -> tuple[dict[str, object] | None, int]:
    category = metadata.get("categories", {}).get(category_name, {})
    state = category.get("states", {}).get(direction, {}) if isinstance(category, dict) else {}
    assets = state.get("assets", {}) if isinstance(state, dict) else {}
    frames, frame_ms = load_source_frames(asset_root, assets)
    if not frames:
        return None, frame_ms

    sprite = assets.get("sprite", {}) if isinstance(assets.get("sprite"), dict) else {}
    sprite_path = asset_root / str(sprite.get("file", ""))
    reference_bbox_size = None
    if sprite_path.is_file():
        with Image.open(sprite_path) as source:
            bbox = source.convert("RGBA").getchannel("A").getbbox()
        if bbox is not None:
            reference_bbox_size = [bbox[2] - bbox[0], bbox[3] - bbox[1]]

    scale = state.get("scale_adjustment", {}) if isinstance(state.get("scale_adjustment"), dict) else {}
    alignment = state.get("transition_alignment", {}) if isinstance(state.get("transition_alignment"), dict) else {}
    return {
        "frames": frames,
        "scale": float(scale.get("factor", 1.0)),
        "offset_x": int(alignment.get("offset_x", 0)),
        "offset_y": int(alignment.get("offset_y", 0)),
        "offset_reference_size": alignment.get("reference_size"),
        "reference_bbox_size": reference_bbox_size,
    }, frame_ms


def load_composite_frames(metadata: dict[str, object], asset_root: Path,
                          state_name: str, direction: str,
                          frame_size: int) -> tuple[list[Image.Image], int, str]:
    plan = COMPOSITE_STATES.get(state_name)
    if plan is None:
        return [], 90, ""
    layers = []
    durations = []
    selected = []
    for candidates in plan:
        layer = None
        duration = 90
        category_name = ""
        for candidate in candidates:
            layer, duration = composite_layer(metadata, asset_root, candidate, direction)
            if layer is not None:
                category_name = candidate
                break
        if layer is None:
            return [], 90, ""
        layers.append(layer)
        durations.append(duration)
        selected.append(category_name)
    composed = compose_layers(layers, max_frames=120)
    fitted = []
    for frame in composed:
        contained = ImageOps.contain(frame, (frame_size, frame_size), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (frame_size, frame_size))
        canvas.alpha_composite(contained, ((frame_size - contained.width) // 2,
                                            (frame_size - contained.height) // 2))
        fitted.append(canvas)
    return fitted, min(durations), "+".join(selected)


def write_atlas(frames: list[Image.Image], destination: Path, columns: int) -> tuple[int, int]:
    rows = math.ceil(len(frames) / columns)
    frame_size = frames[0].width
    atlas = Image.new("RGBA", (columns * frame_size, rows * frame_size))
    for index, frame in enumerate(frames):
        atlas.alpha_composite(frame, ((index % columns) * frame_size, (index // columns) * frame_size))
    destination.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(destination, optimize=True)
    return columns, rows


def fit_frames_to_width(frames: list[Image.Image], target_width: int) -> list[Image.Image]:
    if not frames:
        return []
    target_height = max(1, round(frames[0].height * target_width / frames[0].width))
    for index, frame in enumerate(frames):
        resized = frame.resize((target_width, target_height), Image.Resampling.LANCZOS)
        frame.close()
        frames[index] = resized
    return frames


def write_chunked_atlases(frames: list[Image.Image], output: Path,
                          relative_dir: Path) -> list[dict[str, int | str]]:
    frame_width, frame_height = frames[0].size
    columns = max(1, MAX_ATLAS_SIZE // frame_width)
    rows = max(1, MAX_ATLAS_SIZE // frame_height)
    frames_per_atlas = columns * rows
    chunks: list[dict[str, int | str]] = []
    for chunk_index, first in enumerate(range(0, len(frames), frames_per_atlas)):
        chunk = frames[first:first + frames_per_atlas]
        chunk_columns = min(columns, len(chunk))
        relative_path = relative_dir / f"{chunk_index:02d}.png"
        atlas_columns, atlas_rows = write_atlas(chunk, output / relative_path, chunk_columns)
        chunks.append({
            "path": relative_path.as_posix(),
            "first": first,
            "frames": len(chunk),
            "columns": atlas_columns,
            "rows": atlas_rows,
        })
    return chunks


def package_tile(metadata: dict[str, object], asset_root: Path, output: Path,
                 args: argparse.Namespace) -> int:
    manifest = configparser.ConfigParser()
    manifest.optionxform = str
    manifest["Tile"] = {
        "TerrainType": str(args.item_id),
        "SourceUnit": str(metadata.get("slug", args.source_unit.name)),
        "Variants": str(len(TILE_VARIANTS)),
    }
    manifest["Render"] = {"PixelsPerTile": str(args.frame_size)}

    states = metadata.get("categories", {}).get("tile_base", {}).get("states", {})
    packaged = 0
    for index, variant in enumerate(TILE_VARIANTS):
        assets = states.get(variant, {}).get("assets", {})
        sprite_value = assets.get("sprite", {}).get("file", "")
        sprite_path = asset_root / sprite_value
        if not sprite_path.is_file():
            print(f"skip tile_base/{variant}: no enhanced sprite")
            continue
        with Image.open(sprite_path) as source:
            tile = source.convert("RGBA").resize(
                (args.frame_size, args.frame_size), Image.Resampling.LANCZOS)
        relative_image = Path("tiles") / f"{variant}.png"
        (output / relative_image).parent.mkdir(parents=True, exist_ok=True)
        tile.save(output / relative_image, optimize=True)

        section = f"Variant.{index}"
        manifest[section] = {"Name": variant, "Image": relative_image.as_posix()}
        compact_value = assets.get("processed", {}).get("file", "")
        compact_path = asset_root / compact_value
        if compact_path.is_file():
            relative_compact = Path("compact") / f"{variant}.png"
            (output / relative_compact).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(compact_path, output / relative_compact)
            manifest[section]["Compact"] = relative_compact.as_posix()
        packaged += 1

    if packaged != len(TILE_VARIANTS):
        raise SystemExit(f"Tile package needs all {len(TILE_VARIANTS)} topology sprites; found {packaged}")
    with (output / "tile.ini").open("w", encoding="ascii", newline="\n") as handle:
        manifest.write(handle, space_around_delimiters=False)
    print(f"wrote {output / 'tile.ini'} with {packaged} topology sprites")
    return packaged


def package_building(metadata: dict[str, object], asset_root: Path, output: Path,
                     args: argparse.Namespace) -> int:
    profile = metadata.get("render_profile", {})
    footprint = profile.get("logical_footprint_tiles", [1, 1])
    footprint_width = max(1, int(footprint[0]))
    footprint_height = max(1, int(footprint[1]))
    target_width = footprint_width * args.frame_size

    manifest = configparser.ConfigParser()
    manifest.optionxform = str
    manifest["Building"] = {
        "ItemID": str(args.item_id),
        "HouseID": str(args.house_id),
        "SourceUnit": str(metadata.get("slug", args.source_unit.name)),
        "FootprintWidth": str(footprint_width),
        "FootprintHeight": str(footprint_height),
    }
    manifest["Render"] = {"PixelsPerTile": str(args.frame_size)}

    packaged = 0
    for category_name, (state_name, loops) in BUILDING_STATES.items():
        category = metadata.get("categories", {}).get(category_name, {})
        state = category.get("states", {}).get("default", {})
        assets = state.get("assets", {})
        frames, frame_ms = load_source_frames(asset_root, assets)
        if not frames:
            print(f"skip {category_name}/default: no processed frames or sprite fallback")
            continue
        frames = fit_frames_to_width(frames, target_width)
        chunks = write_chunked_atlases(
            frames, output, Path("atlases") / state_name.lower())
        section = f"State.{state_name}"
        manifest[section] = {
            "Frames": str(len(frames)),
            "FrameMs": str(frame_ms),
            "FrameWidth": str(frames[0].width),
            "FrameHeight": str(frames[0].height),
            "AnchorX": str(frames[0].width // 2),
            "AnchorY": str(frames[0].height),
            "Loop": "true" if loops else "false",
            "AtlasCount": str(len(chunks)),
        }
        for index, chunk in enumerate(chunks):
            manifest[section][f"Atlas.{index}"] = str(chunk["path"])
            manifest[section][f"FirstFrame.{index}"] = str(chunk["first"])
            manifest[section][f"ChunkFrames.{index}"] = str(chunk["frames"])
            manifest[section][f"Columns.{index}"] = str(chunk["columns"])
            manifest[section][f"Rows.{index}"] = str(chunk["rows"])
        packaged += 1
        print(f"packaged {category_name}/default: {len(frames)} frame(s) in {len(chunks)} atlas chunk(s)")

    if packaged == 0:
        raise SystemExit("No enhanced building frames or sprite fallbacks were eligible for packaging")
    with (output / "building.ini").open("w", encoding="ascii", newline="\n") as handle:
        manifest.write(handle, space_around_delimiters=False)
    print(f"wrote {output / 'building.ini'} with {packaged} visual state(s)")
    return packaged


def package_unit(args: argparse.Namespace) -> int:
    source_unit = args.source_unit.resolve()
    metadata_path = source_unit / "unit.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    asset_root = source_unit.parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    unit_type = str(metadata.get("unit_type", "ground")).lower()
    if unit_type == "tile":
        package_tile(metadata, asset_root, output, args)
        return 0
    if unit_type == "building":
        package_building(metadata, asset_root, output, args)
        return 0

    manifest = configparser.ConfigParser()
    manifest.optionxform = str
    manifest["Unit"] = {
        "ItemID": str(args.item_id),
        "HouseID": str(args.house_id),
        "SourceUnit": metadata.get("slug", source_unit.name),
    }
    manifest["Render"] = {
        "BaseWidth": str(args.base_width),
        "BaseHeight": str(args.base_height),
        "Scale": str(args.scale),
    }

    packaged = 0
    for source_state, (category_candidates, manifest_state, loops) in SUPPORTED_STATES.items():
        selected_category = source_state
        states: dict[str, object] = {}
        for category_name in category_candidates:
            category = metadata.get("categories", {}).get(category_name, {})
            candidate_states = category.get("states", {})
            if any(candidate_states.get(direction, {}).get("assets") for direction in DIRECTIONS):
                selected_category = category_name
                states = candidate_states
                break
        if source_state == "movement" and not states:
            # Older Oathkeeper unit records stored movement directly under
            # `directions`; newer records expose it as a normal category.
            states = metadata.get("directions", {})
        for direction in DIRECTIONS:
            state = states.get(direction, {}) or states.get("default", {})
            assets = state.get("assets", {})
            animation = assets.get("animation", {})
            frames_value = animation.get("frames_dir", "")
            frames = load_frames(asset_root / frames_value, args.frame_size) if frames_value else []
            if not frames:
                sprite_value = assets.get("sprite", {}).get("file", "")
                frames = load_sprite(asset_root / sprite_value, args.frame_size) if sprite_value else []
            if not frames:
                frames, composite_frame_ms, composite_name = load_composite_frames(
                    metadata, asset_root, source_state, direction, args.frame_size)
                if frames:
                    animation = {"frame_duration_ms": composite_frame_ms}
                    selected_category = composite_name
                else:
                    print(f"skip {selected_category}/{direction}: no enhanced PNG frames or sprite")
                    continue

            relative_atlas = Path("atlases") / source_state / f"{direction}.png"
            columns, rows = write_atlas(frames, output / relative_atlas, args.columns)
            section = f"{manifest_state}.{direction}"
            manifest[section] = {
                "Atlas": relative_atlas.as_posix(),
                "Columns": str(columns),
                "Rows": str(rows),
                "Frames": str(len(frames)),
                "FrameMs": str(max(1, int(animation.get("frame_duration_ms", 90)))),
                "AnchorX": str(args.frame_size // 2),
                "AnchorY": str(args.frame_size // 2),
                "Loop": "true" if loops else "false",
            }
            packaged += 1
            print(f"packaged {selected_category}/{direction}: {len(frames)} frame(s)")

    if packaged == 0:
        raise SystemExit("No enhanced animation frames were eligible for packaging")

    with (output / "unit.ini").open("w", encoding="ascii", newline="\n") as handle:
        manifest.write(handle, space_around_delimiters=False)

    print(f"wrote {output / 'unit.ini'} with {packaged} animation(s)")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_unit", type=Path, help="Oathkeeper dune2/units/<unit> directory")
    parser.add_argument("output", type=Path, help="mods/<mod>/graphics_hd/units/<unit> directory")
    parser.add_argument("--item-id", type=int, required=True, help="Stable DuneCity ItemID")
    parser.add_argument("--house-id", type=int, default=-1, help="House restriction; -1 means every house")
    parser.add_argument("--frame-size", type=int, default=192)
    parser.add_argument("--columns", type=int, default=8)
    parser.add_argument("--base-width", type=int, default=48)
    parser.add_argument("--base-height", type=int, default=48)
    parser.add_argument("--scale", type=float, default=1.0)
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(package_unit(parse_args()))
