"""Local atlas targets and original-art references, sourced from this engine.

Original art is decoded from the user's PAK files and never published by this
module. ICN layout follows Icnfile.cpp; SHP/PAK decoding reuses the existing tool.
"""
from __future__ import annotations

import importlib.util
import re
import struct
from pathlib import Path

from PIL import Image

HOUSES = {0: "Harkonnen", 1: "Atreides", 2: "Ordos", 3: "Fremen",
          4: "Sardaukar", 5: "Mercenary", -1: "All houses"}


def load_helper(path: Path):
    spec = importlib.util.spec_from_file_location(path.stem.replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


def targets(repo: Path) -> list[dict]:
    header = (repo / "include/data.h").read_text(encoding="utf-8")
    sources = {p.stem.lower(): p for p in (repo / "src/structures").glob("*.cpp")}
    result = []
    for prefix, name, number in re.findall(r"\b(Structure|Unit)_([A-Za-z0-9]+)\s*=\s*(\d+)", header):
        if name.endswith("ID"):
            continue
        kind = "building" if prefix == "Structure" else "unit"
        footprint = None
        source = sources.get(name.lower())
        if source:
            code = source.read_text(encoding="utf-8")
            if name in {"GunTurret", "RocketTurret"}:
                code = sources["turretbase"].read_text(encoding="utf-8")
            x = re.findall(r"structureSize\.x\s*=\s*(\d+)\s*;", code)
            y = re.findall(r"structureSize\.y\s*=\s*(\d+)\s*;", code)
            if len(set(x)) == len(set(y)) == 1:
                footprint = [int(x[0]), int(y[0])]
            if footprint is None:
                sizes = re.findall(rf"Structure_{name},\s*ObjPic_\w+,\s*Coord\((\d+),\s*(\d+)\)", code)
                if len(set(sizes)) == 1:
                    footprint = [int(n) for n in sizes[0]]
        # Slabs/walls and mod-specific objects have specialized renderers.
        supported = (kind == "building" and footprint is not None and int(number) <= 19
                     and name not in {"Wall", "Slab1", "Slab4"}) or (
                         kind == "unit" and 27 <= int(number) <= 47 and name != "Special")
        result.append(dict(key=f"{kind}:{number}", kind=kind, item_id=int(number),
                           name=re.sub(r"(?<=[a-z])(?=[A-Z])", " ", name), symbol=name,
                           footprint=footprint, supported=supported))
    terrain = re.search(r"typedef\s+enum\s*\{([^{}]+)\}\s*TERRAINTYPE", header).group(1)
    for number, name in enumerate(re.findall(r"\bTerrain_(\w+)", terrain)):
        result.append(dict(key=f"tile:{number}", kind="tile", item_id=number,
                           name="Gravel / Rock" if name == "Rock" else name,
                           symbol=name, footprint=[1, 1], supported=True))
    return result


class ReferenceSprites:
    def __init__(self, repo: Path):
        self.repo = repo
        self.decoder = load_helper(repo / "scripts/extract-unit-sprite.py")
        wanted = {"IBM.PAL", "ICON.ICN", "ICON.MAP", "UNITS.SHP", "UNITS1.SHP", "UNITS2.SHP"}
        self.files = {}
        for path in (repo / "data").iterdir():
            if path.name.upper() in wanted:
                self.files[path.name.upper()] = path.read_bytes()
        for path in sorted((repo / "data").glob("*.PAK")):
            buf = path.read_bytes()
            for name in wanted - self.files.keys():
                blob = self.decoder.pak_extract(buf, name)
                if blob is not None:
                    self.files[name] = blob
            if wanted <= self.files.keys():
                break
        self.palette = self.decoder.load_palette(self.files["IBM.PAL"])
        icn = self.files["ICON.ICN"]
        if icn[24:28] != b"SSET":
            raise ValueError("Invalid ICON.ICN SSET")
        length = struct.unpack_from(">I", icn, 28)[0] - 8
        self.sset = icn[40:40 + length]
        offset = 40 + length
        if icn[offset:offset + 4] != b"RPAL":
            raise ValueError("Invalid ICON.ICN RPAL")
        size = struct.unpack_from(">I", icn, offset + 4)[0]
        self.rpal = icn[offset + 8:offset + 8 + size]
        offset += 8 + size
        if icn[offset:offset + 4] != b"RTBL":
            raise ValueError("Invalid ICON.ICN RTBL")
        self.rtbl = icn[offset + 8:]
        data = self.files["ICON.MAP"]
        words = struct.unpack("<" + "H" * (len(data) // 2), data)
        self.entries = [words[words[i]:words[i + 1] if i + 1 < words[0] else len(words)]
                        for i in range(words[0])]
        gfx = (repo / "src/FileClasses/GFXManager.cpp").read_text(encoding="utf-8")
        self.building_entries = dict(re.findall(
            r"objPic\[ObjPic_(\w+)\]\[HOUSE_HARKONNEN\]\[0\] = icon->getPictureArray\((\d+)\)", gfx))
        self.terrain_offsets = {name: int(value, 16) for name, value in re.findall(
            r"TerrainTile_(\w+)\s*=\s*0x([0-9A-Fa-f]+)",
            (repo / "include/Tile.h").read_text(encoding="utf-8"))}

    def rgba(self, indices: bytes, size: tuple[int, int], house: int = 0) -> Image.Image:
        colors = []
        for index in indices:
            if 144 <= index < 151 and 0 <= house <= 5:
                index += house * 16
            colors.append((*self.palette[index], 0 if index == 0 else 255))
        image = Image.new("RGBA", size)
        image.putdata(colors)
        return image

    def tile(self, index: int, house: int = 0) -> Image.Image:
        palette = self.rpal[self.rtbl[index] * 16: self.rtbl[index] * 16 + 16]
        data = self.sset[index * 128:(index + 1) * 128]
        pixels = bytes(palette[n] for byte in data for n in (byte >> 4, byte & 15))
        return self.rgba(pixels, (16, 16), house)

    def slab(self, footprint: list[int]) -> Image.Image:
        w, h = footprint
        image = Image.new("RGBA", (w * 16, h * 16))
        tile = self.tile(124 + self.terrain_offsets["Slab"])
        for y in range(h):
            for x in range(w):
                image.paste(tile, (x * 16, y * 16))
        return image

    def preview(self, target: dict, house: int) -> Image.Image | None:
        name = target["symbol"]
        if target["kind"] == "building":
            entry = next((v for k, v in self.building_entries.items() if k.lower() == name.lower()), None)
            if entry is None or not target["footprint"]:
                return None
            w, h = target["footprint"]
            tiles = self.entries[int(entry)]
            count = w * h
            # Frames 0/1 are construction, frame 2 is the completed structure.
            start = 2 * count if len(tiles) >= 3 * count else 0
            image = Image.new("RGBA", (w * 16, h * 16))
            for n, tile in enumerate(tiles[start:start + count]):
                image.paste(self.tile(tile, house), ((n % w) * 16, (n // w) * 16))
            return image
        if target["kind"] == "tile":
            # Additional spice colors are runtime-tinted, not vanilla art.
            if name not in self.terrain_offsets:
                return None
            offset = self.terrain_offsets[name]
            if name in {"Rock", "Dunes", "Mountain", "Spice", "ThickSpice"}:
                offset += 15
            return self.tile(124 + offset)
        layers = {
            "Tank": ("UNITS2.SHP", [0, 5]), "SiegeTank": ("UNITS2.SHP", [10, 15]),
            "Devastator": ("UNITS2.SHP", [20, 25]), "SonicTank": ("UNITS2.SHP", [0, 30]),
            "Launcher": ("UNITS2.SHP", [0, 35]), "Deviator": ("UNITS2.SHP", [0, 35]),
            "Quad": ("UNITS.SHP", [0]), "Trike": ("UNITS.SHP", [5]),
            "RaiderTrike": ("UNITS.SHP", [5]), "Harvester": ("UNITS.SHP", [10]),
            "MCV": ("UNITS.SHP", [15]), "Carryall": ("UNITS.SHP", [45]),
            "Frigate": ("UNITS.SHP", [60]), "Ornithopter": ("UNITS.SHP", [51]),
            "Soldier": ("UNITS.SHP", [73]), "Trooper": ("UNITS.SHP", [82]),
            "Infantry": ("UNITS.SHP", [91]), "Troopers": ("UNITS.SHP", [103]),
            "Saboteur": ("UNITS.SHP", [63]), "Sandworm": ("UNITS1.SHP", [71]),
        }
        if name not in layers:
            return None
        filename, frames = layers[name]
        data = self.files[filename]
        index = self.decoder.shp_read_index(data)
        images = []
        for n in frames:
            w, h, pixels = self.decoder.shp_get_frame(data, index, n)
            images.append(self.rgba(pixels, (w, h), house))
        result = Image.new("RGBA", (max(i.width for i in images), max(i.height for i in images)))
        for image in images:
            result.alpha_composite(image, ((result.width - image.width) // 2, (result.height - image.height) // 2))
        return result
