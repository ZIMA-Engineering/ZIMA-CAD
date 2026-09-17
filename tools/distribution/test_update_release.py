"""Signing input gates; all repositories, archives and reports are disposable."""
import json
from pathlib import Path
import runpy
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PUBLISH = runpy.run_path(str(ROOT / 'tools/distribution/update-release.py'))
PACKAGE = PUBLISH['PACKAGE']
VERSION = '2026091504'


class PublisherInputs(unittest.TestCase):
    folder = 'windows'
    platform = 'windows-x64'
    executable = 'zima-cad-cpp.exe'
    launcher = 'ZIMA-CAD.exe'

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='zima-signing-input-')
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'; self.repo.mkdir()
        git = PACKAGE['git']
        git(self.repo, 'init', '-q')
        git(self.repo, 'config', 'user.name', 'Release fixture')
        git(self.repo, 'config', 'user.email', 'fixture@example.invalid')
        (self.repo / 'VERSION').write_text(VERSION)
        (self.repo / 'README.md').write_text('Committed source')
        git(self.repo, 'add', '.'); git(self.repo, 'commit', '-qm', 'fixture')
        git(self.repo, 'tag', 'ZIMA-CAD-' + VERSION)
        self.commit = git(self.repo, 'rev-parse', 'HEAD').decode().strip()
        self.package = self.root / 'candidate'; self.package.mkdir()
        PACKAGE['export_source'](self.repo, self.commit, self.package / 'source' / VERSION)
        runtime = self.package / self.folder / VERSION; runtime.mkdir(parents=True)
        (runtime / self.executable).parent.mkdir(parents=True, exist_ok=True)
        (runtime / self.executable).write_bytes(b'fixture runtime')
        (self.package / self.launcher).write_bytes(b'fixture launcher')
        sums = PACKAGE['inventory'](self.package)
        archive = self.root / ('ZIMA-CAD-' + VERSION + '.zip')
        with zipfile.ZipFile(archive, 'w') as zipped:
            for name in sums: zipped.write(self.package / name, 'ZIMA-CAD/' + name)
            zipped.writestr('ZIMA-CAD/checksums.json', PACKAGE['canonical'](sums))
        self.report = self.root / 'candidate.validation.json'
        self.report.write_text(json.dumps({'archive': archive.name, 'version': VERSION,
            'commit': self.commit, 'sha256': PACKAGE['sha'](archive), self.folder + '_smoke': 'passed'}))
        self.args = SimpleNamespace(repo=self.repo, validation=[self.report])
        self.platforms = {self.platform: {'runtime': self.folder + '/' + VERSION}}

    def tearDown(self): self.temp.cleanup()

    def verify(self):
        PUBLISH['verify_candidate_inputs'](self.args, VERSION, self.package, self.platforms, self.commit)

    def test_exact_candidate_and_clean_tag_pass(self): self.verify()

    def test_committed_candidate_requires_full_provenance_verification(self):
        runtime = self.package / self.folder / VERSION
        metadata = {'version': VERSION, 'platform': self.platform,
                    'commit': self.commit, 'source_modified': False,
                    'origin': 'committed-candidate'}
        (runtime / 'version.json').write_text(json.dumps(metadata))
        helper = 'zima-cad-update.exe' if self.folder == 'windows' else 'bin/zima-cad-update'
        (runtime / helper).write_bytes(b'fixture updater')
        (self.package / 'LICENSE').write_text('fixture license')
        args = SimpleNamespace(version=VERSION, package=self.package,
            output=self.root / 'signed', development=False)
        globals_ = PUBLISH['finalize'].__globals__
        with patch.dict(globals_, verify_candidate_inputs=unittest.mock.Mock(
                side_effect=ValueError('provenance gate reached'))):
            with self.assertRaisesRegex(ValueError, 'provenance gate reached'):
                PUBLISH['finalize'](args)
            globals_['verify_candidate_inputs'].assert_called_once()
            metadata['source_modified'] = True
            (runtime / 'version.json').write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, 'clean, verified platform candidates'):
                PUBLISH['finalize'](args)
            self.assertEqual(globals_['verify_candidate_inputs'].call_count, 1)

    def test_untracked_or_dirty_source_is_rejected(self):
        (self.repo / 'private.txt').write_text('not distributable')
        with self.assertRaisesRegex(ValueError, 'clean repository'): self.verify()

    def test_source_changed_after_export_is_rejected(self):
        (self.package / 'source' / VERSION / 'README.md').write_text('modified export')
        with self.assertRaisesRegex(ValueError, 'Git bytes'): self.verify()

    def test_runtime_changed_after_smoke_is_rejected(self):
        (self.package / self.folder / VERSION / self.executable).write_bytes(b'changed runtime')
        with self.assertRaisesRegex(ValueError, 'smoke-tested candidate'): self.verify()

    def test_launcher_changed_after_smoke_is_rejected(self):
        (self.package / self.launcher).write_bytes(b'changed launcher')
        with self.assertRaisesRegex(ValueError, 'launcher'): self.verify()

    def test_missing_or_failed_platform_smoke_is_rejected(self):
        self.args.validation = []
        with self.assertRaisesRegex(ValueError, 'validation report'): self.verify()
        self.args.validation = [self.report]
        report = json.loads(self.report.read_text()); report[self.folder + '_smoke'] = 'not-run'
        self.report.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'validation report'): self.verify()

    def test_tampered_archive_is_rejected(self):
        report = json.loads(self.report.read_text())
        archive = self.report.parent / report['archive']; archive.write_bytes(archive.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'): self.verify()


class LinuxPublisherInputs(PublisherInputs):
    folder = 'linux'
    platform = 'linux-x86_64'
    executable = 'bin/zima-cad-cpp'
    launcher = 'ZIMA-CAD.sh'


if __name__ == '__main__': unittest.main(verbosity=2)
