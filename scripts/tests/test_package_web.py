import importlib.util
from pathlib import Path
import tempfile
import unittest
from html.parser import HTMLParser
import json
import hashlib

spec = importlib.util.spec_from_file_location('package_web', Path(__file__).resolve().parents[1] / 'package-web.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_versions_minified_and_quoted_html_and_hashes_final_files(self):
        for quote in ('', '"', "'"):
            with self.subTest(quote=quote), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                output = root / 'build/bin'
                output.mkdir(parents=True)
                (output / 'dunecity.html').write_text(f'<link href={quote}shell.css{quote} rel=stylesheet><script src={quote}shell.js{quote}></script><script src={quote}dunecity.js{quote} async></script>')
                for name in ('dunecity.js','dunecity.wasm','dunecity.data'):
                    (output / name).write_bytes(b'test artifact')
                packager.package(root/'build', root/'play')
                html = (root/'play/index.html').read_text()
                self.assertEqual(html.count('?v='),3)
                manifest = json.loads((root/'play/build.json').read_text())
                for name, digest in manifest['sha256'].items():
                    self.assertEqual(hashlib.sha256((root/'play'/name).read_bytes()).hexdigest(),digest)


if __name__ == '__main__': unittest.main()
