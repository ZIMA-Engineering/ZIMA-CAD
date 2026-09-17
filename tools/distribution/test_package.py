"""Source provenance and archive rejection tests; no signing credentials needed."""
import json
import os
from pathlib import Path
import tempfile
import subprocess
import shutil
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

    @unittest.skipIf(os.name == 'nt', 'Unix permissions contract')
    def test_linux_executable_modes_and_environment(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); content = root / 'content'; content.mkdir()
            launcher = content / 'ZIMA-CAD.sh'
            launcher.write_text('#!/bin/sh\nexit 0\n')
            launcher.chmod(0o755)
            (content / 'data.txt').write_text('read only data')
            archive = root / 'linux.zip'
            with zipfile.ZipFile(archive, 'w') as zipped:
                for path in content.iterdir(): zipped.write(path, 'ZIMA-CAD/' + path.name)
                zipped.writestr('ZIMA-CAD/checksums.json', package.canonical(package.inventory(content)))
            package.validate_archive(archive, root / 'extract')
            self.assertEqual((root / 'extract/ZIMA-CAD/ZIMA-CAD.sh').stat().st_mode & 0o777, 0o755)
            self.assertEqual((root / 'extract/ZIMA-CAD/data.txt').stat().st_mode & 0o777, 0o644)
            environment = package.clean_environment(root / 'runtime')
            self.assertEqual(environment['PATH'], '/usr/bin:/bin')
            self.assertEqual(environment['LD_LIBRARY_PATH'], str(root / 'runtime/lib'))
            self.assertNotIn('LD_PRELOAD', environment)

    @unittest.skipIf(os.name == 'nt', 'Linux shell launcher')
    def test_linux_recovery_preserves_cli_output_and_user_data(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'installation with spaces'; root.mkdir()
            version = '2026091701'; runtime = root / 'linux' / version
            (runtime / 'bin').mkdir(parents=True)
            (runtime / 'build.ini').write_text(f'[build]\nproduct=ZIMA-CAD\nversion={version}\n')
            (root / 'launcher.ini').write_text(f'[launcher]\nlinux={version}\nlinux_custom=false\n')
            (root / 'release-info').mkdir()
            (root / 'release-info' / f'linux-x86_64-{version}.json').write_text('{}')
            launcher = root / 'ZIMA-CAD.sh'
            shutil.copy2(package.ROOT / 'tools/distribution/launcher-linux.sh', launcher)
            launcher.chmod(0o755)
            helper = runtime / 'bin/zima-cad-update'
            helper.write_text("#!/bin/sh\necho '{\"status\":\"recovered\"}'\n")
            helper.chmod(0o755)
            cli = runtime / 'bin/zima-cad-cli'
            cli.write_text('#!/bin/sh\ncat\n'); cli.chmod(0o755)
            (root / 'config').mkdir(); settings = root / 'config/config.ini'
            settings.write_text('[User]\nKeep=true\n')
            reply = subprocess.run([launcher, '-CLI', '--'], input=b'command\n', capture_output=True, check=True)
            self.assertEqual(reply.stdout, b'command\n')
            self.assertEqual(settings.read_text(), '[User]\nKeep=true\n')
            helper.write_text('#!/bin/sh\necho recovery-failed\nexit 1\n')
            reply = subprocess.run([launcher, '-CLI', '--'], input=b'command\n', capture_output=True)
            self.assertNotEqual(reply.returncode, 0)
            self.assertEqual(reply.stdout, b'')
            self.assertIn(b'recovery-failed', reply.stderr)

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
