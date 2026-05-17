#!/usr/bin/env python3
"""
extract-wsa.py — Extract frames from Dune II WSA animation files as PNGs.

WSA files contain animated building portraits used in the build-menu sidebar.
This script extracts individual frames from WSA files found in a user-supplied
local Dune II data directory.

This script does NOT copy or bundle copyrighted assets into the repository.

Usage:
    python3 scripts/extract-wsa.py /path/to/DUNE2/DATA [--out ./imported_sprites/wsa]
    python3 scripts/extract-wsa.py /path/to/DUNE2/DATA --files WINDTRAP.WSA,BARRAC.WSA

Requirements:
    - Python 3.8+
    - Pillow (pip install Pillow)
    - Local Dune II DATA directory with .WSA files and a palette (IBM.PAL)

WSA format notes:
    WSA files use delta-encoded frames with Format80 compression.
    Frame 0 is the keyframe; subsequent frames are deltas applied to the
    previous frame buffer.  This script delegates to the game's own
    format readers where possible, but also includes a standalone
    Python decoder for portability.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Error: Pillow is required.  Install with:  pip install Pillow")


def load_palette(pal_path: Path) -> list[tuple[int, int, int]]:
    data = pal_path.read_bytes()
    if len(data) < 768:
        sys.exit(f"Error: palette file too small ({len(data)} bytes)")
    palette = []
    for i in range(256):
        r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
        palette.append((r << 2 | r >> 4, g << 2 | g >> 4, b << 2 | b >> 4))
    return palette


# ---------------------------------------------------------------------------
# Format80 decompression (Westwood's LZ variant)
# ---------------------------------------------------------------------------

def decode_format80(src: bytes) -> bytes:
    """Decompress a Format80 (Westwood LZ) compressed block."""
    out = bytearray()
    i = 0
    while i < len(src):
        cmd = src[i]
        i += 1

        if cmd == 0x80:
            break  # end marker

        if (cmd & 0x80) == 0:
            # Short copy from source: 2 bytes
            count = ((cmd & 0x70) >> 4) + 3
            offset = ((cmd & 0x0F) << 8) | src[i]
            i += 1
            start = len(out) - offset
            for j in range(count):
                out.append(out[start + j])

        elif (cmd & 0x40) == 0:
            # Long copy from source
            count = (cmd & 0x3F)
            if count == 0:
                break
            offset = struct.unpack_from('<H', src, i)[0]
            i += 2
            start = len(out) - offset
            for j in range(count):
                out.append(out[start + j])

        elif cmd == 0xFE:
            # Fill: next 2 bytes = count (LE), then 1 byte = fill value
            count = struct.unpack_from('<H', src, i)[0]
            i += 2
            val = src[i]
            i += 1
            out.extend(bytes([val]) * count)

        elif cmd == 0xFF:
            # Absolute copy from source position
            count = struct.unpack_from('<H', src, i)[0]
            i += 2
            offset = struct.unpack_from('<H', src, i)[0]
            i += 2
            for j in range(count):
                out.append(out[offset + j])

        else:
            # Short literal copy
            count = cmd & 0x3F
            out.extend(src[i:i + count])
            i += count

    return bytes(out)


# ---------------------------------------------------------------------------
# Format40 XOR delta decoder
# ---------------------------------------------------------------------------

def decode_format40(src: bytes, dest: bytearray) -> None:
    """Apply a Format40 (XOR delta) patch onto dest buffer in-place."""
    i = 0
    d = 0
    while i < len(src):
        cmd = src[i]
        i += 1

        if (cmd & 0x80) == 0:
            if cmd == 0:
                # Extended command
                count = src[i]
                i += 1
                if count == 0:
                    return  # end marker
                elif (count & 0x80) == 0:
                    d += count  # skip
                else:
                    count = ((count & 0x7F) << 8) | src[i]
                    i += 1
                    d += count  # skip
            else:
                # XOR next `cmd` bytes
                for _ in range(cmd):
                    dest[d] ^= src[i]
                    i += 1
                    d += 1
        else:
            count = cmd & 0x7F
            if count == 0:
                count = struct.unpack_from('<H', src, i)[0]
                i += 2
            val = src[i]
            i += 1
            for _ in range(count):
                dest[d] ^= val
                d += 1


# ---------------------------------------------------------------------------
# WSA file reader
# ---------------------------------------------------------------------------

def extract_wsa_frames(wsa_path: Path, palette: list) -> list[Image.Image]:
    """Extract all frames from a WSA file as PIL Images."""
    data = wsa_path.read_bytes()
    if len(data) < 10:
        return []

    num_frames = struct.unpack_from('<H', data, 0)[0]
    width = struct.unpack_from('<H', data, 2)[0]
    height = struct.unpack_from('<H', data, 4)[0]
    # bytes 6-7: delta buffer size (unused here)
    # bytes 8-9: flags

    if width == 0 or height == 0 or num_frames == 0:
        return []

    frame_size = width * height

    # Offset table: (num_frames + 2) uint32 values starting at offset 10
    # +1 for the "end" offset, +1 for potential palette offset
    offsets = []
    for i in range(num_frames + 2):
        off = struct.unpack_from('<I', data, 10 + i * 4)[0]
        offsets.append(off)

    framebuf = bytearray(frame_size)
    frames = []

    for f in range(num_frames):
        frame_start = offsets[f]
        frame_end = offsets[f + 1]
        if frame_start == 0 or frame_end == 0 or frame_start >= len(data):
            frames.append(None)
            continue

        compressed = data[frame_start:frame_end]

        try:
            decompressed = decode_format80(compressed)
            decode_format40(decompressed, framebuf)
        except (IndexError, struct.error):
            frames.append(None)
            continue

        img = Image.new('RGB', (width, height))
        for y in range(height):
            for x in range(width):
                pixel = framebuf[y * width + x]
                img.putpixel((x, y), palette[pixel])
        frames.append(img)

    return frames


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Extract Dune II WSA animation frames to PNG.')
    parser.add_argument('datadir', type=Path,
                        help='Path to Dune II DATA directory with .WSA files and IBM.PAL')
    parser.add_argument('--out', type=Path, default=Path('./imported_sprites/wsa'),
                        help='Output directory (default: ./imported_sprites/wsa)')
    parser.add_argument('--files', type=str, default=None,
                        help='Comma-separated WSA filenames to extract (default: all .WSA files)')
    parser.add_argument('--scale', type=int, default=1, choices=[1, 2, 3],
                        help='Upscale factor (default: 1)')
    args = parser.parse_args()

    datadir = args.datadir
    outdir = args.out

    # Find palette
    pal_path = None
    for palname in ['IBM.PAL', 'DUNE.PAL', 'BENE.PAL']:
        candidate = None
        for f in datadir.iterdir():
            if f.name.upper() == palname.upper():
                candidate = f
                break
        if candidate:
            pal_path = candidate
            break
    if pal_path is None:
        sys.exit(f"Error: no palette file found in {datadir}")

    palette = load_palette(pal_path)
    outdir.mkdir(parents=True, exist_ok=True)

    # Collect WSA files to process
    if args.files:
        wsa_names = [n.strip() for n in args.files.split(',')]
    else:
        wsa_names = sorted(f.name for f in datadir.iterdir()
                           if f.suffix.upper() == '.WSA')

    if not wsa_names:
        sys.exit(f"No .WSA files found in {datadir}")

    total_frames = 0
    for wsa_name in wsa_names:
        wsa_path = None
        for f in datadir.iterdir():
            if f.name.upper() == wsa_name.upper():
                wsa_path = f
                break
        if wsa_path is None:
            print(f"  SKIP {wsa_name} (not found)")
            continue

        print(f"Extracting {wsa_name}...")
        frames = extract_wsa_frames(wsa_path, palette)
        if not frames:
            print(f"  SKIP {wsa_name} (no frames)")
            continue

        stem = wsa_path.stem.lower()
        for i, frame in enumerate(frames):
            if frame is None:
                continue
            if args.scale > 1:
                frame = frame.resize((frame.width * args.scale, frame.height * args.scale),
                                     Image.NEAREST)
            out_path = outdir / f"{stem}_frame{i:02d}.png"
            frame.save(out_path)
            total_frames += 1

        print(f"  → {len([f for f in frames if f])} frames ({frames[0].width}×{frames[0].height})")

    print(f"\nDone. {total_frames} frames exported to {outdir}/")
    print("These files are for LOCAL USE ONLY — do not commit copyrighted assets to the repo.")


if __name__ == '__main__':
    main()
