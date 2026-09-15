"""Build and validate native Windows candidates from committed Git data.

Run from a Visual Studio developer shell. Python is a build tool only.
This tool never publishes, signs, updates an installation, or deletes versions.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MAX_MEMBER = 180  # Includes the ZIMA-CAD/ top-level directory; leaves 79 for destination.
MAX_FILES = 50000
MAX_BYTES = 8 * 1024**3


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def git(repo, *args):
    return run(['git', '-C', repo, *args], stdout=subprocess.PIPE).stdout


def version_id(value):
    if not re.fullmatch(r'[0-9]{10}', value) or value[8:] == '00':
        raise ValueError('Build ID must be YYYYMMDDNN, sequence 01-99')
    datetime.datetime.strptime(value[:8], '%Y%m%d')
    return value


def safe_name(name):
    if not name or len(name.encode('utf-16-le')) // 2 > MAX_MEMBER or '\\' in name:
        raise ValueError('Archive member exceeds the path budget or is unsafe: ' + name)
    for part in name.split('/'):
        if not part or part in ('.', '..') or part.endswith(('.', ' ')) or re.search(r'[\x00-\x1f:<>"|?*]', part):
            raise ValueError('Unsafe archive member: ' + name)
        if re.fullmatch(r'(con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\..*)?', part, re.I):
            raise ValueError('Reserved Windows file name: ' + name)
    return name


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def inventory(folder):
    files, names = {}, {}
    for path in sorted(folder.rglob('*')):
        if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
            raise ValueError('Links are not allowed in a Windows candidate: ' + str(path))
        name = safe_name('ZIMA-CAD/' + path.relative_to(folder).as_posix())
        key = name.casefold()
        if key in names:
            raise ValueError('Case-colliding paths: ' + name)
        names[key] = True
        if path.is_file():
            files[name] = {'size': path.stat().st_size, 'sha256': sha(path)}
    if len(files) > MAX_FILES or sum(f['size'] for f in files.values()) > MAX_BYTES:
        raise ValueError('Candidate exceeds extraction limits')
    return files


def export_source(repo, commit, destination):
    """Git objects only; untracked/modified files and submodule worktrees are never copied."""
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryFile() as data:
        run(['git', '-C', repo, 'archive', '--format=tar', commit], stdout=data)
        data.seek(0)
        with tarfile.open(fileobj=data) as archive:
            for member in archive:
                name = safe_name('ZIMA-CAD/source/0000000001/' + member.name.rstrip('/'))
                target = destination / member.name
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open('xb') as output, archive.extractfile(member) as source:
                        shutil.copyfileobj(source, output)
                    target.chmod(member.mode & 0o777)
                else:
                    raise ValueError('Unsupported committed source entry: ' + name)
    for record in git(repo, 'ls-tree', '-rz', commit).split(b'\0'):
        if not record:
            continue
        metadata, name = record.split(b'\t', 1)
        mode, kind, revision = metadata.split()
        if mode == b'160000':
            child = Path(repo) / name.decode('utf-8')
            if not child.is_dir():
                raise ValueError('Initialize the recorded submodule before packaging: ' + str(child))
            export_source(child, revision.decode(), destination / name.decode('utf-8'))


def deploy_dependencies(runtime, installed, redist, dumpbin):
    plugins = installed / 'Qt6/plugins'
    for group, names in {'platforms': ['qwindows.dll', 'qoffscreen.dll'],
                         'imageformats': ['qjpeg.dll', 'qsvg.dll'],
                         'styles': ['qmodernwindowsstyle.dll']}.items():
        (runtime / 'plugins' / group).mkdir(parents=True)
        for name in names:
            shutil.copy2(plugins / group / name, runtime / 'plugins' / group / name)
    candidates = {}
    for folder in (installed / 'bin', redist):
        for path in folder.glob('*.dll'):
            candidates[path.name.casefold()] = path
    for path in redist.glob('*.dll'):
        shutil.copy2(path, runtime / path.name)
    system = Path(os.environ['SystemRoot']) / 'System32'
    pending = list(runtime.rglob('*.exe')) + list(runtime.rglob('*.dll'))
    visited = set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        result = run([dumpbin, '/nologo', '/dependents', binary], stdout=subprocess.PIPE)
        for name in re.findall(r'^\s+([A-Za-z0-9_.-]+\.dll)\s*$', result.stdout.decode(errors='replace'), re.M | re.I):
            key = name.casefold()
            if key.startswith(('api-ms-', 'ext-ms-')):
                continue
            if key in candidates:
                target = runtime / candidates[key].name
                if not target.exists():
                    shutil.copy2(candidates[key], target)
                    pending.append(target)
            elif key.startswith(('vcruntime', 'msvcp', 'concrt')) or not (system / name).is_file():
                raise ValueError('Unresolved native dependency: ' + name)
    (runtime / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=plugins\n', encoding='utf-8')


def clean_environment(runtime):
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('QT_', 'QML', 'CSF_', 'ZIMA_VERIFY'))}
    windows = Path(env['SystemRoot'])
    env['PATH'] = os.pathsep.join(map(str, (runtime, windows / 'System32', windows)))
    return env


def smoke(root, version, gui=True):
    runtime = root / 'windows' / version
    env = clean_environment(runtime)
    project = root / 'Projects' / 'package-smoke'
    project.mkdir(parents=True)
    selected = run([root / 'ZIMA-CAD.exe', '-Check'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15).stdout
    if Path(selected.decode('utf-8')) != runtime / 'zima-cad-cpp.exe':
        raise ValueError('Launcher selected an unexpected executable')
    run([root / 'ZIMA-CAD.exe', '-CLI', '--', '--build-info'], cwd=project, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    user_config = root / 'config/config.ini'
    user_config.write_text('[UserData]\nPreserve=package-smoke\n', encoding='utf-8')
    config_before = user_config.read_bytes()
    for executable in ('zima-cad-cpp.exe', 'zima-cad-cli.exe'):
        value = json.loads(run([runtime / executable, '--build-info'], cwd=project, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout)
        if value['version'] != version or value['product'] != 'ZIMA-CAD':
            raise ValueError('Executable build identity mismatch')
    def commands(items):
        args = [runtime / 'zima-cad-cli.exe', '--working-directory', project]
        for command in items:
            args += ['--command', json.dumps(command) if isinstance(command, dict) else command]
        output = run(args, cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        records = [json.loads(line) for line in output.stdout.splitlines()]
        if len(records) != len(items) or not all(r.get('ok') for r in records):
            raise ValueError('CLI smoke command failed')
        return records
    records = commands(['new part package-smoke', 'box.create 10 20 30', 'save'])
    owner = records[1]['data']['container']
    result = commands(['open package-smoke.prtz', {'command': 'measurement.evaluate', 'arguments': {
        'references': [{'kind': 'object', 'owner': owner}]}}])
    if abs(result[1]['data']['values'][0]['volume']['value'] - 6000) > 1e-6:
        raise ValueError('Saved/reopened Part has an incorrect volume')
    drawing = commands(['new drawing package-drawing', 'drawing.sheet.list', 'save'])
    sheet = drawing[1]['data']['items'][0]['sheet']
    commands(['open package-drawing.drwz',
              {'command': 'export.pdf', 'arguments': {'path': 'smoke.pdf'}},
              {'command': 'export.image', 'arguments': {'path': 'smoke.jpg', 'sheet': sheet, 'dpi': 30}}])
    if not (project / 'smoke.pdf').read_bytes().startswith(b'%PDF-') or not (project / 'smoke.jpg').read_bytes().startswith(b'\xff\xd8'):
        raise ValueError('PDF/JPEG export failed')
    if gui:
        env['ZIMA_VERIFY_CONSOLE_ONLY'] = '1'
        run([runtime / 'zima-cad-cpp.exe', '--working-directory', project, '--verify-startup'],
            cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    if user_config.read_bytes() != config_before:
        raise ValueError('Startup/smoke replaced shared user configuration')
    for name, record in json.loads((root / 'checksums.json').read_text(encoding='utf-8')).items():
        if sha(root.parent / name) != record['sha256']:
            raise ValueError('Smoke modified a packaged source/runtime file: ' + name)


def validate_archive(archive, destination=None):
    with zipfile.ZipFile(archive) as zipped:
        members = zipped.infolist()
        if len(members) > MAX_FILES or sum(m.file_size for m in members) > MAX_BYTES:
            raise ValueError('ZIP exceeds extraction limits')
        names = set()
        for member in members:
            safe_name(member.filename)
            if not member.filename.startswith('ZIMA-CAD/') or member.is_dir() or ((member.external_attr >> 16) & 0o170000) not in (0, 0o100000):
                raise ValueError('Unexpected ZIP entry type or root')
            key = member.filename.casefold()
            if key in names:
                raise ValueError('Duplicate/case-colliding ZIP entry')
            names.add(key)
        for name in names:
            parts = name.split('/')
            if any('/'.join(parts[:i]) in names for i in range(1, len(parts))):
                raise ValueError('ZIP file/directory path collision')
        if zipped.testzip():
            raise ValueError('ZIP CRC verification failed')
        if zipped.getinfo('ZIMA-CAD/checksums.json').file_size > 32 * 1024**2:
            raise ValueError('ZIP inventory is too large')
        checksums = json.loads(zipped.read('ZIMA-CAD/checksums.json'))
        if set(checksums) != {m.filename for m in members} - {'ZIMA-CAD/checksums.json'}:
            raise ValueError('ZIP inventory mismatch')
        for name, record in checksums.items():
            with zipped.open(name) as data:
                digest = hashlib.file_digest(data, 'sha256').hexdigest()
            if zipped.getinfo(name).file_size != record['size'] or digest != record['sha256']:
                raise ValueError('ZIP SHA-256 mismatch: ' + name)
        if destination is not None:
            if destination.exists():
                raise ValueError('Extraction requires a new directory')
            destination.mkdir(parents=True)
            for member in members:
                target = destination / member.filename
                target.parent.mkdir(parents=True, exist_ok=True)
                with zipped.open(member) as source, target.open('xb') as output:
                    shutil.copyfileobj(source, output)
    return sha(archive)


def package(args):
    if os.name != 'nt':
        raise ValueError('Build Windows candidates on Windows; Linux has its own handoff')
    commit = git(ROOT, 'rev-parse', args.commit + '^{commit}').decode().strip()
    if args.release and git(ROOT, 'status', '--porcelain'):
        raise ValueError('Release input must be clean, including untracked files')
    version = version_id(git(ROOT, 'show', commit + ':VERSION').decode().strip())
    if args.release and git(ROOT, 'rev-parse', 'ZIMA-CAD-' + version + '^{commit}').decode().strip() != commit:
        raise ValueError('Release tag must identify the packaged commit')
    stage = args.stage.resolve()
    if stage.exists() or len(str(stage)) > 60:
        raise ValueError('Use a new short staging directory (at most 60 characters)')
    stage.mkdir(parents=True)
    source = stage / 's'
    export_source(ROOT, commit, source)
    build = stage / 'b'
    run([args.cmake, '-S', source / 'cpp', '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DZIMA_BUILD_TESTS=OFF', '-DZIMA_SOURCE_COMMIT=' + commit,
         '-DCMAKE_TOOLCHAIN_FILE=' + str(args.toolchain.resolve()),
         '-DVCPKG_INSTALLED_DIR=' + str(args.installed.resolve().parent), '-DVCPKG_TARGET_TRIPLET=x64-windows'])
    run([args.cmake, '--build', build, '--target', 'zima-cad-cpp', 'zima-cad-cli', 'zima-cad-launcher', '--parallel', str(args.jobs)])
    root = stage / 'p/ZIMA-CAD'; runtime = root / 'windows' / version
    runtime.mkdir(parents=True)
    for exe in ('zima-cad-cpp.exe', 'zima-cad-cli.exe'):
        shutil.copy2(build / exe, runtime / exe)
    shutil.copy2(build / 'ZIMA-CAD.exe', root / 'ZIMA-CAD.exe')
    shutil.copy2(source / 'tools/distribution/launcher-linux.sh', root / 'ZIMA-CAD.sh')
    deploy_dependencies(runtime, args.installed, args.redist, args.dumpbin)
    for name in ('config', 'resources'):
        shutil.copytree(source / name, runtime / name, ignore=shutil.ignore_patterns('.gitkeep', '*.ini.[0-9]*', '*.frmz.[0-9]*', '*.tblz.[0-9]*'))
    shutil.copytree(args.installed / 'share/opencascade/resources', runtime / 'resources/occt')
    licenses = runtime / 'licenses'; licenses.mkdir()
    shutil.copy2(source / 'LICENSE', root / 'LICENSE')
    shutil.copy2(source / 'LICENSE', licenses / 'ZIMA-CAD.txt')
    for notice in (args.installed / 'share').glob('*/copyright'):
        shutil.copy2(notice, licenses / (notice.parent.name + '.txt'))
    if not list(licenses.glob('qt*.txt')) or not (licenses / 'opencascade.txt').exists():
        raise ValueError('Missing Qt/OCCT license notices')
    shutil.copytree(source, root / 'source' / version)
    metadata = json.loads(run([runtime / 'zima-cad-cli.exe', '--build-info'], stdout=subprocess.PIPE).stdout)
    if metadata['commit'] != commit or metadata['version'] != version or metadata['source_modified']:
        raise ValueError('Built executable/source identity mismatch')
    metadata.update(origin='release-candidate' if args.release else 'committed-candidate', signed=False)
    write_json(runtime / 'version.json', metadata)
    (runtime / 'build.ini').write_text(f'[build]\nproduct=ZIMA-CAD\nversion={version}\nplatform=windows-x64\n', encoding='utf-8')
    (root / 'launcher.ini').write_text(f'[launcher]\nwindows={version}\nwindows_custom=false\nlinux=\nlinux_custom=false\n', encoding='utf-8')
    # User config and project directories are created on first launch. Updates
    # must never extract a supplied user config over an existing installation.
    write_json(root / 'checksums.json', inventory(root))
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    archive = output / ('ZIMA-CAD-' + version + '.zip')
    if archive.exists():
        raise ValueError('Archive already exists; use a new build ID or output directory')
    candidate = stage / 'candidate.zip'
    with zipfile.ZipFile(candidate, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in sorted(root.rglob('*')):
            if path.is_file(): zipped.write(path, 'ZIMA-CAD/' + path.relative_to(root).as_posix())
    extracted = stage / 'ověření balíku'
    digest = validate_archive(candidate, extracted)
    smoke(extracted / 'ZIMA-CAD', version)
    # Validation report is outside the immutable archive. No official publication.
    with archive.open('xb') as target, candidate.open('rb') as stream:
        shutil.copyfileobj(stream, target)
    if sha(archive) != digest:
        raise ValueError('Final archive copy differs')
    write_json(output / (archive.stem + '.validation.json'), {'archive': archive.name, 'sha256': digest,
        'commit': commit, 'version': version, 'windows_smoke': 'passed', 'linux_smoke': 'not-run', 'signed': False})
    print('Validated candidate:', archive)


def main():
    parser = argparse.ArgumentParser(__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('windows')
    build.add_argument('--commit', default='HEAD')
    build.add_argument('--stage', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    build.add_argument('--installed', type=Path, required=True, help='vcpkg x64-windows installed directory')
    build.add_argument('--toolchain', type=Path, required=True)
    build.add_argument('--redist', type=Path, required=True, help='Microsoft.VC*.CRT redistributable directory')
    build.add_argument('--cmake', default='cmake')
    build.add_argument('--dumpbin', default='dumpbin')
    build.add_argument('--jobs', type=int, default=4)
    build.add_argument('--release', action='store_true', help='Require a clean checkout and matching tag; still unsigned/unpublished')
    validate = commands.add_parser('validate')
    validate.add_argument('archive', type=Path)
    args = parser.parse_args()
    if args.command == 'windows': package(args)
    else: print(validate_archive(args.archive))


if __name__ == '__main__':
    main()
