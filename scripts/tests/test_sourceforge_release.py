import json
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sf', Path(__file__).parents[1]/'sourceforge-release.py')
sf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sf)

class ReleaseTests(unittest.TestCase):
    def release(self):
        return {'tag_name':'v1.0.612','draft':False,'prerelease':False,
                'assets':[{'name':n,'size':1} for n in sf.expected_files('v1.0.612')]}

    def test_stable_tags_only(self):
        for tag in ['latest-dev','v1.0.612-rc1','v1.0.612;echo test','../bad','v1.0.612\n']:
            with self.assertRaises(ValueError): sf.version(tag)

    def test_all_platforms_required(self):
        release=self.release()
        self.assertEqual(len(sf.validate_release(release,'v1.0.612')),6)
        release['assets'].pop(1)
        with self.assertRaises(ValueError): sf.validate_release(release,'v1.0.612')

    def test_no_draft_or_prerelease(self):
        for field in ['draft','prerelease']:
            release=self.release();release[field]=True
            with self.assertRaises(ValueError): sf.validate_release(release,'v1.0.612')

    def test_corruption_and_unsafe_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);file=root/'asset.zip';file.write_bytes(b'good')
            expected={'asset.zip':sf.digest(file)}
            sf.verify_files(root,expected)
            file.write_bytes(b'bad')
            with self.assertRaises(ValueError): sf.verify_files(root,expected)
            with self.assertRaises(ValueError): sf.verify_files(root,{'../outside':'bad'})

    def test_failed_readback_never_pushes_source_or_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'asset.zip').write_bytes(b'good')
            with patch.dict(sf.os.environ, {'SOURCEFORGE_USER':'release-user',
                    'SOURCEFORGE_API_KEY':'test', 'GIT_SSH_COMMAND':'ssh'}), \
                 patch.object(sf.subprocess, 'run') as command, \
                 patch.object(sf.urllib.request, 'urlopen') as request:
                with self.assertRaises(FileNotFoundError):
                    sf.publish('v1.0.612', root, 'abc')
                self.assertEqual([c.args[0][0] for c in command.call_args_list], ['rsync','rsync'])
                request.assert_not_called()

    def test_historical_backfill_preserves_branch_and_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.dict(sf.os.environ, {'SOURCEFORGE_USER':'release-user',
                    'SOURCEFORGE_API_KEY':'test', 'GIT_SSH_COMMAND':'ssh'}), \
                 patch.object(sf.subprocess, 'run') as command, \
                 patch.object(sf, 'run', return_value='{"tag_name":"v1.0.613"}'), \
                 patch.object(sf.urllib.request, 'urlopen') as request:
                sf.publish('v1.0.612', Path(tmp), 'abc')
                pushes=[c.args[0] for c in command.call_args_list if c.args[0][0]=='git']
                self.assertEqual(len(pushes), 1)
                self.assertEqual(pushes[0][-1], 'refs/tags/v1.0.612:refs/tags/dunecity-v1.0.612')
                request.assert_not_called()

    def test_preparation_excludes_source_archive_and_links_tagged_repo(self):
        release=self.release()
        release['body']='Release notes'
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'release'
            def fake_run(*args, **kwargs):
                if args[:2]==('gh','api'): return json.dumps(release)
                if args[:2]==('git','rev-parse'): return 'abc123'
                if args[:3]==('gh','release','download'):
                    name=args[args.index('--pattern')+1]
                    (output/name).write_bytes(b'x')
                return ''
            with patch.object(sf,'run',side_effect=fake_run), \
                 patch.object(sf.subprocess,'run') as command:
                sf.prepare('v1.0.612',output)
                command.assert_not_called()
            self.assertEqual({p.name for p in output.iterdir()},
                set(sf.expected_files('v1.0.612'))|{'README.md','SHA256SUMS'})
            self.assertIn('https://github.com/VR48/dunecity/tree/v1.0.612',
                (output/'README.md').read_text())
            self.assertNotIn('-source.tar.gz',(output/'SHA256SUMS').read_text())
