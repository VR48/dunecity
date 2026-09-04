import configparser
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
from dune2_atlas_reference import load_helper
publisher = load_helper(REPO / "scripts/publish-dune2r-asset.py")


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.pack = self.root / "refinery"
        self.pack.mkdir()
        Image.new("RGBA", (48, 32), (20, 50, 90, 255)).save(self.pack / "idle.png")
        (self.pack / "building.ini").write_text(
            "[Building]\nItemID=10\nHouseID=1\nFootprintWidth=3\nFootprintHeight=2\n"
            "[State.Idle]\nStill=idle.png\n")

    def tearDown(self):
        self.temp.cleanup()

    def test_dry_run_and_nonvisual_rejection(self):
        self.assertIn("no Git changes", publisher.publish(REPO, self.pack))
        (self.pack / "QuantBot.ini").write_text("not visual")
        with self.assertRaises(ValueError):
            publisher.publish(REPO, self.pack)

    def test_publication_preserves_dirty_worktree_and_index(self):
        checkout, remote = self.root / "checkout", self.root / "remote.git"
        checkout.mkdir()
        def git(*args):
            return subprocess.check_output(["git", *args], cwd=checkout, stderr=subprocess.DEVNULL)
        git("init", "-b", "main")
        git("config", "user.name", "Atlas Test")
        git("config", "user.email", "atlas-test@example.invalid")
        (checkout / "README.txt").write_text("baseline")
        (checkout / "scripts").mkdir()
        shutil.copy2(REPO / "scripts/generate-dune2r-asset-catalog.py", checkout / "scripts")
        git("add", ".")
        git("commit", "-m", "baseline")
        git("init", "--bare", str(remote))
        git("remote", "add", "origin", str(remote))
        git("push", "origin", "main")
        (checkout / "README.txt").write_text("uncommitted user work")
        (checkout / "pending.txt").write_text("staged user work")
        git("add", "pending.txt")
        status, index, head = git("status", "--porcelain"), git("write-tree"), git("rev-parse", "HEAD")
        original = subprocess.check_output
        def command(args, *a, **kw):
            if args[:4] == ["git", "remote", "get-url", "origin"]:
                return "https://github.com/VR48/dunecity.git\n"
            return original(args, *a, **kw)
        # All actual fetch/push operations still target the temporary local bare repo.
        with patch.object(publisher.subprocess, "check_output", side_effect=command):
            revision = publisher.publish(checkout, self.pack, dry_run=False)
        self.assertEqual(git("status", "--porcelain"), status)
        self.assertEqual(git("write-tree"), index)
        self.assertEqual(git("rev-parse", "HEAD"), head)
        catalog = configparser.ConfigParser()
        catalog.read_string(git("show", f"{revision}:mods/Dune2R/asset-catalog.ini").decode())
        art_revision = catalog["Catalog"]["Revision"]
        self.assertEqual(git("rev-parse", revision + "^" ).decode().strip(), art_revision)
        self.assertEqual(catalog["Pack.0"]["Unit"], "refinery")
        files = git("diff-tree", "--no-commit-id", "--name-only", "-r", head.decode().strip(), revision).decode().splitlines()
        self.assertTrue(all(p.startswith("mods/Dune2R/") for p in files))
        self.assertEqual(git("show", f"{revision}:README.txt"), b"baseline")
        duplicate = self.root / "duplicate-refinery"
        shutil.copytree(self.pack, duplicate)
        with patch.object(publisher.subprocess, "check_output", side_effect=command):
            with self.assertRaisesRegex(ValueError, "already owns"):
                publisher.publish(checkout, duplicate, dry_run=False)
            manifest = self.pack / "building.ini"
            manifest.write_text(manifest.read_text().replace("HouseID=1", "HouseID=0"))
            with self.assertRaisesRegex(ValueError, "repurpose"):
                publisher.publish(checkout, self.pack, dry_run=False)
        self.assertEqual(git("rev-parse", "refs/remotes/origin/main").decode().strip(), revision)
        self.assertEqual(git("status", "--porcelain"), status)


if __name__ == "__main__":
    unittest.main()
