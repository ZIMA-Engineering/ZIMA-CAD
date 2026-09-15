"""Source provenance and archive rejection tests; no signing credentials needed."""
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile
import package

class PackageTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'Windows environment contract')
    def test_clean_environment(self):
        runtime = Path('C:/candidate/windows/2026091501')
        environment = package.clean_environment(runtime)
        self.assertEqual(environment['PATH'].split(os.pathsep)[0], str(runtime))
        self.assertIn(str(Path(os.environ['SystemRoot']) / 'System32'), environment['PATH'])
        self.assertFalse(any(k.upper().startswith(('QT_', 'QML', 'CSF_')) for k in environment))

    def test_build_ids(self):
        self.assertEqual(package.version_id('2024022901'), '2024022901')
        for value in ('2026022901', '2026091500', '../2026091501', '20260915', '2026130101'):
            with self.subTest(value=value), self.assertRaises(ValueError): package.version_id(value)

    def test_unsafe_names(self):
        for value in ('/abs', '../file', 'folder/../file', 'C:/file', 'a\\b', 'a/NUL.txt',
                      'con', 'x/COM¹', 'x/a.', 'x/a ', 'a' * (package.MAX_MEMBER + 1)):
            with self.subTest(value=value), self.assertRaises(ValueError): package.safe_name(value)

    def test_export_uses_committed_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); repo = root / 'repo'; repo.mkdir()
            package.git(repo, 'init', '-q')
            package.git(repo, 'config', 'user.name', 'Packaging test')
            package.git(repo, 'config', 'user.email', 'test@example.invalid')
            (repo / 'tracked.txt').write_text('committed', encoding='utf-8')
            package.git(repo, 'add', 'tracked.txt'); package.git(repo, 'commit', '-qm', 'fixture')
            (repo / 'tracked.txt').write_text('dirty', encoding='utf-8')
            (repo / 'private.txt').write_text('not source', encoding='utf-8')
            package.export_source(repo, 'HEAD', root / 'export')
            self.assertEqual((root / 'export/tracked.txt').read_text(), 'committed')
            self.assertFalse((root / 'export/private.txt').exists())

    def test_archive_hash_and_extraction(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); content = root / 'content'; content.mkdir()
            (content / 'file.txt').write_text('verified', encoding='utf-8')
            checksums = package.inventory(content); archive = root / 'good.zip'
            with zipfile.ZipFile(archive, 'w') as zipped:
                zipped.write(content / 'file.txt', 'ZIMA-CAD/file.txt')
                zipped.writestr('ZIMA-CAD/checksums.json', json.dumps(checksums))
            package.validate_archive(archive, root / 'extracted')
            self.assertEqual((root / 'extracted/ZIMA-CAD/file.txt').read_text(), 'verified')
            with self.assertRaises(ValueError): package.validate_archive(archive, root / 'extracted')
            for bad_name, bad_bytes in [('ZIMA-CAD/file.txt', b'changed'), ('ZIMA-CAD/../escape.txt', b'x')]:
                bad = root / 'bad.zip'
                with zipfile.ZipFile(bad, 'w') as zipped:
                    zipped.writestr(bad_name, bad_bytes)
                    zipped.writestr('ZIMA-CAD/checksums.json', json.dumps(checksums))
                with self.assertRaises(ValueError): package.validate_archive(bad)

    def test_reuse_build_preserves_source_and_rejects_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); repo = root / 'repo'; repo.mkdir()
            package.git(repo, 'init', '-q'); package.git(repo, 'config', 'user.name', 'Packaging test')
            package.git(repo, 'config', 'user.email', 'test@example.invalid')
            for name in ('change.txt', 'stable.txt'): (repo / name).write_text('old', encoding='utf-8')
            package.git(repo, 'add', '.'); package.git(repo, 'commit', '-qm', 'old')
            previous = package.git(repo, 'rev-parse', 'HEAD').decode().strip()
            source = root / 'source'; package.export_source(repo, previous, source)
            stamp = (source / 'stable.txt').stat().st_mtime_ns
            (repo / 'change.txt').write_text('new', encoding='utf-8')
            package.git(repo, 'add', '.'); package.git(repo, 'commit', '-qm', 'new')
            current = package.git(repo, 'rev-parse', 'HEAD').decode().strip()
            package.refresh_source(repo, previous, current, source, root)
            self.assertEqual((source / 'change.txt').read_text(), 'new')
            self.assertEqual((source / 'stable.txt').stat().st_mtime_ns, stamp)
            (source / 'unknown.txt').write_text('preserve', encoding='utf-8')
            with self.assertRaises(ValueError): package.refresh_source(repo, current, current, source, root)
            self.assertEqual((source / 'unknown.txt').read_text(), 'preserve')

    def test_case_collision(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'duplicate.zip'
            with zipfile.ZipFile(archive, 'w') as zipped:
                zipped.writestr('ZIMA-CAD/A.txt', 'a'); zipped.writestr('ZIMA-CAD/a.txt', 'b')
            with self.assertRaises(ValueError): package.validate_archive(archive)

if __name__ == '__main__': unittest.main()
