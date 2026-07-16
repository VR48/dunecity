#!/usr/bin/env python3
"""
extract-unit-sprite.py — Extract a ground-unit sprite strip from the original
Dune II UNITS.SHP and write it as an RGBA PNG sprite sheet for DuneCity.

This reproduces, in Python, the exact decode path used by the game engine:
  * PAK container index             (src/FileClasses/Pakfile.cpp)
  * SHP index + Format80 (decode80) (src/FileClasses/Shpfile.cpp, Decode.cpp)
  * shpCorrectLF run-length-zero expansion
  * IBM.PAL 6-bit palette            (src/FileClasses/Palfile.cpp)
  * GROUNDUNIT_ROW(i) 8-facing layout (include/FileClasses/GFXManager.h)

It reads the user's own local Dune II data (the .PAK files in ./data) and
writes a single horizontal 8-frame strip.  Palette index 0 (the SHP
background) becomes transparent.  No artwork is committed by this script
itself — it only operates on data the user already supplies locally.

Usage:
    python3 scripts/extract-unit-sprite.py                 # default: Trike -> data/RocketTrike.png
    python3 scripts/extract-unit-sprite.py --base-frame 5 --out data/RocketTrike.png
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Error: Pillow is required.  Install with:  pip install Pillow")

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "data"

# ---- GROUNDUNIT_ROW(i) from include/FileClasses/GFXManager.h ---------------
# (i+2)|NORMAL, (i+1)|NORMAL, i|NORMAL, (i+1)|FLIPV, (i+2)|FLIPV, (i+3)|FLIPV,
# (i+4)|NORMAL, (i+3)|NORMAL
TILE_NORMAL, TILE_FLIPH, TILE_FLIPV, TILE_ROTATE = 0, 1, 2, 3


def ground_unit_row(i):
    return [
        (i + 2, TILE_NORMAL), (i + 1, TILE_NORMAL), (i + 0, TILE_NORMAL),
        (i + 1, TILE_FLIPV),  (i + 2, TILE_FLIPV),  (i + 3, TILE_FLIPV),
        (i + 4, TILE_NORMAL), (i + 3, TILE_NORMAL),
    ]


# ---- PAK container (Pakfile.cpp) -------------------------------------------
def read_pak_index(buf):
    entries = []  # (name, start, end)
    pos = 0
    prev = None
    while True:
        (start,) = struct.unpack_from("<I", buf, pos)
        pos += 4
        if start == 0:
            break
        name = b""
        while buf[pos] != 0:
            name += bytes([buf[pos]])
            pos += 1
        pos += 1  # skip NUL
        if prev is not None:
            entries[prev] = (entries[prev][0], entries[prev][1], start - 1)
        entries.append((name.decode("ascii", "replace"), start, 0))
        prev = len(entries) - 1
    if entries:
        entries[-1] = (entries[-1][0], entries[-1][1], len(buf) - 1)
    return entries


def pak_extract(buf, filename):
    for name, start, end in read_pak_index(buf):
        if name.upper() == filename.upper():
            return buf[start:end + 1]
    return None


# ---- IBM.PAL (Palfile.cpp) -------------------------------------------------
def load_palette(buf):
    pal = []
    for i in range(len(buf) // 3):
        r, g, b = buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]
        pal.append((int(r * 255.0 / 63.0), int(g * 255.0 / 63.0), int(b * 255.0 / 63.0)))
    return pal


# ---- Format80 decode (Decode.cpp::decode80) --------------------------------
def decode80(data, out_size):
    out = bytearray()
    rp = 0
    while True:
        b0 = data[rp]
        if (b0 & 0xc0) == 0x80:           # 10cccccc (1)
            count = b0 & 0x3f
            rp += 1
            if count == 0:
                break
            out += data[rp:rp + count]
            rp += count
        elif (b0 & 0x80) == 0x00:         # 0cccpppp p (2)
            count = ((b0 & 0x70) >> 4) + 3
            relpos = ((b0 & 0x0f) << 8) | data[rp + 1]
            rp += 2
            for _ in range(count):
                out.append(out[len(out) - relpos])
        elif b0 == 0xff:                  # 11111111 c c p p (5)
            (count,) = struct.unpack_from("<H", data, rp + 1)
            (pos,) = struct.unpack_from("<H", data, rp + 3)
            rp += 5
            for k in range(count):
                out.append(out[pos + k])
        elif b0 == 0xfe:                  # 11111110 c c v (4)
            (count,) = struct.unpack_from("<H", data, rp + 1)
            color = data[rp + 3]
            rp += 4
            out += bytes([color]) * count
        elif (b0 & 0xc0) == 0xc0:         # 11cccccc p p (3)
            count = (b0 & 0x3f) + 3
            (pos,) = struct.unpack_from("<H", data, rp + 1)
            rp += 3
            for k in range(count):
                out.append(out[pos + k])
        else:
            raise ValueError("unknown format80 command 0x%02x" % b0)
    return bytes(out[:out_size]) if out_size else bytes(out)


def shp_correct_lf(data):
    """Expand run-length zeros (Shpfile.cpp::shpCorrectLF)."""
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        val = data[i]
        i += 1
        if val != 0:
            out.append(val)
        else:
            if i >= n:
                break
            count = data[i]
            i += 1
            if count == 0:
                break
            out += b"\x00" * count
    return bytes(out)


# ---- SHP index (Shpfile.cpp::readIndex) ------------------------------------
def shp_read_index(buf):
    num = struct.unpack_from("<H", buf, 0)[0]
    entries = []  # (start, end)
    has_2byte = struct.unpack_from("<H", buf, 4)[0] != 0
    if has_2byte:
        offs = [struct.unpack_from("<H", buf, 2 + i * 2)[0] for i in range(num)]
        last = struct.unpack_from("<H", buf, 2 + num * 2)[0] - 1 + 2
    else:
        offs = [struct.unpack_from("<I", buf, 2 + i * 4)[0] + 2 for i in range(num)]
        last = struct.unpack_from("<H", buf, 2 + num * 4)[0] - 1 + 2
    for i in range(num):
        start = offs[i]
        end = (offs[i + 1] - 1) if i + 1 < num else last
        entries.append((start, end))
    return entries


def shp_get_frame(buf, entries, index):
    start = entries[index][0]
    hdr = buf[start:]
    typ = hdr[0]
    sizeY = hdr[2]
    sizeX = hdr[3]
    size = struct.unpack_from("<H", hdr, 8)[0]
    if typ == 0:
        img = shp_correct_lf(decode80(hdr[10:], size))
    elif typ == 1:
        img = shp_correct_lf(decode80(hdr[10 + 16:], size))
        img = bytes(hdr[10 + d] for d in img)  # applyPalOffsets
    elif typ == 2:
        img = shp_correct_lf(hdr[10:])
    elif typ == 3:
        img = shp_correct_lf(hdr[10 + 16:])
        img = bytes(hdr[10 + d] for d in img)
    else:
        raise ValueError("unsupported SHP frame type %d" % typ)
    img = (img + b"\x00" * (sizeX * sizeY))[: sizeX * sizeY]
    return sizeX, sizeY, img


def flip(img, w, h, mode):
    rows = [img[y * w:(y + 1) * w] for y in range(h)]
    if mode == TILE_NORMAL:
        pass
    elif mode == TILE_FLIPH:          # mirror vertically (engine naming)
        rows = rows[::-1]
    elif mode == TILE_FLIPV:          # mirror horizontally
        rows = [r[::-1] for r in rows]
    elif mode == TILE_ROTATE:
        rows = [r[::-1] for r in rows[::-1]]
    return b"".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shp", default="UNITS.SHP")
    ap.add_argument("--base-frame", type=int, default=5,
                    help="GROUNDUNIT_ROW base frame index (Trike=5)")
    ap.add_argument("--out", default=str(DATA / "RocketTrike.png"))
    ap.add_argument("--data", default=str(DATA))
    args = ap.parse_args()

    data_dir = Path(args.data)
    # locate palette + shp inside the PAK archives
    pal = None
    shp = None
    for pak in sorted(data_dir.glob("*.PAK")):
        buf = pak.read_bytes()
        try:
            entries = read_pak_index(buf)
        except Exception:
            continue
        names = {n.upper() for n, _, _ in entries}
        if pal is None and "IBM.PAL" in names:
            pal = load_palette(pak_extract(buf, "IBM.PAL"))
        if shp is None and args.shp.upper() in names:
            shp = pak_extract(buf, args.shp)
        if pal and shp:
            break
    if pal is None:
        sys.exit("Error: IBM.PAL not found in any %s/*.PAK" % data_dir)
    if shp is None:
        sys.exit("Error: %s not found in any %s/*.PAK" % (args.shp, data_dir))

    entries = shp_read_index(shp)
    layout = ground_unit_row(args.base_frame)

    # all frames share size; read the first for dimensions
    w, h, _ = shp_get_frame(shp, entries, layout[0][0])
    strip = Image.new("RGBA", (w * len(layout), h), (0, 0, 0, 0))
    for col, (idx, mode) in enumerate(layout):
        fw, fh, img = shp_get_frame(shp, entries, idx)
        img = flip(img, fw, fh, mode)
        frame = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
        px = frame.load()
        for y in range(fh):
            for x in range(fw):
                p = img[y * fw + x]
                if p == 0:                # SHP background -> transparent
                    continue
                r, g, b = pal[p]
                px[x, y] = (r, g, b, 255)
        strip.paste(frame, (col * w, 0))

    out = Path(args.out)
    strip.save(out)
    print("Wrote %s  (%dx%d, %d frames, base frame %d from %s)"
          % (out, strip.width, strip.height, len(layout), args.base_frame, args.shp))


if __name__ == "__main__":
    main()
