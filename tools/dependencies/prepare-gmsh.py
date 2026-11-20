"""Validate and prepare the pinned Gmsh source for the shared OCCT 8 probe."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile

VERSION = '4.15.2'
SOURCE_URL = f'https://gmsh.info/src/gmsh-{VERSION}-source.tgz'
SOURCE_SHA256 = 'be3f66f225d27ba9fa014f07e83169285da8a051b0e8ab7103d88066b39bdd3e'
PATCH_SHA256 = '769b3837f28c50318fc3943a6d4cbc7103780ed9ad2a2ba7e44c313185a50dd7'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--archive', type=Path, required=True,
                        help=f'Archive downloaded from {SOURCE_URL}')
    parser.add_argument('--root', type=Path, required=True,
                        help='New, short, dedicated extraction directory')
    args = parser.parse_args()
    root = args.root.resolve()
    if root.exists() or len(str(root)) > 65:
        raise ValueError('Use a new short dedicated directory; existing data are never overwritten')
    if sha(args.archive) != SOURCE_SHA256:
        raise ValueError('Gmsh source SHA-256 differs from the reviewed pin')
    repo = Path(__file__).resolve().parents[2]
    patch = Path(__file__).with_name('gmsh-occt8.patch')
    if sha(patch) != PATCH_SHA256:
        raise ValueError('Gmsh patch changed; review it and update its pin')
    # Reuse the distribution path policy, including Windows collisions/reserved
    # names. This is a build-time Python tool, not the product implementation.
    spec = importlib.util.spec_from_file_location('zima_package', repo / 'tools/distribution/package.py')
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    prefix = f'gmsh-{VERSION}-source'
    with tarfile.open(args.archive, 'r:gz') as archive:
        members = archive.getmembers()
        seen = set()
        total = 0
        for member in members:
            name = member.name.rstrip('/')
            package.safe_name(name)
            if not (name == prefix or name.startswith(prefix + '/')):
                raise ValueError(f'Unexpected source root: {name}')
            if not (member.isdir() or member.isfile()):
                raise ValueError(f'Unsupported archive member: {name}')
            if name.casefold() in seen:
                raise ValueError(f'Case-colliding archive member: {name}')
            seen.add(name.casefold())
            destination = (root / name).resolve()
            if not destination.is_relative_to(root) or len(str(destination)) >= 245:
                raise ValueError(f'Unsafe or overlong extraction path: {name}')
            total += member.size
        if len(members) > 100000 or total > 1024**3:
            raise ValueError('Source extraction exceeds size/count limits')
        root.mkdir(parents=True)
        archive.extractall(root, members=members, filter='data')
    source = root / prefix
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
    record = {'version': VERSION, 'source_url': SOURCE_URL,
              'source_sha256': SOURCE_SHA256, 'patch_sha256': PATCH_SHA256,
              'required_occt': '8.0.1', 'status': 'prepared, not built or accepted'}
    (root / 'source-provenance.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(source)


if __name__ == '__main__':
    main()
