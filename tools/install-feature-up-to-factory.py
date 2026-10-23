"""Install verified committed factory outputs while preserving private edits.

Validation is read-only by default. --install updates clean working assets and
stages the converted committed baseline of the explicitly protected private M10
Part directly in the Git index, leaving its private working bytes unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/05-NUTS/09-WING/01-PRESSED/PWN-CSN021665-M10.0.prtz'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args, **kwargs):
    return subprocess.check_output(['git', *args], cwd=ROOT, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, default=ROOT / 'build/feature-up-to-factory')
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Resume only known factory outputs already copied/staged by this run')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    inventory = json.loads((evidence / 'inventory.json').read_text(encoding='utf-8'))
    converted = json.loads((evidence / 'verification.json').read_text(encoding='utf-8'))
    assert git('rev-parse', 'HEAD').decode().strip() == inventory['commit'] == converted['commit']
    assert converted['total'] == converted['passed'] == 1016 and not converted['partial']
    assert sha(ROOT / 'build/cpp-windows-release/zima_refresh_start_templates.exe') == converted['native_sha256']
    for name, count in (('mass', 1016), ('load', 1185), ('drawings', 137)):
        report = json.loads((evidence / (name + '-verification.json')).read_text(encoding='utf-8'))
        assert report['commit'] == inventory['commit'] and report['total'] == report['passed'] == count
        assert not report.get('partial', False)
        if name in ('load', 'drawings'):
            assert report['cli_sha256'] == sha(ROOT / 'build/cpp-windows-release/zima-cad-cli.exe'), 'Dependency verification runtime changed'
    templates = json.loads((evidence / 'templates-verification.json').read_text(encoding='utf-8'))
    assert templates['ok'] and len(templates['assets']) == 6 and templates['commit'] == inventory['commit']
    assert templates['native_sha256'] == converted['native_sha256']
    stage = Path(inventory['stage'])
    marker = json.loads((stage / '.zima-up-to-factory.json').read_text(encoding='utf-8'))
    assert marker == {'repository': str(ROOT), 'commit': inventory['commit'], 'kind': 'feature-up-to-factory'}
    original = {row['path']: row for row in inventory['assets']}
    outputs = {row['path']: row['output_sha256'] for row in converted['assets']}
    outputs.update({row['path']: row['sha256'] for row in templates['assets']})
    staged = set(git('diff', '--cached', '--name-only', '-z').decode('utf-8').split('\0')) - {''}
    assert not staged or (args.resume and staged <= outputs.keys()), 'Unexpected staged changes'
    text_assets = {row['path'] for row in templates['assets']} | {
        row['path'] for row in converted['assets'] if '/01-FORM/' in row['path']}

    def exact_indexed(name, raw, repair_private=False):
        indexed = git('show', ':' + name)
        # Git normalizes these tracked native INI texts on Windows. Prove that
        # this changes only physical CRLF separators, never any native value.
        text_normalization = name in text_assets or (repair_private and name == PRIVATE)
        assert indexed == raw or (text_normalization and indexed == raw.replace(b'\r\n', b'\n')), 'Staged factory content differs: ' + name
        return indexed

    for name in staged:
        exact_indexed(name, (stage / 'verified' / name).read_bytes(), args.resume)
    rows = []
    for name, output_hash in outputs.items():
        authored = original[name]
        working = sha(ROOT / name)
        original_bytes = (stage / 'source' / name).read_bytes()
        original_forms = {hashlib.sha256(original_bytes).hexdigest(), hashlib.sha256(
            original_bytes.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')).hexdigest()}
        private = authored['working_sha256'] not in original_forms
        allowed_working = {authored['working_sha256']}
        if args.resume and not private:
            allowed_working.add(output_hash)
        assert working in allowed_working, 'Working file changed during verification: ' + name
        assert sha(stage / 'source' / name) == authored['source_sha256']
        assert sha(stage / 'verified' / name) == output_hash, 'Verified output changed: ' + name
        assert not private or name == PRIVATE, 'Unexpected private factory edit: ' + name
        rows.append({'path': name, 'output_sha256': output_hash, 'working_before_sha256': authored['working_sha256'],
                     'private_working_bytes_preserved': private})
    if not args.install:
        print('Validated', len(rows), 'factory outputs; protected private working files:', sum(row['private_working_bytes_preserved'] for row in rows))
        return
    (evidence / 'installation-intent.json').write_text(json.dumps({'commit': inventory['commit'], 'assets': rows}, indent=2), encoding='utf-8')
    clean = []
    for row in rows:
        name = row['path']
        if row['private_working_bytes_preserved']:
            blob = git('hash-object', '-w', '--no-filters', str(stage / 'verified' / name)).decode().strip()
            git('update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name)
        else:
            shutil.copyfile(stage / 'verified' / name, ROOT / name)
            clean.append(name)
    pathspec = evidence / 'install-paths.nul'
    pathspec.write_bytes(b'\0'.join(name.encode('utf-8') for name in clean) + b'\0')
    git('add', '--pathspec-from-file=' + str(pathspec), '--pathspec-file-nul')
    for row in rows:
        expected = row['working_before_sha256'] if row['private_working_bytes_preserved'] else row['output_sha256']
        assert sha(ROOT / row['path']) == expected
        indexed = exact_indexed(row['path'], (stage / 'verified' / row['path']).read_bytes())
        row['committed_sha256'] = hashlib.sha256(indexed).hexdigest()
        row['git_line_endings_normalized'] = row['committed_sha256'] != row['output_sha256']
        if row['git_line_endings_normalized']:
            normalized = stage / 'committed-readback' / row['path']
            normalized.parent.mkdir(parents=True, exist_ok=True)
            normalized.write_bytes(indexed)
    (evidence / 'installation.json').write_text(json.dumps({'commit': inventory['commit'],
        'installed_and_staged': True, 'assets': rows}, indent=2), encoding='utf-8')
    print('Installed and staged', len(rows), 'verified factory outputs; private M10 working data preserved')


if __name__ == '__main__':
    main()
