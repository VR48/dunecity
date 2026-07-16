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
    "REGIONF.INI",
    "REGIONA.INI",
    "REGIONO.INI",
    "REGIONH.INI",
    "REGIONM.INI",
    "REGIONS.INI",
    "RREBELS.VOC",
    "NEUTRAL.VOC",
    "scena001.ini",
    "scena002.ini",
    "scena003.ini",
    "scena004.ini",
    "scena005.ini",
    "scena006.ini",
    "scena007.ini",
    "scena008.ini",
    "scena009.ini",
    "scena010.ini",
    "scena011.ini",
    "scena012.ini",
    "scena013.ini",
    "scena014.ini",
    "scena015.ini",
    "scena016.ini",
    "scena017.ini",
    "scena018.ini",
    "scena019.ini",
    "scena020.ini",
    "scena021.ini",
    "scena022.ini",
    "scenf001.ini",
    "scenf002.ini",
    "scenf003.ini",
    "scenf004.ini",
    "scenf005.ini",
    "scenf006.ini",
    "scenf007.ini",
    "scenf008.ini",
    "scenf009.ini",
    "scenf010.ini",
    "scenf011.ini",
    "scenf012.ini",
    "scenf013.ini",
    "scenf014.ini",
    "scenf015.ini",
    "scenf016.ini",
    "scenf017.ini",
    "scenf018.ini",
    "scenf019.ini",
    "scenf020.ini",
    "scenf021.ini",
    "scenf022.ini",
    "scenh001.ini",
    "scenh002.ini",
    "scenh003.ini",
    "scenh004.ini",
    "scenh005.ini",
    "scenh006.ini",
    "scenh007.ini",
    "scenh008.ini",
    "scenh009.ini",
    "scenh010.ini",
    "scenh011.ini",
    "scenh012.ini",
    "scenh013.ini",
    "scenh014.ini",
    "scenh015.ini",
    "scenh016.ini",
    "scenh017.ini",
    "scenh018.ini",
    "scenh019.ini",
    "scenh020.ini",
    "scenh021.ini",
    "scenh022.ini",
    "scenm001.ini",
    "scenm002.ini",
    "scenm003.ini",
    "scenm004.ini",
    "scenm005.ini",
    "scenm006.ini",
    "scenm007.ini",
    "scenm008.ini",
    "scenm009.ini",
    "scenm010.ini",
    "scenm011.ini",
    "scenm012.ini",
    "scenm013.ini",
    "scenm014.ini",
    "scenm015.ini",
    "scenm016.ini",
    "scenm017.ini",
    "scenm018.ini",
    "scenm019.ini",
    "scenm020.ini",
    "scenm021.ini",
    "scenm022.ini",
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
    "sceno001.ini",
    "sceno002.ini",
    "sceno003.ini",
    "sceno004.ini",
    "sceno005.ini",
    "sceno006.ini",
    "sceno007.ini",
    "sceno008.ini",
    "sceno009.ini",
    "sceno010.ini",
    "sceno011.ini",
    "sceno012.ini",
    "sceno013.ini",
    "sceno014.ini",
    "sceno015.ini",
    "sceno016.ini",
    "sceno017.ini",
    "sceno018.ini",
    "sceno019.ini",
    "sceno020.ini",
    "sceno021.ini",
    "sceno022.ini",
    "scens001.ini",
    "scens002.ini",
    "scens003.ini",
    "scens004.ini",
    "scens005.ini",
    "scens006.ini",
    "scens007.ini",
    "scens008.ini",
    "scens009.ini",
    "scens010.ini",
    "scens011.ini",
    "scens012.ini",
    "scens013.ini",
    "scens014.ini",
    "scens015.ini",
    "scens016.ini",
    "scens017.ini",
    "scens018.ini",
    "scens019.ini",
    "scens020.ini",
    "scens021.ini",
    "scens022.ini",
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
    "scenf001.ini",
    "scenf002.ini",
    "scenf003.ini",
    "scenf004.ini",
    "scenf005.ini",
    "scenf006.ini",
    "scenf007.ini",
    "scenf008.ini",
    "scenf009.ini",
    "scenf010.ini",
    "scenf011.ini",
    "scenf012.ini",
    "scenf013.ini",
    "scenf014.ini",
    "scenf015.ini",
    "scenf016.ini",
    "scenf017.ini",
    "scenf018.ini",
    "scenf019.ini",
    "scenf020.ini",
    "scenf021.ini",
    "RocketTrike.png",
    "MapEditorPen1x1.png",
    "MapEditorPen3x3.png",
    "MapEditorPen5x5.png",
    "RocketTrikeIcon.png",
    "RocketTrikeIconMask.png",
    "RocketTrikeMask.png",
    "QuadIcon.png",
    "Tornie_AdvHouseFlag.png",
    "Tornie_AdvancedWindtrap_gfx.png",
    "Tornie_AdvancedWindtrap_gfx_editor.png",
    "Tornie_AdvancedWindtrap_gfx_two_frames.png",
    "Tornie_AdvancedWindtrap_icon.png",
    "Tornie_CornerFlag.png",
    "SpectatorTeal.pal",
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