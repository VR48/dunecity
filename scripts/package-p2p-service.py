#!/usr/bin/env python3
"""Copy the reviewed PHP service into the website's private deployment source tree."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
source = root / "tools/p2p-signaling"
args.output.mkdir(parents=True, exist_ok=True)
for directory in ("src", "public"):
    destination = args.output / directory
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source / directory, destination)
files = {str(p.relative_to(args.output)): hashlib.sha256(p.read_bytes()).hexdigest()
         for directory in ("src", "public") for p in sorted((args.output / directory).rglob("*"))
         if p.is_file()}
manifest = {"repository": "https://github.com/VR48/dunecity",
            "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "sha256": files}
(args.output / "SOURCE.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Packaged {len(files)} signaling files from {manifest['commit']}")
