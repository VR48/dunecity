"""Validate the focused Dune2R Gravel/Sand visual test map."""

from pathlib import Path
import unittest


MAP_PATH = (Path(__file__).resolve().parents[1] / "data" / "maps" / "multiplayer"
            / "2P - 32x32 - Dune2R Gravel Sand Seam Test.ini")


class TerrainSeamMapTests(unittest.TestCase):
    def test_map_exercises_every_gravel_neighbor_mask(self):
        lines = MAP_PATH.read_text(encoding="ascii").splitlines()
        rows = [line.split("=", 1)[1] for line in lines
                if len(line) >= 4 and line[:3].isdigit() and line[3] == "="]
        self.assertEqual(len(rows), 32)
        self.assertTrue(all(len(row) == 32 for row in rows))
        self.assertEqual(set("".join(rows)), {"-", "%"})

        masks = set()
        for y, row in enumerate(rows):
            for x, terrain in enumerate(row):
                if terrain != "%":
                    continue
                mask = 0
                for bit, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < 32 and 0 <= ny < 32 and rows[ny][nx] == "%":
                        mask |= 1 << bit
                masks.add(mask)

        self.assertEqual(masks, set(range(16)))


if __name__ == "__main__":
    unittest.main()
