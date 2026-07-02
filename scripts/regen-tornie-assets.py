#!/usr/bin/env python3
"""Asset-regeneration scripts for the Tornie mod TODO list.

This script bundles the asset work for items 3, 7+15, 10, 16, 17, 18 of
the user's todo list. Each item needs a real PNG to be hand-edited or
re-derived from a vanilla source, which requires the original PAK / WSA
artwork. The bash commands below are the cookbook — they assume
ImageMagick (`convert`) is installed and the user has the source art
available.

Run from the repo root:
    python3 scripts/regen-tornie-assets.py --dry-run
    python3 scripts/regen-tornie-assets.py --apply

Each step is gated by a flag so the user can run just the parts they
have art for. None of this is automatic — it requires the source art
to be present in `data/source_art/` (which is NOT committed; the user
must drop the files there from their working copy of the game PAKs).
"""

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, "data")
SOURCE_ART = os.path.join(DATA, "source_art")
SCRIPTS = os.path.join(REPO, "scripts")

# Each step is (id, description, command-list, required-source-files)
STEPS = [
    {
        "id": "item3",
        "title": "Regenerate vanilla spice tile sprites and put them in Tornie.PAK",
        "needs": [
            "source_art/vanilla_spice_red.png",
            "source_art/vanilla_spice_green.png",
        ],
        "commands": [
            # Item 3: copy vanilla spice tile art into data/ as the
            # Tornie replacement. The user must extract these from the
            # original PAK/WSA files first.
            "cp {source_art}/vanilla_spice_red.png  {data}/Tornie_SpiceRed.png",
            "cp {source_art}/vanilla_spice_green.png {data}/Tornie_SpiceGreen.png",
            "python3 {scripts}/make-tornie-pak.py",
        ],
    },
    {
        "id": "item7_15",
        "title": "Apply Tornie_AdvancedWindtrap_gfx_two_frames.png (2-frame animation)",
        "needs": [
            "source_art/advanced_windtrap_frame1.png",
            "source_art/advanced_windtrap_frame2.png",
        ],
        "commands": [
            # Combine 2 source frames into a 2-frame vertical strip.
            "convert -append {source_art}/advanced_windtrap_frame1.png "
            "{source_art}/advanced_windtrap_frame2.png "
            "{data}/Tornie_AdvancedWindtrap_gfx.png",
            "python3 {scripts}/make-tornie-pak.py",
        ],
    },
    {
        "id": "item10",
        "title": "Repair Rocket Trike tile sprite (not icon)",
        "needs": [
            "source_art/siege_tank_base.png",   # source for the repair / recolor
        ],
        "commands": [
            # Re-derive the Rocket Trike tile from the base by recoloring
            # and adjusting. The user can also just drop a hand-edited
            # PNG into data/ directly.
            "convert {source_art}/siege_tank_base.png "
            "-modulate 100,80,100 -channel R -level 60%,100% "
            "{data}/RocketTrike.png",
        ],
    },
    {
        "id": "item16",
        "title": "Flame Tank sprite: 5px gap between frames",
        "needs": [
            "source_art/flame_tank.png",
        ],
        "commands": [
            # Add 5px transparent padding between each NUM_ANGLES=8 frame
            # in the source sprite. Assumes the source is 8 frames wide.
            "convert {source_art}/flame_tank.png "
            "-bordercolor transparent -border 0x0 "
            "-splice {w}x0 -gravity North -background none "
            "-extent {tot_w}x{h} "
            "{data}/FlameTank.png",
        ],
    },
    {
        "id": "item17",
        "title": "Elite Siege Tank: copy Siege Tank + gold tint, per-house recolor (red zone untouched)",
        "needs": [
            "source_art/siege_tank_base.png",
        ],
        "commands": [
            # Apply a gold-tint (high R, lower G, low B) to the source,
            # preserving the red zone by keeping a high-R-low-GB band.
            "convert {source_art}/siege_tank_base.png "
            "-modulate 100,140,100 -channel G -level 0%,80% "
            "-channel B -level 0%,70% "
            "{data}/EliteSiegeTank.png",
        ],
    },
    {
        "id": "item18",
        "title": "Remove red stars from Advanced Powerplant (super_power_plant.png)",
        "needs": [
            "source_art/super_power_plant.png",
        ],
        "commands": [
            # The red stars are a small region in the top-center. Mask
            # the red channel and replace with the surrounding green/grey.
            "convert {source_art}/super_power_plant.png "
            "-fuzz 25% -fill transparent -opaque '#FF0000' "
            "{data}/super_power_plant.png",
        ],
    },
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true",
                        help="Actually run the commands. Default: dry-run only.")
    parser.add_argument("--only", help="Run only the step with this id (e.g. item3)")
    args = parser.parse_args()

    vars = {
        "source_art": SOURCE_ART,
        "data": DATA,
        "scripts": SCRIPTS,
        "w": "UNKNOWN",   # user must compute these from their source art
        "h": "UNKNOWN",
        "tot_w": "UNKNOWN",
    }

    for step in STEPS:
        if args.only and step["id"] != args.only:
            continue
        print(f"\n=== {step['id']}: {step['title']} ===")
        missing = [n for n in step["needs"] if not os.path.exists(os.path.join(REPO, n))]
        if missing:
            print(f"  SKIP: missing source art:")
            for m in missing:
                print(f"    - {m}")
            continue
        for cmd in step["commands"]:
            cmd_str = cmd.format(**vars)
            print(f"  $ {cmd_str}")
            if args.apply:
                # The user must fill in {w}/{h}/{tot_w} manually for item16.
                # Skip commands that contain UNKNOWN placeholders.
                if any(f"{{{k}}}" in cmd for k in ("w", "h", "tot_w")):
                    print("    SKIP: requires manual width/height computation")
                    continue
                try:
                    subprocess.run(cmd_str, shell=True, check=True, cwd=REPO)
                except subprocess.CalledProcessError as e:
                    print(f"    FAIL: {e}")
                    sys.exit(1)

    print("\nDone.")

if __name__ == "__main__":
    main()
