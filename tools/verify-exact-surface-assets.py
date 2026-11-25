"""Recalculate tracked native assets into an isolated verification tree.

The native authoring tool compares complete history geometry, reference identities,
curves, sides, physical properties and authored definitions, then saves/reopens the
new exact-surface packets. This driver records immutable input/output hashes. It
does not overwrite libraries, stage Git changes or publish a package.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PART_EXTENSIONS = {'.prtz', '.toolz', '.frmz', '.tblz', '.symz'}


def native_path(path):
    value = str(path.absolute())
    return Path('\\\\?\\' + value) if os.name == 'nt' and len(value) >= 248 and not value.startswith('\\\\?\\') else path


def sha(path):
    with native_path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, default=ROOT / 'build/cpp-windows-release/zima_refresh_start_templates.exe')
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--prepare-only', action='store_true', help='Prepare schema-only copies; does not verify calculation')
    parser.add_argument('--source-root', type=Path, default=ROOT, help='Repository root or an owned prepared tree with hash receipts')
    parser.add_argument('--kind', choices=('part', 'drawing'), default='part')
    parser.add_argument('--path', action='append', help='Restrict to these tracked repository-relative paths')
    args = parser.parse_args()
    stage, native, source_root = args.stage.resolve(), args.native.resolve(), args.source_root.resolve()
    if not stage.is_relative_to(ROOT / 'build'):
        parser.error('Verification stage must be inside the repository build directory')
    stage.mkdir(parents=True, exist_ok=True)
    identity = {'repository': str(ROOT), 'kind': 'exact-surface-asset-verification'}
    marker = stage / 'ownership.json'
    if marker.exists():
        if json.loads(marker.read_text(encoding='utf8')) != identity:
            parser.error('The stage belongs to another operation')
    elif any(stage.iterdir()):
        parser.error('Refusing a nonempty unowned stage')
    else:
        marker.write_text(json.dumps(identity, indent=2), encoding='utf8')
    names = subprocess.check_output(['git', 'ls-files', '-z', 'config', 'cpp/tests/fixtures'], cwd=ROOT).decode().split('\0')
    extensions = PART_EXTENSIONS if args.kind == 'part' else {'.drwz'}
    names = sorted(name for name in names if Path(name).suffix.lower() in extensions)
    if args.path:
        unknown = set(args.path) - set(names)
        if unknown:
            parser.error('Paths are not tracked assets of this kind: ' + ', '.join(sorted(unknown)))
        names = [name for name in names if name in args.path]
    if args.limit is not None:
        names = names[:args.limit]
    native_hash = sha(native)

    def verify(name):
        source = native_path(source_root / name)
        prefix = 'preparation-' if args.prepare_only else ''
        target = native_path(stage / ('prepared' if args.prepare_only else 'calculated') / name)
        report = native_path(stage / (prefix + 'evidence') / (name + '.json'))
        receipt = native_path(stage / (prefix + 'receipts') / (name + '.json'))
        log_path = native_path(stage / (prefix + 'logs') / (name + '.log'))
        input_hash = sha(source)
        original_hash = sha(ROOT / name)
        if source_root != ROOT:
            prior = json.loads(native_path(source_root.parent / 'preparation-receipts' / (name + '.json')).read_text(encoding='utf8'))
            if not prior.get('ok') or prior['output_sha256'] != input_hash or prior['original_sha256'] != original_hash:
                raise RuntimeError('Prepared input provenance is invalid: ' + name)
        if args.resume and receipt.exists():
            saved = json.loads(receipt.read_text(encoding='utf8'))
            if saved.get('ok') and saved['source_sha256'] == input_hash and saved.get('original_sha256') == original_hash and saved['native_sha256'] == native_hash and target.exists() and saved['output_sha256'] == sha(target):
                return saved
        if shutil.disk_usage(stage).free < 512 * 1024 * 1024:
            raise RuntimeError('Less than 512 MiB free; stopped before another asset calculation')
        for path in (target, report, receipt, log_path):
            path.parent.mkdir(parents=True, exist_ok=True)
        start = time.perf_counter()
        failure = None
        with log_path.open('wb') as log:
            try:
                mode = '--regenerate-exact-surface-asset' if args.kind == 'part' else '--regenerate-exact-surface-drawing'
                if args.prepare_only:
                    mode = '--prepare-exact-surface-verification-document'
                process = subprocess.run([str(native), mode, str(source), str(target), str(report)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=300)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                exit_code = None
                failure = 'Native regeneration exceeded the 300-second verification limit'
        unchanged = sha(source) == input_hash
        evidence = json.loads(report.read_text(encoding='utf8')) if report.exists() else {}
        ok = exit_code == 0 and unchanged and evidence.get('native_readback_exact') and evidence.get('calculated_contracts_preserved') and evidence.get('authored_history_unchanged') and evidence.get('exact_surface_regeneration_repeatable')
        if args.prepare_only:
            ok = exit_code == 0 and unchanged and (evidence.get('native_readback_exact') or evidence.get('native_readback')) and evidence.get('calculation_verified') is False and evidence.get('publication_allowed') is False
        result = {'path': name, 'ok': bool(ok), 'exit_code': exit_code, 'failure': failure,
                  'seconds': time.perf_counter() - start, 'source_sha256': input_hash,
                  'original_sha256': original_hash, 'preparation_only': args.prepare_only,
                  'source_unchanged': unchanged, 'native_sha256': native_hash,
                  'output_sha256': sha(target) if ok else None}
        receipt.write_text(json.dumps(result, indent=2), encoding='utf8')
        return result

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 4))) as pool:
        for result in pool.map(verify, names):
            results.append(result)
            if not result['ok'] or len(results) % 25 == 0:
                print(json.dumps({'completed': len(results), 'total': len(names), 'failed': sum(not row['ok'] for row in results), 'last': result['path']}), flush=True)
    summary = {'count': len(results), 'passed': sum(row['ok'] for row in results),
               'native_sha256': native_hash, 'results': results}
    summary_name = 'summary.json' if args.kind == 'part' else 'summary-drawings.json'
    if args.path or args.limit is not None:
        summary_name = 'summary-selected-' + args.kind + '.json'
    if args.prepare_only:
        summary_name = 'preparation-' + summary_name
    (stage / summary_name).write_text(json.dumps(summary, indent=2), encoding='utf8')
    print(json.dumps({'count': summary['count'], 'passed': summary['passed']}), flush=True)
    return 0 if summary['count'] == summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
