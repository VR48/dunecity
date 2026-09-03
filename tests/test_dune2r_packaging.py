"""Atlas layout regression tests, including the rectangular Refinery failure."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import configparser
from types import SimpleNamespace

from PIL import Image, ImageChops

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("packager", SCRIPTS / "package-dune2r-unit.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class AtlasTests(unittest.TestCase):
    def test_rectangular_and_square_frames_roundtrip_without_clipping(self):
        for size in [(57, 61), (57, 53), (32, 32)]:
            with self.subTest(size=size), tempfile.TemporaryDirectory() as directory:
                frames = [Image.new("RGBA", size, (i * 19, 200, 30, 255)) for i in range(7)]
                for frame in frames:
                    frame.putpixel((0, size[1] - 1), (255, 0, 255, 255))
                path = Path(directory) / "atlas.png"
                columns, rows = packager.write_atlas(frames, path, 3)
                with Image.open(path) as atlas:
                    self.assertEqual(atlas.size, (columns * size[0], rows * size[1]))
                    for i, frame in enumerate(frames):
                        x, y = (i % columns) * size[0], (i // columns) * size[1]
                        restored = atlas.crop((x, y, x + size[0], y + size[1]))
                        self.assertIsNone(ImageChops.difference(frame, restored).getbbox(alpha_only=False))

    def test_chunk_metadata_matches_real_png_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            frames = [Image.new("RGBA", (576, 603), (i, 20, 30, 255)) for i in range(21)]
            chunks = packager.write_chunked_atlases(frames, Path(directory), Path("atlases"))
            covered = 0
            for chunk in chunks:
                self.assertEqual(chunk["first"], covered)
                covered += chunk["frames"]
                with Image.open(Path(directory) / chunk["path"]) as atlas:
                    self.assertEqual(atlas.size, (chunk["columns"] * 576, chunk["rows"] * 603))
                    self.assertLessEqual(max(atlas.size), 2048)
            self.assertEqual(covered, len(frames))

    def test_reject_mismatched_or_oversized_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                packager.write_atlas([Image.new("RGBA", (3, 4)), Image.new("RGBA", (4, 3))],
                                     Path(directory) / "invalid.png", 2)
            with self.assertRaises(ValueError):
                packager.write_chunked_atlases([Image.new("RGBA", (2049, 2))],
                                              Path(directory), Path("atlases"))

    def test_building_keeps_authored_still_alongside_animation_and_sprite_only_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "frames").mkdir()
            Image.new("RGBA", (30, 42), "blue").save(root / "frames/frame_0000.png")
            Image.new("RGBA", (30, 42), "green").save(root / "frames/frame_0001.png")
            Image.new("RGBA", (30, 42), "red").save(root / "sprite.png")
            sprite = {"file": "sprite.png"}
            metadata = {"slug": "refinery", "render_profile": {"logical_footprint_tiles": [3, 2]},
                        "categories": {
                            "building_idle": {"states": {"default": {"assets": {
                                "sprite": sprite, "animation": {"frames_dir": "frames", "frame_duration_ms": 42}}}}},
                            "building_active": {"states": {"default": {"assets": {"sprite": sprite}}}}}}
            output = root / "pack"
            args = SimpleNamespace(frame_size=16, item_id=10, house_id=1, source_unit=root)
            self.assertEqual(packager.package_building(metadata, root, output, args), 2)
            manifest = configparser.ConfigParser()
            manifest.read(output / "building.ini")
            self.assertEqual(manifest["Building"]["HouseID"], "1")
            self.assertEqual(manifest["State.Idle"]["Frames"], "2")
            self.assertEqual(manifest["State.Working"]["Frames"], "1")
            with Image.open(output / manifest["State.Idle"]["Still"]) as still:
                self.assertEqual(still.getpixel((0, 0)), (255, 0, 0, 255))
            with Image.open(output / manifest["State.Idle"]["Atlas.0"]) as atlas:
                self.assertEqual(atlas.getpixel((0, 0)), (0, 0, 255, 255))


if __name__ == "__main__":
    unittest.main()
