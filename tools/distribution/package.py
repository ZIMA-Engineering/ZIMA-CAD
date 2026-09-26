"""Build and validate native platform candidates from committed Git data.

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
import sys
import tarfile
import tempfile
import zipfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
MAX_MEMBER = 180  # Includes the ZIMA-CAD/ top-level directory; leaves 79 for destination.
MAX_FILES = 50000
MAX_BYTES = 8 * 1024**3


def run(args, **kwargs):
    try:
        return subprocess.run([str(a) for a in args], check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        for output in (error.stdout, error.stderr):
            if output: print(output.decode('utf-8', errors='replace') if isinstance(output, bytes) else output, file=sys.stderr)
        raise


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
    Path(path).write_bytes(canonical(value))


def canonical(value):
    def order(v):
        if isinstance(v, dict):
            return {k: order(v[k]) for k in sorted(v, key=lambda s: s.encode('utf-16-be'))}
        if isinstance(v, list): return [order(x) for x in v]
        return v
    return (json.dumps(order(value), ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')


def inventory(folder):
    files, names = {}, {}
    for path in sorted(folder.rglob('*')):
        if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
            raise ValueError('Links are not allowed in a Windows candidate: ' + str(path))
        name = path.relative_to(folder).as_posix()
        safe_name('ZIMA-CAD/' + name)
        key = name.casefold()
        if key in names:
            raise ValueError('Case-colliding paths: ' + name)
        names[key] = True
        if path.is_file():
            with path.open('rb') as stream:
                executable = name.endswith('.sh') or stream.read(4).startswith((b'#!', b'\x7fELF'))
            files[name] = {'size': path.stat().st_size, 'sha256': sha(path), 'executable': executable}
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


def refresh_source(repo, previous, commit, source, stage):
    """Reuse compilation only after verifying the old source, then export the new commit.

    Unchanged file timestamps survive; changed files always come from Git objects.
    Unknown/modified files cause failure before any source is replaced or removed.
    """
    with tempfile.TemporaryDirectory(prefix='verify-source-', dir=stage) as temporary:
        expected = Path(temporary) / 'old'
        export_source(repo, previous, expected)
        if inventory(source) != inventory(expected):
            raise ValueError('Existing staging source differs from its recorded Git commit')
        current = Path(temporary) / 'new'
        export_source(repo, commit, current)
        incoming = {p.relative_to(current): p for p in current.rglob('*') if p.is_file()}
        for path in list(source.rglob('*')):
            if path.is_file() and path.relative_to(source) not in incoming:
                if source.resolve() not in path.resolve().parents:
                    raise ValueError('Source cleanup escapes staging')
                path.unlink()
        for relative, path in incoming.items():
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or sha(target) != sha(path):
                shutil.copy2(path, target)


def deploy_dependencies(runtime, installed, redist, dumpbin):
    plugins = installed / 'Qt6/plugins'
    for group, names in {'platforms': ['qwindows.dll', 'qoffscreen.dll'],
                         'imageformats': ['qjpeg.dll', 'qsvg.dll'],
                         'styles': ['qmodernwindowsstyle.dll'],
                         'tls': ['qschannelbackend.dll']}.items():
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
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('QT_', 'QML', 'CSF_', 'ZIMA_VERIFY', 'ZIMA_UPDATE', 'ZIMA_INSTALL'))}
    if os.name != 'nt':
        for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD'):
            env.pop(key, None)
        env['PATH'] = '/usr/bin:/bin'
        env['LD_LIBRARY_PATH'] = str(runtime / 'lib')
        env['QT_PLUGIN_PATH'] = str(runtime / 'plugins')
        for variable, directory in {'CSF_ShadersDirectory': 'Shaders', 'CSF_XSMessage': 'XSMessage',
                                    'CSF_SHMessage': 'SHMessage', 'CSF_XSTEPDefaults': 'XSTEPResource',
                                    'CSF_STEPDefaults': 'XSTEPResource', 'CSF_IGESDefaults': 'XSTEPResource',
                                    'CSF_PluginDefaults': 'StdResource', 'CSF_StandardDefaults': 'StdResource',
                                    'CSF_XCAFDefaults': 'StdResource', 'CSF_XmlOcafResource': 'XmlOcafResource'}.items():
            env[variable] = str(runtime / 'resources/occt' / directory)
        return env
    windows = Path(os.environ['SystemRoot'])
    env['PATH'] = os.pathsep.join(map(str, (runtime, windows / 'System32', windows)))
    return env


def smoke(root, version, gui=True):
    linux = os.name != 'nt'
    runtime = root / ('linux' if linux else 'windows') / version
    binary = runtime / 'bin' if linux else runtime
    suffix = '' if linux else '.exe'
    launcher = root / ('ZIMA-CAD.sh' if linux else 'ZIMA-CAD.exe')
    env = clean_environment(runtime)
    project = root / 'Projects' / 'package-smoke'
    project.mkdir(parents=True)
    selected = run([launcher, '-Check'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15).stdout
    if Path(selected.decode('utf-8').strip()) != binary / ('zima-cad-cpp' + suffix):
        raise ValueError('Launcher selected an unexpected executable')
    launched = run([launcher, '-CLI', '--', '--build-info'], cwd=project, env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if json.loads(launched.stdout)['version'] != version:
        raise ValueError('Launcher did not preserve the CLI protocol output')
    launched = run([launcher, '-CLI', '--', '--working-directory', project, '--stdin'],
                   cwd=project, env=env, input=b'documents\n', stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, timeout=30)
    if not json.loads(launched.stdout)['ok']:
        raise ValueError('Launcher did not preserve the CLI input/output pipes')
    user_config = root / 'config/config.ini'
    user_config.write_text('[UserData]\nPreserve=package-smoke\n', encoding='utf-8')
    config_before = user_config.read_bytes()
    for executable in ('zima-cad-cpp' + suffix, 'zima-cad-cli' + suffix):
        value = json.loads(run([binary / executable, '--build-info'], cwd=project, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout)
        if value['version'] != version or value['product'] != 'ZIMA-CAD':
            raise ValueError('Executable build identity mismatch')
    def commands(items):
        args = [binary / ('zima-cad-cli' + suffix), '--working-directory', project]
        for command in items:
            args += ['--command', json.dumps(command) if isinstance(command, dict) else command]
        output = run(args, cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        records = [json.loads(line) for line in output.stdout.splitlines()]
        if len(records) != len(items) or not all(r.get('ok') for r in records):
            raise ValueError('CLI smoke command failed')
        return records
    records = commands(['new part package-smoke', {'command': 'sketch.create',
        'arguments': {'name': 'Package profile', 'plane': 'XY'}}, 'save'])
    sketch = records[1]['data']['sketch']
    corners = [(-5, -10), (5, -10), (5, 10), (-5, 10)]
    profile = [{'command': 'sketch.segment.create', 'arguments': {
        'sketch': sketch, 'first': corners[i], 'second': corners[(i + 1) % 4],
        'snap_mm': 0.000001}} for i in range(4)]
    records = commands(['open package-smoke.prtz', *profile,
        {'command': 'extrusion.create', 'arguments': {'sketch': sketch,
            'extent': 'symmetric', 'length_forward_mm': 15}}, 'save'])
    owner = records[-2]['data']['container']
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
        if linux:
            if os.environ.get('XDG_SESSION_TYPE') != 'wayland':
                raise ValueError('Linux desktop acceptance requires a Wayland session')
            env['QT_QPA_PLATFORM'] = 'wayland'
        env['ZIMA_VERIFY_PACKAGE_ONLY'] = '1'
        run([binary / ('zima-cad-cpp' + suffix), '--working-directory', project, '--verify-startup'],
            cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    if user_config.read_bytes() != config_before:
        raise ValueError('Startup/smoke replaced shared user configuration')
    for name, record in json.loads((root / 'checksums.json').read_text(encoding='utf-8')).items():
        if sha(root / name) != record['sha256']:
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
        if set(checksums) != {m.filename.removeprefix('ZIMA-CAD/') for m in members} - {'checksums.json'}:
            raise ValueError('ZIP inventory mismatch')
        for name, record in checksums.items():
            with zipped.open('ZIMA-CAD/' + name) as data:
                digest = hashlib.file_digest(data, 'sha256').hexdigest()
            if zipped.getinfo('ZIMA-CAD/' + name).file_size != record['size'] or digest != record['sha256']:
                raise ValueError('ZIP SHA-256 mismatch: ' + name)
        if destination is not None:
            if destination.exists():
                raise ValueError('Extraction requires a new directory')
            if any(len(str(destination / m.filename).encode('utf-16-le')) // 2 > 259 for m in members):
                raise ValueError('Choose a shorter extraction directory; full paths exceed 259 characters')
            destination.mkdir(parents=True)
            for member in members:
                target = destination / member.filename
                target.parent.mkdir(parents=True, exist_ok=True)
                with zipped.open(member) as source, target.open('xb') as output:
                    shutil.copyfileobj(source, output)
                if os.name != 'nt':
                    record = checksums.get(member.filename.removeprefix('ZIMA-CAD/'), {})
                    target.chmod(0o755 if record.get('executable') else 0o644)
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
    if len(str(stage)) > 60 or (stage.exists() and not args.reuse_build):
        raise ValueError('Use a new short staging directory (at most 60 characters)')
    source = stage / 's'
    if args.reuse_build:
        if not source.is_dir() or not (stage / 'b/CMakeCache.txt').is_file():
            raise ValueError('Reuse requires the source and build of a previous candidate attempt')
        cache = (stage / 'b/CMakeCache.txt').read_text(encoding='utf-8')
        previous = re.search(r'^ZIMA_SOURCE_COMMIT:[^=]+=([0-9a-f]{40})$', cache, re.M)
        home = re.search(r'^CMAKE_HOME_DIRECTORY:[^=]+=(.+)$', cache, re.M)
        if not previous or not home or Path(home[1]).resolve() != (source / 'cpp').resolve():
            raise ValueError('Cached build does not belong to this source staging directory')
        refresh_source(ROOT, previous[1], commit, source, stage)
    else:
        stage.mkdir(parents=True)
        export_source(ROOT, commit, source)
    build = stage / 'b'
    run([args.cmake, '-S', source / 'cpp', '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DZIMA_BUILD_TESTS=OFF', '-DZIMA_SOURCE_COMMIT=' + commit,
         '-DCMAKE_TOOLCHAIN_FILE=' + str(args.toolchain.resolve()),
         '-DVCPKG_MANIFEST_INSTALL=OFF',
         '-DVCPKG_INSTALLED_DIR=' + str(args.installed.resolve().parent), '-DVCPKG_TARGET_TRIPLET=x64-windows'])
    run([args.cmake, '--build', build, '--target', 'zima-cad-cpp', 'zima-cad-cli', 'zima-cad-launcher', 'zima-cad-update', '--parallel', str(args.jobs)])
    assembly = Path(tempfile.mkdtemp(prefix='p-', dir=stage))
    root = assembly / 'ZIMA-CAD'; runtime = root / 'windows' / version
    runtime.mkdir(parents=True)
    for exe in ('zima-cad-cpp.exe', 'zima-cad-cli.exe', 'zima-cad-update.exe'):
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
    write_json(root / 'installation.json', {'product': 'ZIMA-CAD', 'protocol': 1, 'id': str(uuid.uuid4())})
    # User config and project directories are created on first launch. Updates
    # must never extract a supplied user config over an existing installation.
    write_json(root / 'checksums.json', inventory(root))
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    archive = output / ('ZIMA-CAD-' + version + '.zip')
    if archive.exists():
        raise ValueError('Archive already exists; use a new build ID or output directory')
    candidate = assembly / 'candidate.zip'
    with zipfile.ZipFile(candidate, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in sorted(root.rglob('*')):
            if path.is_file(): zipped.write(path, 'ZIMA-CAD/' + path.relative_to(root).as_posix())
    extracted = assembly / 'ověření balíku'
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
    build.add_argument('--reuse-build', action='store_true', help='Verify previous source against Git, export the selected commit and reuse unchanged compilation')
    validate = commands.add_parser('validate')
    validate.add_argument('archive', type=Path)
    args = parser.parse_args()
    if args.command == 'windows': package(args)
    else: print(validate_archive(args.archive))


if __name__ == '__main__':
    main()
