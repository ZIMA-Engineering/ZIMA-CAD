"""Create signing keys and finalize update archives. Never publishes a release.

Requires cryptography for publisher operations. Private keys are DPAPI-protected
on Windows; export uses an encrypted PEM with an interactively entered password.
"""
import argparse
import ctypes
import datetime
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import zipfile
import runpy
import configparser

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = runpy.run_path(str(ROOT / 'tools/distribution/package.py'))


def canonical(value):
    # Qt sorts object keys by UTF-16 code units. All protocol property names
    # are ASCII; sorting explicitly also handles source inventory file names.
    def order(v):
        if isinstance(v, dict):
            return {k: order(v[k]) for k in sorted(v, key=lambda s: s.encode('utf-16-be'))}
        if isinstance(v, list):
            return [order(x) for x in v]
        return v
    return (json.dumps(order(value), ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')


def sha(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def valid_version(version):
    if not re.fullmatch(r'\d{10}', version) or version[8:] == '00':
        raise ValueError('Expected YYYYMMDDNN with sequence 01-99')
    datetime.datetime.strptime(version[:8], '%Y%m%d')
    return version


def safe_name(name):
    if not name or '\\' in name or name.startswith('/') or len(name.encode('utf-16-le')) // 2 > 180:
        raise ValueError('Unsafe archive path: ' + name)
    for part in name.split('/'):
        if part in ('', '.', '..') or part[-1:] in ('.', ' ') or any(c in ':<>\"|?*' or ord(c) < 32 for c in part):
            raise ValueError('Unsafe archive path: ' + name)
        if re.fullmatch(r'(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part, re.I):
            raise ValueError('Reserved archive path: ' + name)
    return name


def dpapi(data, protect):
    if os.name != 'nt':
        raise ValueError('This key belongs to a Windows account. Export an encrypted PEM on Windows first.')
    from ctypes import wintypes
    class Blob(ctypes.Structure):
        _fields_ = [('length', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    buffer = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    output = Blob()
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    fn = crypt.CryptProtectData if protect else crypt.CryptUnprotectData
    fn.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                   ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    fn.restype = wintypes.BOOL
    if not fn(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(output)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return ctypes.string_at(output.data, output.length)
    finally:
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        kernel.LocalFree(output.data)


def load_key(path):
    data = path.read_bytes()
    if data.startswith(b'ZCP-DPAPI-1\n'):
        return Ed25519PrivateKey.from_private_bytes(dpapi(data.split(b'\n', 1)[1], False))
    password = getpass.getpass('Signing key password: ').encode('utf-8')
    key = serialization.load_pem_private_key(data, password=password)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError('An Ed25519 signing key is required')
    return key


def write_new(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(data)
    if os.name != 'nt':
        path.chmod(0o600)


def keygen(args):
    key = Ed25519PrivateKey.generate()
    raw = key.private_bytes_raw()
    if os.name == 'nt':
        data = b'ZCP-DPAPI-1\n' + dpapi(raw, True)
    else:
        password = getpass.getpass('New signing key password: ').encode('utf-8')
        if len(password) < 12 or password != getpass.getpass('Repeat password: ').encode('utf-8'):
            raise ValueError('Use a matching password of at least 12 characters')
        data = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                 serialization.BestAvailableEncryption(password))
    public = key.public_key().public_bytes_raw().hex()
    key_id = 'zima-' + hashlib.sha256(bytes.fromhex(public)).hexdigest()[:16]
    registry = json.loads(args.public.read_text(encoding='utf-8')) if args.public.exists() else {}
    registry[key_id] = public
    write_new(args.private, data)
    args.public.parent.mkdir(parents=True, exist_ok=True)
    args.public.write_bytes(canonical(registry))
    print('Public key ID:', key_id)
    print('Private key saved locally; its contents are not printed.')


def export_key(args):
    key = load_key(args.private)
    password = getpass.getpass('Backup password: ').encode('utf-8')
    if len(password) < 12 or password != getpass.getpass('Repeat password: ').encode('utf-8'):
        raise ValueError('Use a matching password of at least 12 characters')
    write_new(args.output, key.private_bytes(serialization.Encoding.PEM,
              serialization.PrivateFormat.PKCS8, serialization.BestAvailableEncryption(password)))
    print('Encrypted portable backup written:', args.output)


def inventory(folder):
    result = {}
    names = set()
    for path in sorted(folder.rglob('*')):
        if path.is_symlink() or bool(getattr(path.lstat(), 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0)):
            raise ValueError('Links are not permitted: ' + str(path))
        if not path.is_file():
            continue
        name = safe_name(path.relative_to(folder).as_posix())
        if name.casefold() in names:
            raise ValueError('Case-colliding file names')
        names.add(name.casefold())
        with path.open('rb') as stream:
            executable = name.endswith('.sh') or stream.read(4).startswith((b'#!', b'\x7fELF'))
        result[name] = {'sha256': sha(path), 'size': path.stat().st_size, 'executable': executable}
    return result


def verify_candidate_inputs(args, version, package, platforms, commit):
    """Bind clean Git bytes and smoke reports to the exact supplied runtimes."""
    repo = getattr(args, 'repo', ROOT).resolve()
    git = PACKAGE['git']
    if git(repo, 'status', '--porcelain'):
        raise ValueError('Signing a stable release requires a clean repository')
    if git(repo, 'rev-parse', 'ZIMA-CAD-' + version + '^{commit}').decode().strip() != commit:
        raise ValueError('Release tag must identify the exact packaged commit')
    if git(repo, 'show', commit + ':VERSION').decode().strip() != version:
        raise ValueError('VERSION differs from release identity')
    with tempfile.TemporaryDirectory(prefix='zima-source-') as tmp:
        exported = Path(tmp) / 'source'
        PACKAGE['export_source'](repo, commit, exported)
        if inventory(exported) != inventory(package / 'source' / version):
            raise ValueError('Packaged sources differ from committed Git bytes')
    accepted = set()
    for report_path in getattr(args, 'validation', []):
        report = json.loads(report_path.read_text(encoding='utf-8'))
        archive = report_path.parent / report['archive']
        if archive.parent.resolve() != report_path.parent.resolve():
            raise ValueError('Validation archive escapes its directory')
        if report.get('commit') != commit or report.get('version') != version or sha(archive) != report.get('sha256'):
            raise ValueError('Candidate validation identity/hash mismatch')
        PACKAGE['validate_archive'](archive)
        with zipfile.ZipFile(archive) as zipped:
            for target, config in platforms.items():
                folder = config['runtime'].split('/')[0]
                if report.get(folder + '_smoke') != 'passed': continue
                launcher = 'ZIMA-CAD.exe' if folder == 'windows' else 'ZIMA-CAD.sh'
                if zipped.read('ZIMA-CAD/' + launcher) != (package / launcher).read_bytes():
                    raise ValueError('Root launcher differs from the smoke-tested candidate')
                for relative in (config['runtime'], 'source/' + version):
                    prefix = 'ZIMA-CAD/' + relative + '/'
                    expected = inventory(package / relative)
                    actual = {name[len(prefix):] for name in zipped.namelist() if name.startswith(prefix) and not name.endswith('/')}
                    if actual != set(expected): raise ValueError('Validated candidate file inventory mismatch')
                    for name, record in expected.items():
                        if hashlib.sha256(zipped.read(prefix + name)).hexdigest() != record['sha256']:
                            raise ValueError('Runtime/source differs from the smoke-tested candidate: ' + name)
                accepted.add(target)
    if accepted != set(platforms):
        raise ValueError('Supply the matching candidate validation report for every included platform')


def finalize(args):
    version = valid_version(args.version)
    package = args.package.resolve()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output must be a new directory')
    if output == package or package in output.parents:
        raise ValueError('Output must be outside the package')
    source = package / 'source' / version
    if not source.is_dir() or not (package / 'LICENSE').is_file():
        raise ValueError('Missing matching sources or root LICENSE')
    allowed = {'ZIMA-CAD.exe', 'ZIMA-CAD.sh', 'LICENSE', 'launcher.ini',
               'installation.json', 'checksums.json', 'windows', 'linux', 'source'}
    if {path.name for path in package.iterdir()} - allowed:
        raise ValueError('Use a fresh candidate package without user configuration, projects or working files')
    for folder in ('windows', 'linux', 'source'):
        if (package / folder).exists() and {path.name for path in (package / folder).iterdir()} != {version}:
            raise ValueError('A release package contains exactly one build identity')
    platforms = {}
    commits = set()
    for platform, directory, entry in [('windows-x64', 'windows', 'zima-cad-cpp.exe'),
                                        ('linux-x86_64', 'linux', 'bin/zima-cad-cpp')]:
        runtime = package / directory / version
        if not runtime.exists():
            continue
        metadata = json.loads((runtime / 'version.json').read_text(encoding='utf-8'))
        if metadata.get('version') != version or metadata.get('platform') != platform:
            raise ValueError('Platform manifest mismatch')
        # A committed candidate can be tagged after packaging. The exact tag,
        # source bytes and smoke-tested archive are verified below before signing.
        if not args.development and (metadata.get('source_modified') is not False or metadata.get('origin') not in ('committed-candidate', 'release-candidate', 'official')):
            raise ValueError('Release requires clean, verified platform candidates')
        if not re.fullmatch('[0-9a-f]{40}', metadata.get('commit', '')):
            raise ValueError('Missing source commit')
        commits.add(metadata['commit'])
        helper = runtime / ('zima-cad-update.exe' if directory == 'windows' else 'bin/zima-cad-update')
        if not (runtime / entry).is_file() or not helper.is_file():
            raise ValueError('Missing GUI or updater binary')
        platforms[platform] = {'runtime': f'{directory}/{version}', 'entry': entry,
                               'systemPackages': metadata.get('system_packages', [])}
    if not platforms or len(commits) != 1:
        raise ValueError('Build platforms must share one source commit')
    if not args.development:
        verify_candidate_inputs(args, version, package, platforms, next(iter(commits)))
    if 'windows-x64' in platforms and not (package / 'ZIMA-CAD.exe').is_file():
        raise ValueError('Missing Windows root launcher')
    if not (package / 'ZIMA-CAD.sh').is_file():
        raise ValueError('Missing Linux root launcher')
    selection = configparser.ConfigParser()
    selection.read(package / 'launcher.ini', encoding='utf-8')
    for config in platforms.values():
        folder = config['runtime'].split('/')[0]
        if selection.get('launcher', folder, fallback='') != version or selection.getboolean('launcher', folder + '_custom', fallback=True):
            raise ValueError('Launcher must select the included official version')
    if not args.development:
        for root_name, source_name in [('LICENSE', 'LICENSE'), ('ZIMA-CAD.sh', 'tools/distribution/launcher-linux.sh')]:
            if (package / root_name).read_bytes() != (source / source_name).read_bytes():
                raise ValueError('Root distribution file differs from committed source: ' + root_name)
    # Validate links before copytree, which would otherwise dereference them.
    inventory(package)
    marker = json.loads((package / 'installation.json').read_text(encoding='utf-8'))
    if marker.get('product') != 'ZIMA-CAD' or marker.get('protocol') != 1:
        raise ValueError('Missing protocol-1 installation marker')
    key = load_key(args.private)
    public = key.public_key().public_bytes_raw()
    # The packaged source must already trust this key; private keys stay local.
    trusted = json.loads((source / 'cpp/update/trusted-keys.json').read_text(encoding='utf-8'))
    matches = [key_id for key_id, value in trusted.items() if value == public.hex()]
    if len(matches) != 1: raise ValueError('Signing key is not uniquely trusted by the packaged source build')
    key_id = matches[0]
    output.mkdir(parents=True)
    try:
        with tempfile.TemporaryDirectory(prefix='zima-release-') as tmp:
            assembled = Path(tmp) / 'ZIMA-CAD'
            shutil.copytree(package, assembled)
            # Never package local installation transaction state or private keys.
            for path in assembled.rglob('*'):
                if any(part in ('.git', '.local-backups', '.updates', 'instances', '__pycache__') for part in path.relative_to(assembled).parts):
                    raise ValueError('Working state must not be present in a release package')
            (assembled / 'README.txt').write_text(
                f'ZIMA-CAD-{version}\n'
                'Run ZIMA-CAD.exe on Windows, or ZIMA-CAD.sh on Linux.\n'
                + 'Included targets: ' + ', '.join(sorted(platforms)) + '\n'
                + ('Development package; not accepted as a stable automatic update.\n' if args.development else 'Stable publisher release.\n')
                + 'Signed publisher inventories are in release-info/.\n'
                + f'Sources and documentation: source/{version}/. License: LICENSE.\n'
                + 'Custom builds belong in custom/windows/ or custom/linux/.\n', encoding='utf-8')
            (assembled / 'release-info').mkdir(exist_ok=True)
            for target, configuration in platforms.items():
                metadata_path = assembled / configuration['runtime'] / 'version.json'
                metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
                metadata['signed'] = True
                metadata['origin'] = 'development' if args.development else 'official'
                metadata_path.write_bytes(canonical(metadata))
                attestation = {'schemaVersion': 1, 'kind': 'installed-build', 'product': 'ZIMA-CAD',
                               'version': version, 'commit': next(iter(commits)), 'platform': target,
                               'runtimeFiles': inventory(assembled / configuration['runtime']),
                               'sourceFiles': inventory(assembled / 'source' / version)}
                signed = {'attestation': attestation, 'signature': {
                    'keyId': key_id, 'signature': key.sign(canonical(attestation)).hex()}}
                (assembled / 'release-info' / f'{target}-{version}.json').write_bytes(canonical(signed))
            files = inventory(assembled)
            files.pop('checksums.json', None)
            (assembled / 'checksums.json').write_bytes(canonical(files))
            files = inventory(assembled)
            if len(files) > 50000 or any(f['size'] > 512 * 1024 * 1024 for f in files.values()):
                raise ValueError('Release exceeds updater extraction limits')
            if sum(f['size'] for f in files.values()) > 8 * 1024**3:
                raise ValueError('Release exceeds unpacked size limit')
            archive = output / f'ZIMA-CAD-{version}.zip'
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=False) as zipped:
                for path in sorted(assembled.rglob('*')):
                    if path.is_dir(): continue
                    member = safe_name('ZIMA-CAD/' + path.relative_to(assembled).as_posix())
                    info = zipfile.ZipInfo(member)
                    info.date_time = (1980, 1, 1, 0, 0, 0)
                    info.create_system = 3
                    permissions = 0o755 if path.is_dir() or files[path.relative_to(assembled).as_posix()]['executable'] else 0o644
                    info.external_attr = ((stat.S_IFDIR if path.is_dir() else stat.S_IFREG) | permissions) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    zipped.writestr(info, b'' if path.is_dir() else path.read_bytes())
            if archive.stat().st_size >= 2 * 1024**3:
                raise ValueError('Protocol 1 archives must be smaller than 2 GiB')
            source_files = inventory(assembled / 'source' / version)
            manifest = {'schemaVersion': 1, 'product': 'ZIMA-CAD', 'version': version,
                        'tag': 'ZIMA-CAD-' + version, 'commit': commits.pop(),
                        'archive': {'name': archive.name, 'size': archive.stat().st_size, 'sha256': sha(archive),
                                    'unpackedSize': sum(f['size'] for f in files.values()), 'fileCount': len(files)},
                        'checksumsSha256': files['checksums.json']['sha256'],
                        'platforms': platforms, 'source': {'path': 'source/' + version,
                                      'treeSha256': hashlib.sha256(canonical(source_files)).hexdigest()},
                        'minimumUpdaterVersion': 1, 'launcherProtocol': 1,
                        'channel': 'development' if args.development else 'stable'}
            payload = canonical(manifest)
            (output / 'update-manifest.json').write_bytes(payload)
            (output / 'update-manifest.sig').write_bytes(canonical({'keyId': key_id, 'signature': key.sign(payload).hex()}))
            with zipfile.ZipFile(archive) as zipped:
                if zipped.testzip():
                    raise ValueError('ZIP CRC verification failed')
            PACKAGE['validate_archive'](archive)
            native_platform = 'windows-x64' if os.name == 'nt' else 'linux-x86_64'
            if not args.development and native_platform in platforms:
                checked = Path(tmp) / 'verified'
                PACKAGE['validate_archive'](archive, checked)
                PACKAGE['smoke'](checked / 'ZIMA-CAD', version)
            (output / (archive.stem + '.validation.json')).write_bytes(canonical({
                'archive': archive.name, 'sha256': sha(archive), 'version': version,
                'commit': manifest['commit'], 'signed': True,
                'windows_smoke': 'passed' if not args.development and os.name == 'nt' and 'windows-x64' in platforms else 'not-run',
                'linux_smoke': 'passed' if not args.development and os.name != 'nt' and 'linux-x86_64' in platforms else 'not-run'}))
        print('Prepared signed assets (not published):', output)
    except Exception:
        # Keep failed output for inspection; never erase an arbitrary supplied path.
        raise


def main():
    parser = argparse.ArgumentParser(__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    gen = commands.add_parser('keygen')
    gen.add_argument('--private', type=Path, required=True)
    gen.add_argument('--public', type=Path, required=True)
    export = commands.add_parser('export-key')
    export.add_argument('--private', type=Path, required=True)
    export.add_argument('--output', type=Path, required=True)
    release = commands.add_parser('finalize')
    release.add_argument('--private', type=Path, required=True)
    release.add_argument('--package', type=Path, required=True)
    release.add_argument('--output', type=Path, required=True)
    release.add_argument('--version', required=True)
    release.add_argument('--development', action='store_true')
    release.add_argument('--repo', type=Path, default=ROOT)
    release.add_argument('--validation', type=Path, action='append', default=[])
    args = parser.parse_args()
    {'keygen': keygen, 'export-key': export_key, 'finalize': finalize}[args.command](args)


if __name__ == '__main__':
    main()
