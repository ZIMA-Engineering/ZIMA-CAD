"""Regenerate the approved Part format-48 factory baseline in isolation.

The tool reads exact committed Part-based assets, calculates each asset with
the selected native authoring executable, saves and reopens the result, and
checks geometry/reference preservation plus the persisted edge-treatment
method and explicit perpendicular end slots. Working factory files are
read-only unless ``--install`` is supplied.
"""

import argparse
import concurrent.futures
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import traceback


ROOT = Path(__file__).resolve().parents[1]
PART_EXTENSIONS = {'.prtz', '.frmz', '.tblz', '.symz'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized(data):
    return data.replace(b'\r\n', b'\n')


def ini(path):
    value = configparser.ConfigParser(interpolation=None, strict=False)
    value.optionxform = str
    value.read(path, encoding='utf-8-sig')
    return value


def treatment_contract(data):
    count = 0
    methods = []
    route_ends = []
    route_counts = []
    embedded_versions = []

    def walk(item):
        nonlocal count
        if isinstance(item, dict):
            if item.get('format') == 'zima-cad-cpp' and \
                    item.get('type') == 'part':
                embedded_versions.append(item.get('format_version'))
            if item.get('type') in {'fillet', 'chamfer'}:
                count += 1
                methods.append(item.get('calculation_method'))
                route_ends.append(item.get('route_end_faces'))
                routes = item.get('routes')
                route_counts.append(len(routes) if isinstance(routes, list)
                                    else None)
            for child in item.values():
                walk(child)
        elif isinstance(item, list):
            for child in item:
                walk(child)
        elif isinstance(item, str) and item.lstrip().startswith(('{', '[')):
            try:
                walk(json.loads(item))
            except (ValueError, TypeError):
                pass

    for section in data.sections():
        if not section.startswith('Container.'):
            continue
        text = data[section].get('param.cpp_history')
        if text:
            walk(json.loads(text))
    return count, methods, route_ends, route_counts, embedded_versions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path,
        default=ROOT / 'build/edge-treatment-format-factory')
    parser.add_argument('--native', type=Path,
        default=ROOT / 'build/cpp-windows-release/zima_refresh_start_templates.exe')
    parser.add_argument('--source-commit',
        help='Format-47 commit used as the immutable conversion source')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    stage = args.stage.resolve()
    native = args.native.resolve()
    commit = subprocess.check_output(
        ['git', 'rev-parse', args.source_commit or 'HEAD'], cwd=ROOT,
        text=True).strip()
    ownership = {
        'repository': str(ROOT),
        'source_commit': commit,
        'kind': 'edge-treatment-format-48-end-faces',
    }
    marker = stage / 'ownership.json'
    if marker.exists():
        assert json.loads(marker.read_text(encoding='utf-8')) == ownership, \
            'Stage belongs to another source revision or task'
    else:
        assert not stage.exists() or not any(stage.iterdir()), \
            'Refusing to reuse an unrelated non-empty stage'
        stage.mkdir(parents=True, exist_ok=True)
        marker.write_text(json.dumps(ownership, indent=2), encoding='utf-8')
    assert shutil.disk_usage(stage.parent).free >= 3 * 1024 ** 3, \
        'At least 3 GiB free space is required for isolated native assets'

    names = subprocess.check_output(
        ['git', 'ls-files', '-z', 'config'], cwd=ROOT).decode('utf-8').split('\0')
    names = sorted(name for name in names
                   if Path(name).suffix.lower() in PART_EXTENSIONS)
    assert len(names) == 1046, f'Unexpected Part-based factory count: {len(names)}'
    source = stage / 'source'
    verified = stage / 'verified'
    reports = stage / 'reports'
    reports.mkdir(parents=True, exist_ok=True)
    inventory = []
    reader = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for name in names:
            reader.stdin.write((commit + ':' + name + '\n').encode('utf-8'))
            reader.stdin.flush()
            header = reader.stdout.readline().decode().split()
            assert len(header) == 3 and header[1] == 'blob', header
            blob = reader.stdout.read(int(header[2]))
            assert reader.stdout.read(1) == b'\n'
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.exists():
                assert path.read_bytes() == blob, 'Changed isolated source: ' + name
            else:
                path.write_bytes(blob)
            working = ROOT / name
            working_bytes = working.read_bytes()
            working_is_source = normalized(working_bytes) == normalized(blob)
            if not working_is_source:
                working_ini = ini(working)
                assert working_ini['Document']['format_version'] == '48', \
                    'Working asset is neither the source nor format 48: ' + name
            before = ini(path)
            assert before['Document']['format_version'] == '47', \
                'Factory source is not format 47: ' + name
            treatments, methods, ends, route_counts, embedded = \
                treatment_contract(before)
            assert all(method is None for method in methods), \
                'Format-47 source unexpectedly contains a method flag: ' + name
            assert all(value is None for value in ends), \
                'Format-47 source unexpectedly contains end-face slots: ' + name
            assert all(value is not None for value in route_counts), \
                'Format-47 source has malformed treatment routes: ' + name
            inventory.append({
                'path': name,
                'source_sha256': hashlib.sha256(blob).hexdigest(),
                'working_sha256': sha(working),
                'working_is_source': working_is_source,
                'bytes': len(blob),
                'edge_treatments': treatments,
                'embedded_parts': len(embedded),
            })
    finally:
        reader.stdin.close()
        assert reader.wait() == 0
    (stage / 'inventory.json').write_text(json.dumps({
        **ownership,
        'total': len(inventory),
        'edge_treatments': sum(row['edge_treatments'] for row in inventory),
        'embedded_parts': sum(row['embedded_parts'] for row in inventory),
        'assets': inventory,
    }, indent=2), encoding='utf-8')
    print('Prepared', len(inventory), 'committed Part-based assets;',
          sum(row['edge_treatments'] for row in inventory), 'edge treatments;',
          sum(row['embedded_parts'] for row in inventory), 'embedded Parts',
          flush=True)
    if args.prepare_only:
        return

    runtime_hash = sha(native)
    report_path = stage / 'verification.json'
    previous = {}
    if args.resume and report_path.exists():
        prior = json.loads(report_path.read_text(encoding='utf-8'))
        assert prior['source_commit'] == commit and \
            prior['native_sha256'] == runtime_hash, \
            'Conversion runtime or source revision changed'
        previous = {row['path']: row for row in prior['assets'] if row['ok']}
    environment = os.environ | {
        'QT_QPA_PLATFORM': 'offscreen',
        'QT_STYLE_OVERRIDE': 'Fusion',
        'TEMP': str(stage / 'temp'),
        'TMP': str(stage / 'temp'),
    }
    (stage / 'temp').mkdir(exist_ok=True)

    def convert(item):
        name = item['path']
        original = source / name
        output = verified / name
        prior = previous.get(name)
        if prior and output.exists() and sha(output) == prior['output_sha256']:
            return prior
        evidence_path = reports / (hashlib.sha256(name.encode()).hexdigest() + '.json')
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            output.unlink()
        started = time.monotonic()
        try:
            run = subprocess.run([str(native), '--regenerate-format-asset',
                    str(original), str(output), str(evidence_path)],
                cwd=source, env=environment, capture_output=True, timeout=300)
            assert run.returncode == 0, (run.stdout + run.stderr).decode(
                'utf-8', errors='replace')[-4000:]
            evidence = json.loads(evidence_path.read_text(encoding='utf-8'))
            assert all(evidence[key] for key in (
                'native_readback_exact', 'authored_history_unchanged',
                'sketches_unchanged', 'calculated_contracts_preserved'))
            assert not evidence.get('errors'), 'Regenerated asset reports errors'
            before, after = ini(original), ini(output)
            assert after['Document']['format_version'] == '48'
            old_count, old_methods, old_ends, old_route_counts, old_embedded = \
                treatment_contract(before)
            new_count, new_methods, new_ends, new_route_counts, new_embedded = \
                treatment_contract(after)
            assert old_count == new_count == item['edge_treatments']
            assert all(method is None for method in old_methods)
            assert all(method == 'occt' for method in new_methods), \
                'Existing edge treatment did not preserve the OCCT route'
            assert all(value is None for value in old_ends)
            assert all(isinstance(value, list) for value in new_ends), \
                'Converted edge treatment omitted end-face slots'
            assert old_route_counts == new_route_counts
            for value, route_count in zip(new_ends, new_route_counts):
                assert len(value) == route_count, \
                    'End-face slot count does not match authored routes'
                assert all(set(slot) == {'first', 'last'} and
                    slot['first'] is None and slot['last'] is None
                    for slot in value), \
                    'Legacy treatment did not preserve perpendicular ends'
            assert len(old_embedded) == len(new_embedded) == item['embedded_parts']
            assert all(version == 72 for version in new_embedded), \
                'Embedded Part definition did not reach JSON format 72'
            for section in ('DocumentUnits', 'DocumentPrecision', 'Material',
                    'MaterialDescriptions', 'MaterialProperties', 'MaterialUnits',
                    'Relations', 'UserParameters', 'UserParameterLabels',
                    'UserParameterValues'):
                first_section = dict(before[section]) \
                    if before.has_section(section) else {}
                second_section = dict(after[section]) \
                    if after.has_section(section) else {}
                assert first_section == second_section, \
                    ('Changed authored metadata', section)
            assert sha(original) == item['source_sha256']
            assert sha(ROOT / name) == item['working_sha256']
            if not item['working_is_source']:
                assert normalized(output.read_bytes()) == normalized(
                    (ROOT / name).read_bytes()), \
                    'Format-48 working asset differs from regenerated output'
            return {
                'path': name,
                'ok': True,
                'seconds': time.monotonic() - started,
                'output_sha256': sha(output),
                'output_bytes': output.stat().st_size,
                'edge_treatments': new_count,
                'embedded_parts': len(new_embedded),
                'evidence': evidence,
            }
        except Exception as error:
            return {'path': name, 'ok': False,
                    'seconds': time.monotonic() - started,
                    'error': repr(error), 'traceback': traceback.format_exc()}

    selected = inventory[:args.limit] if args.limit else inventory
    rows = []
    started = time.monotonic()

    def publish():
        report_path.write_text(json.dumps({
            **ownership,
            'native_sha256': runtime_hash,
            'partial': len(selected) != len(inventory),
            'total': len(rows),
            'passed': sum(row['ok'] for row in rows),
            'seconds': time.monotonic() - started,
            'assets': rows,
        }, indent=2), encoding='utf-8')

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(convert, selected):
            rows.append(row)
            if not row['ok']:
                print('FAILED', row['path'], row['error'], flush=True)
            if len(rows) % 25 == 0:
                publish()
                print('Verified', len(rows), '/', len(selected), flush=True)
    publish()
    assert all(row['ok'] for row in rows), 'One or more factory assets failed'
    print('Verified', len(rows), '/', len(selected), 'format-48 assets', flush=True)
    if not args.install:
        return
    assert not args.limit and len(rows) == len(inventory), \
        'A partial verification cannot be installed'
    assert all(item['working_is_source'] for item in inventory), \
        'Installation requires untouched format-47 working factory assets'
    by_name = {row['path']: row for row in rows}
    for item in inventory:
        name = item['path']
        assert sha(ROOT / name) == item['working_sha256'], \
            'Working factory asset changed before installation: ' + name
        assert sha(verified / name) == by_name[name]['output_sha256']
    for item in inventory:
        name = item['path']
        shutil.copyfile(verified / name, ROOT / name)
    for item in inventory:
        assert sha(ROOT / item['path']) == by_name[item['path']]['output_sha256']
    (stage / 'installation.json').write_text(json.dumps({
        **ownership,
        'native_sha256': runtime_hash,
        'installed': True,
        'assets': [{'path': item['path'],
                    'sha256': by_name[item['path']]['output_sha256']}
                   for item in inventory],
    }, indent=2), encoding='utf-8')
    print('Installed', len(inventory),
          'verified Part-based factory assets; no user files were touched')


if __name__ == '__main__':
    main()
