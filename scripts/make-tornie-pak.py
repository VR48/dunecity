#!/usr/bin/env python3
"""Build data/Tornie.PAK from loose PNG files in data/.

PAK format (Dune Legacy / Westwood):
  Header:  [uint32_le offset, null-terminated name] × N, then uint32_le 0
  Body:    raw file data concatenated in entry order
Offsets are absolute byte positions in the file.
"""

import struct, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, "data")
OUT  = os.path.join(DATA, "Tornie.PAK")

ENTRIES = [
    "FlameTank.png",
    "FlameTankIcon.png",
    "HeraldNeu.png",
    "HeraldNeuMask.png",
    "NeutralLauncherIcon.png",
    "PalaceTrikeAndQuadIcon.png",
    "REGIONN.INI",
    "RocketTrikeIcon.png",
    "RocketTrikeMask.png",
    "Tornie_AdvancedWindtrap_gfx.png",
    "Tornie_AdvancedWindtrap_icon.png",
    "Tornie_SpiceRed.png",
    "Tornie_SpiceGreen.png",
    "PaulAtreidesMentat.png",
    "PaulAtreidesEyes.png",
    "PaulAtreidesMouth.png",
    "scenn001.ini",
    "scenn002.ini",
    "scenn003.ini",
    "scenn004.ini",
    "scenn005.ini",
    "scenn006.ini",
    "scenn007.ini",
    "scenn008.ini",
    "scenn009.ini",
    "scenn010.ini",
    "scenn011.ini",
    "scenn012.ini",
    "scenn013.ini",
    "scenn014.ini",
    "scenn015.ini",
    "scenn016.ini",
    "scenn017.ini",
    "scenn018.ini",
    "scenn019.ini",
    "scenn020.ini",
    "scenn021.ini",
    "scenn022.ini",
]

def main():
    blobs = []
    for name in ENTRIES:
        path = os.path.join(DATA, name)
        if not os.path.isfile(path):
            print(f"ERROR: missing {path}", file=sys.stderr)
            sys.exit(1)
        blobs.append(open(path, "rb").read())

    # header size: per entry 4 bytes offset + len(name)+1 null, plus 4 byte terminator
    header_size = sum(4 + len(n) + 1 for n in ENTRIES) + 4

    # compute absolute offsets
    data_offset = 0
    offsets = []
    for blob in blobs:
        offsets.append(header_size + data_offset)
        data_offset += len(blob)

    with open(OUT, "wb") as f:
        for name, off in zip(ENTRIES, offsets):
            f.write(struct.pack("<I", off))
            f.write(name.encode("ascii") + b"\x00")
        f.write(struct.pack("<I", 0))  # terminator
        for blob in blobs:
            f.write(blob)

    print(f"Wrote {os.path.getsize(OUT)} bytes to {OUT}")
    print(f"  {len(ENTRIES)} entries packed")

if __name__ == "__main__":
    main()
