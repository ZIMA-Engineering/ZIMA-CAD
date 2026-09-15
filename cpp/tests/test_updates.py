"""Isolated signed-release and installer regression tests.

Build zima_update_test_helper and zima_update_fixture with CMake, then set
ZIMA_UPDATER_TEST_EXE and ZIMA_UPDATE_FIXTURE_EXE. Test keys and HTTP overrides
are compiled out of the production helper. No publisher key or GitHub is used.
"""
import copy
import hashlib
import http.server
import json
import os
from pathlib import Path
import runpy
import shutil
import stat
import subprocess
import tempfile
import threading
import time
import unittest
import uuid
import zipfile
from types import SimpleNamespace
from unittest.mock import patch
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
PUBLISH = runpy.run_path(str(ROOT / 'tools/distribution/update-release.py'))
canonical = PUBLISH['canonical']
UPDATER = Path(os.environ['ZIMA_UPDATER_TEST_EXE']).resolve()
FIXTURE = Path(os.environ['ZIMA_UPDATE_FIXTURE_EXE']).resolve()
PLATFORM = 'windows-x64' if os.name == 'nt' else 'linux-x86_64'
FOLDER = 'windows' if os.name == 'nt' else 'linux'
EXE = 'zima-cad-cpp.exe' if os.name == 'nt' else 'bin/zima-cad-cpp'
V1, V2, V3 = '2026091501', '2026091502', '2026091503'
COMMIT = 'a' * 40


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inventory(files):
    return {name: {'sha256': digest(data), 'size': len(data),
                   'executable': name.endswith('.sh') or data.startswith((b'#!', b'\x7fELF'))}
            for name, data in files.items()}


class Updates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = {EXE: FIXTURE.read_bytes()}
        if os.name == 'nt':
            runtime = Path(os.environ.get('ZIMA_UPDATE_TEST_RUNTIME', str(ROOT)))
            for name in ['Qt6Core.dll', 'Qt6Network.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'icuin78.dll', 'icuuc78.dll', 'icudt78.dll', 'z.dll', 'zstd.dll', 'pcre2-16.dll', 'double-conversion.dll', 'brotlidec.dll', 'brotlicommon.dll', 'libcrypto-3-x64.dll']:
                path = runtime / name
                if not path.exists(): path = Path(os.environ['SystemRoot']) / 'System32' / name
                cls.runtime[name] = path.read_bytes()
        else:
            cls.runtime['ZIMA-CAD'] = b'#!/bin/sh\nexec "$(dirname "$0")/bin/ZIMA-CAD" "$@"\n'
        if '--archive' not in subprocess.check_output([str(UPDATER), '--help'], text=True):
            raise RuntimeError('This suite requires the isolated test helper, never the production helper')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='zima-update-test-')
        self.base = Path(self.tmp.name)
        self.root = self.base / 'ZIMA-CAD'
        self.root.mkdir()
        self.key = Ed25519PrivateKey.generate()
        self.key_id = 'test-' + uuid.uuid4().hex
        keys = self.base / 'keys.json'
        keys.write_bytes(canonical({self.key_id: self.key.public_key().public_bytes_raw().hex()}))
        self.env = dict(os.environ, ZIMA_UPDATE_TEST_KEYS=str(keys),
                        ZIMA_UPDATE_TEST_STATE=str(self.base / 'settings'), ZIMA_UPDATE_TEST_WAIT='1200')
        (self.root / 'installation.json').write_bytes(canonical({'product': 'ZIMA-CAD', 'protocol': 1, 'id': str(uuid.uuid4())}))
        (self.root / 'launcher.ini').write_text(f'[launcher]\nwindows={V1}\nwindows_custom=false\nlinux={V1}\nlinux_custom=false\n')
        self.install_initial(V1)
        custom = self.root / 'custom' / FOLDER / 'my-build'
        custom.mkdir(parents=True); (custom / 'keep.txt').write_text('custom')
        (self.root / 'user-projects').mkdir(); (self.root / 'user-projects' / 'part.pdf').write_text('project')
        self.routes = {}
        owner = self
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                value = owner.routes.get(self.path.split('?')[0])
                if value is None:
                    self.send_error(404); return
                code, headers, body = value
                self.send_response(code)
                for key, val in headers.items(): self.send_header(key, val)
                self.end_headers(); self.wfile.write(body)
            def log_message(self, *args): pass
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True); self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}'
        self.env['ZIMA_UPDATE_TEST_API'] = self.url + '/releases'

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        # The fixture exits itself; no user process is terminated by these tests.
        deadline = time.monotonic() + 4
        while any((self.root / '.updates/instances').glob('*.lock')) and time.monotonic() < deadline:
            time.sleep(.1)
        self.tmp.cleanup()

    def sign(self, value):
        return {'keyId': self.key_id, 'signature': self.key.sign(canonical(value)).hex()}

    def runtime_files(self, version, fail=False):
        files = dict(self.runtime)
        files['version.json'] = canonical({'version': version, 'commit': COMMIT, 'platform': PLATFORM})
        files['build.ini'] = f'[build]\nversion={version}\nplatform={PLATFORM}\n'.encode()
        if fail: files['fail-start'] = b'fail'
        return files

    def install_initial(self, version):
        files = self.runtime_files(version)
        target = self.root / FOLDER / version
        for name, data in files.items():
            path = target / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            if os.name != 'nt' and inventory({name: data})[name]['executable']: path.chmod(0o755)
        source = self.root / 'source' / version; source.mkdir(parents=True)
        (source / 'README.md').write_bytes(b'initial source')
        att = {'schemaVersion': 1, 'kind': 'installed-build', 'product': 'ZIMA-CAD',
               'version': version, 'commit': COMMIT, 'platform': PLATFORM,
               'runtimeFiles': inventory(files), 'sourceFiles': inventory({'README.md': b'initial source'})}
        info = self.root / 'release-info'; info.mkdir(exist_ok=True)
        (info / f'{PLATFORM}-{version}.json').write_bytes(canonical({'attestation': att, 'signature': self.sign(att)}))

    def package(self, version=V2, fail=False, extra=None, include_other=True):
        files = {f'{FOLDER}/{version}/{name}': data for name, data in self.runtime_files(version, fail).items()}
        files[f'source/{version}/README.md'] = ('sources ' + version).encode()
        files['LICENSE'] = b'test license'
        # The other platform is verified but must not be installed on this host.
        other = 'linux' if FOLDER == 'windows' else 'windows'
        if include_other:
            files[f'{other}/{version}/untouched'] = b'other platform'
        if extra: files.update(extra)
        sums = canonical(inventory(files)); files['checksums.json'] = sums
        archive = self.base / f'ZIMA-CAD-{version}.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as zipped:
            for name, data in files.items():
                zipped.writestr('ZIMA-CAD/' + name, data)
        manifest = {'schemaVersion': 1, 'product': 'ZIMA-CAD', 'version': version,
                    'tag': 'ZIMA-CAD-' + version, 'commit': COMMIT, 'channel': 'development',
                    'archive': {'name': archive.name, 'size': archive.stat().st_size,
                                'sha256': digest(archive.read_bytes()), 'fileCount': len(files),
                                'unpackedSize': sum(map(len, files.values()))},
                    'checksumsSha256': digest(sums), 'minimumUpdaterVersion': 1, 'launcherProtocol': 1,
                    'source': {'path': f'source/{version}', 'treeSha256': digest(canonical(inventory({name.removeprefix(f'source/{version}/'): data for name, data in files.items() if name.startswith(f'source/{version}/')})))},
                    'platforms': {PLATFORM: {'runtime': f'{FOLDER}/{version}', 'entry': 'zima-cad-cpp.exe' if os.name == 'nt' else 'bin/zima-cad-cpp'}}}
        return archive, manifest

    def run_update(self, *args, ok=True):
        process = subprocess.run([str(UPDATER), *args, '--root', str(self.root)], env=self.env,
                                 capture_output=True, text=True, encoding='utf-8', timeout=25)
        records = [json.loads(line) for line in process.stdout.splitlines() if line.startswith('{')]
        result = records[-1] if records else {}
        if ok: self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        else: self.assertNotEqual(process.returncode, 0, process.stdout)
        return result

    def prepare(self, package, signature=None, ok=True):
        archive, manifest = package
        payload = self.base / 'manifest.json'; sig = self.base / 'manifest.sig'
        payload.write_bytes(canonical(manifest)); sig.write_bytes(canonical(signature or self.sign(manifest)))
        return self.run_update('prepare', '--archive', str(archive), '--manifest', str(payload), '--signature', str(sig), ok=ok)

    def release(self, package):
        archive, manifest = package
        version = manifest['version']
        assets = []
        for name, data in [('update-manifest.json', canonical(manifest)), ('update-manifest.sig', canonical(self.sign(manifest))), (archive.name, archive.read_bytes())]:
            path = f'/{version}/{name}'; self.routes[path] = (200, {}, data)
            assets.append({'name': name, 'state': 'uploaded', 'browser_download_url': self.url + path})
        return {'tag_name': manifest['tag'], 'draft': False, 'prerelease': False, 'assets': assets, 'body': 'Test release notes'}

    def catalog(self, releases): self.routes['/releases'] = (200, {'ETag': 'test'}, json.dumps(releases).encode())
    def selected(self):
        return next(line.split('=', 1)[1] for line in (self.root / 'launcher.ini').read_text().splitlines() if line.startswith(FOLDER + '='))

    def test_check_sort_download_install_and_rollback(self):
        p2, p3 = self.package(), self.package(V3)
        draft = copy.deepcopy(self.release(p3)); draft['draft'] = True
        self.catalog([self.release(p2), draft])
        offer = self.run_update('check'); self.assertEqual(offer['availableVersion'], V2)
        self.assertTrue(offer['installable'])
        self.assertEqual(self.run_update('download', '--target', V2)['status'], 'prepared')
        self.assertEqual(self.selected(), V1)
        self.assertEqual(self.run_update('install', '--target', V2)['status'], 'confirmation-required')
        result = self.run_update('install', '--target', V2, '--apply')
        self.assertEqual(result['status'], 'installed'); self.assertEqual(self.selected(), V2)
        self.assertEqual(self.run_update('rollback', '--apply')['version'], V1)
        other = 'linux' if FOLDER == 'windows' else 'windows'
        self.assertFalse((self.root / other / V2).exists())
        self.assertIn(f'{other}={V1}', (self.root / 'launcher.ini').read_text())
        self.assertEqual((self.root / 'custom' / FOLDER / 'my-build/keep.txt').read_text(), 'custom')
        self.assertEqual((self.root / 'user-projects/part.pdf').read_text(), 'project')

    def test_restart_does_not_activate_prepared_update(self):
        self.prepare(self.package())
        self.run_update('launch')
        self.assertEqual(self.selected(), V1)
        self.assertFalse((self.root / FOLDER / V2).exists())

    def test_single_platform_archive_is_offered_and_installed(self):
        package = self.package(include_other=False)
        other = 'linux' if FOLDER == 'windows' else 'windows'
        with zipfile.ZipFile(package[0]) as archive:
            self.assertFalse(any(name.startswith(f'ZIMA-CAD/{other}/') for name in archive.namelist()))
        self.catalog([self.release(package)])
        offer = self.run_update('check')
        self.assertEqual(offer['availableVersion'], V2)
        self.assertEqual(set(offer['manifest']['platforms']), {PLATFORM})
        self.assertTrue(offer['installable'])
        self.assertEqual(self.run_update('download', '--target', V2)['status'], 'prepared')
        self.assertEqual(self.run_update('install', '--target', V2, '--apply')['status'], 'installed')
        self.assertEqual(self.selected(), V2)
        self.assertFalse((self.root / other).exists())

    def test_other_platform_release_does_not_hide_available_host_update(self):
        # Discovery uses signed platform metadata; this foreign runtime is never executed.
        foreign = self.package(V3)
        other, target, entry = ('linux', 'linux-x86_64', 'bin/zima-cad-cpp') if FOLDER == 'windows' else ('windows', 'windows-x64', 'zima-cad-cpp.exe')
        foreign[1]['platforms'] = {target: {'runtime': f'{other}/{V3}', 'entry': entry}}
        foreign_release = self.release(foreign)
        self.catalog([foreign_release])
        self.assertEqual(self.run_update('check')['status'], 'current')
        native_release = self.release(self.package(include_other=False))
        self.catalog([foreign_release, native_release])
        offer = self.run_update('check')
        self.assertEqual(offer['availableVersion'], V2)
        self.assertTrue(offer['installable'])

    def test_publisher_accepts_each_platform_independently(self):
        # Tiny publisher fixtures verify the archive contract, not Linux binaries.
        password = 'isolated publisher fixture'
        private = self.base / 'test-only.pem'
        private.write_bytes(self.key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8, serialization.BestAvailableEncryption(password.encode())))
        for target, folder, entry, helper in [
            ('windows-x64', 'windows', 'zima-cad-cpp.exe', 'zima-cad-update.exe'),
            ('linux-x86_64', 'linux', 'bin/zima-cad-cpp', 'bin/zima-cad-update')]:
            with self.subTest(platform=target):
                package = self.base / ('publisher-' + folder)
                runtime = package / folder / V2
                runtime.mkdir(parents=True)
                (runtime / entry).parent.mkdir(parents=True, exist_ok=True)
                (runtime / entry).write_bytes(b'fixture entry')
                (runtime / helper).parent.mkdir(parents=True, exist_ok=True)
                (runtime / helper).write_bytes(b'fixture helper')
                (runtime / 'version.json').write_bytes(canonical({'version': V2, 'platform': target,
                    'commit': COMMIT, 'source_modified': False, 'origin': 'release-candidate'}))
                source = package / 'source' / V2; source.mkdir(parents=True)
                (source / 'README.md').write_text('matching fixture source')
                (source / 'cpp/update').mkdir(parents=True)
                (source / 'cpp/update/trusted-keys.json').write_bytes(canonical({self.key_id: self.key.public_key().public_bytes_raw().hex()}))
                (package / 'LICENSE').write_text('fixture license')
                (package / 'installation.json').write_bytes(canonical({'product': 'ZIMA-CAD', 'protocol': 1}))
                (package / 'launcher.ini').write_text(f'[launcher]\n{folder}={V2}\n{folder}_custom=false\n')
                (package / 'ZIMA-CAD.sh').write_text('#!/bin/sh\n')
                if folder == 'windows': (package / 'ZIMA-CAD.exe').write_bytes(b'fixture launcher')
                output = self.base / ('published-' + folder)
                args = SimpleNamespace(version=V2, package=package, output=output, private=private, development=True)
                with patch('getpass.getpass', return_value=password): PUBLISH['finalize'](args)
                payload = (output / 'update-manifest.json').read_bytes()
                signature = json.loads((output / 'update-manifest.sig').read_text())
                self.key.public_key().verify(bytes.fromhex(signature['signature']), payload)
                manifest = json.loads(payload)
                self.assertEqual(set(manifest['platforms']), {target})
                self.assertEqual(manifest['channel'], 'development')
                with zipfile.ZipFile(output / manifest['archive']['name']) as archive:
                    self.assertIsNone(archive.testzip())
                    other = 'linux' if folder == 'windows' else 'windows'
                    self.assertFalse(any(name.startswith(f'ZIMA-CAD/{other}/') for name in archive.namelist()))

    def test_case_colliding_zip_names_are_rejected(self):
        package = self.package(extra={f'{FOLDER}/{V2}/VERSION.JSON': b'collision'})
        self.assertIn('ZIP entry', self.prepare(package, ok=False)['error'])
        self.assertEqual(self.selected(), V1)

    def test_unsigned_initial_copy_cannot_install(self):
        (self.root / 'release-info' / f'{PLATFORM}-{V1}.json').unlink()
        self.catalog([self.release(self.package())])
        self.assertFalse(self.run_update('check')['installable'])
        self.run_update('download', '--target', V2, ok=False)
        self.assertFalse((self.root / FOLDER / V2).exists())

    def test_sources_referenced_by_other_platform_survive_cleanup(self):
        other = 'linux' if FOLDER == 'windows' else 'windows'
        (self.root / other / V1).mkdir(parents=True)
        (self.root / other / V1 / 'other-build').write_text('keep')
        self.prepare(self.package()); self.run_update('install', '--target', V2, '--apply')
        self.prepare(self.package(V3)); self.run_update('install', '--target', V3, '--apply')
        self.assertFalse((self.root / FOLDER / V1).exists())
        self.assertTrue((self.root / 'source' / V1).exists())

    def test_bad_signature_and_hash_never_switch(self):
        package = self.package()
        sig = self.sign(package[1]); sig['signature'] = '0' * 128
        self.assertIn('signature', self.prepare(package, sig, ok=False)['error'])
        archive, manifest = package; archive.write_bytes(archive.read_bytes() + b'bad')
        self.assertIn('hash or size', self.prepare(package, ok=False)['error'])
        self.assertEqual(self.selected(), V1)

    def test_signed_traversal_is_rejected(self):
        self.assertIn('ZIP entry', self.prepare(self.package(extra={'../escaped.txt': b'bad'}), ok=False)['error'])
        self.assertFalse((self.base / 'escaped.txt').exists())

    def test_modified_staging_preserved(self):
        result = self.prepare(self.package())
        path = self.root / FOLDER / ('.u' + result['transaction'][:8]) / 'version.json'
        path.write_text('local modification')
        self.run_update('install', '--target', V2, '--apply', ok=False)
        self.assertEqual(self.selected(), V1); self.assertEqual(path.read_text(), 'local modification')

    def test_live_instance_blocks_install_without_being_killed(self):
        self.prepare(self.package())
        instance = subprocess.Popen([str(self.root / FOLDER / V1 / EXE), '--hold'], env=self.env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 3
            while not (self.root / 'fixture-started').exists() and time.monotonic() < deadline: time.sleep(.05)
            result = self.run_update('install', '--target', V2, '--apply', ok=False)
            self.assertIn('still running', result['error'])
            self.assertIsNone(instance.poll())
            self.assertEqual(self.selected(), V1)
        finally:
            instance.wait(timeout=8)
        self.assertEqual(self.run_update('install', '--target', V2, '--apply')['status'], 'installed')

    def test_archive_path_budget_and_file_directory_collision(self):
        for extra in ({'a' * 181: b'long'}, {f'{FOLDER}/{V2}/parent': b'file', f'{FOLDER}/{V2}/parent/child': b'collision'}):
            with self.subTest(extra=tuple(extra)):
                self.prepare(self.package(extra=extra), ok=False)
                self.assertEqual(self.selected(), V1)

    def test_longest_allowed_source_path_can_be_prepared(self):
        prefix = f'source/{V2}/'
        name = prefix + 'a' * 70 + '/' + 'b' * (180 - len('ZIMA-CAD/') - len(prefix) - 75) + '.txt'
        self.assertEqual(len('ZIMA-CAD/' + name), 180)
        result = self.prepare(self.package(extra={name: b'long committed source'}))
        self.assertEqual(result['status'], 'prepared')
        path = self.root / 'source' / ('.u' + result['transaction'][:8]) / name.removeprefix(prefix)
        self.assertEqual(path.read_bytes(), b'long committed source')

    def test_recovery_does_not_start_a_second_gui(self):
        self.run_update('recover')
        self.assertFalse((self.root / 'fixture-started').exists())

    def test_other_product_signature_is_rejected(self):
        package = self.package()
        package[1]['product'] = 'ZIMA-CAD-Parts'
        self.prepare(package, ok=False)
        self.assertEqual(self.selected(), V1)

    def test_partial_import_is_retryable(self):
        result = self.prepare(self.package())
        staged = self.root / FOLDER / ('.u' + result['transaction'][:8])
        staged.rename(self.root / FOLDER / V2)
        self.assertEqual(self.run_update('install', '--target', V2, '--apply')['status'], 'installed')

    def test_failed_start_restores_previous(self):
        self.prepare(self.package(fail=True))
        self.run_update('install', '--target', V2, '--apply', ok=False)
        self.assertEqual(self.selected(), V1)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            started = (self.root / 'fixture-started').read_text()
            if V1 in started: break
            time.sleep(.1)
        self.assertIn(V1, started)

    def test_retains_two_verified_versions(self):
        self.prepare(self.package()); self.run_update('install', '--target', V2, '--apply')
        self.prepare(self.package(V3)); self.run_update('install', '--target', V3, '--apply')
        self.assertFalse((self.root / FOLDER / V1).exists())
        self.assertFalse((self.root / 'source' / V1).exists())
        self.assertTrue((self.root / FOLDER / V2).exists()); self.assertTrue((self.root / FOLDER / V3).exists())

    def test_modified_old_version_is_not_deleted(self):
        self.prepare(self.package()); self.run_update('install', '--target', V2, '--apply')
        (self.root / FOLDER / V1 / 'my-file.txt').write_text('keep')
        self.prepare(self.package(V3)); result = self.run_update('install', '--target', V3, '--apply')
        self.assertIn(V1, result['cleanupPending'])
        self.assertTrue((self.root / FOLDER / V1 / 'my-file.txt').exists())

    def test_incompatible_protocol_is_visible_but_not_installable(self):
        package = self.package(); package[1]['launcherProtocol'] = 2
        self.catalog([self.release(package)])
        self.assertFalse(self.run_update('check')['installable'])
        self.prepare(package, ok=False)

    def test_catalog_replay_and_offline_leave_selection(self):
        self.catalog([self.release(self.package())]); self.run_update('check')
        self.catalog([]); self.run_update('check', ok=False)
        self.routes['/releases'] = (503, {}, b'unavailable'); self.run_update('check', ok=False)
        self.assertEqual(self.selected(), V1)

    def test_launch_recovers_interrupted_selection(self):
        self.install_initial(V2)
        (self.root / 'launcher.ini').write_text((self.root / 'launcher.ini').read_text().replace(FOLDER+'='+V1, FOLDER+'='+V2))
        state = self.root / '.updates'; state.mkdir(exist_ok=True)
        (state / f'{PLATFORM}-journal.json').write_bytes(canonical({'phase': 'candidate', 'previous': V1, 'candidate': V2}))
        self.run_update('launch'); self.assertEqual(self.selected(), V1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
