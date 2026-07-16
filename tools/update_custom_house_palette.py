#!/usr/bin/env python3
"""Update the six visual-only house ramps in Custom_IBM.PAL.

Dune II PAL components are six-bit values. Keeping every component in the
0..63 range is important because the runtime expands them to 0..255.
"""

from pathlib import Path


RAMPS = {
    144: (
        (42, 22, 58), (35, 17, 51), (29, 12, 44), (23, 8, 37),
        (17, 5, 30), (12, 3, 23), (7, 1, 16),
    ),
    160: (
        (63, 22, 40), (58, 17, 36), (52, 13, 32), (46, 9, 28),
        (39, 6, 23), (31, 3, 18), (22, 1, 12),
    ),
    176: (
        (8, 63, 61), (6, 56, 55), (4, 49, 49), (3, 42, 43),
        (2, 35, 36), (1, 27, 29), (0, 19, 21),
    ),
    192: (
        (63, 63, 23), (62, 58, 17), (58, 52, 12), (53, 45, 8),
        (47, 38, 5), (39, 30, 3), (30, 22, 1),
    ),
    208: (
        (8, 28, 11), (7, 24, 9), (6, 20, 8), (5, 17, 7),
        (4, 14, 6), (3, 11, 4), (2, 8, 3),
    ),
    224: (
        (63, 47, 55), (61, 41, 51), (58, 35, 47), (54, 29, 42),
        (49, 23, 37), (42, 17, 31), (34, 11, 24),
    ),
}

PALETTE_PATHS = (
    Path("data/Custom_IBM.PAL"),
    Path("mods/Tornie/data/Custom_IBM.PAL"),
)


def update_palette(path: Path) -> None:
    data = bytearray(path.read_bytes())
    if len(data) != 256 * 3:
        raise ValueError(f"{path}: expected 768 bytes, found {len(data)}")

    for start, colors in RAMPS.items():
        for offset, color in enumerate(colors):
            if any(component < 0 or component > 63 for component in color):
                raise ValueError(f"{path}: invalid six-bit color {color}")
            index = (start + offset) * 3
            data[index:index + 3] = bytes(color)

    path.write_bytes(data)
    print(f"updated {path}")


def main() -> None:
    for path in PALETTE_PATHS:
        update_palette(path)


if __name__ == "__main__":
    main()
