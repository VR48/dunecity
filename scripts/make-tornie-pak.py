#!/usr/bin/env python3
"""Build data/Tornie.PAK from loose PNG files in data/.

PAK format (Dune Legacy / Westwood):
  Header:  [uint32_le offset, null-terminated name] x N, then uint32_le 0
  Body:    raw file data concatenated in entry order
Offsets are absolute byte positions in the file.
"""

import struct, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, "data")
OUT  = os.path.join(DATA, "Tornie.PAK")

# DuneCity 1.0.341: canonical Tornie mod bundle minus 8th-house
# resources. After Tornie's repeated confirmation that the green-grid
# bug never goes away for HOUSE_REBELS regardless of how many (R)
# entries we mirror or what code paths we restore, this release
# reverts the Rebels-side mod assets: no HeraldRebels.png, no
# HeraldRebelsMask.png, no REGIONR.INI, no scenr*.ini entries in the
# PAK. Only the Neutral (REGIONN / scenn00X) campaign set is shipped.
# The 8th house is defined purely by include/DataTypes.h:311
# (HOUSE_REBELS = 7) plus houseChar[] and houseToPaletteIndex[] in
# include/globals.h. Custom_IBM.pal is kept intact at the
# data/ and mods/Tornie/data/ paths so the dark grey/black range
# still drives the in-game tint when the faction is loaded by the
# vanilla code path.
ENTRIES = [
    "EliteSiegeTankIcon.png",
    "EliteSiegeTank.png",
    "FlameTank.png",
    "HeraldRebels.png",
    "HeraldRebelsMask.png",
    "HeraldNeu.png",
    "HeraldNeuMask.png",
    "NeutralLauncherIcon.png",
    "PalaceTrikeAndQuadIcon.png",
    "PalaceRebelsIcon.png",
    "REGIONN.INI",
    "REGIONR.INI",
    "RREBELS.voc",
    "scenr001.ini",
    "scenr002.ini",
    "scenr003.ini",
    "scenr004.ini",
    "scenr005.ini",
    "scenr006.ini",
    "scenr007.ini",
    "scenr008.ini",
    "scenr009.ini",
    "scenr010.ini",
    "scenr011.ini",
    "scenr012.ini",
    "scenr013.ini",
    "scenr014.ini",
    "scenr015.ini",
    "scenr016.ini",
    "scenr017.ini",
    "scenr018.ini",
    "scenr019.ini",
    "scenr020.ini",
    "scenr021.ini",
    "scenr022.ini",
    "RocketTrike.png",
    "RocketTrikeIcon.png",
    "RocketTrikeIconMask.png",
    "RocketTrikeMask.png",
    "QuadIcon.png",
    "Tornie_AdvHouseFlag.png",
    "Tornie_AdvancedWindtrap_gfx.png",
    "Tornie_AdvancedWindtrap_gfx_two_frames.png",
    "Tornie_AdvancedWindtrap_icon.png",
    "Tornie_CornerFlag.png",
    "Tornie_SpiceGreen.png",
    "Tornie_SpiceRed.png",
    "PaulAtreidesEyes.png",
    "PaulAtreidesMentat.png",
    "PaulAtreidesMouth.png",
    "super_power_plant.png",
    "MENTATA.CPS",
    "MENTATH.CPS",
    "MENTATM.CPS",
    "MENTATO.CPS",
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
] # NOTE: entries above must exist on disk for build.

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

        # write the body
        for blob in blobs:
            f.write(blob)

    print(f"Wrote {os.path.getsize(OUT)} bytes to {OUT}")
    print(f"  {len(ENTRIES)} entries packed")

if __name__ == "__main__":
    main()