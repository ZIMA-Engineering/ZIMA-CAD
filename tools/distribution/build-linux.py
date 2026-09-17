"""Build a committed Debian 13 amd64 candidate, deploy native libraries and verify it.

Provision Qt development packages and the OCCT SDK before invoking this tool.
Python is a developer tool only. This builder never signs or publishes assets.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import uuid
import zipfile
import package as shared

# Debian supplies its libc ABI and the graphics dispatcher/driver stack.
HOST = re.compile(r'^(ld-linux.*|lib(c|m|pthread|dl|rt|resolv|util)\.so\..*|libnss_.*|lib(GL|GLX|GLdispatch|EGL|OpenGL|GLESv[12]|drm|gbm|vulkan)([_.-].*)?\.so.*)$')


def baseline():
    distro = platform.freedesktop_os_release()
    if distro.get('ID') != 'debian' or distro.get('VERSION_ID') != '13' or platform.machine() != 'x86_64':
        raise ValueError('Linux candidates require Debian 13 x86_64')
    return dict(distro=distro, machine=platform.machine(), libc=platform.libc_ver(),
                compiler=shared.run(['c++', '--version'], stdout=subprocess.PIPE).stdout.decode().splitlines()[0])


def dependencies(binary, env):
    output = shared.run(['ldd', binary], env=env, stdout=subprocess.PIPE).stdout.decode()
    if 'not found' in output:
        raise ValueError('Unresolved dependency: ' + str(binary) + '\n' + output)
    return [Path(p) for p in re.findall(r'=>\s+(/\S+)', output)]


def deploy(runtime, sdk):
    query = lambda key: Path(shared.run(['qmake6', '-query', key], stdout=subprocess.PIPE).stdout.decode().strip())
    plugins = query('QT_INSTALL_PLUGINS')
    for group in ('platforms', 'imageformats', 'iconengines', 'tls', 'networkinformation',
                  'xcbglintegrations', 'wayland-shell-integration', 'wayland-graphics-integration-client',
                  'wayland-decoration-client'):
        if (plugins / group).is_dir():
            shutil.copytree(plugins / group, runtime / 'plugins' / group)
    for relative in ('platforms/libqwayland-generic.so', 'platforms/libqxcb.so',
                     'platforms/libqoffscreen.so', 'imageformats/libqjpeg.so', 'imageformats/libqsvg.so',
                     'tls/libqopensslbackend.so'):
        if not (runtime / 'plugins' / relative).is_file():
            raise ValueError('Required Qt plugin missing: ' + relative)
    libraries = runtime / 'lib'; libraries.mkdir()
    licenses = runtime / 'licenses'; licenses.mkdir()
    env = dict(os.environ, LD_LIBRARY_PATH=str(sdk / 'lib'))
    pending = list((runtime / 'bin').iterdir()) + list((runtime / 'plugins').rglob('*.so'))
    origins = {p: plugins / p.relative_to(runtime / 'plugins') for p in (runtime / 'plugins').rglob('*.so')}
    host, inspected = set(), set()
    while pending:
        binary = pending.pop()
        if binary in inspected: continue
        inspected.add(binary)
        for path in dependencies(binary, env):
            if HOST.fullmatch(path.name):
                host.add(path.name); continue
            destination = libraries / path.name
            if destination in origins and origins[destination].resolve() != path.resolve():
                raise ValueError('Conflicting dependency: ' + path.name)
            if not destination.exists():
                shutil.copy2(path, destination)
                origins[destination] = path
                pending.append(path)
    packages = {}
    for path in origins.values():
        if sdk in path.resolve().parents: continue
        ownership = shared.run(['dpkg-query', '-S', str(path)], stdout=subprocess.PIPE).stdout.decode().splitlines()[0]
        package = ownership.rsplit(': ', 1)[0]
        name = package.split(':')[0]
        notice = Path('/usr/share/doc') / name / 'copyright'
        if not notice.is_file(): raise ValueError('Missing dependency license: ' + name)
        shutil.copy2(notice, licenses / (name + '.txt'))
        packages[package] = shared.run(['dpkg-query', '-W', '-f=${Version}', package], stdout=subprocess.PIPE).stdout.decode()
    resources = sdk / 'share/opencascade/resources'
    if not resources.is_dir(): raise ValueError('Missing OCCT resources')
    shutil.copytree(resources, runtime / 'resources/occt')
    for name in ('LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt'):
        shutil.copy2(sdk / 'share/doc/opencascade' / name, licenses / name)
    for binary in list((runtime / 'bin').iterdir()) + list(libraries.iterdir()) + list((runtime / 'plugins').rglob('*.so')):
        shared.run(['patchelf', '--set-rpath', '$ORIGIN/' + os.path.relpath(libraries, binary.parent), binary])
        binary.chmod(0o755)
    (runtime / 'bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\n', encoding='utf-8')
    clean = shared.clean_environment(runtime)
    for binary in list((runtime / 'bin').glob('zima-*')) + list(libraries.iterdir()) + list((runtime / 'plugins').rglob('*.so')):
        for path in dependencies(binary, clean):
            if runtime not in path.parents and not HOST.fullmatch(path.name):
                raise ValueError('Dependency escapes portable runtime: ' + str(path))
    return dict(host_libraries=sorted(host), debian_packages=packages)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--commit', default='HEAD')
    parser.add_argument('--repo', type=Path, default=shared.ROOT)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=12)
    parser.add_argument('--release', action='store_true')
    parser.add_argument('--reuse-build', action='store_true')
    args = parser.parse_args()
    metadata = baseline()
    repo, sdk = args.repo.resolve(), args.sdk.resolve()
    commit = shared.git(repo, 'rev-parse', args.commit + '^{commit}').decode().strip()
    version = shared.version_id(shared.git(repo, 'show', commit + ':VERSION').decode().strip())
    if args.release:
        if shared.git(repo, 'status', '--porcelain'):
            raise ValueError('Release input must be clean')
        if shared.git(repo, 'rev-parse', 'ZIMA-CAD-' + version + '^{commit}').decode().strip() != commit:
            raise ValueError('Release tag mismatch')
    stage = args.stage.resolve(); source = stage / 's'; build = stage / 'b'
    if len(str(stage)) > 60: raise ValueError('Use a staging path at most 60 characters long')
    if args.reuse_build:
        previous = json.loads((stage / 'source.json').read_text())['commit']
        shared.refresh_source(repo, previous, commit, source, stage)
    else:
        stage.mkdir(parents=True, exist_ok=False)
        shared.export_source(repo, commit, source)
    shared.write_json(stage / 'source.json', dict(commit=commit))
    shared.run(['cmake', '-S', source / 'cpp', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                '-DZIMA_BUILD_TESTS=OFF', '-DZIMA_SOURCE_COMMIT=' + commit, '-DCMAKE_PREFIX_PATH=' + str(sdk)])
    shared.run(['cmake', '--build', build, '--target', 'zima-cad-cpp', 'zima-cad-cli', 'zima-cad-update', '--parallel', str(args.jobs)])
    assembly = Path(tempfile.mkdtemp(prefix='p-', dir=stage)); root = assembly / 'ZIMA-CAD'
    runtime = root / 'linux' / version; (runtime / 'bin').mkdir(parents=True)
    for name in ('zima-cad-cpp', 'zima-cad-cli', 'zima-cad-update'):
        shutil.copy2(build / name, runtime / 'bin' / name)
    for name in ('config', 'resources'):
        shutil.copytree(source / name, runtime / name, ignore=shutil.ignore_patterns('.gitkeep', '*.ini.[0-9]*', '*.frmz.[0-9]*', '*.tblz.[0-9]*'))
    metadata.update(deploy(runtime, sdk))
    metadata['occt_sdk'] = json.loads((sdk / 'sdk.json').read_text())
    shutil.copy2(source / 'LICENSE', root / 'LICENSE')
    shutil.copy2(source / 'LICENSE', runtime / 'licenses/ZIMA-CAD.txt')
    shutil.copy2(source / 'tools/distribution/launcher-linux.sh', root / 'ZIMA-CAD.sh')
    (root / 'ZIMA-CAD.sh').chmod(0o755)
    shutil.copytree(source, root / 'source' / version)
    identity = json.loads(shared.run([runtime / 'bin/zima-cad-cli', '--build-info'], env=shared.clean_environment(runtime), stdout=subprocess.PIPE).stdout)
    if identity['version'] != version or identity['commit'] != commit or identity['source_modified']:
        raise ValueError('Executable/source identity mismatch')
    metadata.update(identity, origin='release-candidate' if args.release else 'committed-candidate', signed=False)
    shared.write_json(runtime / 'version.json', metadata)
    (runtime / 'build.ini').write_text(f'[build]\nproduct=ZIMA-CAD\nversion={version}\nplatform=linux-x86_64\n')
    (root / 'launcher.ini').write_text(f'[launcher]\nwindows=\nwindows_custom=false\nlinux={version}\nlinux_custom=false\n')
    shared.write_json(root / 'installation.json', dict(product='ZIMA-CAD', protocol=1, id=str(uuid.uuid4())))
    shared.write_json(root / 'checksums.json', shared.inventory(root))
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    archive = output / ('ZIMA-CAD-' + version + '.zip')
    if archive.exists(): raise ValueError('Archive already exists')
    candidate = assembly / 'candidate.zip'
    with zipfile.ZipFile(candidate, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in sorted(root.rglob('*')):
            if path.is_file(): zipped.write(path, 'ZIMA-CAD/' + path.relative_to(root).as_posix())
    extracted = assembly / 'ověření balíku'
    digest = shared.validate_archive(candidate, extracted)
    shared.smoke(extracted / 'ZIMA-CAD', version)
    with archive.open('xb') as target, candidate.open('rb') as data: shutil.copyfileobj(data, target)
    if shared.sha(archive) != digest: raise ValueError('Archive copy differs')
    shared.write_json(output / (archive.stem + '.validation.json'), dict(archive=archive.name, sha256=digest,
                      commit=commit, version=version, windows_smoke='not-run', linux_smoke='passed', signed=False))
    print('Validated candidate:', archive)


if __name__ == '__main__': main()
